#ifndef TABLE_H
#define TABLE_H
#include "LoxBuilder.h"
#include <llvm/IR/Value.h>

namespace lox {
    constexpr bool DEBUG_TABLE_ENTRIES = false;
    void IterateTable(LoxBuilder &Builder, Value *Table, Function *FunctionPtr);
    Value *TableDelete(LoxBuilder &Builder, Value *Table, Value *Key);
}
#endif