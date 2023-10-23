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
    struct GroupingExpr;
    struct LiteralExpr;
    struct LogicalExpr;
    struct UnaryExpr;
    struct VarExpr;
    struct AssignExpr;

    using BinaryExprPtr = std::unique_ptr<BinaryExpr>;
    using CallExprPtr = std::unique_ptr<CallExpr>;
    using GroupingExprPtr = std::unique_ptr<GroupingExpr>;
    using LiteralExprPtr = std::unique_ptr<LiteralExpr>;
    using LogicalExprPtr = std::unique_ptr<LogicalExpr>;
    using UnaryExprPtr = std::unique_ptr<UnaryExpr>;
    using VarExprPtr = std::unique_ptr<VarExpr>;
    using AssignExprPtr = std::unique_ptr<AssignExpr>;

    using Expr = std::variant<BinaryExprPtr, CallExprPtr, GroupingExprPtr, LiteralExprPtr, LogicalExprPtr, UnaryExprPtr, VarExprPtr, AssignExprPtr, std::nullptr_t>;

    struct Uncopyable {
        explicit Uncopyable() = default;
        virtual ~Uncopyable() = default;
        Uncopyable(const Uncopyable &) = delete;
        auto operator=(const Uncopyable &) -> Uncopyable & = delete;
        Uncopyable(Uncopyable &&) = delete;
        auto operator=(Uncopyable &&) -> Uncopyable & = delete;
    };// struct Uncopyable*/

    struct BinaryExpr {
        Expr left;
        Token token;
        BinaryOp op;
        Expr right;
    };

    struct CallExpr {
        Expr callee;
        std::vector<Expr> arguments;
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

    struct VarExpr {
        Token name;
    };

    struct AssignExpr {
        Token name;
        Expr value;
    };

    struct ExpressionStmt;
    struct FunctionStmt;
    struct ReturnStmt;
    struct IfStmt;
    struct PrintStmt;
    struct VarStmt;
    struct BlockStmt;
    struct WhileStmt;

    using ExpressionStmtPtr = std::unique_ptr<ExpressionStmt>;
    using FunctionStmtPtr = std::unique_ptr<FunctionStmt>;
    using ReturnStmtPtr = std::unique_ptr<ReturnStmt>;
    using IfStmtPtr = std::unique_ptr<IfStmt>;
    using PrintStmtPtr = std::unique_ptr<PrintStmt>;
    using VarStmtPtr = std::unique_ptr<VarStmt>;
    using BlockStmtPtr = std::unique_ptr<BlockStmt>;
    using WhileStmtPtr = std::unique_ptr<WhileStmt>;

    using Stmt = std::variant<ExpressionStmtPtr, FunctionStmtPtr, ReturnStmtPtr, IfStmtPtr, PrintStmtPtr, VarStmtPtr, BlockStmtPtr, WhileStmtPtr>;
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

    using Program = std::vector<Stmt>;

    struct AstPrinter {
        std::string operator()(BinaryExprPtr &binaryExpr) {
            return "(" + visit(binaryExpr->left) + " " + std::string(binaryExpr->token.getLexeme()) + " " + visit(binaryExpr->right) + ")";
        }

        std::string operator()(GroupingExprPtr &groupingExpr) {
            return "(" + visit(groupingExpr->expression) + ")";
        }

        std::string operator()(LiteralExprPtr &literalExpr) {
            return std::visit(
                    overloaded{
                            [](bool value) -> std::string { return std::to_string(value); },
                            [](double value) -> std::string { return std::to_string(value); },
                            [](std::string_view value) -> std::string { return std::string(value); },
                            [](std::nullptr_t) -> std::string { return "nil"; }},
                    literalExpr->literal);
        }

        std::string operator()(UnaryExprPtr &unaryExpr) {
            return std::string(unaryExpr->token.getLexeme()) + visit(unaryExpr->expression);
        }

        std::string operator()(std::nullptr_t) {
            return "nil";
        }

        std::string visit(Expr &) {
            //return std::visit(*this, node);
            return "";// TODO
        }

        std::string visit(Program &program) {
            std::string result;
            for (auto &n: program) {
                /// TODO
                //result += visit(n);
            }
            return result;
        }
        /*
        std::string visit(std::vector<Expr> &program) {
            std::string result{};

            for (auto &node: program) {
                result += visit(std::move(node));
            }

            return result;
        }*/
    };
}// namespace lox

#endif//LOX_LLVM_AST_H
