#include "LoxBuilder.h"

namespace lox {

    Value *LoxBuilder::AllocateFunction(llvm::Function *Function, const bool isNative) {
        Value *ptr = AllocateObj(ObjType::FUNCTION, Function->getName());
        Value *name = AllocateString(CreateGlobalStringPtr(Function->getName()), getInt32(Function->getName().size()), "funcName_" + std::string(Function->getName()));

        CreateStore(getInt32(Function->arg_size() - 1), CreateObjStructGEP(ObjType::FUNCTION, ptr, 1));
        CreateStore(Function, CreateObjStructGEP(ObjType::FUNCTION, ptr, 2, "funcPtr"));
        CreateStore(name, CreateObjStructGEP(ObjType::FUNCTION, ptr, 3));
        CreateStore(isNative ? getTrue() : getFalse(), CreateObjStructGEP(ObjType::FUNCTION, ptr, 4));

        return ptr;
    }

    Value *LoxBuilder::AllocateClosure(llvm::Function *F, bool isNative) {
        const auto function = AllocateFunction(F, isNative);
        const auto ptr = AllocateObj(ObjType::CLOSURE, "closure");

        CreateStore(function, CreateObjStructGEP(ObjType::CLOSURE, ptr, 1));
        CreateStore(getInt8(0), CreateObjStructGEP(ObjType::CLOSURE, ptr, 2));
        CreateStore(getInt32(0), CreateObjStructGEP(ObjType::CLOSURE, ptr, 3));

        return ptr;
    }
}// namespace lox
