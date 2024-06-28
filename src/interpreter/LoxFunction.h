#ifndef LOXFUNCTION_H
#define LOXFUNCTION_H
#include "LoxCallable.h"

namespace lox {

    struct LoxFunction final : LoxCallable {
        std::shared_ptr<FunctionStmt> declaration;
        EnvironmentPtr closure;
        bool isInitializer;

        explicit LoxFunction(
            const std::shared_ptr<FunctionStmt> &declaration, const EnvironmentPtr &closure,
            const bool isInitializer = false
        )
            : LoxCallable(static_cast<int>(declaration->parameters.size())), declaration{declaration}, closure{closure},
              isInitializer{isInitializer} {}

        ~LoxFunction() override = default;

        LoxObject operator()(Interpreter &interpreter, const std::vector<LoxObject> &arguments) override;
        LoxFunctionPtr bind(const LoxInstancePtr &instance);
        std::string to_string() override;
    };
}// namespace lox

#endif//LOXFUNCTION_H
