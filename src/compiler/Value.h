#ifndef OBJECT_H
#define OBJECT_H

#include <cstdint>

constexpr uint64_t SIGN_BIT = 0x8000000000000000;
constexpr uint64_t QNAN = 0x7ffc000000000000;

constexpr uint64_t TAG_UNINITIALIZED = 0;
constexpr uint64_t TAG_NIL = 1;
constexpr uint64_t TAG_FALSE = 2;
constexpr uint64_t TAG_TRUE = 3;

constexpr uint64_t FALSE_VAL = QNAN | TAG_FALSE;
constexpr uint64_t TRUE_VAL = QNAN | TAG_TRUE;
constexpr uint64_t NIL_VAL = QNAN | TAG_NIL;
constexpr uint64_t UNITIALIZED_VAL = QNAN | TAG_UNINITIALIZED;

namespace lox {
    enum class ObjType : int8_t {
        STRING = 1,
        FUNCTION = 2,
        CLOSURE = 3,
        UPVALUE = 4,
        CLASS = 5,
        INSTANCE = 6,
    };
}

#endif//OBJECT_H
