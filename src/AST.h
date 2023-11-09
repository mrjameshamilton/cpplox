#ifndef LOX_LLVM_AST_H
#define LOX_LLVM_AST_H

#include "Token.h"
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

    using Expr = std::variant<BinaryExprPtr, CallExprPtr, GetExprPtr, SetExprPtr, ThisExprPtr, SuperExprPtr, GroupingExprPtr, LiteralExprPtr, LogicalExprPtr, UnaryExprPtr, VarExprPtr, AssignExprPtr>;

    struct Uncopyable {
        explicit Uncopyable() = default;
        virtual ~Uncopyable() = default;
        Uncopyable(const Uncopyable &) = delete;
        auto operator=(const Uncopyable &) -> Uncopyable & = delete;
        Uncopyable(Uncopyable &&) = delete;
        auto operator=(Uncopyable &&) -> Uncopyable & = delete;
    };// struct Uncopyable*/

    struct Assignable {
        Token name;
        mutable signed long distance = -1;
        explicit Assignable(const Token &name) : name{name} {
        }
    };

    struct BinaryExpr {
        Expr left;
        Token token;
        BinaryOp op;
        Expr right;
    };

    struct CallExpr {
        Expr callee;
        Token keyword;
        std::vector<Expr> arguments;
    };

    struct GetExpr {
        Expr object;
        Token name;
    };

    struct SetExpr {
        Expr object;
        Token name;
        Expr value;
    };

    struct ThisExpr : Assignable {
        explicit ThisExpr(const Token &name) : Assignable(name) {}
    };

    struct SuperExpr : Assignable {
        Token method;
        explicit SuperExpr(const Token &name, const Token &method) : Assignable(name), method{method} {}
    };

    struct UnaryExpr {
        Token token;
        UnaryOp op;
        Expr expression;
    };

    struct GroupingExpr {
        Expr expression;
    };

    struct LiteralExpr {
        Literal literal;
    };

    struct LogicalExpr {
        Expr left;
        LogicalOp op;
        Expr right;
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
    using FunctionStmtPtr = std::unique_ptr<FunctionStmt>;
    using ReturnStmtPtr = std::unique_ptr<ReturnStmt>;
    using IfStmtPtr = std::unique_ptr<IfStmt>;
    using PrintStmtPtr = std::unique_ptr<PrintStmt>;
    using VarStmtPtr = std::unique_ptr<VarStmt>;
    using BlockStmtPtr = std::unique_ptr<BlockStmt>;
    using WhileStmtPtr = std::unique_ptr<WhileStmt>;
    using ClassStmtPtr = std::unique_ptr<ClassStmt>;

    using Stmt = std::variant<ExpressionStmtPtr, FunctionStmtPtr, ReturnStmtPtr, IfStmtPtr, PrintStmtPtr, VarStmtPtr, BlockStmtPtr, WhileStmtPtr, ClassStmtPtr>;
    using StmtList = std::vector<Stmt>;

    struct ExpressionStmt {
        Expr expression;
    };

    struct IfStmt {
        Expr condition;
        Stmt thenBranch;
        std::optional<Stmt> elseBranch;
    };

    struct FunctionStmt {
        Token name;
        std::vector<Token> parameters;
        StmtList body;
    };

    struct ReturnStmt {
        std::optional<Expr> expression;
        Token keyword;
    };

    struct PrintStmt {
        Expr expression;
    };

    struct VarStmt {
        Token name;
        Expr initializer;
    };

    struct BlockStmt {
        StmtList statements;
    };

    struct WhileStmt {
        Expr condition;
        Stmt body;
    };

    struct ClassStmt {
        Token name;
        std::optional<VarExprPtr> superClass;
        std::vector<FunctionStmtPtr> methods;
    };

    using Program = std::vector<Stmt>;

}// namespace lox

#endif//LOX_LLVM_AST_H
