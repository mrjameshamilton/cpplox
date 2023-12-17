#include "Value.h"
#include "Compiler.h"

#include <llvm/IR/Value.h>

namespace lox {
    Value *Compiler::IsBool(Value *value) const {
        return Builder->CreateICmpEQ(Builder->CreateOr(value, 1), Builder->getInt64(TRUE_VAL));
    }

    Value *Compiler::IsNil(Value *value) const {
        return Builder->CreateICmpEQ(value, Builder->getInt64(NIL_VAL));
    }

    Value *Compiler::IsNumber(Value *value) const {
        return Builder->CreateICmpNE(Builder->CreateAnd(value, QNAN), Builder->getInt64(QNAN));
    }

    Value *Compiler::IsObj(Value *value) const {
        return Builder->CreateICmpEQ(Builder->CreateAnd(value, QNAN | SIGN_BIT), Builder->getInt64(QNAN | SIGN_BIT));
    }

    Value *Compiler::IsString(Value *value) const {
        return Builder->CreateAnd(
            IsObj(value),
            Builder->CreateICmpEQ(ObjType(value), Builder->getInt8(static_cast<uint8_t>(ObjType::STRING)))
        );
    }

    Value *Compiler::ObjType(Value *value) const {
        return Builder->CreateLoad(
            Builder->getInt8Ty(),
            Builder->CreateStructGEP(ObjStructType, AsObj(value), 0)
        );
    }

    Value *Compiler::BoolVal(Value *value) const {
        assert(value->getType() == Builder->getInt1Ty());
        return Builder->CreateSelect(value, Builder->getInt64(TRUE_VAL), Builder->getInt64(FALSE_VAL));
    }

    Value *Compiler::AsBool(Value *value) const {
        assert(value->getType() == Builder->getInt64Ty());
        return Builder->CreateICmpEQ(value, Builder->getInt64(TRUE_VAL));
    }

    Value *Compiler::AsNumber(Value *value) const {
        return Builder->CreateBitCast(value, Builder->getDoubleTy());
    }

    Value *Compiler::ObjVal(Value *value) const {
        return Builder->CreateOr(value, SIGN_BIT | QNAN);
    }

    Value *Compiler::AsObj(Value *value) const {
        return Builder->CreateBitCast(
            Builder->CreateIntToPtr(Builder->CreateAnd(value, ~(SIGN_BIT | QNAN)), Builder->getInt8PtrTy()),
            ObjStructType->getPointerTo()
        );
    }

    Value *Compiler::AsString(Value *value) const {
        return Builder->CreateBitCast(AsObj(value), StringStructType->getPointerTo());
    }

    Value *Compiler::AsCString(Value *value) const {
        const auto string = Builder->CreateIntToPtr(Builder->CreateAnd(value, ~(SIGN_BIT | QNAN)), Builder->getInt8PtrTy());
        return Builder->CreateLoad(Builder->getInt8PtrTy(), Builder->CreateStructGEP(StringStructType, string, 1));
    }

    Value *Compiler::NumberVal(Value *value) const {
        return Builder->CreateBitCast(value, Builder->getInt64Ty());
    }

    void Compiler::PrintF(const std::string &stringFormat, Value *value) const {
        PrintF(Builder->CreateGlobalStringPtr(stringFormat), value);
    }

    void Compiler::PrintF(Value *strFormat, Value *value) const {
        static const auto PrintF = LoxModule->getOrInsertFunction(
            "printf",
            FunctionType::get(Builder->getInt8Ty(), {Type::getInt8PtrTy(*Context)}, true)
        );
        Builder->CreateCall(PrintF, {strFormat, value});
    }

    void Compiler::PrintString(const std::string &string) const {
        static const auto fmt = Builder->CreateGlobalStringPtr("%s\n");
        PrintF(fmt, Builder->CreateGlobalStringPtr(string));
    }

    void Compiler::PrintNumber(Value *value) const {
        static const auto gfmt = Builder->CreateGlobalStringPtr("%g\n");
        PrintF(gfmt, AsNumber(value));
    }

    void Compiler::PrintNil() const {
        static const auto fmt = Builder->CreateGlobalStringPtr("%s\n");
        static const auto nil = Builder->CreateGlobalStringPtr("nil");
        PrintF(fmt, nil);
    }

    void Compiler::PrintString(Value *value) const {
        static const auto fmt = Builder->CreateGlobalStringPtr("%s\n");
        PrintF(fmt, AsCString(value));
    }

    void Compiler::PrintBool(Value *value) const {
        static const auto fmt = Builder->CreateGlobalStringPtr("%s\n");
        static const auto true_ = Builder->CreateGlobalStringPtr("true");
        static const auto false_ = Builder->CreateGlobalStringPtr("false");
        PrintF(fmt, Builder->CreateSelect(AsBool(value), true_, false_));
    }
}// namespace lox
