#include "AST.h"
#include "Error.h"
#include "Token.h"
#include <functional>
#include <iostream>
#include <optional>
#include <vector>

namespace lox {
    class ParseError : public std::runtime_error {
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
                    auto decl = declaration();
                    if (decl.has_value()) {
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

        static Expr createBinaryExpr(Expr left, Token token, BinaryOp op, Expr right) {
            return std::make_unique<BinaryExpr>(std::move(left), token, op, std::move(right));
        }

        static Expr createUnaryExpr(Token token, UnaryOp op, Expr right) {
            return std::make_unique<UnaryExpr>(token, op, std::move(right));
        }

        static Expr createGroupingExpr(Expr right) {
            return std::make_unique<GroupingExpr>(std::move(right));
        }

        static Expr createLiteralExpr(Literal literal) {
            return std::make_unique<LiteralExpr>(literal);
        }

        static Expr createVarExpr(const Token name) {
            return std::make_unique<VarExpr>(name);
        }

        static Expr createAssignExpr(const Token name, Expr value) {
            return std::make_unique<AssignExpr>(name, std::move(value));
        }

        static Stmt createExpressionStatement(Expr expr) {
            return std::make_unique<ExpressionStmt>(std::move(expr));
        }

        static Stmt createIfStatement(Expr condition, Stmt thenBranch, std::optional<Stmt> elseBranch) {
            return std::make_unique<IfStmt>(std::move(condition), std::move(thenBranch), std::move(elseBranch));
        }

        static Stmt createPrintStatement(Expr expr) {
            return std::make_unique<PrintStmt>(std::move(expr));
        }
        static Stmt createVarStatement(const Token name, Expr initializer) {
            return std::make_unique<VarStmt>(name, std::move(initializer));
        }

        static Stmt createBlockStatement(StmtList statements) {
            return std::make_unique<BlockStmt>(std::move(statements));
        }

        std::optional<Stmt> declaration() {
            try {
                if (match(VAR)) return varDeclaration();
                if (match(FUN)) return function("function");

                return std::make_optional<Stmt>(statement());
            } catch (ParseError &error) {
                synchronize();
                return std::make_optional<Stmt>();
            }
        }

        Stmt varDeclaration() {
            Token name = consume(IDENTIFIER, "Expect variable name.");
            Expr initializer = match(EQUAL) ? expression() : nullptr;
            consume(SEMICOLON, "Expect ';' after variable declaration.");
            return createVarStatement(name, std::move(initializer));
        }

        Stmt whileStatement() {
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
            if (match(LEFT_BRACE)) return createBlockStatement(block());

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
                auto variant = createExpressionStatement(std::move(increment.value()));
                StmtList statements;
                statements.push_back(std::move(body));
                statements.push_back(std::move(variant));
                body = createBlockStatement(std::move(statements));
            }

            if (!condition.has_value()) {
                condition = createLiteralExpr(true);
            }

            body = std::make_unique<WhileStmt>(std::move(condition.value()), std::move(body));

            if (initializer.has_value()) {
                StmtList b;
                b.push_back(std::move(initializer.value()));
                b.push_back(std::move(body));
                body = createBlockStatement(std::move(b));
            }

            return body;
        }

        Stmt printStatement() {
            Expr value = expression();
            consume(TokenType::SEMICOLON, "Expect ';' after value.");
            return createPrintStatement(std::move(value));
        }

        Stmt ifStatement() {
            consume(LEFT_PAREN, "Expect '(' after 'if'.");
            auto condition = expression();
            consume(RIGHT_PAREN, "Expect ')' after if condition.");

            auto thenBranch = statement();
            std::optional<Stmt> elseBranch = {};
            if (match(ELSE)) {
                elseBranch = statement();
            }

            return createIfStatement(std::move(condition), std::move(thenBranch), std::move(elseBranch));
        }

        Stmt expressionStatement() {
            Expr expr = expression();
            consume(SEMICOLON, "Expect ';' after expression.");
            return createExpressionStatement(std::move(expr));
        }

        Stmt function(const std::string &kind) {
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

            return std::make_unique<FunctionStmt>(name, parameters, std::move(body));
        }

        Stmt returnStatement() {
            Token keyword = previous();
            std::optional<Expr> value = {};
            if (!check(SEMICOLON)) {
                value = expression();
            }

            consume(SEMICOLON, "Expect ';' after return value.");
            return std::make_unique<ReturnStmt>(std::move(value), keyword);
        }

        StmtList block() {
            std::vector<Stmt> statements;

            while (!check(RIGHT_BRACE) && !isAtEnd()) {
                auto decl = declaration();
                if (decl.has_value()) {
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
                expr = createBinaryExpr(std::move(expr), token, static_cast<BinaryOp>(token.getType()), std::invoke(f, this));
            }

            return expr;
        }

        Expr expression() {
            return assignment();
        }

        Expr assignment() {
            auto expr = or_();

            if (match(TokenType::EQUAL)) {
                auto equals = previous();
                auto value = assignment();

                if (std::holds_alternative<VarExprPtr>(expr)) {
                    auto name = std::get<VarExprPtr>(expr)->name;
                    return createAssignExpr(name, std::move(value));
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
            return parseBinaryExpr({TokenType::BANG_EQUAL, TokenType::EQUAL_EQUAL},
                                   comparison(),
                                   &Parser::comparison);
        }

        Expr comparison() {
            return parseBinaryExpr({TokenType::GREATER, TokenType::GREATER_EQUAL, TokenType::LESS, TokenType::LESS_EQUAL},
                                   term(),
                                   &Parser::term);
        }

        Expr term() {
            return parseBinaryExpr({TokenType::MINUS, TokenType::PLUS},
                                   factor(),
                                   &Parser::factor);
        }

        Expr factor() {
            return parseBinaryExpr({TokenType::SLASH, TokenType::STAR},
                                   unary(),
                                   &Parser::unary);
        }

        Expr unary() {
            if (match({TokenType::BANG, TokenType::MINUS})) {
                Token token = previous();
                auto right = unary();
                return createUnaryExpr(token, static_cast<UnaryOp>(token.getType()), std::move(right));
            }

            return call();
        }

        Expr call() {
            Expr expr = primary();

            while (true) {
                if (match(LEFT_PAREN)) {
                    expr = finishCall(expr);
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

            return std::make_unique<CallExpr>(std::move(callee), std::move(arguments));
        }

        Expr primary() {
            if (match(TokenType::FALSE)) return createLiteralExpr(false);
            if (match(TokenType::TRUE)) return createLiteralExpr(true);
            if (match(TokenType::NIL)) return createLiteralExpr(nullptr);

            if (match({TokenType::NUMBER, TokenType::STRING})) {
                return createLiteralExpr(previous().getLiteral());
            }

            if (match(TokenType::IDENTIFIER)) {
                return createVarExpr(previous());
            }

            if (match(TokenType::LEFT_PAREN)) {
                auto expr = expression();
                consume(TokenType::RIGHT_PAREN, "Expect ')' after expression.");
                return createGroupingExpr(std::move(expr));
            }

            throw ParseError("Invalid");
        }

        static ParseError error(const Token token, const std::string &message) {
            lox::error(token, message);
            return ParseError{message};
        }

        void synchronize() {
            advance();

            while (!isAtEnd()) {
                if (previous().getType() == TokenType::SEMICOLON) return;

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

        bool match(TokenType type) { return match({type}); }

        bool check(TokenType type) {
            return !isAtEnd() && (peek().getType() == type);
        }

        Token advance() {
            if (!isAtEnd())
                current++;
            return previous();
        }

        bool isAtEnd() { return peek().getType() == TokenType::END; }

        Token peek() { return tokens.at(current); }

        Token previous() { return tokens.at(current - 1); }
    };
}// namespace lox