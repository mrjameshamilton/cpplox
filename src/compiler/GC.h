#ifndef GC_H
#define GC_H

#include "LoxBuilder.h"

constexpr bool STRESS_GC = false;
constexpr int GC_GROWTH_FACTOR = 2;


namespace lox {
    Function *CreateGcFunction(LoxBuilder &Builder);
}


#endif//GC_H
