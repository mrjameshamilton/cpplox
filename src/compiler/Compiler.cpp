
#ifndef COMPILER_CPP
#define COMPILER_CPP
#include "Compiler.h"
#include "../AST.h"
#include "Object.h"
#include <iostream>
#include <llvm/ADT/DenseMapInfo.h>
#include <llvm/ADT/ScopedHashTable.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/NoFolder.h>
#include <llvm/Passes/PassBuilder.h>
#include <ranges>

using namespace llvm;
using namespace llvm::sys;

namespace lox {

    void Compiler::operator()(const BlockStmtPtr &blockStmt) {
        beginScope();
        for (auto &statement: blockStmt->statements) {
            evaluate(statement);
        }
        endScope();
    }

    void Compiler::operator()(const FunctionStmtPtr &functionStmt) const {
    }

    void Compiler::operator()(const ExpressionStmtPtr &expressionStmt) {
        evaluate(expressionStmt->expression);
    }

    void Compiler::operator()(const PrintStmtPtr &printStmt) {
        const auto value = evaluate(printStmt->expression);

        const auto BoolBlock = BasicBlock::Create(*Context, "if.bool", MainFunction);
        const auto EndBoolBlock = BasicBlock::Create(*Context, "if.bool.end", MainFunction);
        const auto NilBlock = BasicBlock::Create(*Context, "if.nil", MainFunction);
        const auto EndNilBlock = BasicBlock::Create(*Context, "if.nil.end", MainFunction);
        const auto NumBlock = BasicBlock::Create(*Context, "if.num", MainFunction);
        const auto ObjBlock = BasicBlock::Create(*Context, "if.obj", MainFunction);
        const auto EndBlock = BasicBlock::Create(*Context, "if.end", MainFunction);

        static const auto fmt = Builder->CreateGlobalStringPtr("%s\n");
        static const auto true_ = Builder->CreateGlobalStringPtr("true");
        static const auto false_ = Builder->CreateGlobalStringPtr("false");
        static const auto nil = Builder->CreateGlobalStringPtr("nil");
        static const auto gfmt = Builder->CreateGlobalStringPtr("%g\n");
        static const auto PrintF = LoxModule->getOrInsertFunction(
            "printf",
            FunctionType::get(Builder->getInt8Ty(), {Type::getInt8PtrTy(*Context)}, true)
        );

        Builder->CreateCondBr(IsBool(value), BoolBlock, EndBoolBlock);
        Builder->SetInsertPoint(BoolBlock);
        Builder->CreateCall(PrintF, {fmt, (Builder->CreateSelect(AsBool(value), true_, false_))});
        Builder->CreateBr(EndBlock);
        Builder->SetInsertPoint(EndBoolBlock);

        Builder->CreateCondBr(IsNil(value), NilBlock, EndNilBlock);
        Builder->SetInsertPoint(NilBlock);
        Builder->CreateCall(PrintF, {fmt, nil});
        Builder->CreateBr(EndBlock);
        Builder->SetInsertPoint(EndNilBlock);

        Builder->CreateCondBr(IsNumber(value), NumBlock, ObjBlock);
        Builder->SetInsertPoint(NumBlock);
        Builder->CreateCall(PrintF, {gfmt, AsNumber(value)});
        Builder->CreateBr(EndBlock);
        Builder->SetInsertPoint(ObjBlock);
        Builder->CreateCall(PrintF, {fmt, AsCString(value)});
        Builder->CreateBr(EndBlock);
        Builder->SetInsertPoint(EndBlock);
    }

    void Compiler::operator()(const ReturnStmtPtr &returnStmt) const {
    }

    void Compiler::operator()(const VarStmtPtr &varStmt) {
        const auto alloca = CreateEntryBlockAlloca(MainFunction, Builder->getInt64Ty(), varStmt->name.getLexeme());
        Builder->CreateStore(evaluate(varStmt->initializer), alloca);
        variables.insert(varStmt->name.getLexeme(), alloca);
        /*LoxModule->getOrInsertGlobal(varStmt->name.getLexeme(), Builder->getInt64Ty());
            const auto global = LoxModule->getNamedGlobal(varStmt->name.getLexeme());
            global->setLinkage(GlobalValue::PrivateLinkage);
            global->setAlignment(Align(8));
            global->setConstant(false);
            global->setInitializer(Builder->getInt64(NIL_VAL));

            Builder->CreateStore(evaluate(varStmt->initializer), global);*/
    }

    void Compiler::operator()(const WhileStmtPtr &whileStmt) {
        const auto Cond = BasicBlock::Create(*Context, "Cond", MainFunction);
        const auto Body = BasicBlock::Create(*Context, "Loop", MainFunction);
        const auto Exit = BasicBlock::Create(*Context, "Exit", MainFunction);

        Builder->CreateBr(Cond);
        Builder->SetInsertPoint(Cond);
        Builder->CreateCondBr(IsTruthy(evaluate(whileStmt->condition)), Body, Exit);
        Builder->SetInsertPoint(Body);
        evaluate(whileStmt->body);
        Builder->CreateBr(Cond);
        Builder->SetInsertPoint(Exit);
    }

    void Compiler::operator()(const IfStmtPtr &ifStmt) {
        const auto TrueBlock = BasicBlock::Create(*Context, "if.true", MainFunction);
        const auto FalseBlock = BasicBlock::Create(*Context, "else", MainFunction);
        const auto EndBlock = BasicBlock::Create(*Context, "if.end", MainFunction);
        Builder->CreateCondBr(IsTruthy(evaluate(ifStmt->condition)), TrueBlock, FalseBlock);
        Builder->SetInsertPoint(TrueBlock);
        evaluate(ifStmt->thenBranch);
        Builder->CreateBr(EndBlock);
        Builder->SetInsertPoint(FalseBlock);
        if (ifStmt->elseBranch.has_value()) {
            evaluate(ifStmt->elseBranch.value());
        }
        Builder->CreateBr(EndBlock);
        Builder->SetInsertPoint(EndBlock);
    }

    void Compiler::operator()(const ClassStmtPtr &classStmt) const {
    }

    Value *Compiler::operator()(const AssignExprPtr &assignExpr) {
        const auto value = evaluate(assignExpr->value);
        /*const auto global = LoxModule->getNamedGlobal(assignExpr->name.getLexeme());
            Builder->CreateStore(value, global);*/
        const auto current = *variables.begin(assignExpr->name.getLexeme());
        Builder->CreateStore(value, current);
        return value;
    }

    Value *Compiler::operator()(const BinaryExprPtr &binaryExpr) {
        const auto left = evaluate(binaryExpr->left);
        const auto right = evaluate(binaryExpr->right);

        switch (binaryExpr->op) {
            case BinaryOp::PLUS: {
                const auto IsMaybeStringBlock = BasicBlock::Create(*Context, "if.string", MainFunction);
                const auto IsStringBlock = BasicBlock::Create(*Context, "is.string", MainFunction);
                const auto IsNumBlock = BasicBlock::Create(*Context, "if.num", MainFunction);
                const auto InvalidBlock = BasicBlock::Create(*Context, "invalid", MainFunction);
                const auto EndBlock = BasicBlock::Create(*Context, "if.end", MainFunction);
                Builder->CreateCondBr(Builder->CreateAnd(IsNumber(left), IsNumber(right)), IsNumBlock, IsMaybeStringBlock);
                Builder->SetInsertPoint(IsNumBlock);
                const auto &X = NumberVal(Builder->CreateFAdd(AsNumber(left), AsNumber(right)));
                Builder->CreateBr(EndBlock);

                Builder->SetInsertPoint(IsMaybeStringBlock);
                Builder->CreateCondBr(Builder->CreateAnd(IsString(left), IsString(right)), IsStringBlock, InvalidBlock);
                Builder->SetInsertPoint(IsStringBlock);
                const auto &Y = Concat(left, right);
                Builder->CreateBr(EndBlock);

                Builder->SetInsertPoint(InvalidBlock);
                // TODO: Throw exception here.
                const auto &Z = Builder->getInt64(NIL_VAL);
                Builder->CreateBr(EndBlock);

                Builder->SetInsertPoint(EndBlock);

                const auto &Result = Builder->CreatePHI(Builder->getInt64Ty(), 3);
                Result->addIncoming(X, IsNumBlock);
                Result->addIncoming(Y, IsStringBlock);
                Result->addIncoming(Z, InvalidBlock);

                return Result;
            }
            case BinaryOp::MINUS:
                return NumberVal(Builder->CreateFSub(AsNumber(left), AsNumber(right)));
            case BinaryOp::SLASH:
                return NumberVal(Builder->CreateFDiv(AsNumber(left), AsNumber(right)));
            case BinaryOp::STAR:
                return NumberVal(Builder->CreateFMul(AsNumber(left), AsNumber(right)));
            case BinaryOp::GREATER:
                return BoolVal(Builder->CreateFCmpOGT(AsNumber(left), AsNumber(right)));
            case BinaryOp::GREATER_EQUAL:
                return BoolVal(Builder->CreateFCmpOGE(AsNumber(left), AsNumber(right)));
            case BinaryOp::LESS:
                return BoolVal(Builder->CreateFCmpOLT(AsNumber(left), AsNumber(right)));
            case BinaryOp::LESS_EQUAL:
                return BoolVal(Builder->CreateFCmpOLE(AsNumber(left), AsNumber(right)));
            case BinaryOp::BANG_EQUAL:
            case BinaryOp::EQUAL_EQUAL:
                // A == B if both are numbers and they're equal as fp numbers or they're both equal int64 values.
                const auto IsNumBlock = BasicBlock::Create(*Context, "if.num", MainFunction);
                const auto NotNumBlock = BasicBlock::Create(*Context, "not.num", MainFunction);
                const auto IsStringBlock = BasicBlock::Create(*Context, "is.string", MainFunction);
                const auto NotStringBlock = BasicBlock::Create(*Context, "not.string", MainFunction);
                const auto EndBlock = BasicBlock::Create(*Context, "end", MainFunction);

                Builder->CreateCondBr(Builder->CreateAnd(IsNumber(left), IsNumber(right)), IsNumBlock, NotNumBlock);
                Builder->SetInsertPoint(IsNumBlock);
                const auto X = Builder->CreateFCmpOEQ(AsNumber(left), AsNumber(right));
                Builder->CreateBr(EndBlock);
                Builder->SetInsertPoint(NotNumBlock);
                Builder->CreateCondBr(Builder->CreateAnd(IsString(left), IsString(right)), IsStringBlock, NotStringBlock);
                Builder->SetInsertPoint(IsStringBlock);
                const auto Y = StrEquals(left, right);
                Builder->CreateBr(EndBlock);
                Builder->SetInsertPoint(NotStringBlock);
                const auto Z = Builder->CreateICmpEQ(left, right);
                Builder->CreateBr(EndBlock);
                Builder->SetInsertPoint(EndBlock);

                const auto Result = Builder->CreatePHI(Builder->getInt1Ty(), 3);
                Result->addIncoming(X, IsNumBlock);
                Result->addIncoming(Y, IsStringBlock);
                Result->addIncoming(Z, NotStringBlock);

                return Builder->CreateSelect(
                    Result,
                    Builder->getInt64(binaryExpr->op == BinaryOp::EQUAL_EQUAL ? TRUE_VAL : FALSE_VAL),
                    Builder->getInt64(binaryExpr->op == BinaryOp::EQUAL_EQUAL ? FALSE_VAL : TRUE_VAL)
                );
        }

        std::unreachable();
    }

    Value *Compiler::operator()(const CallExprPtr &callExpr) const {
        throw std::runtime_error("not implemented");
    }

    Value *Compiler::operator()(const GetExprPtr &getExpr) const {
        throw std::runtime_error("not implemented");
    }

    Value *Compiler::operator()(const SetExprPtr &setExpr) const {
        throw std::runtime_error("not implemented");
    }

    Value *Compiler::operator()(const ThisExprPtr &thisExpr) const {
        throw std::runtime_error("not implemented");
    }

    Value *Compiler::operator()(const SuperExprPtr &superExpr) const {
        throw std::runtime_error("not implemented");
    }

    Value *Compiler::operator()(const VarExprPtr &varExpr) const {
        const auto value = variables.lookup(varExpr->name.getLexeme());
        return Builder->CreateLoad(Builder->getInt64Ty(), value);
        //if (varExpr->distance == -1) {
        const auto global = LoxModule->getNamedGlobal(varExpr->name.getLexeme());
        return Builder->CreateLoad(Builder->getInt64Ty(), global);
        //}
    }

    Value *Compiler::operator()(const GroupingExprPtr &groupingExpr) {
        return evaluate(groupingExpr->expression);
    }

    Value *Compiler::operator()(const LiteralExprPtr &literalExpr) {
        return std::visit(
            overloaded{
                [this](const bool value) -> Value * { return value ? Builder->getInt64(TRUE_VAL) : Builder->getInt64(FALSE_VAL); },
                [this](const double double_value) -> Value * {
                    // bitcast double -> i64.
                    uint64_t value;
                    memcpy(&value, &double_value, sizeof(double));
                    return Builder->getInt64(value);
                },
                [this](const std::string_view string_value) -> Value * {
                    if (strings.contains(string_value))
                        return strings.at(string_value);

                    const auto String = CreateEntryBlockAlloca(MainFunction, StringStructType, "s");

                    Builder->CreateStore(
                        Builder->getInt8(static_cast<uint8_t>(ObjType::STRING)),
                        Builder->CreateStructGEP(ObjStructType, Builder->CreateBitCast(String, ObjStructType->getPointerTo()), 0)
                    );

                    const auto String_Ptr = Builder->CreateStructGEP(StringStructType, String, 1);
                    const auto String_Length = Builder->CreateStructGEP(StringStructType, String, 2);
                    Builder->CreateStore(Builder->CreateGlobalStringPtr(string_value), String_Ptr);
                    Builder->CreateStore(Builder->getInt32(string_value.length()), String_Length);

                    const auto value =
                        ObjVal(Builder->CreatePtrToInt(String, Builder->getInt64Ty()));

                    strings[string_value] = value;

                    return value;
                },
                [this](const std::nullptr_t) -> Value * { return Builder->getInt64(NIL_VAL); },
            },
            literalExpr->literal
        );
    }

    Value *Compiler::operator()(const LogicalExprPtr &logicalExpr) {
        const auto left = evaluate(logicalExpr->left);

        switch (logicalExpr->op) {
            case LogicalOp::AND:
            case LogicalOp::OR: {
                const auto LeftIsTruthyBlock = BasicBlock::Create(*Context, "if.left.truthy", MainFunction);
                const auto LeftNotTruthyBlock = BasicBlock::Create(*Context, "if.left.nottruthy", MainFunction);
                const auto EndBlock = BasicBlock::Create(*Context, "end", MainFunction);
                Builder->CreateCondBr(
                    IsTruthy(left),
                    logicalExpr->op == LogicalOp::OR ? LeftIsTruthyBlock : LeftNotTruthyBlock,
                    logicalExpr->op == LogicalOp::OR ? LeftNotTruthyBlock : LeftIsTruthyBlock
                );
                Builder->SetInsertPoint(LeftNotTruthyBlock);
                const auto X = IsTruthy(evaluate(logicalExpr->right));
                const auto EndLeftNotTruthyBlock = Builder->GetInsertBlock();
                Builder->CreateBr(EndBlock);
                Builder->SetInsertPoint(LeftIsTruthyBlock);
                const auto Y = logicalExpr->op == LogicalOp::OR ? Builder->getTrue() : Builder->getFalse();
                Builder->CreateBr(EndBlock);
                Builder->SetInsertPoint(EndBlock);

                const auto Result = Builder->CreatePHI(Builder->getInt1Ty(), 2);
                Result->addIncoming(X, EndLeftNotTruthyBlock);
                Result->addIncoming(Y, LeftIsTruthyBlock);

                return BoolVal(Result);
            }
        }
        std::unreachable();
    }

    Value *Compiler::operator()(const UnaryExprPtr &unaryExpr) {
        const auto left = evaluate(unaryExpr->expression);

        switch (unaryExpr->op) {
            case UnaryOp::BANG:
                return Builder->CreateSelect(IsTruthy(left), Builder->getInt64(FALSE_VAL), Builder->getInt64(TRUE_VAL));
            case UnaryOp::MINUS:
                return NumberVal(Builder->CreateFNeg(AsNumber(left)));
        }

        std::unreachable();
    }

    void Compiler::evaluate(const Program &program) {
        beginScope();
        const auto EntryBasicBlock = BasicBlock::Create(*Context, "entry", MainFunction);
        Builder->SetInsertPoint(EntryBasicBlock);
        for (auto &stmt: program) {
            evaluate(stmt);
        }
        Builder->CreateRet(Builder->getInt32(0));
        endScope();
    }
}// namespace lox
#endif//COMPILER_CPP
