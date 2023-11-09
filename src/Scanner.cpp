#include "Error.h"
#include "Token.h"
#include <string>
#include <utility>
#include <vector>

namespace lox {
    class Scanner {
    public:
        explicit Scanner(std::string Source) : source{std::move(Source)} {}

        std::vector<Token> scanTokens() {
            while (!isAtEnd()) {
                // We are at the beginning of the next lexeme.
                start = current;
                scanToken();
            }

            tokens.emplace_back(END, "", "", line);

            return tokens;
        }

    private:
        const std::string source;
        long unsigned int start = 0;
        long unsigned int current = 0;
        long unsigned int line = 1;
        std::vector<Token> tokens = std::vector<Token>();

        void addToken(const TokenType type) {
            addToken(type, nullptr);
        }

        void addToken(TokenType type, Literal literal) {
            auto lexeme = std::string_view(source).substr(start, current - start);
            tokens.emplace_back(type, lexeme, literal, line);
        }

        char advance() { return source[current++]; }

        [[nodiscard]] bool isAtEnd() const { return current >= source.length(); }

        void scanToken() {
            switch (const char c = advance()) {
                case '(':
                    addToken(LEFT_PAREN);
                    break;
                case ')':
                    addToken(RIGHT_PAREN);
                    break;
                case '{':
                    addToken(LEFT_BRACE);
                    break;
                case '}':
                    addToken(RIGHT_BRACE);
                    break;
                case ',':
                    addToken(COMMA);
                    break;
                case '.':
                    addToken(DOT);
                    break;
                case '-':
                    addToken(MINUS);
                    break;
                case '+':
                    addToken(PLUS);
                    break;
                case ';':
                    addToken(SEMICOLON);
                    break;
                case '*':
                    addToken(STAR);
                    break;
                case '/':
                    if (match('/')) {
                        // A comment goes until the end of the line.
                        while (peek() != '\n' && !isAtEnd()) advance();
                    } else {
                        addToken(SLASH);
                    }
                    break;
                case '!':
                    addToken(match('=') ? BANG_EQUAL : BANG);
                    break;
                case '=':
                    addToken(match('=') ? EQUAL_EQUAL : EQUAL);
                    break;
                case '<':
                    addToken(match('=') ? LESS_EQUAL : LESS);
                    break;
                case '>':
                    addToken(match('=') ? GREATER_EQUAL : GREATER);
                    break;
                case ' ':
                case '\r':
                case '\t':
                    // Ignore whitespace.
                    break;
                case '\n':
                    line++;
                    break;
                case '"':
                    string();
                    break;
                default: {
                    if (isDigit(c)) {
                        number();
                    } else if (isAlpha(c)) {
                        identifier();
                    } else {
                        if (isDigit(c)) {
                            number();
                        } else {
                            lox::error(line, "Unexpected character.");
                        }
                    }
                    break;
                }
            }
        }

        void identifier() {
            while (isAlphaNumeric(peek())) advance();

            const auto text = std::string_view(source).substr(start, current - start);
            TokenType type;

            if (text == "and")
                type = AND;
            else if (text == "class")
                type = CLASS;
            else if (text == "else")
                type = ELSE;
            else if (text == "false")
                type = FALSE;
            else if (text == "for")
                type = FOR;
            else if (text == "fun")
                type = FUN;
            else if (text == "if")
                type = IF;
            else if (text == "nil")
                type = NIL;
            else if (text == "or")
                type = OR;
            else if (text == "print")
                type = PRINT;
            else if (text == "return")
                type = RETURN;
            else if (text == "super")
                type = SUPER;
            else if (text == "this")
                type = THIS;
            else if (text == "true")
                type = TRUE;
            else if (text == "var")
                type = VAR;
            else if (text == "while")
                type = WHILE;
            else
                type = IDENTIFIER;
            addToken(type);
        }

        void number() {
            while (isDigit(peek())) advance();

            // Look for a fractional part.
            if (peek() == '.' && isDigit(peekNext())) {
                // Consume the "."
                advance();

                while (isDigit(peek())) advance();
            }

            addToken(NUMBER, std::stod(source.substr(start, current - start)));
        }

        void string() {
            while (peek() != '"' && !isAtEnd()) {
                if (peek() == '\n') line++;
                advance();
            }

            if (isAtEnd()) {
                lox::error(line, "Unterminated string.");
                return;
            }

            // The closing ".
            advance();

            // Trim the surrounding quotes.
            addToken(STRING, std::string_view(source).substr(start + 1, current - 1 - start - 1));
        }

        static bool isAlpha(const char c) {
            return (c >= 'a' && c <= 'z') ||
                   (c >= 'A' && c <= 'Z') ||
                   c == '_';
        }

        static bool isAlphaNumeric(const char c) {
            return isAlpha(c) || isDigit(c);
        }

        static bool isDigit(const char c) {
            return c >= '0' && c <= '9';
        }

        bool match(const char expected) {
            if (isAtEnd()) return false;
            if (source[current] != expected) return false;
            current++;
            return true;
        }

        [[nodiscard]] char peek() const {
            if (isAtEnd()) return '\0';
            return source[current];
        }

        [[nodiscard]] char peekNext() const {
            if (current + 1 >= source.length()) return '\0';
            return source[current + 1];
        }
    };
}// namespace lox
