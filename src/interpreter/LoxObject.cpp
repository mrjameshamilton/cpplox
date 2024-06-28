#include "LoxObject.h"
#include "../Util.h"
#include "LoxCallable.h"
#include "LoxInstance.h"

#include <format>

namespace lox {

    bool isTruthy(const LoxObject &object) {
        if (std::holds_alternative<LoxNil>(object)) return false;
        if (std::holds_alternative<LoxBoolean>(object)) return std::get<LoxBoolean>(object);
        return true;
    }

    std::string to_string(const LoxObject &object) {
        return std::visit(
            overloaded{
                [](const LoxBoolean value) -> std::string { return value ? "true" : "false"; },
                [](const LoxNumber value) -> std::string { return std::format("{:g}", value); },
                [](const LoxString &value) -> std::string { return value; },
                [](const LoxCallablePtr &callable) -> std::string { return callable->to_string(); },
                [](const LoxInstancePtr &instance) -> std::string { return instance->to_string(); },
                [](LoxNil) -> std::string { return "nil"; },
            },
            object
        );
    }
}// namespace lox