#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include "../frontend/Token.h"
#include "LoxObject.h"

#include <format>
#include <memory>
#include <unordered_map>

namespace lox {
    class Environment;
    using EnvironmentPtr = std::shared_ptr<Environment>;

    class Environment : public std::enable_shared_from_this<Environment> {
        std::unordered_map<std::string_view, LoxObject> values;
        EnvironmentPtr enclosing;
    public:
        explicit Environment() = default;
        explicit Environment(EnvironmentPtr environment) : enclosing{std::move(environment)} {}

        EnvironmentPtr get_enclosing() const { return enclosing; }
        void define(std::string_view name, const LoxObject &value = LoxNil{});
        LoxObject &getAt(unsigned long distance, const std::string_view &name);
        EnvironmentPtr ancestor(unsigned long distance);
        LoxObject &get(const Token &name);
        void assign(const Token &name, const LoxObject &value);
        void assignAt(unsigned long distance, const Token &name, const LoxObject &value);
    };
}// namespace lox


#endif//ENVIRONMENT_H
