#include "LoxBuilder.h"

namespace lox {

#define STORE_FUNCTION_ARITY(PTR, LENGTH) \
    CreateStore(LENGTH, CreateStructGEP(getModule().getStructType(ObjType::FUNCTION), CreateLoad(getPtrTy(), PTR), 1))
#define STORE_FUNCTION_PTR(PTR, STR) \
    CreateStore(STR, CreateStructGEP(getModule().getStructType(ObjType::FUNCTION), CreateLoad(getPtrTy(), PTR), 2, "funcPtr"))
#define STORE_FUNCTION_NAME(PTR, STR) \
    CreateStore(STR, CreateStructGEP(getModule().getStructType(ObjType::FUNCTION), CreateLoad(getPtrTy(), PTR), 3))

    Value *LoxBuilder::AllocateFunction(llvm::Function *Function) {
        Value *obj = AllocateObj(ObjType::FUNCTION, Function->getName());
        Value *name = AllocateString(CreateGlobalStringPtr(Function->getName()), getInt32(Function->getName().size()), "funcName_" + std::string(Function->getName()));

        STORE_FUNCTION_ARITY(obj, getInt32(Function->arg_size()));
        STORE_FUNCTION_PTR(obj, Function);
        STORE_FUNCTION_NAME(obj, name);

        return ObjVal(
            CreatePtrToInt(
                CreateLoad(getPtrTy(), obj),
                getInt64Ty()
            )
        );
    }
}// namespace lox
