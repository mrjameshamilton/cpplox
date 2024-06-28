#ifndef NATIVEFUNCTION_H
#define NATIVEFUNCTION_H
#include "LoxCallable.h"

#include <functional>

namespace lox {

    struct NativeFunction final : LoxCallable {
        using NativeFnType = std::function<LoxObject(const std::vector<LoxObject> &)>;
        NativeFnType function;

        explicit NativeFunction(NativeFnType function, const int arity = 0)
            : LoxCallable(arity), function{std::move(function)} {}

        ~NativeFunction() override = default;

        LoxObject operator()(Interpreter & /*interpreter*/, const std::vector<LoxObject> &arguments) override {
            return function(arguments);
        }

        std::string to_string() override { return "<native fn>"; }
    };
}// namespace lox

#endif//NATIVEFUNCTION_H
