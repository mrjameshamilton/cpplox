#include "Callstack.h"
#include "FunctionCompiler.h"
#include "ModuleCompiler.h"
#include <bit>
#include <iostream>
#include <ranges>
#include <string_view>
#include <vector>

#define DEBUG false

using namespace llvm;
using namespace llvm::sys;
using namespace std::string_view_literals;

namespace lox {

    Value *FunctionCompiler::evaluate(const Expr &expr) {
        return std::visit(*this, expr);
    }

    Value *FunctionCompiler::operator()(const AssignExprPtr &assignExpr) {
        const auto value = evaluate(assignExpr->value);
        const auto variable = lookupVariable(*assignExpr);
        Builder.CreateStore(value, variable);
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
                Builder.RuntimeError(
                    binaryExpr->token.getLine(),
                    "Operands must be numbers.\n",
                    {},
                    Builder.getFunction()
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
                Builder.RuntimeError(
                    binaryExpr->token.getLine(),
                    "Operands must be two numbers or two strings.\n",
                    {},
                    Builder.getFunction()
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
                // All strings are interned, so we don't need to check characters for equality: strings
                // that have the same character sequence will have the same address.
                const auto IsNumBlock = Builder.CreateBasicBlock("if.num");
                const auto NotNumBlock = Builder.CreateBasicBlock("not.num");
                const auto EndBlock = Builder.CreateBasicBlock("end");

                Builder.CreateCondBr(Builder.CreateAnd(Builder.IsNumber(left), Builder.IsNumber(right)), IsNumBlock, NotNumBlock);
                Builder.SetInsertPoint(IsNumBlock);
                const auto X = Builder.CreateFCmpOEQ(Builder.AsNumber(left), Builder.AsNumber(right));
                Builder.CreateBr(EndBlock);
                Builder.SetInsertPoint(NotNumBlock);
                const auto Y = Builder.CreateICmpEQ(left, right);
                Builder.CreateBr(EndBlock);
                Builder.SetInsertPoint(EndBlock);

                const auto Result = Builder.CreatePHI(Builder.getInt1Ty(), 2);
                Result->addIncoming(X, IsNumBlock);
                Result->addIncoming(Y, NotNumBlock);

                return Builder.BoolVal(binaryExpr->op == BinaryOp::EQUAL_EQUAL ? Result : Builder.CreateNot(Result));
        }

        std::unreachable();
    }

    void CheckArity(FunctionCompiler &Compiler, BasicBlock *CallBlock, Value *arity, const unsigned int actual, const unsigned int line) {
        LoxBuilder &Builder = Compiler.getBuilder();

        const auto WrongArityBlock = Builder.CreateBasicBlock("wrong.arity");

        Builder.CreateCondBr(Builder.CreateICmpEQ(arity, Builder.getInt32(actual)), CallBlock, WrongArityBlock);

        Builder.SetInsertPoint(WrongArityBlock);

        Builder.RuntimeError(
            line,
            "Expected %d arguments but got %d.\n",
            {arity, Builder.getInt32(actual)},
            Builder.getFunction()
        );
        Builder.CreateUnreachable();
    }

    Value *FunctionCompiler::call(Value *receiver, Value *closure, std::vector<Value *> paramValues, const unsigned int line) {
        assert(receiver->getType() == Builder.getInt64Ty());
        assert(closure->getType() == Builder.getPtrTy());

        const auto function = Builder.CreateLoad(Builder.getPtrTy(), Builder.CreateStructGEP(Builder.getModule().getStructType(ObjType::CLOSURE), closure, 1));
        const auto upvalues = Builder.CreateLoad(Builder.getPtrTy(), Builder.CreateStructGEP(Builder.getModule().getStructType(ObjType::CLOSURE), closure, 2));

        std::vector<Type *> paramTypes(paramValues.size(), Builder.getInt64Ty());

        paramTypes.insert(paramTypes.begin(), Builder.getInt64Ty());
        paramValues.insert(paramValues.begin(), receiver);
        paramTypes.insert(paramTypes.begin(), Builder.getPtrTy());
        paramValues.insert(paramValues.begin(), upvalues);

        FunctionType *FT = FunctionType::get(IntegerType::getInt64Ty(Builder.getContext()), paramTypes, false);

        const auto functionPtr = Builder.CreateLoad(
            Builder.getPtrTy(),
            Builder.CreateStructGEP(
                Builder.getModule().getStructType(ObjType::FUNCTION),
                function, 2
            ),
            "func"
        );

        // Check arity.
        const auto arity = Builder.CreateLoad(
            Builder.getInt32Ty(),
            Builder.CreateStructGEP(Builder.getModule().getStructType(ObjType::FUNCTION), function, 1),
            "arity"
        );

        const auto CallBlock = Builder.CreateBasicBlock("call");

        CheckArity(*this, CallBlock, arity, paramValues.size() - 2, line);

        Builder.SetInsertPoint(CallBlock);
#if DEBUG
        Builder.PrintF({Builder.CreateGlobalCachedString("Calling func at %p with function ptr %p\n"), callee, functionPtr});
#endif

        Push(Builder, Builder.getInt32(line), Builder.CreateGlobalCachedString(Builder.getFunction()->getName()));
        const auto result = Builder.CreateCall(FT, functionPtr, paramValues);
        Pop(Builder);

        return result;
    }

    Value *FunctionCompiler::operator()(const CallExprPtr &callExpr) {
        const auto value = evaluate(callExpr->callee);
        const auto valuePtr = Builder.AsObj(value);

        const auto paramValues = to<std::vector<Value *>>(
            callExpr->arguments | std::views::transform([&](const auto &p) -> Value * {
                return evaluate(p);
            })
        );

        const auto IsClosureBlock = Builder.CreateBasicBlock("is.closure");
        const auto CheckMethodBlock = Builder.CreateBasicBlock("check.method");
        const auto CheckClassBlock = Builder.CreateBasicBlock("check.class");
        const auto IsClassBlock = Builder.CreateBasicBlock("is.class");
        const auto IsMethodBlock = Builder.CreateBasicBlock("is.method");
        const auto NotCallableBlock = Builder.CreateBasicBlock("not.callable");
        const auto ExecuteBlock = Builder.CreateBasicBlock("execute");
        const auto EndBlock = Builder.CreateBasicBlock("end.block");

        Builder.CreateCondBr(Builder.IsClosure(value), IsClosureBlock, CheckClassBlock);

        Builder.SetInsertPoint(CheckClassBlock);
        Builder.CreateCondBr(Builder.IsClass(value), IsClassBlock, CheckMethodBlock);
        Builder.SetInsertPoint(IsClassBlock);
        const auto klass = valuePtr;
        const auto initializer = Builder.TableGet(Builder.CreateLoad(Builder.getPtrTy(), Builder.CreateObjStructGEP(ObjType::CLASS, klass, 2)), Builder.AllocateString("init"));

        const auto instance = Builder.AllocateInstance(klass);
        const auto instanceVal = Builder.ObjVal(instance);

        const auto EndClassBlock = Builder.CreateBasicBlock("class.end");
        const auto HasInitializerBlock = Builder.CreateBasicBlock("call.init");
        const auto NoInitializerBlock = Builder.CreateBasicBlock("call.noinit");

        Builder.CreateCondBr(Builder.IsUninitialized(initializer), NoInitializerBlock, HasInitializerBlock);
        Builder.SetInsertPoint(HasInitializerBlock);
        call(instanceVal, Builder.AsObj(initializer), paramValues, callExpr->keyword.getLine());
        Builder.CreateBr(EndClassBlock);

        Builder.SetInsertPoint(NoInitializerBlock);
        CheckArity(*this, EndClassBlock, Builder.getInt32(0), paramValues.size(), callExpr->keyword.getLine());

        Builder.SetInsertPoint(EndClassBlock);

        Builder.CreateBr(EndBlock);

        Builder.SetInsertPoint(CheckMethodBlock);
        Builder.CreateCondBr(Builder.IsBoundMethod(value), IsMethodBlock, NotCallableBlock);
        Builder.SetInsertPoint(IsMethodBlock);
        const auto receiverObjVal = Builder.CreateLoad(Builder.getInt64Ty(), Builder.CreateObjStructGEP(ObjType::BOUND_METHOD, valuePtr, 1));
        const auto methodPtr = Builder.CreateLoad(Builder.getPtrTy(), Builder.CreateObjStructGEP(ObjType::BOUND_METHOD, valuePtr, 2));
        Builder.CreateBr(ExecuteBlock);

        Builder.SetInsertPoint(NotCallableBlock);
        Builder.RuntimeError(
            callExpr->keyword.getLine(),
            "Can only call functions and classes.\n",
            {},
            Builder.getFunction()
        );
        Builder.CreateUnreachable();

        Builder.SetInsertPoint(IsClosureBlock);
        const auto closurePtr = valuePtr;
        Builder.CreateBr(ExecuteBlock);

        Builder.SetInsertPoint(ExecuteBlock);

        // The function is wrapped in a closure.
        const auto closure = Builder.CreatePHI(Builder.getPtrTy(), 2);
        closure->addIncoming(closurePtr, IsClosureBlock);
        closure->addIncoming(methodPtr, IsMethodBlock);
        const auto receiver = Builder.CreatePHI(Builder.getInt64Ty(), 2);
        receiver->addIncoming(receiverObjVal, IsMethodBlock);
        receiver->addIncoming(Builder.getNilVal(), IsClosureBlock);

        const auto functionReturnVal = call(receiver, closure, paramValues, callExpr->keyword.getLine());
        const auto EndCall = Builder.GetInsertBlock();

        Builder.CreateBr(EndBlock);
        Builder.SetInsertPoint(EndBlock);
        const auto result = Builder.CreatePHI(Builder.getInt64Ty(), 2);
        result->addIncoming(instanceVal, EndClassBlock);
        result->addIncoming(functionReturnVal, EndCall);

        return result;
    }

    void CheckInstance(LoxBuilder &Builder, const std::string_view message, const unsigned int line, Value *instance) {
        const auto NotInstanceBlock = Builder.CreateBasicBlock("not.instance");
        const auto EndBlock = Builder.CreateBasicBlock("end");

        Builder.CreateCondBr(Builder.IsInstance(instance), EndBlock, NotInstanceBlock);

        Builder.SetInsertPoint(NotInstanceBlock);
        Builder.RuntimeError(line, message, {}, Builder.getFunction());
        Builder.CreateUnreachable();

        Builder.SetInsertPoint(EndBlock);
    }

    Value *FunctionCompiler::operator()(const GetExprPtr &getExpr) {
        Value *object = evaluate(getExpr->object);
        CheckInstance(Builder, "Only instances have properties.\n"sv, getExpr->name.getLine(), object);

        const auto instance = Builder.AsObj(object);
        const auto fields = Builder.CreateLoad(Builder.getPtrTy(), Builder.CreateObjStructGEP(ObjType::INSTANCE, instance, 2));

        const auto key = Builder.AllocateString(getExpr->name.getLexeme(), getExpr->name.getLexeme());
        const auto result = Builder.TableGet(fields, key);

        const auto IsMethodBlock = Builder.CreateBasicBlock("property.ismethod");
        const auto IsDefinedBlock = Builder.CreateBasicBlock("property.defined");

        const auto BeforeBlock = Builder.GetInsertBlock();

        Builder.CreateCondBr(Builder.IsUninitialized(result), IsMethodBlock, IsDefinedBlock);

        Builder.SetInsertPoint(IsMethodBlock);

        const auto klass = Builder.CreateLoad(Builder.getPtrTy(), Builder.CreateObjStructGEP(ObjType::INSTANCE, instance, 1));
        const auto bound = Builder.ObjVal(Builder.BindMethod(klass, instance, key, getExpr->name.getLine(), enclosing == nullptr ? nullptr : Builder.getFunction()));

        Builder.CreateBr(IsDefinedBlock);

        Builder.SetInsertPoint(IsDefinedBlock);
        const auto R = Builder.CreatePHI(Builder.getInt64Ty(), 2);
        R->addIncoming(result, BeforeBlock);
        R->addIncoming(bound, IsMethodBlock);

        return R;
    }

    Value *FunctionCompiler::operator()(const SetExprPtr &setExpr) {
        const auto object = evaluate(setExpr->object);
        CheckInstance(Builder, "Only instances have fields.\n"sv, setExpr->name.getLine(), object);

        const auto instance = Builder.AsObj(object);
        const auto value = evaluate(setExpr->value);
        const auto fields = Builder.CreateLoad(Builder.getPtrTy(), Builder.CreateObjStructGEP(ObjType::INSTANCE, instance, 2));

        Builder.TableSet(fields, Builder.AllocateString(setExpr->name.getLexeme(), "key"), value);

        return value;
    }

    Value *FunctionCompiler::operator()(const ThisExprPtr &thisExpr) {
        return Builder.CreateLoad(Builder.getInt64Ty(), lookupVariable(*thisExpr));
    }

    Value *FunctionCompiler::operator()(const SuperExprPtr &superExpr) {
        static auto assignable = Assignable{Token(THIS, "this"sv, nullptr, superExpr->name.getLine())};
        const auto instance = Builder.CreateLoad(Builder.getInt64Ty(), lookupVariable(assignable));
        const auto klass = Builder.CreateLoad(Builder.getInt64Ty(), lookupVariable(*superExpr));
        const auto method = Builder.BindMethod(
            Builder.AsObj(klass),
            Builder.AsObj(instance),
            Builder.AllocateString(superExpr->method.getLexeme()),
            superExpr->name.getLine(),
            enclosing == nullptr ? nullptr : Builder.getFunction()
        );
        return Builder.ObjVal(method);
    }

    Value *FunctionCompiler::operator()(const VarExprPtr &varExpr) {
        const auto value = lookupVariable(*varExpr);
        return Builder.CreateLoad(Builder.getInt64Ty(), value);
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
                    return Builder.ObjVal(Builder.AllocateString(string_value));
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
                const auto EndLeftTruthyBlock = Builder.GetInsertBlock();
                Builder.CreateBr(EndBlock);
                Builder.SetInsertPoint(EndBlock);

                const auto Result = Builder.CreatePHI(Builder.getInt64Ty(), 2);
                Result->addIncoming(X, EndLeftTruthyBlock);
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
                Builder.RuntimeError(
                    unaryExpr->token.getLine(),
                    "Operand must be a number.\n",
                    {},
                    Builder.getFunction()
                );
                Builder.CreateBr(EndBlock);
                Builder.SetInsertPoint(EndBlock);

                return Builder.NumberVal(Builder.CreateFNeg(Builder.AsNumber(left)));
            }
        }

        std::unreachable();
    }

}// namespace lox
