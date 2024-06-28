#ifndef LOXCLASS_H
#define LOXCLASS_H
#include "LoxCallable.h"

#include <memory>

namespace lox {

    struct LoxClass final : LoxCallable, std::enable_shared_from_this<LoxClass> {
        std::string_view name;
        std::optional<std::shared_ptr<LoxClass>> superClass;
        std::unordered_map<std::string_view, LoxFunctionPtr> methods;
        LoxFunctionPtr initializer;

        explicit LoxClass(
            const std::string_view &name, const std::optional<std::shared_ptr<LoxClass>> &superClass,
            const std::unordered_map<std::string_view, LoxFunctionPtr> &methods
        )
            : LoxCallable(0), name{name}, superClass{superClass}, methods{methods} {
            this->initializer = findMethod("init");
        }

        ~LoxClass() override = default;

        LoxObject operator()(Interpreter &interpreter, const std::vector<LoxObject> &arguments) override;
        LoxFunctionPtr findMethod(std::string_view method_name);
        int arity() override;
        std::string to_string() override;
    };
}// namespace lox
#endif//LOXCLASS_H
