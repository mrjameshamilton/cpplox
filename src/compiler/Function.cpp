#include "LoxBuilder.h"

namespace lox {

#define STORE_FUNCTION_ARITY(PTR, ARITY) \
    CreateStore(ARITY, CreateStructGEP(getModule().getStructType(ObjType::FUNCTION), CreateLoad(getPtrTy(), PTR), 1))
#define STORE_FUNCTION_PTR(PTR, STR) \
    CreateStore(STR, CreateStructGEP(getModule().getStructType(ObjType::FUNCTION), CreateLoad(getPtrTy(), PTR), 2, "funcPtr"))
#define STORE_FUNCTION_NAME(PTR, STR) \
    CreateStore(STR, CreateStructGEP(getModule().getStructType(ObjType::FUNCTION), CreateLoad(getPtrTy(), PTR), 3))
#define STORE_FUNCTION_NATIVE(PTR, BOOL) \
    CreateStore(BOOL, CreateStructGEP(getModule().getStructType(ObjType::FUNCTION), CreateLoad(getPtrTy(), PTR), 4))

    Value *LoxBuilder::AllocateFunction(llvm::Function *Function, const bool isNative) {
        Value *obj = AllocateObj(ObjType::FUNCTION, Function->getName());
        Value *name = AllocateString(CreateGlobalStringPtr(Function->getName()), getInt32(Function->getName().size()), "funcName_" + std::string(Function->getName()));

        STORE_FUNCTION_ARITY(obj, getInt32(Function->arg_size() - 1 /* first arg is upvalue array */));
        STORE_FUNCTION_PTR(obj, Function);
        STORE_FUNCTION_NAME(obj, name);
        STORE_FUNCTION_NATIVE(obj, isNative ? getTrue() : getFalse());

        return ObjVal(
            CreatePtrToInt(
                CreateLoad(getPtrTy(), obj),
                getInt64Ty()
            )
        );
    }

    Value *LoxBuilder::AllocateClosure(llvm::Value *function) {
        Value *obj = AllocateObj(ObjType::CLOSURE, "closure");

        CreateStore(function, CreateStructGEP(getModule().getStructType(ObjType::CLOSURE), CreateLoad(getPtrTy(), obj), 1));
        CreateStore(getInt8(0), CreateStructGEP(getModule().getStructType(ObjType::CLOSURE), CreateLoad(getPtrTy(), obj), 2));
        CreateStore(getInt32(0), CreateStructGEP(getModule().getStructType(ObjType::CLOSURE), CreateLoad(getPtrTy(), obj), 3));

        return ObjVal(
            CreatePtrToInt(
                CreateLoad(getPtrTy(), obj),
                getInt64Ty()
            )
        );
    }
}// namespace lox
