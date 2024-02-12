#include "LoxBuilder.h"

namespace lox {

    Value *LoxBuilder::AllocateFunction(llvm::Function *Function, const bool isNative) {
        Value *obj = AllocateObj(ObjType::FUNCTION, Function->getName());
        Value *name = AllocateString(CreateGlobalStringPtr(Function->getName()), getInt32(Function->getName().size()), "funcName_" + std::string(Function->getName()));

        const auto ptr = CreateLoad(getPtrTy(), obj);
        CreateStore(getInt32(Function->arg_size() - 1), CreateStructGEP(getModule().getStructType(ObjType::FUNCTION), ptr, 1));
        CreateStore(Function, CreateStructGEP(getModule().getStructType(ObjType::FUNCTION), ptr, 2, "funcPtr"));
        CreateStore(name, CreateStructGEP(getModule().getStructType(ObjType::FUNCTION), ptr, 3));
        CreateStore(isNative ? getTrue() : getFalse(), CreateStructGEP(getModule().getStructType(ObjType::FUNCTION), ptr, 4));

        return ptr;
    }

    Value *LoxBuilder::AllocateClosure(llvm::Function *F, bool isNative) {
        const auto function = AllocateFunction(F, isNative);
        const auto obj = AllocateObj(ObjType::CLOSURE, "closure");

        const auto ptr = CreateLoad(getPtrTy(), obj);
        CreateStore(function, CreateStructGEP(getModule().getStructType(ObjType::CLOSURE), ptr, 1));
        CreateStore(getInt8(0), CreateStructGEP(getModule().getStructType(ObjType::CLOSURE), ptr, 2));
        CreateStore(getInt32(0), CreateStructGEP(getModule().getStructType(ObjType::CLOSURE), ptr, 3));

        return ptr;
    }
}// namespace lox
