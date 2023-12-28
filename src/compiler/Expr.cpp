#include "FunctionCompiler.h"
#include "ModuleCompiler.h"
#include <bit>
#include <iostream>
#include <ranges>
#include <vector>

#define DEBUG false

using namespace llvm;
using namespace llvm::sys;

namespace lox {

    Value *FunctionCompiler::evaluate(const Expr &expr) {
        return std::visit(*this, expr);
    }

    Value *FunctionCompiler::operator()(const AssignExprPtr &assignExpr) {
        const auto value = evaluate(assignExpr->value);
        /*const auto global = LoxModule->getNamedGlobal(assignExpr->name.getLexeme());
            Builder->CreateStore(value, global);*/
        const auto current = *variables.begin(assignExpr->name.getLexeme());
        Builder.CreateStore(value, current);
        return value;
    }

    Value *FunctionCompiler::operator()(const BinaryExprPtr &binaryExpr) {
        const auto left = evaluate(binaryExpr->left);
        const auto right = evaluate(binaryExpr->right);

        switch (binaryExpr->op) {
            case BinaryOp::PLUS: {
                const auto IsMaybeStringBlock = Builder.CreateBasicBlock("if.string");
                const auto IsStringBlock = Builder.CreateBasicBlock("is.string");
                const auto IsNumBlock = Builder.CreateBasicBlock("if.num");
                const auto InvalidBlock = Builder.CreateBasicBlock("invalid");
                const auto EndBlock = Builder.CreateBasicBlock("if.end");
                Builder.CreateCondBr(Builder.CreateAnd(Builder.IsNumber(left), Builder.IsNumber(right)), IsNumBlock, IsMaybeStringBlock);
                Builder.SetInsertPoint(IsNumBlock);
                const auto &X = Builder.NumberVal(Builder.CreateFAdd(Builder.AsNumber(left), Builder.AsNumber(right)));
                Builder.CreateBr(EndBlock);

                Builder.SetInsertPoint(IsMaybeStringBlock);
                Builder.CreateCondBr(Builder.CreateAnd(Builder.IsString(left), Builder.IsString(right)), IsStringBlock, InvalidBlock);
                Builder.SetInsertPoint(IsStringBlock);
                const auto &Y = Builder.Concat(left, right);
                Builder.CreateBr(EndBlock);

                Builder.SetInsertPoint(InvalidBlock);
                // TODO: Throw exception here.
                const auto &Z = Builder.getNilVal();
                Builder.CreateBr(EndBlock);

                Builder.SetInsertPoint(EndBlock);

                const auto &Result = Builder.CreatePHI(Builder.getInt64Ty(), 3);
                Result->addIncoming(X, IsNumBlock);
                Result->addIncoming(Y, IsStringBlock);
                Result->addIncoming(Z, InvalidBlock);

                return Result;
            }
            case BinaryOp::MINUS:
                return Builder.NumberVal(Builder.CreateFSub(Builder.AsNumber(left), Builder.AsNumber(right)));
            case BinaryOp::SLASH:
                return Builder.NumberVal(Builder.CreateFDiv(Builder.AsNumber(left), Builder.AsNumber(right)));
            case BinaryOp::STAR:
                return Builder.NumberVal(Builder.CreateFMul(Builder.AsNumber(left), Builder.AsNumber(right)));
            case BinaryOp::GREATER:
                return Builder.BoolVal(Builder.CreateFCmpOGT(Builder.AsNumber(left), Builder.AsNumber(right)));
            case BinaryOp::GREATER_EQUAL:
                return Builder.BoolVal(Builder.CreateFCmpOGE(Builder.AsNumber(left), Builder.AsNumber(right)));
            case BinaryOp::LESS:
                return Builder.BoolVal(Builder.CreateFCmpOLT(Builder.AsNumber(left), Builder.AsNumber(right)));
            case BinaryOp::LESS_EQUAL:
                return Builder.BoolVal(Builder.CreateFCmpOLE(Builder.AsNumber(left), Builder.AsNumber(right)));
            case BinaryOp::BANG_EQUAL:
            case BinaryOp::EQUAL_EQUAL:
                // A == B if both are numbers and they're equal as fp numbers or they're both equal int64 values.
                const auto IsNumBlock = Builder.CreateBasicBlock("if.num");
                const auto NotNumBlock = Builder.CreateBasicBlock("not.num");
                const auto IsStringBlock = Builder.CreateBasicBlock("is.string");
                const auto NotStringBlock = Builder.CreateBasicBlock("not.string");
                const auto EndBlock = Builder.CreateBasicBlock("end");

                Builder.CreateCondBr(Builder.CreateAnd(Builder.IsNumber(left), Builder.IsNumber(right)), IsNumBlock, NotNumBlock);
                Builder.SetInsertPoint(IsNumBlock);
                const auto X = Builder.CreateFCmpOEQ(Builder.AsNumber(left), Builder.AsNumber(right));
                Builder.CreateBr(EndBlock);
                Builder.SetInsertPoint(NotNumBlock);
                Builder.CreateCondBr(Builder.CreateAnd(Builder.IsString(left), Builder.IsString(right)), IsStringBlock, NotStringBlock);
                Builder.SetInsertPoint(IsStringBlock);
                const auto Y = Builder.StrEquals(left, right);
                Builder.CreateBr(EndBlock);
                Builder.SetInsertPoint(NotStringBlock);
                const auto Z = Builder.CreateICmpEQ(left, right);
                Builder.CreateBr(EndBlock);
                Builder.SetInsertPoint(EndBlock);

                const auto Result = Builder.CreatePHI(Builder.getInt1Ty(), 3);
                Result->addIncoming(X, IsNumBlock);
                Result->addIncoming(Y, IsStringBlock);
                Result->addIncoming(Z, NotStringBlock);

                return Builder.BoolVal(binaryExpr->op == BinaryOp::EQUAL_EQUAL ? Result : Builder.CreateNot(Result));
        }

        std::unreachable();
    }

    Value *FunctionCompiler::operator()(const CallExprPtr &callExpr) {
        const auto value = evaluate(callExpr->callee);
        const auto callee = Builder.AsFunction(value);

        const std::vector<Type *> paramTypes(callExpr->arguments.size() + 1, Builder.getInt64Ty());
        auto paramValues = to<std::vector<Value *>>(
            callExpr->arguments | std::views::transform([&](const auto &p) -> Value * {
                return evaluate(p);
            })
        );
        // Insert a extra parameter containing the function obj itself, to support self referencing functions.
        paramValues.insert(paramValues.begin(), value);

        FunctionType *FT = FunctionType::get(IntegerType::getInt64Ty(Builder.getContext()), paramTypes, false);

        auto x = Builder.CreateLoad(
            Builder.getPtrTy(),
            Builder.CreateStructGEP(
                Builder.getModule().getStructType(ObjType::FUNCTION),
                callee, 2
            ),
            "func"
        );

#if DEBUG
        Builder.PrintF({Builder.CreateGlobalStringPtr("Calling func at %p with function ptr %p\n"), callee, x});
#endif

        return Builder.CreateCall(FT, x, paramValues);
    }

    Value *FunctionCompiler::operator()(const GetExprPtr &getExpr) const {
        throw std::runtime_error("not implemented");
    }

    Value *FunctionCompiler::operator()(const SetExprPtr &setExpr) const {
        throw std::runtime_error("not implemented");
    }

    Value *FunctionCompiler::operator()(const ThisExprPtr &thisExpr) const {
        throw std::runtime_error("not implemented");
    }

    Value *FunctionCompiler::operator()(const SuperExprPtr &superExpr) const {
        throw std::runtime_error("not implemented");
    }

    Value *FunctionCompiler::operator()(const VarExprPtr &varExpr) const {
        const auto value = variables.lookup(varExpr->name.getLexeme());
        if (!value) {
            std::cerr << "Undefined variable '" << varExpr->name.getLexeme() << "'" << std::endl;
        }
        return Builder.CreateLoad(Builder.getInt64Ty(), value);
    }

    Value *FunctionCompiler::operator()(const GroupingExprPtr &groupingExpr) {
        return evaluate(groupingExpr->expression);
    }

    Value *FunctionCompiler::operator()(const LiteralExprPtr &literalExpr) {
        return std::visit(
            overloaded{
                [this](const bool value) -> Value * {
                    return Builder.BoolVal(value ? Builder.getTrue() : Builder.getFalse());
                },
                [this](const double double_value) -> Value * {
                    return Builder.getInt64(std::bit_cast<int64_t>(double_value));
                },
                [this](const std::string_view string_value) -> Value * {
                    /*if (strings.contains(string_value))
                        return strings.at(string_value);*/

                    const auto value = Builder.AllocateString(
                        Builder.getModule().getNamedGlobal("objects"),
                        Builder.CreateGlobalStringPtr(string_value),
                        Builder.getInt32(string_value.length())
                    );

                    //strings[string_value] = value;

                    return value;
                },
                [this](const std::nullptr_t) -> Value * { return Builder.getNilVal(); },
            },
            literalExpr->literal
        );
    }

    Value *FunctionCompiler::operator()(const LogicalExprPtr &logicalExpr) {
        const auto left = evaluate(logicalExpr->left);

        switch (logicalExpr->op) {
            case LogicalOp::AND:
            case LogicalOp::OR: {
                const auto LeftIsTruthyBlock = Builder.CreateBasicBlock("if.left.truthy");
                const auto LeftNotTruthyBlock = Builder.CreateBasicBlock("if.left.nottruthy");
                const auto EndBlock = Builder.CreateBasicBlock("end");
                Builder.CreateCondBr(
                    Builder.IsTruthy(left),
                    logicalExpr->op == LogicalOp::OR ? LeftIsTruthyBlock : LeftNotTruthyBlock,
                    logicalExpr->op == LogicalOp::OR ? LeftNotTruthyBlock : LeftIsTruthyBlock
                );
                Builder.SetInsertPoint(LeftNotTruthyBlock);
                const auto X = Builder.IsTruthy(evaluate(logicalExpr->right));
                const auto EndLeftNotTruthyBlock = Builder.GetInsertBlock();
                Builder.CreateBr(EndBlock);
                Builder.SetInsertPoint(LeftIsTruthyBlock);
                const auto Y = logicalExpr->op == LogicalOp::OR ? Builder.getTrue() : Builder.getFalse();
                Builder.CreateBr(EndBlock);
                Builder.SetInsertPoint(EndBlock);

                const auto Result = Builder.CreatePHI(Builder.getInt1Ty(), 2);
                Result->addIncoming(X, EndLeftNotTruthyBlock);
                Result->addIncoming(Y, LeftIsTruthyBlock);

                return Builder.BoolVal(Result);
            }
        }
        std::unreachable();
    }

    Value *FunctionCompiler::operator()(const UnaryExprPtr &unaryExpr) {
        const auto left = evaluate(unaryExpr->expression);

        switch (unaryExpr->op) {
            case UnaryOp::BANG:
                return Builder.BoolVal(Builder.CreateSelect(Builder.IsTruthy(left), Builder.getFalse(), Builder.getTrue()));
            case UnaryOp::MINUS:
                return Builder.NumberVal(Builder.CreateFNeg(Builder.AsNumber(left)));
        }

        std::unreachable();
    }

}// namespace lox
