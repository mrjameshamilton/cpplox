#ifndef GC_H
#define GC_H

#include "LoxBuilder.h"

constexpr bool STRESS_GC = false;
constexpr int GC_GROWTH_FACTOR = 2;

namespace lox {
    Function *CreateGcFunction(LoxBuilder &Builder);
    void MarkObject(LoxBuilder &Builder, Value *ObjectPtr);
    void AddGlobalGCRoot(LoxModule &Module, GlobalVariable *global);

    /**
     * The garbage collector will be disabled for the duration of the block
     * and executed after.
     * The block function must return a point to a Lox object which
     * will be used as an extra GC root, or nullptr.
     * This is useful when multiple allocations need to happen consecutively,
     * to avoid triggering the GC before they are all complete.
     *
     * @return the value produced by the block function.
     */
    Value *DelayGC(LoxBuilder &B, const std::function<Value *(LoxBuilder &)> &block);
}// namespace lox

#endif//GC_H
