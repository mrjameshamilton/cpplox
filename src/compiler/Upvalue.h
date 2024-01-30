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

namespace lox {
    struct Upvalue {
        unsigned long index;
        Value *value;
        bool isLocal;
    };
}// namespace lox
#endif//CPPLOX_UPVALUE_H
