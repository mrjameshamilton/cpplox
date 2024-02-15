#include "Class.h"

namespace lox {

    Value *LoxBuilder::AllocateClass(const std::string_view className) {
        const auto ptr = AllocateObj(ObjType::CLASS, "class");

        Value *name = AllocateString(CreateGlobalStringPtr(className), getInt32(className.size()), ("class_" + className).str());

        CreateStore(name, CreateStructGEP(getModule().getStructType(ObjType::CLASS), ptr, 1));

        return ptr;
    }

    Value *LoxBuilder::AllocateInstance(llvm::Value *klass) {
        const auto ptr = AllocateObj(ObjType::INSTANCE, "instance");

        CreateStore(klass, CreateStructGEP(getModule().getStructType(ObjType::INSTANCE), ptr, 1));

        return ptr;
    }
}// namespace lox