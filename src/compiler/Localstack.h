#ifndef LOCALSTACK_H
#define LOCALSTACK_H
#include "LoxBuilder.h"

namespace lox {
    void PushLocal(LoxBuilder &Builder, Value *local, StringRef what);
    Value *PushTemp(LoxBuilder &Builder, Value *value, StringRef what);
    void PopLocal(LoxBuilder &Builder, const StringRef what);
    void IterateLocals(LoxBuilder& Builder, Function* FunctionPointer);
}// namespace lox

#endif//LOCALSTACK_H
