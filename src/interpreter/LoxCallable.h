#ifndef LOXCALLABLE_H
#define LOXCALLABLE_H
#include "Interpreter.h"
#include "LoxObject.h"

namespace lox {

    struct LoxCallable {
        int _arity = 0;
        explicit LoxCallable(const int arity) : _arity{arity} {}
        virtual ~LoxCallable() = default;

        virtual LoxObject operator()(Interpreter &interpreter, const std::vector<LoxObject> &arguments) = 0;
        virtual std::string to_string() = 0;
        virtual int arity() { return this->_arity; };
    };

}// namespace lox

#endif//LOXCALLABLE_H
