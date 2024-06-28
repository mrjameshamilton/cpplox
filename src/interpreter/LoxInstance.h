#ifndef LOXINSTANCE_H
#define LOXINSTANCE_H
#include "LoxObject.h"

#include <memory>
#include <unordered_map>
#include <utility>

namespace lox {

    struct LoxInstance : std::enable_shared_from_this<LoxInstance> {
        LoxClassPtr klass;
        std::unordered_map<std::string_view, LoxObject> fields;

        explicit LoxInstance(LoxClassPtr klass) : klass{std::move(klass)} {}

        LoxObject get(const Token &name);
        void set(const Token &name, const LoxObject &value);
        std::string to_string() const;
    };

}// namespace lox

#endif//LOXINSTANCE_H
