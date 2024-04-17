#ifndef GLOBALSTACK_H
#define GLOBALSTACK_H
#include "LoxBuilder.h"

namespace lox {
    void PushGlobal(LoxBuilder &Builder, Value *global);
    void IterateGlobals(LoxBuilder &Builder, Function *FunctionPointer);
}// namespace lox

#endif//GLOBALSTACK_H
