#ifndef OBJECT_H
#define OBJECT_H

#include <cstdint>

#define SIGN_BIT ((uint64_t) 0x8000000000000000)
#define QNAN ((uint64_t) 0x7ffc000000000000)

#define TAG_UNINITIALIZED 0
#define TAG_NIL 1
#define TAG_FALSE 2
#define TAG_TRUE 3

#define FALSE_VAL ((uint64_t) (QNAN | TAG_FALSE))
#define TRUE_VAL ((uint64_t) (QNAN | TAG_TRUE))
#define NIL_VAL ((uint64_t) (QNAN | TAG_NIL))
#define UNITIALIZED_VAL ((uint64_t) (QNAN | TAG_UNINITIALIZED))

namespace lox {
    enum class ObjType : int8_t {
        STRING = 1,
        FUNCTION = 2
    };
}

#endif//OBJECT_H
