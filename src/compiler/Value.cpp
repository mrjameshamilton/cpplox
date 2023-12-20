#include "Value.h"
#include "LoxCompiler.h"

#include <llvm/ADT/DenseMapInfo.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Value.h>
#include <llvm/Passes/PassBuilder.h>

using namespace llvm;
using namespace llvm::sys;

namespace lox {
    Value *LoxCompiler::IsBool(Value *value) const {
        return Builder->CreateICmpEQ(Builder->CreateOr(value, 1), Builder->getInt64(TRUE_VAL));
    }

    Value *LoxCompiler::IsNil(Value *value) const {
        return Builder->CreateICmpEQ(value, Builder->getInt64(NIL_VAL));
    }

    Value *LoxCompiler::IsNumber(Value *value) const {
        return Builder->CreateICmpNE(Builder->CreateAnd(value, QNAN), Builder->getInt64(QNAN));
    }

    Value *LoxCompiler::IsObj(Value *value) const {
        return Builder->CreateICmpEQ(Builder->CreateAnd(value, QNAN | SIGN_BIT), Builder->getInt64(QNAN | SIGN_BIT));
    }

    Value *LoxCompiler::IsString(Value *value) const {
        return Builder->CreateAnd(
            IsObj(value),
            Builder->CreateICmpEQ(ObjType(value), Builder->getInt8(static_cast<uint8_t>(ObjType::STRING)))
        );
    }

    Value *LoxCompiler::ObjType(Value *value) const {
        return Builder->CreateLoad(
            Builder->getInt8Ty(),
            Builder->CreateStructGEP(ObjStructType, AsObj(value), 0)
        );
    }

    Value *LoxCompiler::BoolVal(Value *value) const {
        assert(value->getType() == Builder->getInt1Ty());
        return Builder->CreateSelect(value, Builder->getInt64(TRUE_VAL), Builder->getInt64(FALSE_VAL));
    }

    Value *LoxCompiler::AsBool(Value *value) const {
        assert(value->getType() == Builder->getInt64Ty());
        return Builder->CreateICmpEQ(value, Builder->getInt64(TRUE_VAL));
    }

    Value *LoxCompiler::AsNumber(Value *value) const {
        return Builder->CreateBitCast(value, Builder->getDoubleTy());
    }

    Value *LoxCompiler::ObjVal(Value *value) const {
        return Builder->CreateOr(value, SIGN_BIT | QNAN);
    }

    Value *LoxCompiler::AsObj(Value *value) const {
        return Builder->CreateBitCast(
            Builder->CreateIntToPtr(Builder->CreateAnd(value, ~(SIGN_BIT | QNAN)), Builder->getInt8PtrTy()),
            ObjStructType->getPointerTo()
        );
    }

    Value *LoxCompiler::AsString(Value *value) const {
        return Builder->CreateBitCast(AsObj(value), StringStructType->getPointerTo());
    }

    Value *LoxCompiler::AsCString(Value *value) const {
        const auto string = Builder->CreateIntToPtr(Builder->CreateAnd(value, ~(SIGN_BIT | QNAN)), Builder->getInt8PtrTy());
        return Builder->CreateLoad(Builder->getInt8PtrTy(), Builder->CreateStructGEP(StringStructType, string, 1));
    }

    Value *LoxCompiler::NumberVal(Value *value) const {
        return Builder->CreateBitCast(value, Builder->getInt64Ty());
    }

    void LoxCompiler::PrintF(const std::string &stringFormat, Value *value) const {
        PrintF({Builder->CreateGlobalStringPtr(stringFormat), value});
    }

    void LoxCompiler::PrintF(const std::initializer_list<Value *> value) const {
        static const auto PrintF = LoxModule->getOrInsertFunction(
            "printf",
            FunctionType::get(Builder->getInt8Ty(), {Type::getInt8PtrTy(*Context)}, true)
        );
        Builder->CreateCall(PrintF, value);
    }

    void LoxCompiler::PrintString(const std::string &string) const {
        static const auto fmt = Builder->CreateGlobalStringPtr("%s\n");
        PrintF({fmt, Builder->CreateGlobalStringPtr(string)});
    }

    void LoxCompiler::PrintNumber(Value *value) const {
        static const auto gfmt = Builder->CreateGlobalStringPtr("%g\n");
        PrintF({gfmt, AsNumber(value)});
    }

    void LoxCompiler::PrintNil() const {
        static const auto fmt = Builder->CreateGlobalStringPtr("%s\n");
        static const auto nil = Builder->CreateGlobalStringPtr("nil");
        PrintF({fmt, nil});
    }

    void LoxCompiler::PrintObject(Value *value) const {
        const auto IsStringBlock = BasicBlock::Create(*Context, "string", MainFunction);
        const auto DefaultBlock = BasicBlock::Create(*Context, "default", MainFunction);

        const auto Switch = Builder->CreateSwitch(ObjType(value), DefaultBlock);
        Switch->addCase(Builder->getInt8(static_cast<uint8_t>(ObjType::STRING)), IsStringBlock);

        Builder->SetInsertPoint(IsStringBlock);
        PrintString(value);
        Builder->CreateBr(DefaultBlock);
        Builder->SetInsertPoint(DefaultBlock);
    }

    void LoxCompiler::PrintString(Value *value) const {
        static const auto fmt = Builder->CreateGlobalStringPtr("%s\n");
        PrintF({fmt, AsCString(value)});
    }

    void LoxCompiler::PrintBool(Value *value) const {
        static const auto fmt = Builder->CreateGlobalStringPtr("%s\n");
        static const auto true_ = Builder->CreateGlobalStringPtr("true");
        static const auto false_ = Builder->CreateGlobalStringPtr("false");
        PrintF({fmt, Builder->CreateSelect(AsBool(value), true_, false_)});
    }
}// namespace lox
