#include "LoxFunction.h"

namespace lox {

    LoxObject LoxFunction::operator()(Interpreter &interpreter, const std::vector<LoxObject> &arguments) {
        const auto environment = std::make_shared<Environment>(closure);
        for (int i = 0; i < static_cast<int>(declaration->parameters.size()); i++) {
            environment->define(declaration->parameters[i].getLexeme(), arguments[i]);
        }

        if (const auto &result = interpreter.executeBlock(declaration->body, environment);
            std::holds_alternative<Return>(result)) {
            if (isInitializer) { return std::move(closure->getAt(0, "this")); }

            return std::move(std::get<Return>(result).value);
        }

        if (isInitializer) { return std::move(closure->getAt(0, "this")); }

        return LoxNil();
    }

    LoxFunctionPtr LoxFunction::bind(const LoxInstancePtr &instance) {
        auto environment = std::make_shared<Environment>(closure);
        environment->define("this", instance);
        return std::make_shared<LoxFunction>(declaration, environment, isInitializer);
    }

    std::string LoxFunction::to_string() { return std::format("<fn {}>", std::string(declaration->name.getLexeme())); }
}// namespace lox