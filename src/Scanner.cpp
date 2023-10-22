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

            tokens.emplace_back(TokenType::END, "", "", line);

            return tokens;
        }

    private:
        const std::string source;
        long unsigned int start = 0;
        long unsigned int current = 0;
        long unsigned int line = 1;
        std::vector<Token> tokens = std::vector<Token>();

        void addToken(TokenType type) {
            addToken(type, nullptr);
        }

        void addToken(TokenType type, Literal literal) {
            auto lexeme = std::string_view(source).substr(start, current - start);
            tokens.emplace_back(type, lexeme, literal, line);
        }

        char advance() { return source[current++]; }

        bool isAtEnd() { return current >= source.length(); }

        void scanToken() {
            char c = advance();
            switch (c) {
                case '(':
                    addToken(TokenType::LEFT_PAREN);
                    break;
                case ')':
                    addToken(TokenType::RIGHT_PAREN);
                    break;
                case '{':
                    addToken(TokenType::LEFT_BRACE);
                    break;
                case '}':
                    addToken(TokenType::RIGHT_BRACE);
                    break;
                case ',':
                    addToken(TokenType::COMMA);
                    break;
                case '.':
                    addToken(TokenType::DOT);
                    break;
                case '-':
                    addToken(TokenType::MINUS);
                    break;
                case '+':
                    addToken(TokenType::PLUS);
                    break;
                case ';':
                    addToken(TokenType::SEMICOLON);
                    break;
                case '*':
                    addToken(TokenType::STAR);
                    break;
                case '/':
                    if (match('/')) {
                        // A comment goes until the end of the line.
                        while (peek() != '\n' && !isAtEnd()) advance();
                    } else {
                        addToken(TokenType::SLASH);
                    }
                    break;
                case '!':
                    addToken(match('=') ? TokenType::BANG_EQUAL : TokenType::BANG);
                    break;
                case '=':
                    addToken(match('=') ? TokenType::EQUAL_EQUAL : TokenType::EQUAL);
                    break;
                case '<':
                    addToken(match('=') ? TokenType::LESS_EQUAL : TokenType::LESS);
                    break;
                case '>':
                    addToken(match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER);
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

            auto text = std::string_view(source).substr(start, current - start);
            TokenType type;

            if (text == "and")
                type = TokenType::AND;
            else if (text == "class")
                type = TokenType::CLASS;
            else if (text == "else")
                type = TokenType::ELSE;
            else if (text == "false")
                type = TokenType::FALSE;
            else if (text == "for")
                type = TokenType::FOR;
            else if (text == "fun")
                type = TokenType::FUN;
            else if (text == "if")
                type = TokenType::IF;
            else if (text == "nil")
                type = TokenType::NIL;
            else if (text == "or")
                type = TokenType::OR;
            else if (text == "print")
                type = TokenType::PRINT;
            else if (text == "return")
                type = TokenType::RETURN;
            else if (text == "super")
                type = TokenType::SUPER;
            else if (text == "this")
                type = TokenType::THIS;
            else if (text == "true")
                type = TokenType::TRUE;
            else if (text == "var")
                type = TokenType::VAR;
            else if (text == "while")
                type = TokenType::WHILE;
            else
                type = TokenType::IDENTIFIER;
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

            addToken(TokenType::NUMBER, std::stod(source.substr(start, current - start)));
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
            addToken(TokenType::STRING, std::string_view(source).substr(start + 1, current - 1 - start - 1));
        }

        static inline bool isAlpha(char c) {
            return (c >= 'a' && c <= 'z') ||
                   (c >= 'A' && c <= 'Z') ||
                   c == '_';
        }

        static inline bool isAlphaNumeric(char c) {
            return isAlpha(c) || isDigit(c);
        }

        static inline bool isDigit(char c) {
            return c >= '0' && c <= '9';
        }

        bool match(char expected) {
            if (isAtEnd()) return false;
            if (source[current] != expected) return false;
            current++;
            return true;
        }

        char peek() {
            if (isAtEnd()) return '\0';
            return source[current];
        }

        char peekNext() {
            if (current + 1 >= source.length()) return '\0';
            return source[current + 1];
        }
    };
}// namespace lox
