#ifndef LOX_LLVM_AST_H
#define LOX_LLVM_AST_H

#include "Token.h"
#include "Util.h"

#include <memory>
#include <optional>
#include <sstream>
#include <utility>
#include <variant>
#include <vector>
namespace lox {

    enum class UnaryOp {
        BANG = TokenType::BANG,
        MINUS = TokenType::MINUS
    };

    enum class BinaryOp {
        PLUS = TokenType::PLUS,
        MINUS = TokenType::MINUS,
        SLASH = TokenType::SLASH,
        STAR = TokenType::STAR,
        GREATER = TokenType::GREATER,
        GREATER_EQUAL = TokenType::GREATER_EQUAL,
        LESS = TokenType::LESS,
        LESS_EQUAL = TokenType::LESS_EQUAL,
        BANG = TokenType::BANG,
        BANG_EQUAL = TokenType::BANG_EQUAL,
        EQUAL_EQUAL = TokenType::EQUAL_EQUAL
    };

    enum class LogicalOp {
        OR = TokenType::OR,
        AND = TokenType::AND
    };

    struct BinaryExpr;
    struct CallExpr;
    struct GetExpr;
    struct SetExpr;
    struct ThisExpr;
    struct SuperExpr;
    struct GroupingExpr;
    struct LiteralExpr;
    struct LogicalExpr;
    struct UnaryExpr;
    struct VarExpr;
    struct AssignExpr;

    using BinaryExprPtr = std::unique_ptr<BinaryExpr>;
    using CallExprPtr = std::unique_ptr<CallExpr>;
    using GetExprPtr = std::unique_ptr<GetExpr>;
    using SetExprPtr = std::unique_ptr<SetExpr>;
    using ThisExprPtr = std::unique_ptr<ThisExpr>;
    using SuperExprPtr = std::unique_ptr<SuperExpr>;
    using GroupingExprPtr = std::unique_ptr<GroupingExpr>;
    using LiteralExprPtr = std::unique_ptr<LiteralExpr>;
    using LogicalExprPtr = std::unique_ptr<LogicalExpr>;
    using UnaryExprPtr = std::unique_ptr<UnaryExpr>;
    using VarExprPtr = std::unique_ptr<VarExpr>;
    using AssignExprPtr = std::unique_ptr<AssignExpr>;

    using Expr = std::variant<
            BinaryExprPtr,
            CallExprPtr,
            GetExprPtr,
            SetExprPtr,
            ThisExprPtr,
            SuperExprPtr,
            GroupingExprPtr,
            LiteralExprPtr,
            LogicalExprPtr,
            UnaryExprPtr,
            VarExprPtr,
            AssignExprPtr>;

    struct Assignable : private Uncopyable {
        Token name;
        mutable signed long distance = -1;
        explicit Assignable(const Token &name) : name{name} {
        }
    };

    struct BinaryExpr final : private Uncopyable {
        Expr left;
        Token token;
        BinaryOp op;
        Expr right;
        explicit BinaryExpr(Expr left, const Token &token, const BinaryOp op, Expr right)
            : left(std::move(left)), token{token}, op{op}, right{std::move(right)} {}
    };

    struct CallExpr : private Uncopyable {
        Expr callee;
        Token keyword;
        std::vector<Expr> arguments;
        explicit CallExpr(Expr callee, const Token &keyword, std::vector<Expr> arguments)
            : callee{std::move(callee)}, keyword{keyword}, arguments{std::move(arguments)} {}
    };

    struct GetExpr : private Uncopyable {
        Expr object;
        Token name;
        explicit GetExpr(Expr object, const Token &name)
            : object{std::move(object)}, name{name} {}
    };

    struct SetExpr : Uncopyable {
        Expr object;
        Token name;
        Expr value;
        explicit SetExpr(Expr object, const Token &name, Expr value)
            : object{std::move(object)}, name{name}, value{std::move(value)} {}
    };

    struct ThisExpr : Assignable {
        explicit ThisExpr(const Token &name) : Assignable(name) {}
    };

    struct SuperExpr : Assignable {
        Token method;
        explicit SuperExpr(const Token &name, const Token &method)
            : Assignable(name), method{method} {}
    };

    struct UnaryExpr : Uncopyable {
        Token token;
        UnaryOp op;
        Expr expression;
        explicit UnaryExpr(const Token &token, const UnaryOp op, Expr expression)
            : token{token}, op{op}, expression{std::move(expression)} {}
    };

    struct GroupingExpr : Uncopyable {
        Expr expression;
        explicit GroupingExpr(Expr expression) : expression{std::move(expression)} {}
    };

    struct LiteralExpr : Uncopyable {
        Literal literal;
        explicit LiteralExpr(const Literal &literal) : literal{literal} {}
    };

    struct LogicalExpr : Uncopyable {
        Expr left;
        LogicalOp op;
        Expr right;
        explicit LogicalExpr(Expr left, const LogicalOp op, Expr right)
            : left{std::move(left)}, op{op}, right{std::move(right)} {}
    };

    struct VarExpr : Assignable {
        explicit VarExpr(const Token &name) : Assignable(name) {}
    };

    struct AssignExpr : Assignable {
        Expr value;
        AssignExpr(const Token &name, Expr value) : Assignable(name), value{std::move(value)} {}
    };

    struct ExpressionStmt;
    struct FunctionStmt;
    struct ReturnStmt;
    struct IfStmt;
    struct PrintStmt;
    struct VarStmt;
    struct BlockStmt;
    struct WhileStmt;
    struct ClassStmt;

    using ExpressionStmtPtr = std::unique_ptr<ExpressionStmt>;
    using FunctionStmtPtr = std::shared_ptr<FunctionStmt>;
    using ReturnStmtPtr = std::unique_ptr<ReturnStmt>;
    using IfStmtPtr = std::unique_ptr<IfStmt>;
    using PrintStmtPtr = std::unique_ptr<PrintStmt>;
    using VarStmtPtr = std::unique_ptr<VarStmt>;
    using BlockStmtPtr = std::unique_ptr<BlockStmt>;
    using WhileStmtPtr = std::unique_ptr<WhileStmt>;
    using ClassStmtPtr = std::unique_ptr<ClassStmt>;

    using Stmt = std::variant<
            ExpressionStmtPtr,
            FunctionStmtPtr,
            ReturnStmtPtr,
            IfStmtPtr,
            PrintStmtPtr,
            VarStmtPtr,
            BlockStmtPtr,
            WhileStmtPtr,
            ClassStmtPtr>;

    using StmtList = std::vector<Stmt>;

    struct ExpressionStmt : Uncopyable {
        Expr expression;
        explicit ExpressionStmt(Expr expression) : expression{std::move(expression)} {}
    };

    struct IfStmt : Uncopyable {
        Expr condition;
        Stmt thenBranch;
        std::optional<Stmt> elseBranch;
        explicit IfStmt(Expr condition, Stmt thenBranch, std::optional<Stmt> elseBranch)
            : condition{std::move(condition)}, thenBranch{std::move(thenBranch)}, elseBranch{std::move(elseBranch)} {}
    };

    struct FunctionStmt : Uncopyable {
        Token name;
        std::vector<Token> parameters;
        StmtList body;
        explicit FunctionStmt(const Token &name, std::vector<Token> parameters, StmtList body)
            : name{name}, parameters{std::move(parameters)}, body{std::move(body)} {}
    };

    struct ReturnStmt : Uncopyable {
        Token keyword;
        std::optional<Expr> expression;
        explicit ReturnStmt(const Token &keyword, std::optional<Expr> expression)
            : keyword{keyword}, expression{std::move(expression)} {}
    };

    struct PrintStmt : Uncopyable {
        Expr expression;
        explicit PrintStmt(Expr expression) : expression{std::move(expression)} {}
    };

    struct VarStmt : Uncopyable {
        Token name;
        Expr initializer;
        explicit VarStmt(const Token &name, Expr initializer) : name{name}, initializer{std::move(initializer)} {}
    };

    struct BlockStmt : Uncopyable {
        StmtList statements;
        explicit BlockStmt(StmtList statements)
            : statements{std::move(statements)} {}
    };

    struct WhileStmt : Uncopyable {
        Expr condition;
        Stmt body;
        WhileStmt(Expr condition, Stmt body)
            : condition{std::move(condition)},
              body{std::move(body)} {}
    };

    struct ClassStmt {
        Token name;
        std::optional<VarExprPtr> super_class;
        std::vector<FunctionStmtPtr> methods;
        ClassStmt(const Token &name, std::optional<VarExprPtr> super_class, std::vector<FunctionStmtPtr> methods)
            : name{name},
              super_class{std::move(super_class)},
              methods{std::move(methods)} {}
    };

    using Program = std::vector<Stmt>;

}// namespace lox

#endif//LOX_LLVM_AST_H
