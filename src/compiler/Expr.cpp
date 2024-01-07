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

        if (const auto variable = lookupVariable(*assignExpr)) {
            Builder.CreateStore(value, variable);
        } else {
            static const auto fmt = Builder.CreateGlobalStringPtr("Undefined variable '%s'.\n");
            Builder.RuntimeError(
                assignExpr->name.getLine(),
                fmt,
                {Builder.CreateGlobalStringPtr(assignExpr->name.getLexeme())},
                enclosing == nullptr ? nullptr : Builder.getFunction()
            );
        }

        return value;
    }

    Value *FunctionCompiler::operator()(const BinaryExprPtr &binaryExpr) {
        const auto left = evaluate(binaryExpr->left);
        const auto right = evaluate(binaryExpr->right);

        switch (binaryExpr->op) {
            case BinaryOp::MINUS:
            case BinaryOp::SLASH:
            case BinaryOp::STAR:
            case BinaryOp::GREATER:
            case BinaryOp::GREATER_EQUAL:
            case BinaryOp::LESS:
            case BinaryOp::LESS_EQUAL: {
                const auto InvalidNumBlock = Builder.CreateBasicBlock("if.num");
                const auto EndBlock = Builder.CreateBasicBlock("if.end");

                Builder.CreateCondBr(Builder.CreateAnd(Builder.IsNumber(left), Builder.IsNumber(right)), EndBlock, InvalidNumBlock);
                Builder.SetInsertPoint(InvalidNumBlock);
                static const auto msg = Builder.CreateGlobalStringPtr("Operands must be numbers.\n");
                Builder.RuntimeError(
                    binaryExpr->token.getLine(),
                    msg,
                    {},
                    enclosing == nullptr ? nullptr : Builder.getFunction()
                );
                Builder.CreateBr(EndBlock);

                Builder.SetInsertPoint(EndBlock);
                break;
            }
            default: {
                // No need to check other binary ops here.
            }
        }

        switch (binaryExpr->op) {
            case BinaryOp::PLUS: {
                const auto IsMaybeStringBlock = Builder.CreateBasicBlock("if.string");
                const auto IsStringBlock = Builder.CreateBasicBlock("is.string");
                const auto IsNumBlock = Builder.CreateBasicBlock("if.num");
                const auto InvalidBlock = Builder.CreateBasicBlock("invalid");
                const auto EndBlock = Builder.CreateBasicBlock("if.end");
                Builder.CreateCondBr(Builder.CreateAnd(Builder.IsNumber(left), Builder.IsNumber(right)), IsNumBlock, IsMaybeStringBlock);
                Builder.SetInsertPoint(IsNumBlock);
                const auto X = Builder.NumberVal(Builder.CreateFAdd(Builder.AsNumber(left), Builder.AsNumber(right)));
                Builder.CreateBr(EndBlock);

                Builder.SetInsertPoint(IsMaybeStringBlock);
                Builder.CreateCondBr(Builder.CreateAnd(Builder.IsString(left), Builder.IsString(right)), IsStringBlock, InvalidBlock);
                Builder.SetInsertPoint(IsStringBlock);
                const auto Y = Builder.Concat(left, right);
                Builder.CreateBr(EndBlock);

                Builder.SetInsertPoint(InvalidBlock);
                static const auto msg = Builder.CreateGlobalStringPtr("Operands must be two numbers or two strings.\n");
                Builder.RuntimeError(
                    binaryExpr->token.getLine(),
                    msg,
                    {},
                    enclosing == nullptr ? nullptr : Builder.getFunction()
                );

                const auto Z = Builder.getNilVal();
                Builder.CreateBr(EndBlock);

                Builder.SetInsertPoint(EndBlock);

                const auto Result = Builder.CreatePHI(Builder.getInt64Ty(), 2);
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

        const auto IsCallable = Builder.CreateBasicBlock("is.callable");
        const auto NotCallableBlock = Builder.CreateBasicBlock("not.callable");

        Builder.CreateCondBr(Builder.IsFunction(value), IsCallable, NotCallableBlock);
        Builder.SetInsertPoint(NotCallableBlock);

        static const auto fmt = Builder.CreateGlobalStringPtr("Can only call functions and classes.\n");
        Builder.RuntimeError(
            callExpr->keyword.getLine(),
            fmt,
            {},
            enclosing == nullptr ? nullptr : Builder.getFunction()
        );
        Builder.CreateUnreachable();

        Builder.SetInsertPoint(IsCallable);
        const auto callee = Builder.AsFunction(value);

        const std::vector<Type *> paramTypes(callExpr->arguments.size(), Builder.getInt64Ty());
        const auto paramValues = to<std::vector<Value *>>(
            callExpr->arguments | std::views::transform([&](const auto &p) -> Value * {
                return evaluate(p);
            })
        );

        FunctionType *FT = FunctionType::get(IntegerType::getInt64Ty(Builder.getContext()), paramTypes, false);

        const auto functionPtr = Builder.CreateLoad(
            Builder.getPtrTy(),
            Builder.CreateStructGEP(
                Builder.getModule().getStructType(ObjType::FUNCTION),
                callee, 2
            ),
            "func"
        );

        // Check arity.
        const auto arity = Builder.CreateLoad(
            Builder.getInt32Ty(),
            Builder.CreateStructGEP(Builder.getModule().getStructType(ObjType::FUNCTION), callee, 1),
            "arity"
        );

        const auto CallBlock = Builder.CreateBasicBlock("call");
        const auto WrongArityBlock = Builder.CreateBasicBlock("wrong.arity");

        const auto actual = Builder.getInt32(paramValues.size());
        Builder.CreateCondBr(Builder.CreateICmpEQ(arity, actual), CallBlock, WrongArityBlock);

        Builder.SetInsertPoint(WrongArityBlock);

        static const auto fmt2 = Builder.CreateGlobalStringPtr("Expected %d arguments but got %d.\n");
        Builder.RuntimeError(
            callExpr->keyword.getLine(),
            fmt2,
            {arity, actual},
            enclosing == nullptr ? nullptr : Builder.getFunction()
        );
        Builder.CreateUnreachable();

        Builder.SetInsertPoint(CallBlock);
#if DEBUG
        Builder.PrintF({Builder.CreateGlobalStringPtr("Calling func at %p with function ptr %p\n"), callee, x});
#endif
        return Builder.CreateCall(FT, functionPtr, paramValues);
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

    Value *FunctionCompiler::operator()(const VarExprPtr &varExpr) {
        if (const auto value = lookupVariable(*varExpr)) {
            return Builder.CreateLoad(Builder.getInt64Ty(), value);
        }

        static const auto fmt = Builder.CreateGlobalStringPtr("Undefined variable '%s'.\n");
        Builder.RuntimeError(
            varExpr->name.getLine(),
            fmt,
            {Builder.CreateGlobalStringPtr(varExpr->name.getLexeme())},
            enclosing == nullptr ? nullptr : Builder.getFunction()
        );

        return Builder.getNilVal();
    }

    Value *FunctionCompiler::operator()(const GroupingExprPtr &groupingExpr) {
        return evaluate(groupingExpr->expression);
    }

    Value *FunctionCompiler::operator()(const LiteralExprPtr &literalExpr) {
        return std::visit(
            overloaded{
                [this](const bool value) -> Value * {
                    return value ? Builder.getTrueVal() : Builder.getFalseVal();
                },
                [this](const double double_value) -> Value * {
                    return Builder.getInt64(std::bit_cast<int64_t>(double_value));
                },
                [this](const std::string_view string_value) -> Value * {
                    const auto value = Builder.AllocateString(
                        Builder.CreateGlobalStringPtr(string_value),
                        Builder.getInt32(string_value.length())
                    );

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
            case LogicalOp::AND: {
                const auto LeftIsTruthyBlock = Builder.CreateBasicBlock("if.left.truthy");
                const auto LeftNotTruthyBlock = Builder.CreateBasicBlock("if.left.nottruthy");
                const auto EndBlock = Builder.CreateBasicBlock("end");
                Builder.CreateCondBr(
                    Builder.IsTruthy(left),
                    LeftIsTruthyBlock,
                    LeftNotTruthyBlock
                );
                Builder.SetInsertPoint(LeftNotTruthyBlock);
                const auto Y = left;
                const auto EndLeftNotTruthyBlock = Builder.GetInsertBlock();
                Builder.CreateBr(EndBlock);
                Builder.SetInsertPoint(LeftIsTruthyBlock);
                const auto X = evaluate(logicalExpr->right);
                Builder.CreateBr(EndBlock);
                Builder.SetInsertPoint(EndBlock);

                const auto Result = Builder.CreatePHI(Builder.getInt64Ty(), 2);
                Result->addIncoming(X, LeftIsTruthyBlock);
                Result->addIncoming(Y, EndLeftNotTruthyBlock);

                return Result;
            }
            case LogicalOp::OR: {
                const auto LeftIsTruthyBlock = Builder.CreateBasicBlock("if.left.truthy");
                const auto LeftNotTruthyBlock = Builder.CreateBasicBlock("if.left.nottruthy");
                const auto EndBlock = Builder.CreateBasicBlock("end");
                Builder.CreateCondBr(
                    Builder.IsTruthy(left),
                    LeftIsTruthyBlock,
                    LeftNotTruthyBlock
                );
                Builder.SetInsertPoint(LeftNotTruthyBlock);
                const auto right = evaluate(logicalExpr->right);
                const auto Y = Builder.CreateSelect(Builder.IsTruthy(right), right, left);
                const auto EndLeftNotTruthyBlock = Builder.GetInsertBlock();
                Builder.CreateBr(EndBlock);
                Builder.SetInsertPoint(LeftIsTruthyBlock);
                const auto X = left;
                Builder.CreateBr(EndBlock);
                Builder.SetInsertPoint(EndBlock);

                const auto Result = Builder.CreatePHI(Builder.getInt64Ty(), 2);
                Result->addIncoming(X, LeftIsTruthyBlock);
                Result->addIncoming(Y, EndLeftNotTruthyBlock);

                return Result;
            }
        }
        std::unreachable();
    }

    Value *FunctionCompiler::operator()(const UnaryExprPtr &unaryExpr) {
        const auto left = evaluate(unaryExpr->expression);

        switch (unaryExpr->op) {
            case UnaryOp::BANG:
                return Builder.BoolVal(Builder.CreateSelect(Builder.IsTruthy(left), Builder.getFalse(), Builder.getTrue()));
            case UnaryOp::MINUS: {
                const auto InvalidNumBlock = Builder.CreateBasicBlock("if.num");
                const auto EndBlock = Builder.CreateBasicBlock("if.end");

                Builder.CreateCondBr(Builder.IsNumber(left), EndBlock, InvalidNumBlock);
                Builder.SetInsertPoint(InvalidNumBlock);
                static const auto msg = Builder.CreateGlobalStringPtr("Operand must be a number.\n");
                Builder.RuntimeError(
                    unaryExpr->token.getLine(),
                    msg,
                    {},
                    enclosing == nullptr ? nullptr : Builder.getFunction()
                );
                Builder.CreateBr(EndBlock);
                Builder.SetInsertPoint(EndBlock);

                return Builder.NumberVal(Builder.CreateFNeg(Builder.AsNumber(left)));
            }
        }

        std::unreachable();
    }

}// namespace lox
