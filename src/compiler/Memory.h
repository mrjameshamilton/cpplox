#ifndef MEMORY_H
#define MEMORY_H

#include <llvm/IR/IRBuilder.h>

#include <llvm/IR/Instructions.h>

#include "LoxBuilder.h"

using namespace llvm;

namespace lox {
    AllocaInst *CreateEntryBlockAlloca(Function *TheFunction, Type *type, std::string_view VarName, const std::function<void(IRBuilder<> &, AllocaInst *)> &entryBuilder = nullptr);
    void FreeObject(LoxBuilder &Builder, Value *value);
    void FreeObjects(LoxBuilder &Builder);
}// namespace lox

#endif//MEMORY_H
