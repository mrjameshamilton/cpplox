#include "LoxClass.h"
#include "LoxFunction.h"
#include "LoxInstance.h"
#include "LoxObject.h"

namespace lox {

    LoxObject LoxClass::operator()(Interpreter &interpreter, const std::vector<LoxObject> &arguments) {
        const auto &instance = std::make_shared<LoxInstance>(shared_from_this());
        if (const auto &initializer = this->initializer; initializer != nullptr) {
            const auto &function = initializer->bind(instance);
            const auto &callable = std::reinterpret_pointer_cast<LoxCallable>(function);
            (*callable)(interpreter, arguments);
        }
        return instance;
    }

    LoxFunctionPtr LoxClass::findMethod(const std::string_view method_name) {
        if (methods.contains(method_name)) { return methods[method_name]; }

        if (superClass.has_value()) { return superClass.value()->findMethod(method_name); }

        return nullptr;
    }

    int LoxClass::arity() {
        return this->initializer == nullptr ? 0
                                            : std::reinterpret_pointer_cast<LoxCallable>(this->initializer)->arity();
    }

    std::string LoxClass::to_string() { return std::string(name); }
}// namespace lox