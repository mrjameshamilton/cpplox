#include "Value.h"
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

    Value *LoxBuilder::getNilVal() {
        return getInt64(NIL_VAL);
    }

    Value *LoxBuilder::IsBool(Value *value) {
        return CreateICmpEQ(CreateOr(value, 1), getInt64(TRUE_VAL));
    }

    Value *LoxBuilder::IsNil(Value *value) {
        return CreateICmpEQ(value, getNilVal());
    }

    Value *LoxBuilder::IsNumber(Value *value) {
        return CreateICmpNE(CreateAnd(value, QNAN), getInt64(QNAN));
    }

    Value *LoxBuilder::IsObj(Value *value) {
        return CreateICmpEQ(CreateAnd(value, QNAN | SIGN_BIT), getInt64(QNAN | SIGN_BIT));
    }

    Value *LoxBuilder::IsString(Value *value) {
        return CreateAnd(
            IsObj(value),
            CreateICmpEQ(ObjType(value), ObjTypeInt(ObjType::STRING))
        );
    }

    Value *LoxBuilder::ObjType(Value *value) {
        return CreateLoad(
            getInt8Ty(),
            CreateStructGEP(getObjStructType(), AsObj(value), 0)
        );
    }

    ConstantInt *LoxBuilder::ObjTypeInt(enum ObjType objType) {
        return getInt8(static_cast<uint8_t>(objType));
    }


    Value *LoxBuilder::BoolVal(Value *value) {
        assert(value->getType() == getInt1Ty());
        return CreateSelect(value, getInt64(TRUE_VAL), getInt64(FALSE_VAL));
    }

    Value *LoxBuilder::AsBool(Value *value) {
        assert(value->getType() == getInt64Ty());
        return CreateICmpEQ(value, getInt64(TRUE_VAL));
    }

    Value *LoxBuilder::AsNumber(Value *value) {
        return CreateBitCast(value, getDoubleTy());
    }

    Value *LoxBuilder::ObjVal(Value *value) {
        return CreateOr(value, SIGN_BIT | QNAN);
    }

    Value *LoxBuilder::AsObj(Value *value, const std::optional<enum ObjType> type) {
        return CreateBitCast(
            CreateIntToPtr(CreateAnd(value, ~(SIGN_BIT | QNAN)), getInt8PtrTy()),
            (type.has_value() ? getStructType(type.value()) : getObjStructType())->getPointerTo()
        );
    }

    Value *LoxBuilder::AsFunction(Value *value) {
        return AsObj(value, ObjType::FUNCTION);
    }

    Value *LoxBuilder::AsString(Value *value) {
        return AsObj(value, ObjType::STRING);
    }

    Value *LoxBuilder::AsCString(Value *value) {
        const auto string = CreateIntToPtr(CreateAnd(value, ~(SIGN_BIT | QNAN)), getInt8PtrTy());
        return CreateLoad(getInt8PtrTy(), CreateStructGEP(getStructType(ObjType::STRING), string, 1));
    }

    Value *LoxBuilder::NumberVal(Value *value) {
        return CreateBitCast(value, getInt64Ty());
    }

    void LoxBuilder::PrintF(const std::string &stringFormat, Value *value) {
        PrintF({CreateGlobalStringPtr(stringFormat), value});
    }

    void LoxBuilder::PrintF(const std::initializer_list<Value *> value) {
        static const auto PrintF = getModule().getOrInsertFunction(
            "printf",
            FunctionType::get(getInt8Ty(), {Type::getInt8PtrTy(getContext())}, true)
        );
        CreateCall(PrintF, value);
    }

    void LoxBuilder::PrintString(const std::string &string) {
        static const auto fmt = CreateGlobalStringPtr("%s\n");
        PrintF({fmt, CreateGlobalStringPtr(string)});
    }

    void LoxBuilder::PrintNumber(Value *value) {
        static const auto gfmt = CreateGlobalStringPtr("%g\n");
        PrintF({gfmt, AsNumber(value)});
    }

    void LoxBuilder::PrintNil() {
        static const auto fmt = CreateGlobalStringPtr("%s\n");
        static const auto nil = CreateGlobalStringPtr("nil");
        PrintF({fmt, nil});
    }

    void LoxBuilder::PrintObject(Value *value) {
        const auto IsStringBlock = CreateBasicBlock("print.string");
        const auto IsFunctionBlock = CreateBasicBlock("print.function");
        const auto DefaultBlock = CreateBasicBlock("print.default");
        const auto EndBlock = CreateBasicBlock("print.end");

        const auto Switch = CreateSwitch(ObjType(value), DefaultBlock);
        Switch->addCase(ObjTypeInt(ObjType::STRING), IsStringBlock);
        Switch->addCase(ObjTypeInt(ObjType::FUNCTION), IsFunctionBlock);

        SetInsertPoint(IsStringBlock);
        PrintString(value);
        CreateBr(EndBlock);

        SetInsertPoint(IsFunctionBlock);
        static auto global_string_ptr = CreateGlobalStringPtr("<fn %s>\n");
        const auto f = AsFunction(value);
        const auto s = CreateLoad(getInt64Ty(), CreateStructGEP(getStructType(ObjType::FUNCTION), f, 3));
        PrintF({global_string_ptr, AsCString(s)});

        CreateBr(EndBlock);
        SetInsertPoint(DefaultBlock);

        CreateBr(EndBlock);
        SetInsertPoint(EndBlock);
    }

    void LoxBuilder::PrintString(Value *value) {
        static const auto fmt = CreateGlobalStringPtr("%s\n");
        PrintF({fmt, AsCString(value)});
    }

    void LoxBuilder::PrintBool(Value *value) {
        static const auto fmt = CreateGlobalStringPtr("%s\n");
        static const auto true_ = CreateGlobalStringPtr("true");
        static const auto false_ = CreateGlobalStringPtr("false");
        PrintF({fmt, CreateSelect(AsBool(value), true_, false_)});
    }
}// namespace lox
