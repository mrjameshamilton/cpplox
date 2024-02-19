#include "Value.h"
#include "Class.h"
#include "LoxBuilder.h"
#include "ModuleCompiler.h"

#include <llvm/ADT/StringExtras.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Value.h>
#include <llvm/Passes/PassBuilder.h>

using namespace llvm;
using namespace llvm::sys;

namespace lox {

    Value *LoxBuilder::getUninitializedVal() {
        return getInt64(UNITIALIZED_VAL);
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

    Value *LoxBuilder::IsClosure(Value *value) {
        assert(value->getType() == getInt64Ty());
        return CreateAnd(
            IsObj(value),
            CreateICmpEQ(ObjType(value), ObjTypeInt(ObjType::CLOSURE))
        );
    }

    Value *LoxBuilder::IsString(Value *value) {
        assert(value->getType() == getInt64Ty());
        return CreateAnd(
            IsObj(value),
            CreateICmpEQ(ObjType(value), ObjTypeInt(ObjType::STRING))
        );
    }

    Value *LoxBuilder::IsClass(Value *value) {
        assert(value->getType() == getInt64Ty());
        return CreateAnd(
            IsObj(value),
            CreateICmpEQ(ObjType(value), ObjTypeInt(ObjType::CLASS))
        );
    }

    Value *LoxBuilder::IsInstance(Value *value) {
        assert(value->getType() == getInt64Ty());
        return CreateAnd(
            IsObj(value),
            CreateICmpEQ(ObjType(value), ObjTypeInt(ObjType::INSTANCE))
        );
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

    void LoxBuilder::Print(Value *value) {
        static auto PrintFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    getVoidTy(),
                    getInt64Ty(),
                    false
                ),
                Function::InternalLinkage,
                "Print",
                this->getModule()
            );

            LoxBuilder B(this->getContext(), this->getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto value = F->args().begin();

            const auto BoolBlock = B.CreateBasicBlock("if.print.bool");
            const auto EndBoolBlock = B.CreateBasicBlock("if.print.bool.end");
            const auto NilBlock = B.CreateBasicBlock("if.print.nil");
            const auto EndNilBlock = B.CreateBasicBlock("if.print.nil.end");
            const auto NumBlock = B.CreateBasicBlock("if.print.num");
            const auto ObjBlock = B.CreateBasicBlock("if.print.obj");
            const auto EndBlock = B.CreateBasicBlock("if.print.end");

            B.CreateCondBr(B.IsBool(value), BoolBlock, EndBoolBlock);
            B.SetInsertPoint(BoolBlock);
            B.PrintBool(value);
            B.CreateBr(EndBlock);
            B.SetInsertPoint(EndBoolBlock);

            B.CreateCondBr(B.IsNil(value), NilBlock, EndNilBlock);
            B.SetInsertPoint(NilBlock);
            B.PrintNil();
            B.CreateBr(EndBlock);
            B.SetInsertPoint(EndNilBlock);

            B.CreateCondBr(B.IsNumber(value), NumBlock, ObjBlock);
            B.SetInsertPoint(NumBlock);
            B.PrintNumber(value);
            B.CreateBr(EndBlock);

            B.SetInsertPoint(ObjBlock);
            B.PrintObject(value);
            B.CreateBr(EndBlock);

            B.SetInsertPoint(EndBlock);
            B.CreateRetVoid();

            return F;
        }());

        CreateCall(PrintFunction, value);
    }

    void LoxBuilder::PrintF(const std::string &stringFormat, Value *value) {
        PrintF({CreateGlobalCachedString(stringFormat), value});
    }

    void LoxBuilder::PrintF(const std::initializer_list<Value *> value) {
        static const auto PrintF = getModule().getOrInsertFunction(
            "printf",
            FunctionType::get(getInt8Ty(), {PointerType::getUnqual(getContext())}, true)
        );
        CreateCall(PrintF, value);
    }

    void LoxBuilder::PrintString(const std::string &string) {
        PrintF({CreateGlobalCachedString("%s\n"), CreateGlobalCachedString(string)});
    }

    void LoxBuilder::PrintNumber(Value *value) {
        PrintF({CreateGlobalCachedString("%g\n"), AsNumber(value)});
    }

    void LoxBuilder::PrintNil() {
        PrintF({CreateGlobalCachedString("nil\n")});
    }

    void LoxBuilder::PrintObject(Value *value) {
        const auto IsStringBlock = CreateBasicBlock("print.string");
        const auto IsClosureBlock = CreateBasicBlock("print.closure");
        const auto IsUpvalueBlock = CreateBasicBlock("print.upvalue");
        const auto IsClassBlock = CreateBasicBlock("print.class");
        const auto IsInstanceBlock = CreateBasicBlock("print.instance");
        const auto IsNativeFunctionBlock = CreateBasicBlock("print.native.function");
        const auto IsNotNativeFunctionBlock = CreateBasicBlock("print.not.native.function");
        const auto DefaultBlock = CreateBasicBlock("print.default");
        const auto EndBlock = CreateBasicBlock("print.end");

        const auto Switch = CreateSwitch(ObjType(value), DefaultBlock);
        Switch->addCase(ObjTypeInt(ObjType::STRING), IsStringBlock);
        Switch->addCase(ObjTypeInt(ObjType::CLOSURE), IsClosureBlock);
        Switch->addCase(ObjTypeInt(ObjType::UPVALUE), IsUpvalueBlock);
        Switch->addCase(ObjTypeInt(ObjType::CLASS), IsClassBlock);
        Switch->addCase(ObjTypeInt(ObjType::INSTANCE), IsInstanceBlock);

        SetInsertPoint(IsStringBlock);
        PrintString(value);
        CreateBr(EndBlock);

        SetInsertPoint(IsClosureBlock);
        const auto closure = AsObj(value);
        const auto function = CreateLoad(getPtrTy(), CreateObjStructGEP(ObjType::CLOSURE, closure, 1));

        const auto isNative = CreateLoad(getInt1Ty(), CreateObjStructGEP(ObjType::FUNCTION, function, 4));

        CreateCondBr(isNative, IsNativeFunctionBlock, IsNotNativeFunctionBlock);
        SetInsertPoint(IsNativeFunctionBlock);
        PrintF({CreateGlobalCachedString("<native fn>\n")});
        CreateBr(EndBlock);
        SetInsertPoint(IsNotNativeFunctionBlock);
        PrintF({CreateGlobalCachedString("<fn %s>\n"), AsCString(CreateLoad(getInt64Ty(), CreateObjStructGEP(ObjType::FUNCTION, function, 3)))});
        CreateBr(EndBlock);

        SetInsertPoint(IsUpvalueBlock);
        const auto upvalue = AsObj(value);
        const auto object = CreateLoad(getPtrTy(), CreateObjStructGEP(ObjType::UPVALUE, upvalue, 1));
        PrintF({CreateGlobalCachedString("Upvalue(%p, %p) = "), value, object});
        CreateCall(FunctionType::get(getVoidTy(), getInt64Ty(), false), getModule().getFunction("Print"), CreateLoad(getInt64Ty(), object));
        CreateBr(EndBlock);

        SetInsertPoint(IsClassBlock);

        const auto klass = AsObj(value);
        PrintF({CreateGlobalCachedString("%s\n"), AsCString(CreateLoad(getInt64Ty(), CreateObjStructGEP(ObjType::CLASS, klass, 1)))});
        CreateBr(EndBlock);

        SetInsertPoint(IsInstanceBlock);

        const auto instance = AsObj(value);
        const auto instanceKlass = CreateLoad(getPtrTy(), CreateObjStructGEP(ObjType::INSTANCE, instance, 1));
        PrintF({CreateGlobalCachedString("%s instance\n"), AsCString(CreateLoad(getInt64Ty(), CreateObjStructGEP(ObjType::CLASS, instanceKlass, 1)))});
        CreateBr(EndBlock);

        SetInsertPoint(DefaultBlock);

        CreateBr(EndBlock);
        SetInsertPoint(EndBlock);
    }

    void LoxBuilder::PrintString(Value *value) {
        PrintF({CreateGlobalCachedString("%s\n"), AsCString(value)});
    }

    void LoxBuilder::PrintBool(Value *value) {
        PrintF({CreateGlobalCachedString("%s\n"), CreateSelect(AsBool(value), CreateGlobalCachedString("true"), CreateGlobalCachedString("false"))});
    }

    void LoxBuilder::RuntimeError(const unsigned line, StringRef message, const std::vector<Value *> &values, const llvm::Function *function) {
        static const auto StdErr = getModule().getOrInsertGlobal("stderr", getPtrTy());
        static const auto FPrintF = getModule().getOrInsertFunction(
            "fprintf",
            FunctionType::get(getInt8Ty(), {PointerType::get(Context, 0), PointerType::get(Context, 0)}, true)
        );
        static const auto Exit = getModule().getOrInsertFunction(
            "exit",
            FunctionType::get(getVoidTy(), {getInt32Ty()}, true)
        );

        std::vector values2(values);
        values2.insert(values2.begin(), CreateGlobalCachedString(message));
        values2.insert(values2.begin(), CreateLoad(getPtrTy(), StdErr));

        CreateCall(FPrintF, values2);
        if (function == nullptr) {
            CreateCall(
                FPrintF,
                {
                    CreateLoad(getPtrTy(), StdErr),
                    CreateGlobalCachedString("[line %d] in script\n"),
                    getInt32(line),
                }
            );
        } else {
            CreateCall(
                FPrintF,
                {CreateLoad(getPtrTy(), StdErr),
                 CreateGlobalCachedString("[line %d] in %s()\n"),
                 getInt32(line),
                 CreateGlobalCachedString(function->getName())
                }
            );
        }
        CreateCall(Exit, getInt32(70));
    }
}// namespace lox
