#ifndef LOX_LLVM_ERROR_H
#define LOX_LLVM_ERROR_H

#include "Token.h"
#include <iostream>
#include <string>
namespace lox {
    static bool hadError = false;
    static bool hadRuntimeError = false;

    static void report(const long unsigned int line, const std::string &where,
                       const std::string &message) {
        std::cerr << "[line " << line << "] Error" << where << ": " << message << "\n";
        hadError = true;
    }

    static void error(const long unsigned int line, const std::string &message) {
        report(line, "", message);
    }

    static void error(const Token token, const std::string &message) {
        if (token.getType() == TokenType::END) {
            report(token.getLine(), " at end", message);
        } else {
            report(token.getLine(), " at '" + std::string(token.getLexeme()) + "'", message);
        }
    }
    struct runtime_error : std::runtime_error {
        Token token;
        explicit runtime_error(const Token token, const std::string &message) : std::runtime_error(message), token{token} {}
    };
    static void runtimeError(const lox::runtime_error &error) {
        std::cerr << error.what() << "\n[line " << error.token.getLine() << "]";
        hadRuntimeError = true;
    }


}// namespace lox
#endif//LOX_LLVM_ERROR_H
