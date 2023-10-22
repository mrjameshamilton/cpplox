#ifndef LOX_LLVM_TOKEN_H
#define LOX_LLVM_TOKEN_H
#include <string>
#include <utility>
#include <variant>
namespace lox {
    enum TokenType {
        // Single-character tokens.
        LEFT_PAREN,
        RIGHT_PAREN,
        LEFT_BRACE,
        RIGHT_BRACE,
        COMMA,
        DOT,
        MINUS,
        PLUS,
        SEMICOLON,
        SLASH,
        STAR,

        // One or two character tokens.
        BANG,
        BANG_EQUAL,
        EQUAL,
        EQUAL_EQUAL,
        GREATER,
        GREATER_EQUAL,
        LESS,
        LESS_EQUAL,

        // Literals.
        IDENTIFIER,
        STRING,
        NUMBER,

        // Keywords.
        AND,
        CLASS,
        ELSE,
        FALSE,
        FUN,
        FOR,
        IF,
        NIL,
        OR,
        PRINT,
        RETURN,
        SUPER,
        THIS,
        TRUE,
        VAR,
        WHILE,

        END
    };

    using Literal = std::variant<std::string_view, double, bool, std::nullptr_t>;

    class Token {
    private:
        TokenType type;
        std::string_view lexeme;
        Literal literal;
        unsigned int line;

    public:
        explicit Token(const TokenType type, const std::string_view lexeme, const Literal literal, const unsigned int line)
            : type{type}, lexeme{lexeme}, literal{literal}, line{line} {}

        [[nodiscard]] TokenType getType() { return type; }

        [[nodiscard]] unsigned int getLine() const { return line; }

        [[nodiscard]] std::string_view getLexeme() const { return lexeme; }

        [[nodiscard]] Literal getLiteral() const { return literal; }
    };
}// namespace lox
#endif//LOX_LLVM_TOKEN_H
