#include "Value.h"
#include "../Debug.h"
#include "Callstack.h"
#include "LoxBuilder.h"
#include "Memory.h"
#include "Upvalue.h"

#include <llvm/ADT/StringExtras.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Value.h>

using namespace llvm;
using namespace llvm::sys;

namespace lox {

    Value *LoxBuilder::getUninitializedVal() {
        return getInt64(UNINITIALIZED_VAL);
    }

    Value *LoxBuilder::getNilVal() {
        return getInt64(NIL_VAL);
    }

    Value *LoxBuilder::getTrueVal() {
        return getInt64(TRUE_VAL);
    }

    Value *LoxBuilder::getFalseVal() {
        return getInt64(FALSE_VAL);
    }

    Value *LoxBuilder::IsBool(Value *value) {
        assert(value->getType() == getInt64Ty());
        return CreateICmpEQ(CreateOr(value, 1), getTrueVal());
    }

    Value *LoxBuilder::IsUninitialized(Value *value) {
        assert(value->getType() == getInt64Ty());
        return CreateICmpEQ(value, getUninitializedVal());
    }

    Value *LoxBuilder::IsNil(Value *value) {
        assert(value->getType() == getInt64Ty());
        return CreateICmpEQ(value, getNilVal());
    }

    Value *LoxBuilder::IsNumber(Value *value) {
        assert(value->getType() == getInt64Ty());
        return CreateICmpNE(CreateAnd(value, QNAN), getInt64(QNAN));
    }

    Value *LoxBuilder::IsObj(Value *value) {
        assert(value->getType() == getInt64Ty());
        return CreateICmpEQ(CreateAnd(value, QNAN | SIGN_BIT), getInt64(QNAN | SIGN_BIT));
    }

    static Value *CheckType(LoxBuilder &Builder, Value *value, const ObjType type) {
        assert(value->getType() == Builder.getInt64Ty());
        static auto *CheckTypeFunction([&Builder] {
            auto *const F = Function::Create(
                FunctionType::get(
                    Builder.getInt1Ty(),
                    {Builder.getInt64Ty(), Builder.getInt8Ty()},
                    false
                ),
                Function::InternalLinkage,
                "$checkType",
                Builder.getModule()
            );

            F->addFnAttr(Attribute::AlwaysInline);

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->args().begin();
            auto *const value = arguments;
            auto *const type = arguments + 1;

            auto *const IsObjBlock = B.CreateBasicBlock("is.obj");
            auto *const IsNotObjectBlock = B.CreateBasicBlock("is.notobj");

            // The value is of the specified object type if the value is an object
            // and the object type field in the struct matches the type.
            // Must first check if its an object and early return; since
            // the ObjType function will assume value is an object pointer.
            B.CreateCondBr(B.IsObj(value), IsObjBlock, IsNotObjectBlock);

            B.SetInsertPoint(IsNotObjectBlock);
            B.CreateRet(B.getFalse());

            B.SetInsertPoint(IsObjBlock);
            B.CreateRet(B.CreateICmpEQ(B.ObjType(value), type));

            return F;
        }());

        return Builder.CreateCall(CheckTypeFunction, {value, Builder.ObjTypeInt(type)});
    }

    Value *LoxBuilder::IsClosure(Value *value) {
        return CheckType(*this, value, ObjType::CLOSURE);
    }

    Value *LoxBuilder::IsString(Value *value) {
        return CheckType(*this, value, ObjType::STRING);
    }

    Value *LoxBuilder::IsClass(Value *value) {
        return CheckType(*this, value, ObjType::CLASS);
    }

    Value *LoxBuilder::IsInstance(Value *value) {
        return CheckType(*this, value, ObjType::INSTANCE);
    }

    Value *LoxBuilder::IsUpvalue(Value *value) {
        return CheckType(*this, value, ObjType::UPVALUE);
    }

    Value *LoxBuilder::IsBoundMethod(Value *value) {
        return CheckType(*this, value, ObjType::BOUND_METHOD);
    }

    Value *LoxBuilder::ObjType(Value *value) {
        assert(value->getType() == getInt64Ty());
        return CreateLoad(
            getInt8Ty(),
            CreateStructGEP(getModule().getObjStructType(), AsObj(value), 0)
        );
    }

    ConstantInt *LoxBuilder::ObjTypeInt(enum ObjType objType) {
        return getInt8(static_cast<uint8_t>(objType));
    }

    Value *LoxBuilder::BoolVal(Value *value) {
        assert(value->getType() == getInt1Ty());
        return CreateSelect(value, getTrueVal(), getFalseVal());
    }

    Value *LoxBuilder::AsBool(Value *value) {
        assert(value->getType() == getInt64Ty());
        return CreateICmpEQ(value, getTrueVal());
    }

    Value *LoxBuilder::AsNumber(Value *value) {
        assert(value->getType() == getInt64Ty());
        return CreateBitCast(value, getDoubleTy());
    }

    Value *LoxBuilder::ObjVal(Value *ptrValue) {
        assert(ptrValue->getType() == getPtrTy());
        return CreateOr(CreatePtrToInt(ptrValue, getInt64Ty()), SIGN_BIT | QNAN);
    }

    Value *LoxBuilder::AsObj(Value *value) {
        assert(value->getType() == getInt64Ty());
        return CreateIntToPtr(CreateAnd(value, ~(SIGN_BIT | QNAN)), getPtrTy());
    }

    Value *LoxBuilder::AsCString(Value *value) {
        assert(value->getType() == getInt64Ty());
        return CreateLoad(getPtrTy(), CreateObjStructGEP(ObjType::STRING, AsObj(value), 1));
    }

    Value *LoxBuilder::NumberVal(Value *value) {
        return CreateBitCast(value, getInt64Ty());
    }

    Value *LoxBuilder::IsTruthy(Value *value) {
        static auto *IsTruthyFunction([this] {
            auto *const F = Function::Create(
                FunctionType::get(
                    getInt1Ty(),
                    getInt64Ty(),
                    false
                ),
                Function::InternalLinkage,
                "$isTruthy",
                this->getModule()
            );

            LoxBuilder B(getContext(), getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            auto *const IsNullBlock = B.CreateBasicBlock("if.null");
            auto *const IsNotNullBlock = B.CreateBasicBlock("if.not.bool");
            auto *const IsBoolBlock = B.CreateBasicBlock("if.bool");
            auto *const EndBlock = B.CreateBasicBlock("if.end");

            B.SetInsertPoint(EntryBasicBlock);

            auto *const p0 = F->args().begin();
            B.CreateCondBr(B.IsNil(p0), IsNullBlock, IsNotNullBlock);
            B.SetInsertPoint(IsNullBlock);
            {
                B.CreateRet(B.getFalse());
            }
            B.SetInsertPoint(IsNotNullBlock);
            {
                B.CreateCondBr(B.IsBool(p0), IsBoolBlock, EndBlock);
                B.SetInsertPoint(IsBoolBlock);
                B.CreateRet(B.AsBool(p0));
            }
            B.SetInsertPoint(EndBlock);
            {
                B.CreateRet(B.getTrue());
            }

            return F;
        }());

        return CreateCall(IsTruthyFunction, value);
    }

    void LoxBuilder::Print(Value *value) {
        assert(value->getType() == getInt64Ty());
        static auto *PrintFunction([this] {
            auto *const F = Function::Create(
                FunctionType::get(
                    getVoidTy(),
                    getInt64Ty(),
                    false
                ),
                Function::InternalLinkage,
                "$print",
                this->getModule()
            );

            LoxBuilder B(this->getContext(), this->getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const value = F->args().begin();

            auto *const BoolBlock = B.CreateBasicBlock("if.print.bool");
            auto *const EndBoolBlock = B.CreateBasicBlock("if.print.bool.end");
            auto *const NilBlock = B.CreateBasicBlock("if.print.nil");
            auto *const EndNumberBlock = B.CreateBasicBlock("if.print.num.end");
            auto *const NumBlock = B.CreateBasicBlock("if.print.num");
            auto *const CheckObjBlock = B.CreateBasicBlock("if.print.obj.end");
            auto *const EndBlock = B.CreateBasicBlock("if.print.end");

            B.CreateCondBr(B.IsNumber(value), NumBlock, EndNumberBlock);
            B.SetInsertPoint(NumBlock);
            {
                B.PrintNumber(value);
                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(EndNumberBlock);
            B.CreateCondBr(B.IsBool(value), BoolBlock, EndBoolBlock);
            B.SetInsertPoint(BoolBlock);
            {
                B.PrintBool(value);
                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(EndBoolBlock);

            B.CreateCondBr(B.IsNil(value), NilBlock, CheckObjBlock);
            B.SetInsertPoint(NilBlock);
            {
                B.PrintNil();
                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(CheckObjBlock);
            {
                B.PrintObject(value);
                B.CreateBr(EndBlock);
            }

            B.SetInsertPoint(EndBlock);
            B.CreateRetVoid();

            return F;
        }());

        CreateCall(PrintFunction, value);
    }

    void LoxBuilder::PrintF(const std::initializer_list<Value *> value) {
        static const auto PrintF = getModule().getOrInsertFunction(
            "printf",
            FunctionType::get(getInt8Ty(), {PointerType::getUnqual(getContext())}, true)
        );
        CreateCall(PrintF, value);
    }

    void LoxBuilder::PrintFErr(Value *message, const std::vector<Value *> &values) {
        static auto *const StdErr = getModule().getOrInsertGlobal("stderr", getPtrTy());
        static const auto FPrintF = getModule().getOrInsertFunction(
            "fprintf",
            FunctionType::get(getInt8Ty(), {PointerType::get(Context, 0), PointerType::get(Context, 0)}, true)
        );

        std::vector values2(values);
        values2.insert(values2.begin(), message);
        values2.insert(values2.begin(), CreateLoad(getPtrTy(), StdErr));

        CreateCall(FPrintF, values2);
    }

    void LoxBuilder::PrintString(const StringRef string) {
        PrintF({CreateGlobalCachedString("%s\n"), CreateGlobalCachedString(string)});
    }

    void LoxBuilder::PrintNumber(Value *value) {
        PrintF({CreateGlobalCachedString("%g\n"), AsNumber(value)});
    }

    void LoxBuilder::PrintNil() {
        PrintF({CreateGlobalCachedString("nil\n")});
    }

    void LoxBuilder::PrintObject(Value *value) {
        auto *const IsStringBlock = CreateBasicBlock("print.string");
        auto *const IsClosureBlock = CreateBasicBlock("print.closure");
        auto *const IsFunctionBlock = CreateBasicBlock("print.function");
        auto *const IsUpvalueBlock = DEBUG_LOG_GC || DEBUG_UPVALUES ? CreateBasicBlock("print.upvalue") : nullptr;
        auto *const IsClassBlock = CreateBasicBlock("print.class");
        auto *const IsInstanceBlock = CreateBasicBlock("print.instance");
        auto *const IsNativeFunctionBlock = CreateBasicBlock("print.native.function");
        auto *const IsNotNativeFunctionBlock = CreateBasicBlock("print.not.native.function");
        auto *const IsBoundMethod = CreateBasicBlock("print.boundmethod");
        auto *const DefaultBlock = CreateBasicBlock("print.default");
        auto *const EndBlock = CreateBasicBlock("print.end");

        auto *const Switch = CreateSwitch(ObjType(value), DefaultBlock);
        Switch->addCase(ObjTypeInt(ObjType::STRING), IsStringBlock);
        Switch->addCase(ObjTypeInt(ObjType::CLOSURE), IsClosureBlock);
        Switch->addCase(ObjTypeInt(ObjType::FUNCTION), IsFunctionBlock);
        if constexpr (DEBUG_LOG_GC || DEBUG_UPVALUES) {
            Switch->addCase(ObjTypeInt(ObjType::UPVALUE), IsUpvalueBlock);
        }
        Switch->addCase(ObjTypeInt(ObjType::CLASS), IsClassBlock);
        Switch->addCase(ObjTypeInt(ObjType::INSTANCE), IsInstanceBlock);
        Switch->addCase(ObjTypeInt(ObjType::BOUND_METHOD), IsBoundMethod);

        SetInsertPoint(IsStringBlock);
        {
            PrintString(value);
        }
        CreateBr(EndBlock);

        SetInsertPoint(IsClosureBlock);
        {
            auto *const closure = AsObj(value);
            auto *const function = CreateLoad(getPtrTy(), CreateObjStructGEP(ObjType::CLOSURE, closure, 1));

            auto *const isNative = CreateLoad(getInt1Ty(), CreateObjStructGEP(ObjType::FUNCTION, function, 4));

            CreateCondBr(isNative, IsNativeFunctionBlock, IsNotNativeFunctionBlock);
            SetInsertPoint(IsNativeFunctionBlock);
            PrintF({CreateGlobalCachedString("<native fn>\n")});
            CreateBr(EndBlock);
            SetInsertPoint(IsNotNativeFunctionBlock);
            PrintF({CreateGlobalCachedString("<fn %s>\n"), AsCString(CreateLoad(getInt64Ty(), CreateObjStructGEP(ObjType::FUNCTION, function, 3)))});
        }
        CreateBr(EndBlock);

        SetInsertPoint(IsFunctionBlock);
        {
            // Not usually printable, but useful for debugging.
            auto *const function = AsObj(value);
            PrintF({CreateGlobalCachedString("<fn %s>\n"), AsCString(CreateLoad(getInt64Ty(), CreateObjStructGEP(ObjType::FUNCTION, function, 3)))});
        }
        CreateBr(EndBlock);

        if constexpr (DEBUG_LOG_GC || DEBUG_UPVALUES) {
            SetInsertPoint(IsUpvalueBlock);
            {
                // Not usually printable, but useful for debugging.
                auto *const upvalue = AsObj(value);
                auto *const object = CreateLoad(getPtrTy(), CreateObjStructGEP(ObjType::UPVALUE, upvalue, 1));
                auto *const next = CreateLoad(getPtrTy(), CreateObjStructGEP(ObjType::UPVALUE, upvalue, 2));
                auto *const closed = CreateLoad(getPtrTy(), CreateObjStructGEP(ObjType::UPVALUE, upvalue, 3));
                PrintF({CreateGlobalCachedString("Upvalue(%p, %p, next = %p, closed = %d) = "), upvalue, object, next, closed});
                CreateCall(FunctionType::get(getVoidTy(), getInt64Ty(), false), getModule().getFunction("$print"), CreateLoad(getInt64Ty(), object));
            }
            CreateBr(EndBlock);
        }

        SetInsertPoint(IsClassBlock);
        {
            auto *const klass = AsObj(value);
            PrintF({CreateGlobalCachedString("%s\n"), AsCString(CreateLoad(getInt64Ty(), CreateObjStructGEP(ObjType::CLASS, klass, 1)))});
            CreateBr(EndBlock);

            SetInsertPoint(IsInstanceBlock);

            auto *const instance = AsObj(value);
            auto *const instanceKlass = CreateLoad(getPtrTy(), CreateObjStructGEP(ObjType::INSTANCE, instance, 1));
            PrintF({CreateGlobalCachedString("%s instance\n"), AsCString(CreateLoad(getInt64Ty(), CreateObjStructGEP(ObjType::CLASS, instanceKlass, 1)))});
        }
        CreateBr(EndBlock);

        SetInsertPoint(IsBoundMethod);
        {
            auto *const bound = AsObj(value);
            auto *const methodClosure = CreateLoad(getPtrTy(), CreateObjStructGEP(ObjType::BOUND_METHOD, bound, 2));
            CreateCall(FunctionType::get(getVoidTy(), getInt64Ty(), false), getModule().getFunction("$print"), ObjVal(methodClosure));
        }
        CreateBr(EndBlock);

        SetInsertPoint(DefaultBlock);
        {
            if constexpr (DEBUG_LOG_GC) {
                PrintF({CreateGlobalCachedString("{{object %d}}\n"), ObjType(value)});
                CreateBr(EndBlock);
            } else {
                CreateUnreachable();
            }
        }

        SetInsertPoint(EndBlock);
    }

    void LoxBuilder::PrintString(Value *value) {
        PrintF({CreateGlobalCachedString("%s\n"), AsCString(value)});
    }

    void LoxBuilder::PrintBool(Value *value) {
        PrintF({CreateGlobalCachedString("%s\n"), CreateSelect(AsBool(value), CreateGlobalCachedString("true"), CreateGlobalCachedString("false"))});
    }

    void LoxBuilder::RuntimeError(Value *line, const StringRef message, const std::vector<Value *> &values, Value *location, const bool freeObjects) {
        static const auto Exit = getModule().getOrInsertFunction(
            "exit",
            FunctionType::get(getVoidTy(), {getInt32Ty()}, false)
        );

        PrintFErr(CreateGlobalCachedString(message), values);
        // Push the current location onto the call stack, so that it's printed as part of the stacktrace.
        PushCall(*this, line, location);
        PrintStackTrace(*this);

        if (freeObjects) FreeObjects(*this);

        CreateCall(Exit, getInt32(70));
        CreateUnreachable();
    }
}// namespace lox
