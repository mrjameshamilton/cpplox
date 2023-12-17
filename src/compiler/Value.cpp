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

}// namespace lox
