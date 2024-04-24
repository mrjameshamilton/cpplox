#ifndef CPPLOX_UPVALUE_H
#define CPPLOX_UPVALUE_H

#include "LoxBuilder.h"


constexpr bool DEBUG_UPVALUES = false;

namespace lox {
    struct Upvalue {
        unsigned long index;
        Value *value;
        bool isLocal;
    };

    void closeUpvalues(LoxBuilder &Builder, Value *local);
    void IterateUpvalues(LoxBuilder &Builder, Function *FunctionPointer);
}// namespace lox
#endif//CPPLOX_UPVALUE_H
