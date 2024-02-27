#ifndef CPPLOX_UPVALUE_H
#define CPPLOX_UPVALUE_H

#include "../AST.h"
#include "LoxBuilder.h"
#include "ModuleCompiler.h"

#include <iostream>
#include <llvm/ADT/ScopedHashTable.h>
#include <llvm/IR/Value.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <stack>

constexpr bool DEBUG_UPVALUES = false;

namespace lox {
    struct Upvalue {
        unsigned long index;
        Value *value;
        bool isLocal;
    };

    void closeUpvalues(LoxBuilder &Builder, Value *local);
}// namespace lox
#endif//CPPLOX_UPVALUE_H
