#include "Environment.h"

namespace lox {

        void Environment::define(const std::string_view name, const LoxObject &value) { values[name] = value; }

        LoxObject &Environment::getAt(const unsigned long distance, const std::string_view &name) {
            return ancestor(distance)->values[name];
        }

        EnvironmentPtr Environment::ancestor(const unsigned long distance) {
            auto environment = shared_from_this();
            for (unsigned long i = 0; i < distance; i++) { environment = environment->enclosing; }
            return environment;
        }

        LoxObject &Environment::get(const Token &name) {
            if (values.contains(name.getLexeme())) { return values[name.getLexeme()]; }

            if (enclosing != nullptr) { return enclosing->get(name); }

            throw runtime_error(name, "Undefined variable '" + std::string(name.getLexeme()) + "'.");
        }

        void Environment::assign(const Token &name, const LoxObject &value) {
            if (values.contains(name.getLexeme())) {
                values[name.getLexeme()] = value;
                return;
            }

            if (enclosing != nullptr) {
                enclosing->assign(name, value);
                return;
            }

            throw runtime_error(name, std::format("Undefined variable '{}'.", name.getLexeme()));
        }

        void Environment::assignAt(const unsigned long distance, const Token &name, const LoxObject &value) {
            ancestor(distance)->values[name.getLexeme()] = value;
        }

}