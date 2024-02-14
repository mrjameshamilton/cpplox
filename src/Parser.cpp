#include "AST.h"
#include "Error.h"
#include "Token.h"
#include <functional>
#include <iostream>
#include <optional>
#include <vector>

namespace lox {
    class ParseError final : public std::runtime_error {
    public:
        explicit ParseError(const std::string &arg) : runtime_error(arg) {}
    };

    class Parser {
    public:
        explicit Parser(const std::vector<Token> &tokens) : tokens{tokens} {}

        Program parse() {
            auto program = Program();
            try {
                while (!isAtEnd()) {
                    if (auto decl = declaration(); decl.has_value()) {
                        program.push_back(std::move(decl.value()));
                    }
                }
            } catch (const std::invalid_argument &e) {
                std::cout << "error: " << e.what() << "\n"
                          << std::flush;
            }
            return program;
        }

    private:
        std::vector<Token> tokens;
        int current = 0;

        using parserFn = Expr (Parser::*)();

        std::optional<Stmt> declaration() {
            try {
                if (match(CLASS)) return classDeclaration();
                if (match(VAR)) return varDeclaration();
                if (match(FUN)) return function("function");

                return std::make_optional<Stmt>(statement());
            } catch (ParseError &) {
                synchronize();
                return std::make_optional<Stmt>();
            }
        }

        ClassStmtPtr classDeclaration() {
            auto name = consume(IDENTIFIER, "Expect class name.");

            std::optional<VarExprPtr> superclass;
            if (match(LESS)) {
                consume(IDENTIFIER, "Expect superclass name.");
                superclass = std::make_unique<VarExpr>(previous());
            }

            consume(LEFT_BRACE, "Expect '{' before class body.");

            std::vector<FunctionStmtPtr> methods;
            while (!check(RIGHT_BRACE) && !isAtEnd()) {
                methods.push_back(function("method"));
            }

            consume(RIGHT_BRACE, "Expect '}' after class body.");

            return std::make_unique<ClassStmt>(name, std::move(superclass), std::move(methods));
        }

        VarStmtPtr varDeclaration() {
            const Token name = consume(IDENTIFIER, "Expect variable name.");
            Expr initializer = match(EQUAL) ? expression() : std::make_unique<LiteralExpr>(nullptr);
            consume(SEMICOLON, "Expect ';' after variable declaration.");
            return std::make_unique<VarStmt>(name, std::move(initializer));
        }

        WhileStmtPtr whileStatement() {
            consume(LEFT_PAREN, "Expect '(' after 'while'.");
            Expr condition = expression();
            consume(RIGHT_PAREN, "Expect ')' after condition.");
            Stmt body = statement();

            return std::make_unique<WhileStmt>(std::move(condition), std::move(body));
        }

        Stmt statement() {
            if (match(PRINT)) return printStatement();
            if (match(RETURN)) return returnStatement();
            if (match(WHILE)) return whileStatement();
            if (match(FOR)) return forStatement();
            if (match(IF)) return ifStatement();
            if (match(LEFT_BRACE)) return std::make_unique<BlockStmt>(block());

            return expressionStatement();
        }

        Stmt forStatement() {
            consume(LEFT_PAREN, "Expect '(' after 'for'.");

            std::optional<Stmt> initializer;
            if (match(SEMICOLON)) {
                initializer = {};
            } else if (match(VAR)) {
                initializer = varDeclaration();
            } else {
                initializer = expressionStatement();
            }

            std::optional<Expr> condition = {};
            if (!check(SEMICOLON)) {
                condition = expression();
            }
            consume(SEMICOLON, "Expect ';' after loop condition.");

            std::optional<Expr> increment = {};
            if (!check(RIGHT_PAREN)) {
                increment = expression();
            }
            consume(RIGHT_PAREN, "Expect ')' after for clauses.");

            Stmt body = statement();

            if (increment.has_value()) {
                StmtList statements;
                statements.push_back(std::move(body));
                statements.emplace_back(std::make_unique<ExpressionStmt>(std::move(increment.value())));
                body = std::make_unique<BlockStmt>(std::move(statements));
            }

            if (!condition.has_value()) {
                condition = std::make_unique<LiteralExpr>(true);
            }

            body = std::make_unique<WhileStmt>(std::move(condition.value()), std::move(body));

            if (initializer.has_value()) {
                StmtList b;
                b.push_back(std::move(initializer.value()));
                b.push_back(std::move(body));
                body = std::make_unique<BlockStmt>(std::move(b));
            }

            return body;
        }

        PrintStmtPtr printStatement() {
            Expr value = expression();
            consume(SEMICOLON, "Expect ';' after value.");
            return std::make_unique<PrintStmt>(std::move(value));
        }

        IfStmtPtr ifStatement() {
            consume(LEFT_PAREN, "Expect '(' after 'if'.");
            auto condition = expression();
            consume(RIGHT_PAREN, "Expect ')' after if condition.");

            auto thenBranch = statement();
            std::optional<Stmt> elseBranch = {};
            if (match(ELSE)) {
                elseBranch = statement();
            }

            return std::make_unique<IfStmt>(std::move(condition), std::move(thenBranch), std::move(elseBranch));
        }

        ExpressionStmtPtr expressionStatement() {
            Expr expr = expression();
            consume(SEMICOLON, "Expect ';' after expression.");
            return std::make_unique<ExpressionStmt>(std::move(expr));
        }

        FunctionStmtPtr function(const std::string &kind) {
            Token name = consume(IDENTIFIER, "Expect " + kind + " name.");
            consume(LEFT_PAREN, "Expect '(' after " + kind + " name.");
            std::vector<Token> parameters;
            if (!check(RIGHT_PAREN)) {
                do {
                    if (parameters.size() >= 255) {
                        error(peek(), "Can't have more than 255 parameters.");
                    }

                    parameters.push_back(consume(IDENTIFIER, "Expect parameter name."));
                } while (match(COMMA));
            }
            consume(RIGHT_PAREN, "Expect ')' after parameters.");
            consume(LEFT_BRACE, "Expect '{' before " + kind + " body.");
            StmtList body = block();

            return std::make_shared<FunctionStmt>(name, parameters, std::move(body), kind == "method");
        }

        ReturnStmtPtr returnStatement() {
            Token keyword = previous();
            std::optional<Expr> value;
            if (!check(SEMICOLON)) {
                value = expression();
            }

            consume(SEMICOLON, "Expect ';' after return value.");
            return std::make_unique<ReturnStmt>(keyword, std::move(value));
        }

        StmtList block() {
            std::vector<Stmt> statements;

            while (!check(RIGHT_BRACE) && !isAtEnd()) {
                if (auto decl = declaration(); decl.has_value()) {
                    statements.push_back(std::move(decl.value()));
                }
            }

            consume(RIGHT_BRACE, "Expect '}' after block.");
            return statements;
        }


        Expr parseBinaryExpr(
                const std::initializer_list<TokenType> &types,
                Expr expr,
                const parserFn &f) {

            while (match(types)) {
                auto token = previous();
                expr = std::make_unique<BinaryExpr>(std::move(expr), token, static_cast<BinaryOp>(token.getType()), std::invoke(f, this));
            }

            return expr;
        }

        Expr expression() {
            return assignment();
        }

        Expr assignment() {
            auto expr = or_();

            if (match(EQUAL)) {
                const auto equals = previous();
                auto value = assignment();

                if (std::holds_alternative<VarExprPtr>(expr)) {
                    const auto name = std::get<VarExprPtr>(expr)->name;
                    return std::make_unique<AssignExpr>(name, std::move(value));
                }
                if (std::holds_alternative<GetExprPtr>(expr)) {
                    const auto &getExpr = std::get<GetExprPtr>(expr);
                    return std::make_unique<SetExpr>(std::move(getExpr->object), getExpr->name, std::move(value));
                }

                lox::error(equals, "Invalid assignment target.");
            }

            return expr;
        }

        Expr or_() {
            auto expr = and_();

            while (match(OR)) {
                auto right = and_();
                expr = std::make_unique<LogicalExpr>(std::move(expr), LogicalOp::OR, std::move(right));
            }

            return expr;
        }

        Expr and_() {
            auto expr = equality();

            while (match(AND)) {
                auto right = equality();
                expr = std::make_unique<LogicalExpr>(std::move(expr), LogicalOp::AND, std::move(right));
            }

            return expr;
        }

        Expr equality() {
            return parseBinaryExpr({BANG_EQUAL, EQUAL_EQUAL},
                                   comparison(),
                                   &Parser::comparison);
        }

        Expr comparison() {
            return parseBinaryExpr({GREATER, GREATER_EQUAL, LESS, LESS_EQUAL},
                                   term(),
                                   &Parser::term);
        }

        Expr term() {
            return parseBinaryExpr({MINUS, PLUS},
                                   factor(),
                                   &Parser::factor);
        }

        Expr factor() {
            return parseBinaryExpr({SLASH, STAR},
                                   unary(),
                                   &Parser::unary);
        }

        Expr unary() {
            if (match({BANG, MINUS})) {
                const Token token = previous();
                auto right = unary();
                return std::make_unique<UnaryExpr>(token, static_cast<UnaryOp>(token.getType()), std::move(right));
            }

            return call();
        }

        Expr call() {
            Expr expr = primary();

            while (true) {
                if (match(LEFT_PAREN)) {
                    expr = finishCall(expr);
                } else if (match(DOT)) {
                    Token name = consume(IDENTIFIER,
                                         "Expect property name after '.'.");
                    expr = std::make_unique<GetExpr>(std::move(expr), name);
                } else {
                    break;
                }
            }

            return expr;
        }

        Expr finishCall(Expr &callee) {
            std::vector<Expr> arguments;

            if (!check(RIGHT_PAREN)) {
                do {
                    if (arguments.size() >= 255) {
                        lox::error(peek(), "Can't have more than 255 arguments.");
                    }
                    arguments.push_back(expression());
                } while (match(COMMA));
            }

            consume(RIGHT_PAREN, "Expect ')' after arguments.");

            return std::make_unique<CallExpr>(std::move(callee), previous(), std::move(arguments));
        }

        Expr primary() {
            if (match(FALSE)) return std::make_unique<LiteralExpr>(false);
            if (match(TRUE)) return std::make_unique<LiteralExpr>(true);
            if (match(NIL)) return std::make_unique<LiteralExpr>(nullptr);

            if (match({NUMBER, STRING})) {
                return std::make_unique<LiteralExpr>(previous().getLiteral());
            }

            if (match(THIS)) return std::make_unique<ThisExpr>(previous());

            if (match(SUPER)) {
                Token keyword = previous();
                consume(DOT, "Expect '.' after 'super'.");
                Token method = consume(IDENTIFIER,
                                       "Expect superclass method name.");
                return std::make_unique<SuperExpr>(keyword, method);
            }

            if (match(IDENTIFIER)) {
                return std::make_unique<VarExpr>(previous());
            }

            if (match(LEFT_PAREN)) {
                auto expr = expression();
                consume(RIGHT_PAREN, "Expect ')' after expression.");
                return std::make_unique<GroupingExpr>(std::move(expr));
            }

            throw error(peek(), "Expect expression.");
        }

        static ParseError error(const Token &token, const std::string &message) {
            lox::error(token, message);
            return ParseError{message};
        }

        void synchronize() {
            advance();

            while (!isAtEnd()) {
                if (previous().getType() == SEMICOLON) return;

                switch (peek().getType()) {
                    case CLASS:
                    case FUN:
                    case VAR:
                    case FOR:
                    case IF:
                    case WHILE:
                    case PRINT:
                    case RETURN:
                        return;
                    default: {
                    }
                }

                advance();
            }
        }

        Token consume(const TokenType type, const std::string &message) {
            if (check(type))
                return advance();

            throw error(peek(), message);
        }

        template<typename T>
        bool match(const std::initializer_list<T> &types) {
            for (auto &type: types) {
                if (check(type)) {
                    advance();
                    return true;
                }
            }
            return false;
        }

        bool match(const TokenType type) { return match({type}); }

        bool check(const TokenType type) {
            return !isAtEnd() && (peek().getType() == type);
        }

        Token advance() {
            if (!isAtEnd())
                current++;
            return previous();
        }

        bool isAtEnd() { return peek().getType() == END; }

        Token peek() { return tokens.at(current); }

        Token previous() { return tokens.at(current - 1); }
    };
}// namespace lox