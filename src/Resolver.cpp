#include "AST.h"
#include "Interpreter.cpp"

using namespace std::literals;

namespace lox {
    struct Resolver {

        enum class FunctionType {
            NONE,
            FUNCTION
        };

        using Scope = std::unordered_map<std::string_view, bool>;
        std::vector<Scope> scopes;
        FunctionType currentFunction = FunctionType::NONE;

        void beginScope() {
            scopes.emplace_back();
        }

        void endScope() {
            scopes.pop_back();
        }

        void declare(const Token &name) {
            if (scopes.empty()) return;
            auto scope = scopes.front();
            if (scope.contains(name.getLexeme())) {
                lox::error(name, "Already a variable with this name in this scope.");
            }
            scope[name.getLexeme()] = false;
        }

        void define(const Token &name) {
            if (scopes.empty()) return;
            scopes.front()[name.getLexeme()] = true;
        }

        void resolveLocal(Assignable &expr, const Token &name) {
            if (scopes.empty()) return;

            for (signed i = (int) scopes.size() - 1; i >= 0; i--) {
                if (scopes.at(i).contains(name.getLexeme())) {
                    expr.distance = (signed) (scopes.size() - 1 - i);
                    return;
                }
            }
        }

        void operator()(BlockStmtPtr &blockStmt) {
            beginScope();
            resolve(blockStmt->statements);
            endScope();
        }


        void resolveFunction(FunctionStmtPtr &function, FunctionType functionType) {
            FunctionType enclosingFunction = currentFunction;
            currentFunction = functionType;

            beginScope();
            for (auto param: function->parameters) {
                declare(param);
                define(param);
            }
            resolve(function->body);
            endScope();
            currentFunction = enclosingFunction;
        }

        void operator()(FunctionStmtPtr &functionStmt) {
            declare(functionStmt->name);
            define(functionStmt->name);
            resolveFunction(functionStmt, FunctionType::FUNCTION);
        }

        void operator()(ExpressionStmtPtr &expressionStmt) {
            resolve(expressionStmt->expression);
        }

        void operator()(PrintStmtPtr &printStmt) {
            resolve(printStmt->expression);
        }

        void operator()(ReturnStmtPtr &returnStmt) {
            if (currentFunction == FunctionType::NONE) {
                lox::error(returnStmt->keyword, "Can't return from top-level code.");
            }

            resolve(returnStmt->expression);
        }

        void operator()(VarStmtPtr &varStmt) {
            declare(varStmt->name);
            resolve(varStmt->initializer);
            define(varStmt->name);
        }

        void operator()(WhileStmtPtr &whileStmt) {
            resolve(whileStmt->condition);
            resolve(whileStmt->body);
        }

        void operator()(IfStmtPtr &ifStmt) {
            resolve(ifStmt->condition);
            resolve(ifStmt->thenBranch);
            if (ifStmt->elseBranch.has_value()) resolve(ifStmt->elseBranch.value());
        }

        void operator()(ClassStmtPtr &classStmt) {
            declare(classStmt->name);
            define(classStmt->name);
        }

        void operator()(AssignExprPtr &assignExpr) {
            resolve(assignExpr->value);
            resolveLocal(*assignExpr, assignExpr->name);
        }

        void operator()(BinaryExprPtr &binaryExpr) {
            resolve(binaryExpr->left);
            resolve(binaryExpr->right);
        }

        void operator()(CallExprPtr &callExpr) {
            resolve(callExpr->callee);
            for (auto &arg: callExpr->arguments) {
                resolve(arg);
            }
        }

        void operator()(GetExprPtr &getExpr) {
            resolve(getExpr->object);
        }

        void operator()(VarExprPtr &varExpr) {
            if (!scopes.empty() &&
                scopes.front().contains(varExpr->name.getLexeme()) &&
                !scopes.front()[varExpr->name.getLexeme()]) {
                lox::error(varExpr->name,
                           "Can't read local variable in its own initializer.");
                return;
            }

            resolveLocal(*varExpr, varExpr->name);
        }

        void operator()(GroupingExprPtr &groupingExpr) {
            resolve(groupingExpr->expression);
        }

        void operator()(LiteralExprPtr &) {}

        void operator()(LogicalExprPtr &logicalExpr) {
            resolve(logicalExpr->left);
            resolve(logicalExpr->right);
        }

        void operator()(UnaryExprPtr &unaryExpr) {
            resolve(unaryExpr->expression);
        }

        void operator()(std::nullptr_t) {
        }

        inline void resolve(std::optional<Expr> &opt) {
            if (opt.has_value()) resolve(opt.value());
        }

        void resolve(Expr &expr) {
            std::visit(*this, expr);
        }

        void resolve(Stmt &stmt) {
            std::visit(*this, stmt);
        }

        void resolve(Program &program) {
            for (auto &item: program) {
                resolve(item);
            }
        }
    };
}// namespace lox