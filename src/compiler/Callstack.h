#ifndef CPPLOX_CALLSTACK_H
#define CPPLOX_CALLSTACK_H

#include "LoxBuilder.h"

namespace lox {
    void PushCall(LoxBuilder &Builder, Value *line, Value *name);
    void PopCall(LoxBuilder &Builder);
    void PrintStackTrace(LoxBuilder &Builder);
    void CheckStackOverflow(LoxBuilder &Builder, Value *line, Value *name);
}// namespace lox

#endif//CPPLOX_CALLSTACK_H
