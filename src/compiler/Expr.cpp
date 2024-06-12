#include "Callstack.h"
#include "FunctionCompiler.h"
#include "ModuleCompiler.h"

#include <bit>
#include <iostream>
#include <ranges>
#include <string_view>
#include <vector>

using namespace llvm;
using namespace llvm::sys;
using namespace std::string_view_literals;

namespace lox {

    Value *FunctionCompiler::evaluate(const Expr &expr) {
        return std::visit(*this, expr);
    }

    Value *FunctionCompiler::operator()(const AssignExprPtr &assignExpr) {
        auto *const value = evaluate(assignExpr->value);
        auto *const variable = lookupVariable(*assignExpr);
        // Copy metadata from the value to the variable.
        if (isa<Instruction>(value) && isa<Instruction>(variable)) {
            const auto left = cast<Instruction>(variable);
            left->eraseMetadataIf([](unsigned, auto *) { return true; });
            left->copyMetadata(*cast<Instruction>(value));
        }
        Builder.CreateStore(value, variable);
        return value;
    }

    Value *FunctionCompiler::operator()(const BinaryExprPtr &binaryExpr) {
        auto *const left = evaluate(binaryExpr->left);
        auto *const right = evaluate(binaryExpr->right);

        switch (binaryExpr->op) {
            case BinaryOp::MINUS:
            case BinaryOp::SLASH:
            case BinaryOp::STAR:
            case BinaryOp::GREATER:
            case BinaryOp::GREATER_EQUAL:
            case BinaryOp::LESS:
            case BinaryOp::LESS_EQUAL: {
                auto *const InvalidNumBlock = Builder.CreateBasicBlock("if.not.num");
                auto *const EndBlock = Builder.CreateBasicBlock("if.num");

                Builder.CreateCondBr(Builder.CreateAnd(Builder.IsNumber(left), Builder.IsNumber(right)), EndBlock, InvalidNumBlock);
                Builder.SetInsertPoint(InvalidNumBlock);
                Builder.RuntimeError(
                    binaryExpr->token.getLine(),
                    "Operands must be numbers.\n",
                    {},
                    Builder.getFunction()
                );

                Builder.SetInsertPoint(EndBlock);
                break;
            }
            default: {
                // No need to check other binary ops here.
            }
        }

        switch (binaryExpr->op) {
            case BinaryOp::PLUS: {
                auto *const IsMaybeStringBlock = Builder.CreateBasicBlock("if.string");
                auto *const IsStringBlock = Builder.CreateBasicBlock("is.string");
                auto *const IsNumBlock = Builder.CreateBasicBlock("if.num");
                auto *const InvalidBlock = Builder.CreateBasicBlock("invalid");
                auto *const EndBlock = Builder.CreateBasicBlock("if.end");
                Builder.CreateCondBr(Builder.CreateAnd(Builder.IsNumber(left), Builder.IsNumber(right)), IsNumBlock, IsMaybeStringBlock);
                Builder.SetInsertPoint(IsNumBlock);
                auto *const X = Builder.NumberVal(Builder.CreateFAdd(Builder.AsNumber(left), Builder.AsNumber(right)));
                Builder.CreateBr(EndBlock);

                Builder.SetInsertPoint(IsMaybeStringBlock);
                Builder.CreateCondBr(Builder.CreateAnd(Builder.IsString(left), Builder.IsString(right)), IsStringBlock, InvalidBlock);
                Builder.SetInsertPoint(IsStringBlock);
                auto *const Y = insertTemp(Builder.ObjVal(Builder.Concat(left, right)), "string concat");
                Builder.CreateBr(EndBlock);

                Builder.SetInsertPoint(InvalidBlock);
                Builder.RuntimeError(
                    binaryExpr->token.getLine(),
                    "Operands must be two numbers or two strings.\n",
                    {},
                    Builder.getFunction()
                );

                Builder.SetInsertPoint(EndBlock);

                auto *const Result = Builder.CreatePHI(Builder.getInt64Ty(), 2);
                Result->addIncoming(X, IsNumBlock);
                Result->addIncoming(Y, IsStringBlock);

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
                auto *const IsNumBlock = Builder.CreateBasicBlock("if.num");
                auto *const NotNumBlock = Builder.CreateBasicBlock("not.num");
                auto *const EndBlock = Builder.CreateBasicBlock("end");

                Builder.CreateCondBr(Builder.CreateAnd(Builder.IsNumber(left), Builder.IsNumber(right)), IsNumBlock, NotNumBlock);
                Builder.SetInsertPoint(IsNumBlock);
                auto *const X = Builder.CreateFCmpOEQ(Builder.AsNumber(left), Builder.AsNumber(right));
                Builder.CreateBr(EndBlock);
                Builder.SetInsertPoint(NotNumBlock);
                auto *const Y = Builder.CreateICmpEQ(left, right);
                Builder.CreateBr(EndBlock);
                Builder.SetInsertPoint(EndBlock);

                auto *const Result = Builder.CreatePHI(Builder.getInt1Ty(), 2);
                Result->addIncoming(X, IsNumBlock);
                Result->addIncoming(Y, NotNumBlock);

                return Builder.BoolVal(binaryExpr->op == BinaryOp::EQUAL_EQUAL ? Result : Builder.CreateNot(Result));
        }

        std::unreachable();
    }

    void CheckArity(FunctionCompiler &Compiler, BasicBlock *CallBlock, Value *arity, const unsigned int actual, const unsigned int line) {
        LoxBuilder &Builder = Compiler.getBuilder();

        auto *const WrongArityBlock = Builder.CreateBasicBlock("wrong.arity");

        Builder.CreateCondBr(Builder.CreateICmpEQ(arity, Builder.getInt32(actual)), CallBlock, WrongArityBlock);

        Builder.SetInsertPoint(WrongArityBlock);

        Builder.RuntimeError(
            line,
            "Expected %d arguments but got %d.\n",
            {arity, Builder.getInt32(actual)},
            Builder.getFunction()
        );
    }

    Value *FunctionCompiler::call(Value *receiver, Value *closure, std::vector<Value *> paramValues, const unsigned int line) {
        assert(receiver->getType() == Builder.getInt64Ty());
        assert(closure->getType() == Builder.getPtrTy());

        CheckStackOverflow(Builder, Builder.getInt32(line), Builder.CreateGlobalCachedString(Builder.getFunction()->getName()));

        auto *const function = Builder.CreateLoad(Builder.getPtrTy(), Builder.CreateStructGEP(Builder.getModule().getStructType(ObjType::CLOSURE), closure, 1));
        auto *const upvalues = Builder.CreateLoad(Builder.getPtrTy(), Builder.CreateStructGEP(Builder.getModule().getStructType(ObjType::CLOSURE), closure, 2));

        std::vector<Type *> paramTypes(paramValues.size(), Builder.getInt64Ty());

        paramTypes.insert(paramTypes.begin(), Builder.getInt64Ty());
        paramValues.insert(paramValues.begin(), receiver);
        paramTypes.insert(paramTypes.begin(), Builder.getPtrTy());
        paramValues.insert(paramValues.begin(), upvalues);

        FunctionType *FT = FunctionType::get(IntegerType::getInt64Ty(Builder.getContext()), paramTypes, false);

        auto *const functionPtr = Builder.CreateLoad(
            Builder.getPtrTy(),
            Builder.CreateStructGEP(
                Builder.getModule().getStructType(ObjType::FUNCTION),
                function, 2
            ),
            "func"
        );

        if (const auto *const ins = dyn_cast<Instruction>(closure); ins && ins->hasMetadata("lox-function")) {
            auto *const expectedArity = mdconst::extract<ConstantInt>(ins->getMetadata("lox-function")->getOperand(1));
            if (!expectedArity->equalsInt(paramValues.size() - 2)) {
                Builder.RuntimeError(
                    line,
                    "Expected %d arguments but got %d.\n",
                    {expectedArity, Builder.getInt32(paramValues.size() - 2)},
                    Builder.getFunction()
                );
            }
        } else {
            // Check arity.
            auto *const arity = Builder.CreateLoad(
                Builder.getInt32Ty(),
                Builder.CreateStructGEP(Builder.getModule().getStructType(ObjType::FUNCTION), function, 1),
                "arity"
            );

            auto *const CallBlock = Builder.CreateBasicBlock("call");

            CheckArity(*this, CallBlock, arity, paramValues.size() - 2, line);

            Builder.SetInsertPoint(CallBlock);
        }

        PushCall(Builder, Builder.getInt32(line), Builder.CreateGlobalCachedString(Builder.getFunction()->getName()));
        auto *const result = Builder.CreateCall(FT, functionPtr, paramValues);
        PopCall(Builder);

        return result;
    }

    Value *FunctionCompiler::operator()(const CallExprPtr &callExpr) {
        const auto paramValues = to<std::vector<Value *>>(
            callExpr->arguments | std::views::transform([&](const auto &p) -> Value * {
                return evaluate(p);
            })
        );

        auto *const value = evaluate(callExpr->callee);

        if (isa<Instruction>(value) && cast<Instruction>(value)->hasMetadata("lox-function")) {
            // Handle a common case where the function local is known
            // which does not require runtime checks to check if the value is a function e.g.

            /*
             {
                fun foo() { }
                foo(); <-- foo can be found in the locals; locals can't be redefined, so
                           if foo was declared as a function then it must be the function.
             }
             */
            const auto &metadataName = cast<Instruction>(value)->getMetadata("lox-function")->getOperand(0);
            if (auto *local = lookupLocalVariable(cast<MDString>(metadataName)->getString()); local != nullptr) {
                auto *const closure = Builder.AsObj(Builder.CreateLoad(Builder.getInt64Ty(), local));
                auto *const result = call(Builder.getNilVal(), closure, paramValues, callExpr->keyword.getLine());
            const auto &funMD = cast<MDTuple>(cast<Instruction>(value)->getMetadata("lox-function"));
            if (auto *local = lookupLocalVariable(cast<MDString>(funMD->getOperand(0))->getString()); local != nullptr) {
                auto *const closureValue = Builder.CreateLoad(Builder.getInt64Ty(), local);
                auto *const closure = Builder.AsObj(closureValue);
                cast<Instruction>(closure)->copyMetadata(*cast<Instruction>(value));
                // std::cout << "function local found: " << cast<MDString>(metadataName)->getString().str() << std::endl;
                return insertTemp(result, "function return value");
            } else {
                // std::cout << "function local not found: " << name << std::endl;
            }
        }

        auto *const valuePtr = Builder.AsObj(value);

        auto *const IsClosureBlock = Builder.CreateBasicBlock("is.closure");
        auto *const CheckMethodBlock = Builder.CreateBasicBlock("check.method");
        auto *const CheckClassBlock = Builder.CreateBasicBlock("check.class");
        auto *const IsClassBlock = Builder.CreateBasicBlock("is.class");
        auto *const IsMethodBlock = Builder.CreateBasicBlock("is.method");
        auto *const NotCallableBlock = Builder.CreateBasicBlock("not.callable");
        auto *const ExecuteBlock = Builder.CreateBasicBlock("execute");
        auto *const EndBlock = Builder.CreateBasicBlock("end.block");

        Builder.CreateCondBr(Builder.IsClosure(value), IsClosureBlock, CheckClassBlock);

        Builder.SetInsertPoint(CheckClassBlock);
        Builder.CreateCondBr(Builder.IsClass(value), IsClassBlock, CheckMethodBlock);
        Builder.SetInsertPoint(IsClassBlock);
        auto *const klass = valuePtr;
        auto *const initString = Builder.AsObj(Builder.CreateLoad(Builder.getInt64Ty(), lookupGlobal("$initString")));
        auto *const initializer = Builder.TableGet(Builder.CreateLoad(Builder.getPtrTy(), Builder.CreateObjStructGEP(ObjType::CLASS, klass, 2)), initString);

        auto *const instance = Builder.AllocateInstance(klass);
        auto *const instanceVal = insertTemp(Builder.ObjVal(instance), "instance");

        auto *const EndClassBlock = Builder.CreateBasicBlock("class.end");
        auto *const HasInitializerBlock = Builder.CreateBasicBlock("call.init");
        auto *const NoInitializerBlock = Builder.CreateBasicBlock("call.noinit");

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
        auto *const receiverObjVal = Builder.CreateLoad(Builder.getInt64Ty(), Builder.CreateObjStructGEP(ObjType::BOUND_METHOD, valuePtr, 1));
        auto *const methodPtr = Builder.CreateLoad(Builder.getPtrTy(), Builder.CreateObjStructGEP(ObjType::BOUND_METHOD, valuePtr, 2));
        Builder.CreateBr(ExecuteBlock);

        Builder.SetInsertPoint(NotCallableBlock);
        Builder.RuntimeError(
            callExpr->keyword.getLine(),
            "Can only call functions and classes.\n",
            {},
            Builder.getFunction()
        );

        Builder.SetInsertPoint(IsClosureBlock);
        auto *const closurePtr = valuePtr;
        Builder.CreateBr(ExecuteBlock);

        Builder.SetInsertPoint(ExecuteBlock);

        // The function is wrapped in a closure.
        auto *const closure = Builder.CreatePHI(Builder.getPtrTy(), 2);
        closure->addIncoming(closurePtr, IsClosureBlock);
        closure->addIncoming(methodPtr, IsMethodBlock);
        auto *const receiver = Builder.CreatePHI(Builder.getInt64Ty(), 2);
        receiver->addIncoming(receiverObjVal, IsMethodBlock);
        receiver->addIncoming(Builder.getNilVal(), IsClosureBlock);

        auto *const functionReturnVal = call(receiver, closure, paramValues, callExpr->keyword.getLine());


        auto *const EndCall = Builder.GetInsertBlock();

        Builder.CreateBr(EndBlock);
        Builder.SetInsertPoint(EndBlock);
        auto *const result = Builder.CreatePHI(Builder.getInt64Ty(), 2);
        result->addIncoming(instanceVal, EndClassBlock);
        result->addIncoming(functionReturnVal, EndCall);

        auto *const IsObjBlock = Builder.CreateBasicBlock("is.obj");
        auto *const ReturnBlock = Builder.CreateBasicBlock("return");
        Builder.CreateCondBr(Builder.IsObj(result), IsObjBlock, ReturnBlock);
        Builder.SetInsertPoint(IsObjBlock);
        {
            // Make the return value reachable as a GC root e.g. in the following
            // the g closure could be GC'd if not reachable.
            //
            // class Foo {
            //   getClosure() {
            //     fun f() {
            //       fun g() {
            //         fun h() {
            //           return this.toString();
            //         }
            //         return h;
            //       }
            //       return g;
            //     }
            //     return f;
            //   }
            //
            //   toString() { return "Foo"; }
            // }
            //
            // var f = Foo().getClosure();
            // //var g = f(); // assigning the result of f to variable would make it reachable
            // var h = f()/* the result here needs to be reachable */();
            // print h();
            insertTemp(result, "function return value");

            Builder.CreateBr(ReturnBlock);
        }

        Builder.SetInsertPoint(ReturnBlock);

        return result;
    }

    void CheckInstance(LoxBuilder &Builder, const std::string_view message, const unsigned int line, Value *instance) {
        auto *const NotInstanceBlock = Builder.CreateBasicBlock("not.instance");
        auto *const EndBlock = Builder.CreateBasicBlock("end");

        Builder.CreateCondBr(Builder.IsInstance(instance), EndBlock, NotInstanceBlock);

        Builder.SetInsertPoint(NotInstanceBlock);
        Builder.RuntimeError(line, message, {}, Builder.getFunction());

        Builder.SetInsertPoint(EndBlock);
    }

    Value *FunctionCompiler::operator()(const GetExprPtr &getExpr) {
        Value *object = evaluate(getExpr->object);
        CheckInstance(Builder, "Only instances have properties.\n"sv, getExpr->name.getLine(), object);

        auto *const instance = Builder.AsObj(object);
        auto *const fields = Builder.CreateLoad(Builder.getPtrTy(), Builder.CreateObjStructGEP(ObjType::INSTANCE, instance, 2));

        auto *const key = Builder.AllocateString(getExpr->name.getLexeme(), getExpr->name.getLexeme());
        auto *const result = Builder.TableGet(fields, key);

        auto *const IsMethodBlock = Builder.CreateBasicBlock("property.ismethod");
        auto *const IsDefinedBlock = Builder.CreateBasicBlock("property.defined");

        auto *const BeforeBlock = Builder.GetInsertBlock();

        Builder.CreateCondBr(Builder.IsUninitialized(result), IsMethodBlock, IsDefinedBlock);

        Builder.SetInsertPoint(IsMethodBlock);

        auto *const klass = Builder.CreateLoad(Builder.getPtrTy(), Builder.CreateObjStructGEP(ObjType::INSTANCE, instance, 1));
        auto *const bound =
            insertTemp(Builder.ObjVal(Builder.BindMethod(klass, instance, key, getExpr->name.getLine(), enclosing == nullptr ? nullptr : Builder.getFunction())), "bound method");

        Builder.CreateBr(IsDefinedBlock);

        Builder.SetInsertPoint(IsDefinedBlock);
        auto *const R = Builder.CreatePHI(Builder.getInt64Ty(), 2);
        R->addIncoming(result, BeforeBlock);
        R->addIncoming(bound, IsMethodBlock);

        return R;
    }

    Value *FunctionCompiler::operator()(const SetExprPtr &setExpr) {
        auto *const object = evaluate(setExpr->object);
        CheckInstance(Builder, "Only instances have fields.\n"sv, setExpr->name.getLine(), object);

        auto *const instance = Builder.AsObj(object);
        auto *const value = evaluate(setExpr->value);
        auto *const fields = Builder.CreateLoad(Builder.getPtrTy(), Builder.CreateObjStructGEP(ObjType::INSTANCE, instance, 2));

        Builder.TableSet(fields, Builder.AllocateString(setExpr->name.getLexeme(), "key"), value);

        return value;
    }

    Value *FunctionCompiler::operator()(const ThisExprPtr &thisExpr) {
        return Builder.CreateLoad(Builder.getInt64Ty(), lookupVariable(*thisExpr));
    }

    Value *FunctionCompiler::operator()(const SuperExprPtr &superExpr) {
        static auto assignable = Assignable{Token(THIS, "this"sv, nullptr, superExpr->name.getLine())};
        auto *const instance = Builder.CreateLoad(Builder.getInt64Ty(), lookupVariable(assignable));
        auto *const klass = Builder.CreateLoad(Builder.getInt64Ty(), lookupVariable(*superExpr));
        auto *const key = insertTemp(Builder.ObjVal(Builder.AllocateString(superExpr->method.getLexeme())), "super method name");
        auto *const method = Builder.BindMethod(
            Builder.AsObj(klass),
            Builder.AsObj(instance),
            Builder.AsObj(key),
            superExpr->name.getLine(),
            enclosing == nullptr ? nullptr : Builder.getFunction()
        );
        return Builder.ObjVal(method);
    }

    Value *FunctionCompiler::operator()(const VarExprPtr &varExpr) {
        auto *const value = lookupVariable(*varExpr);
        auto *inst = Builder.CreateLoad(Builder.getInt64Ty(), value);
        if (isa<Instruction>(value)) {
            inst->copyMetadata(*cast<Instruction>(value));
        }
        return inst;
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
                    // Push onto the locals stack so that the string is reachable as a GC root, before it's assigned to a variable.
                    return insertTemp(Builder.ObjVal(Builder.AllocateString(string_value)), ("string {" + string_value + "}").str());
                },
                [this](const std::nullptr_t) -> Value * { return Builder.getNilVal(); },
            },
            literalExpr->literal
        );
    }

    Value *FunctionCompiler::operator()(const LogicalExprPtr &logicalExpr) {
        auto *const left = evaluate(logicalExpr->left);

        switch (logicalExpr->op) {
            case LogicalOp::AND: {
                auto *const LeftIsTruthyBlock = Builder.CreateBasicBlock("if.left.truthy");
                auto *const LeftNotTruthyBlock = Builder.CreateBasicBlock("if.left.nottruthy");
                auto *const EndBlock = Builder.CreateBasicBlock("end");
                Builder.CreateCondBr(
                    Builder.IsTruthy(left),
                    LeftIsTruthyBlock,
                    LeftNotTruthyBlock
                );
                Builder.SetInsertPoint(LeftNotTruthyBlock);
                auto *const Y = left;
                auto *const EndLeftNotTruthyBlock = Builder.GetInsertBlock();
                Builder.CreateBr(EndBlock);
                Builder.SetInsertPoint(LeftIsTruthyBlock);
                auto *const X = evaluate(logicalExpr->right);
                auto *const EndLeftTruthyBlock = Builder.GetInsertBlock();
                Builder.CreateBr(EndBlock);
                Builder.SetInsertPoint(EndBlock);

                auto *const Result = Builder.CreatePHI(Builder.getInt64Ty(), 2);
                Result->addIncoming(X, EndLeftTruthyBlock);
                Result->addIncoming(Y, EndLeftNotTruthyBlock);

                return Result;
            }
            case LogicalOp::OR: {
                auto *const LeftIsTruthyBlock = Builder.CreateBasicBlock("if.left.truthy");
                auto *const LeftNotTruthyBlock = Builder.CreateBasicBlock("if.left.nottruthy");
                auto *const EndBlock = Builder.CreateBasicBlock("end");
                Builder.CreateCondBr(
                    Builder.IsTruthy(left),
                    LeftIsTruthyBlock,
                    LeftNotTruthyBlock
                );
                Builder.SetInsertPoint(LeftNotTruthyBlock);
                auto *const right = evaluate(logicalExpr->right);
                auto *const Y = Builder.CreateSelect(Builder.IsTruthy(right), right, left);
                auto *const EndLeftNotTruthyBlock = Builder.GetInsertBlock();
                Builder.CreateBr(EndBlock);
                Builder.SetInsertPoint(LeftIsTruthyBlock);
                auto *const X = left;
                Builder.CreateBr(EndBlock);
                Builder.SetInsertPoint(EndBlock);

                auto *const Result = Builder.CreatePHI(Builder.getInt64Ty(), 2);
                Result->addIncoming(X, LeftIsTruthyBlock);
                Result->addIncoming(Y, EndLeftNotTruthyBlock);

                return Result;
            }
        }
        std::unreachable();
    }

    Value *FunctionCompiler::operator()(const UnaryExprPtr &unaryExpr) {
        auto *const left = evaluate(unaryExpr->expression);

        switch (unaryExpr->op) {
            case UnaryOp::BANG:
                return Builder.BoolVal(Builder.CreateSelect(Builder.IsTruthy(left), Builder.getFalse(), Builder.getTrue()));
            case UnaryOp::MINUS: {
                auto *const InvalidNumBlock = Builder.CreateBasicBlock("if.not.num");
                auto *const EndBlock = Builder.CreateBasicBlock("if.num");

                Builder.CreateCondBr(Builder.IsNumber(left), EndBlock, InvalidNumBlock);
                Builder.SetInsertPoint(InvalidNumBlock);
                Builder.RuntimeError(
                    unaryExpr->token.getLine(),
                    "Operand must be a number.\n",
                    {},
                    Builder.getFunction()
                );

                Builder.SetInsertPoint(EndBlock);

                return Builder.NumberVal(Builder.CreateFNeg(Builder.AsNumber(left)));
            }
        }

        std::unreachable();
    }

}// namespace lox
