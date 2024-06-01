#ifndef GC_H
#define GC_H

#include "LoxBuilder.h"

constexpr bool STRESS_GC = false;
constexpr int GC_GROWTH_FACTOR = 2;

namespace lox {
    Function *CreateGcFunction(LoxBuilder &Builder);
    void MarkObject(LoxBuilder &Builder, Value *ObjectPtr);
    void AddGlobalGCRoot(LoxModule &Module, GlobalVariable *global);
}// namespace lox

#endif//GC_H
