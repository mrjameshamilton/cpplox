#ifndef CPPLOX_CALLSTACK_H
#define CPPLOX_CALLSTACK_H

#include "LoxBuilder.h"
#include "ModuleCompiler.h"

namespace lox {

    void Push(LoxBuilder &Builder, Value *line, Value *name);
    void Pop(LoxBuilder &Builder);
    void PrintStackTrace(LoxBuilder &Builder);
    void CheckStackOverflow(LoxBuilder &Builder, Value *line, Value *name);

}// namespace lox

#endif//CPPLOX_CALLSTACK_H
