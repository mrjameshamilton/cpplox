#include "Compiler.h"
#include "Value.h"

#include <llvm/IR/Value.h>

namespace lox {

    Value *Compiler::AllocateObj(lox::ObjType objType, const std::string_view name) const {
        Type *StructType;

        switch (objType) {
            case ObjType::STRING:
                StructType = StringStructType;
                break;
            // TODO: other types.
            default:
                throw std::runtime_error("Not implemented");
        }

        Type *IntPtrTy = IntegerType::getInt32Ty(*Context);
        // The malloc size IR that is generated with getSizeOf uses a hack described here:
        // https://mukulrathi.com/create-your-own-programming-language/concurrency-runtime-language-tutorial/#malloc
        Constant *allocsize = ConstantExpr::getSizeOf(StructType);
        allocsize = ConstantExpr::getTruncOrBitCast(allocsize, IntPtrTy);

        const auto NewObjMalloc = Builder->CreateMalloc(
            IntPtrTy,
            StructType,
            allocsize,
            nullptr
        );

        Builder->CreateStore(
            Builder->getInt8(static_cast<uint8_t>(objType)),
            Builder->CreateStructGEP(ObjStructType, NewObjMalloc, 0, "ObjType")
        );

        const auto NewObj = CreateEntryBlockAlloca(Builder->GetInsertBlock()->getParent(), StructType, name);
        Builder->CreateStore(NewObjMalloc, NewObj);

        return Builder->CreateBitCast(NewObj, StructType->getPointerTo());
    }
}// namespace lox
