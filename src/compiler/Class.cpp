#include "Class.h"

namespace lox {

    Value *LoxBuilder::AllocateClass(const std::string_view className) {
        const auto ptr = AllocateObj(ObjType::CLASS, "class");

        Value *name = AllocateString(className, ("class_" + className).str());
        Value *methods = AllocateTable();

        CreateStore(name, CreateObjStructGEP(ObjType::CLASS, ptr, 1));
        CreateStore(methods, CreateObjStructGEP(ObjType::CLASS, ptr, 2));

        return ptr;
    }

    Value *LoxBuilder::AllocateInstance(llvm::Value *klass) {
        const auto ptr = AllocateObj(ObjType::INSTANCE, "instance");

        Value *fields = AllocateTable();

        CreateStore(klass, CreateObjStructGEP(ObjType::INSTANCE, ptr, 1));
        CreateStore(fields, CreateObjStructGEP(ObjType::INSTANCE, ptr, 2));

        return ptr;
    }

    Value *LoxBuilder::BindMethod(llvm::Value *receiver, llvm::Value *closure) {
        assert(receiver->getType() == getInt64Ty());
        assert(closure->getType() == getPtrTy());
        const auto ptr = AllocateObj(ObjType::BOUND_METHOD, "bound_method");

        CreateStore(receiver, CreateObjStructGEP(ObjType::BOUND_METHOD, ptr, 1));
        CreateStore(closure, CreateObjStructGEP(ObjType::BOUND_METHOD, ptr, 2));

        return ptr;
    }
}// namespace lox