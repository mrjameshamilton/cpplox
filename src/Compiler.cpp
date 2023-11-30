
#ifndef COMPILER_H
#define COMPILER_H
#include "AST.h"
#include <iostream>
#include <llvm/ADT/DenseMapInfo.h>
#include <llvm/ADT/ScopedHashTable.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/NoFolder.h>
#include <llvm/Passes/PassBuilder.h>
#include <ranges>
#include <stack>

using namespace llvm;
using namespace llvm::sys;

template<>
struct llvm::DenseMapInfo<std::string_view> {
    static inline std::string_view getEmptyKey() {
        return "$EMPTY KEY$";
    }

    static inline std::string_view getTombstoneKey() {
        return "$TOMBSTONE KEY$";
    }

    static inline unsigned getHashValue(std::string_view Val);
    static inline bool isEqual(std::string_view LHS, std::string_view RHS);
};

inline unsigned DenseMapInfo<std::string_view>::getHashValue(const std::string_view Val) {
    constexpr std::hash<std::string_view> hasher;
    return hasher(Val);
}
inline bool DenseMapInfo<std::string_view>::isEqual(const std::string_view LHS, const std::string_view RHS) {
    return LHS == RHS;
}

namespace lox {

#define SIGN_BIT ((uint64_t) 0x8000000000000000)
#define QNAN ((uint64_t) 0x7ffc000000000000)

#define TAG_NIL 1
#define TAG_FALSE 2
#define TAG_TRUE 3

#define FALSE_VAL ((uint64_t) (QNAN | TAG_FALSE))
#define TRUE_VAL ((uint64_t) (QNAN | TAG_TRUE))
#define NIL_VAL ((uint64_t) (QNAN | TAG_NIL))

    struct Compiler {
        std::unique_ptr<LLVMContext> Context = std::make_unique<LLVMContext>();
        std::unique_ptr<Module> LoxModule = std::make_unique<Module>("lox", *Context);
        std::unique_ptr<IRBuilder<NoFolder>> Builder = std::make_unique<IRBuilder<NoFolder>>(*Context);
        Function *MainFunction =
            Function::Create(FunctionType::get(Builder->getInt32Ty(), false), Function::ExternalLinkage, "main", *LoxModule);
        using ScopedHTType = ScopedHashTable<std::string_view, Value *>;
        ScopedHTType variables;
        std::stack<ScopedHTType::ScopeTy> scopes;
        std::unordered_map<std::string_view, Value *> strings;
        StructType *StringType = StructType::create(*Context, {Builder->getInt8PtrTy(), Builder->getInt32Ty()}, "String");

        Compiler() = default;

        Value *IsBool(Value *value) const {
            return Builder->CreateICmpEQ(Builder->CreateOr(value, 1), Builder->getInt64(TRUE_VAL));
        }

        Value *IsNil(Value *value) const {
            return Builder->CreateICmpEQ(value, Builder->getInt64(NIL_VAL));
        }

        Value *IsNumber(Value *value) const {
            return Builder->CreateICmpNE(Builder->CreateAnd(value, QNAN), Builder->getInt64(QNAN));
        }

        Value *IsObj(Value *value) const {
            return Builder->CreateICmpEQ(Builder->CreateAnd(value, QNAN | SIGN_BIT), Builder->getInt64(QNAN | SIGN_BIT));
        }

        Value *IsTruthy(Value *value) const {
            static auto IsTruthyFunction([this] {
                const auto F = Function::Create(
                    FunctionType::get(
                        Builder->getInt1Ty(),
                        Builder->getInt64Ty(),
                        false
                    ),
                    Function::InternalLinkage,
                    "IsTruthy",
                    *LoxModule
                );

                const auto EntryBasicBlock = BasicBlock::Create(*Context, "entry", F);
                const auto IsNullBlock = BasicBlock::Create(*Context, "if.null", F);
                const auto IsNotNullBlock = BasicBlock::Create(*Context, "if.not.bool", F);
                const auto IsBoolBlock = BasicBlock::Create(*Context, "if.bool", F);
                const auto EndBlock = BasicBlock::Create(*Context, "if.end", F);

                const auto InsertPoint = Builder->GetInsertBlock();
                Builder->SetInsertPoint(EntryBasicBlock);

                const auto p0 = F->args().begin();
                Builder->CreateCondBr(IsNil(p0), IsNullBlock, IsNotNullBlock);
                Builder->SetInsertPoint(IsNullBlock);
                Builder->CreateRet(Builder->getFalse());
                Builder->SetInsertPoint(IsNotNullBlock);
                Builder->CreateCondBr(IsBool(p0), IsBoolBlock, EndBlock);
                Builder->SetInsertPoint(IsBoolBlock);
                Builder->CreateRet(AsBool(p0));
                Builder->SetInsertPoint(EndBlock);
                Builder->CreateRet(Builder->getTrue());

                Builder->SetInsertPoint(InsertPoint);
                return F;
            }());

            return Builder->CreateCall(IsTruthyFunction, value);
        }


        Value *Concat(Value *a, Value *b) const {
            static auto ConcatFunction([this] {
                const auto F = Function::Create(
                    FunctionType::get(
                        Builder->getInt64Ty(),
                        {Builder->getInt64Ty(), Builder->getInt64Ty()},
                        false
                    ),
                    Function::InternalLinkage,
                    "concat",
                    *LoxModule
                );

                const auto InsertPoint = Builder->GetInsertBlock();
                const auto EntryBasicBlock = BasicBlock::Create(*Context, "entry", F);
                Builder->SetInsertPoint(EntryBasicBlock);

                const auto iterator = F->args().begin();
                const auto p0 = iterator;
                const auto p1 = iterator + 1;

                const auto p0str = CreateEntryBlockAlloca(F, Builder->getPtrTy(), "p0str");
                Builder->CreateStore(AsObj(p0), p0str);
                const auto p1str = CreateEntryBlockAlloca(F, Builder->getPtrTy(), "p1str");
                Builder->CreateStore(AsObj(p1), p1str);

                const auto p0Length = CreateEntryBlockAlloca(F, Builder->getInt32Ty(), "p0Length");
                const auto p1Length = CreateEntryBlockAlloca(F, Builder->getInt32Ty(), "p1Length");

                Builder->CreateStore(
                    Builder->CreateLoad(Builder->getInt32Ty(), Builder->CreateStructGEP(StringType, Builder->CreateLoad(Builder->getPtrTy(), p0str), 1, "length0")),
                    p0Length
                );
                Builder->CreateStore(
                    Builder->CreateLoad(Builder->getInt32Ty(), Builder->CreateStructGEP(StringType, Builder->CreateLoad(Builder->getPtrTy(), p1str), 1, "length1")),
                    p1Length
                );

                const auto NewLength = Builder->CreateNSWAdd(
                    Builder->CreateLoad(Builder->getInt32Ty(), p0Length),
                    Builder->CreateLoad(Builder->getInt32Ty(), p1Length),
                    "NewLength"
                );

                const auto StringMalloc = Builder->CreateMalloc(
                    Builder->getInt32Ty(),
                    Builder->getInt8PtrTy(),
                    Builder->CreateSExt(Builder->CreateNSWAdd(Builder->getInt32(1), NewLength), Builder->getInt64Ty()),
                    nullptr
                );

                const auto StringTemp = CreateEntryBlockAlloca(F, Builder->getPtrTy(), "StringTemp");
                Builder->CreateStore(StringMalloc, StringTemp);

                const auto ptr_to_str0 = Builder->CreateStructGEP(StringType, Builder->CreateLoad(Builder->getPtrTy(), p0str), 0);
                Builder->CreateMemCpy(
                    Builder->CreateLoad(Builder->getPtrTy(), StringTemp),
                    Align(1),
                    Builder->CreateLoad(Builder->getPtrTy(), ptr_to_str0),
                    Align(1),
                    Builder->CreateSExt(Builder->CreateLoad(Builder->getInt32Ty(), p0Length), Builder->getInt64Ty())
                );

                Builder->CreateMemCpy(
                    Builder->CreateInBoundsGEP(
                        Builder->getInt8Ty(),
                        Builder->CreateLoad(Builder->getPtrTy(), StringTemp),
                        {Builder->CreateSExt(Builder->CreateLoad(Builder->getInt32Ty(), p0Length), Builder->getInt64Ty())}
                    ),
                    Align(1),
                    Builder->CreateLoad(
                        Builder->getPtrTy(),
                        Builder->CreateStructGEP(StringType, Builder->CreateLoad(Builder->getPtrTy(), p1str), 0)
                    ),
                    Align(1),
                    Builder->CreateSExt(
                        Builder->CreateNSWAdd(
                            Builder->CreateLoad(Builder->getInt32Ty(), p1Length),
                            Builder->getInt32(1)
                        ),
                        Builder->getInt64Ty()
                    )
                );

                // TODO: need to free the memory.
                const auto NewString = CreateEntryBlockAlloca(F, StringType, "NewString");
                const auto NewStringMalloc = Builder->CreateMalloc(
                    Builder->getInt32Ty(),
                    StringType,
                    ConstantExpr::getSizeOf(StringType),
                    nullptr
                );
                Builder->CreateStore(NewStringMalloc, NewString);
                Builder->CreateStore(
                    Builder->CreateLoad(Builder->getPtrTy(), StringTemp),
                    Builder->CreateStructGEP(StringType, Builder->CreateLoad(Builder->getPtrTy(), NewString), 0)
                );
                Builder->CreateStore(
                    NewLength,
                    Builder->CreateStructGEP(StringType, Builder->CreateLoad(Builder->getPtrTy(), NewString), 1)
                );
                Builder->CreateRet(
                    ObjVal(
                        Builder->CreatePtrToInt(
                            Builder->CreateLoad(Builder->getPtrTy(), NewString),
                            Builder->getInt64Ty()
                        )
                    )
                );

                Builder->SetInsertPoint(InsertPoint);

                return F;
            }());

            return Builder->CreateCall(ConcatFunction, {a, b});
        }

        Value *IsNotTruthy(Value *value) const {
            return Builder->CreateNot(IsTruthy(value));
        }

        Value *BoolVal(Value *value) const {
            assert(value->getType() == Builder->getInt1Ty());
            return Builder->CreateSelect(value, Builder->getInt64(TRUE_VAL), Builder->getInt64(FALSE_VAL));
        }

        Value *AsBool(Value *value) const {
            assert(value->getType() == Builder->getInt64Ty());
            return Builder->CreateICmpEQ(value, Builder->getInt64(TRUE_VAL));
        }


        Value *AsNumber(Value *value) const {
            return Builder->CreateBitCast(value, Builder->getDoubleTy());
        }

        Value *ObjVal(Value *value) const {
            return Builder->CreateOr(value, SIGN_BIT | QNAN);
        }

        Value *AsObj(Value *value) const {
            return Builder->CreateIntToPtr(Builder->CreateAnd(value, ~(SIGN_BIT | QNAN)), Builder->getInt8PtrTy());
        }

        Value *AsCString(Value *value) const {
            const auto string = Builder->CreateIntToPtr(Builder->CreateAnd(value, ~(SIGN_BIT | QNAN)), Builder->getInt8PtrTy());
            return Builder->CreateLoad(Builder->getInt8PtrTy(), Builder->CreateStructGEP(StringType, string, 0));
        }

        Value *NumberVal(Value *value) const {
            return Builder->CreateBitCast(value, Builder->getInt64Ty());
        }

        static AllocaInst *CreateEntryBlockAlloca(Function *TheFunction, Type *type, const std::string_view &VarName) {
            IRBuilder TmpB(&TheFunction->getEntryBlock(), TheFunction->getEntryBlock().begin());
            return TmpB.CreateAlloca(type, nullptr, VarName);
        }

        void beginScope() {
            scopes.emplace(variables);
        }

        void endScope() {
            scopes.pop();
        }

        void operator()(const BlockStmtPtr &blockStmt) {
            beginScope();
            for (auto &statement: blockStmt->statements) {
                evaluate(statement);
            }
            endScope();
        }

        void operator()(const FunctionStmtPtr &functionStmt) const {
        }

        void operator()(const ExpressionStmtPtr &expressionStmt) {
            evaluate(expressionStmt->expression);
        }

        void operator()(const PrintStmtPtr &printStmt) {
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

        void operator()(const ReturnStmtPtr &returnStmt) const {
        }

        void operator()(const VarStmtPtr &varStmt) {
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

        void operator()(const WhileStmtPtr &whileStmt) {
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

        void operator()(const IfStmtPtr &ifStmt) {
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

        void operator()(const ClassStmtPtr &classStmt) const {
        }

        Value *operator()(const AssignExprPtr &assignExpr) {
            const auto value = evaluate(assignExpr->value);
            /*const auto global = LoxModule->getNamedGlobal(assignExpr->name.getLexeme());
            Builder->CreateStore(value, global);*/
            const auto current = *variables.begin(assignExpr->name.getLexeme());
            Builder->CreateStore(value, current);
            return value;
        }

        Value *operator()(const BinaryExprPtr &binaryExpr) {
            const auto left = evaluate(binaryExpr->left);
            const auto right = evaluate(binaryExpr->right);

            switch (binaryExpr->op) {
                case BinaryOp::PLUS: {
                    const auto IsStringBlock = BasicBlock::Create(*Context, "if.num", MainFunction);
                    const auto IsNumBlock = BasicBlock::Create(*Context, "if.str", MainFunction);
                    const auto EndBlock = BasicBlock::Create(*Context, "if.end", MainFunction);
                    Builder->CreateCondBr(Builder->CreateAnd(IsNumber(left), IsNumber(right)), IsNumBlock, IsStringBlock);
                    Builder->SetInsertPoint(IsNumBlock);
                    const auto &X = NumberVal(Builder->CreateFAdd(AsNumber(left), AsNumber(right)));
                    Builder->CreateBr(EndBlock);
                    Builder->SetInsertPoint(IsStringBlock);
                    const auto &Y = Concat(left, right);
                    Builder->CreateBr(EndBlock);
                    Builder->SetInsertPoint(EndBlock);

                    const auto &Result = Builder->CreatePHI(Builder->getInt64Ty(), 2);
                    Result->addIncoming(X, IsNumBlock);
                    Result->addIncoming(Y, IsStringBlock);

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
                    const auto EndBlock = BasicBlock::Create(*Context, "end", MainFunction);

                    Builder->CreateCondBr(Builder->CreateAnd(IsNumber(left), IsNumber(right)), IsNumBlock, NotNumBlock);
                    Builder->SetInsertPoint(IsNumBlock);
                    const auto X = Builder->CreateFCmpOEQ(AsNumber(left), AsNumber(right));
                    Builder->CreateBr(EndBlock);
                    Builder->SetInsertPoint(NotNumBlock);
                    const auto Y = Builder->CreateICmpEQ(left, right);
                    Builder->CreateBr(EndBlock);
                    Builder->SetInsertPoint(EndBlock);

                    const auto Result = Builder->CreatePHI(Builder->getInt1Ty(), 2);
                    Result->addIncoming(X, IsNumBlock);
                    Result->addIncoming(Y, NotNumBlock);

                    return Builder->CreateSelect(
                        Result,
                        Builder->getInt64(binaryExpr->op == BinaryOp::EQUAL_EQUAL ? TRUE_VAL : FALSE_VAL),
                        Builder->getInt64(binaryExpr->op == BinaryOp::EQUAL_EQUAL ? FALSE_VAL : TRUE_VAL)
                    );
            }

            std::unreachable();
        }

        Value *operator()(const CallExprPtr &callExpr) const {
            throw std::runtime_error("not implemented");
        }

        Value *operator()(const GetExprPtr &getExpr) const {
            throw std::runtime_error("not implemented");
        }

        Value *operator()(const SetExprPtr &setExpr) const {
            throw std::runtime_error("not implemented");
        }

        Value *operator()(const ThisExprPtr &thisExpr) const {
            throw std::runtime_error("not implemented");
        }

        Value *operator()(const SuperExprPtr &superExpr) const {
            throw std::runtime_error("not implemented");
        }

        Value *operator()(const VarExprPtr &varExpr) const {
            const auto value = variables.lookup(varExpr->name.getLexeme());
            return Builder->CreateLoad(Builder->getInt64Ty(), value);
            //if (varExpr->distance == -1) {
            const auto global = LoxModule->getNamedGlobal(varExpr->name.getLexeme());
            return Builder->CreateLoad(Builder->getInt64Ty(), global);
            //}
        }

        Value *operator()(const GroupingExprPtr &groupingExpr) {
            return evaluate(groupingExpr->expression);
        }

        Value *operator()(const LiteralExprPtr &literalExpr) {
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

                        const auto &String = CreateEntryBlockAlloca(MainFunction, StringType, "s");
                        const auto &String_Ptr = Builder->CreateStructGEP(StringType, String, 0);
                        const auto &String_Length = Builder->CreateStructGEP(StringType, String, 1);
                        Builder->CreateStore(Builder->CreateGlobalStringPtr(string_value), String_Ptr);
                        Builder->CreateStore(Builder->getInt32(string_value.length()), String_Length);

                        const auto &value =
                            ObjVal(Builder->CreatePtrToInt(String, Builder->getInt64Ty()));

                        strings[string_value] = value;

                        return value;
                    },
                    [this](const std::nullptr_t) -> Value * { return Builder->getInt64(NIL_VAL); },
                },
                literalExpr->literal
            );
        }

        Value *operator()(const LogicalExprPtr &logicalExpr) {
            const auto left = evaluate(logicalExpr->left);

            switch (logicalExpr->op) {
                case LogicalOp::AND:
                case LogicalOp::OR: {
                    const auto LeftIsTruthyBlock = BasicBlock::Create(*Context, "if.left.truthy", MainFunction);
                    const auto LeftNotTruthyBlock = BasicBlock::Create(*Context, "if.left.nottruthy", MainFunction);
                    const auto Result = Builder->CreateAlloca(Builder->getInt1Ty(), nullptr, "logical.result");
                    Builder->CreateStore(IsTruthy(left), Result);
                    Builder->CreateCondBr(
                        Builder->CreateLoad(Builder->getInt1Ty(), Result),
                        logicalExpr->op == LogicalOp::OR ? LeftIsTruthyBlock : LeftNotTruthyBlock,
                        logicalExpr->op == LogicalOp::OR ? LeftNotTruthyBlock : LeftIsTruthyBlock
                    );
                    Builder->SetInsertPoint(LeftNotTruthyBlock);
                    Builder->CreateStore(IsTruthy(evaluate(logicalExpr->right)), Result);
                    Builder->CreateBr(LeftIsTruthyBlock);
                    Builder->SetInsertPoint(LeftIsTruthyBlock);
                    return BoolVal(Builder->CreateLoad(Builder->getInt1Ty(), Result));
                }
            }
            std::unreachable();
        }

        Value *operator()(const UnaryExprPtr &unaryExpr) {
            const auto left = evaluate(unaryExpr->expression);

            switch (unaryExpr->op) {
                case UnaryOp::BANG:
                    return Builder->CreateSelect(IsTruthy(left), Builder->getInt64(FALSE_VAL), Builder->getInt64(TRUE_VAL));
                case UnaryOp::MINUS:
                    return NumberVal(Builder->CreateFNeg(AsNumber(left)));
            }

            std::unreachable();
        }

        Value *evaluate(const Expr &expr) {
            return std::visit(*this, expr);
        }

        void evaluate(const Stmt &stmt) {
            std::visit(*this, stmt);
        }

        void evaluate(const Program &program) {
            beginScope();
            const auto EntryBasicBlock = BasicBlock::Create(*Context, "entry", MainFunction);
            Builder->SetInsertPoint(EntryBasicBlock);
            for (auto &stmt: program) {
                evaluate(stmt);
            }
            Builder->CreateRet(Builder->getInt32(0));
            endScope();
        }

        bool writeIR(const std::string &Filename) const {
            std::error_code ec;
            auto out = raw_fd_ostream(Filename, ec);
            LoxModule->print(out, nullptr);
            out.close();
            return ec.value() == 0;
        }
    };
}// namespace lox
#endif//COMPILER_H
