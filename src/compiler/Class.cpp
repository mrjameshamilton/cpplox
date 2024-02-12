#include "Class.h"

namespace lox {

    Value *LoxBuilder::AllocateClass(const std::string_view className) {
        const auto obj = AllocateObj(ObjType::CLASS, "class");

        Value *name = AllocateString(CreateGlobalStringPtr(className), getInt32(className.size()), ("class_" + className).str());

        const auto ptr = CreateLoad(getPtrTy(), obj);
        CreateStore(name, CreateStructGEP(getModule().getStructType(ObjType::CLASS), ptr, 1));

        return ptr;
    }

}// namespace lox