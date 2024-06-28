#ifndef LOXOBJECT_H
#define LOXOBJECT_H
#include "../frontend/Error.h"
#include <memory>
#include <string>
#include <variant>

namespace lox {

    // Lox runtime types.
    using LoxNil = std::nullptr_t;
    using LoxString = std::string;
    using LoxNumber = double;
    using LoxBoolean = bool;
    struct LoxCallable;
    struct LoxFunction;
    struct LoxClass;
    struct LoxInstance;
    using LoxCallablePtr = std::shared_ptr<LoxCallable>;
    using LoxFunctionPtr = std::shared_ptr<LoxFunction>;
    using LoxInstancePtr = std::shared_ptr<LoxInstance>;
    using LoxClassPtr = std::shared_ptr<LoxClass>;
    using LoxObject = std::variant<LoxNil, LoxString, LoxNumber, LoxBoolean, LoxCallablePtr, LoxInstancePtr>;

    bool isTruthy(const LoxObject &object);
    std::string to_string(const LoxObject &object);

}// namespace lox

#endif//LOXOBJECT_H
