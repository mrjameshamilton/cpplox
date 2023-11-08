#include "AST.h"
#include "Interpreter.cpp"

using namespace std::literals;

namespace lox {
    struct Resolver {

        enum class FunctionType {
            NONE,
            FUNCTION,
            INITIALIZER,
            METHOD
        };

        enum class ClassType {
            NONE,
            CLASS,
            SUBCLASS
        };

        using Scope = std::unordered_map<std::string_view, bool>;
        std::vector<Scope> scopes;
        FunctionType currentFunction = FunctionType::NONE;
        ClassType currentClass = ClassType::NONE;

        void beginScope() {
            scopes.emplace_back();
        }

        void endScope() {
            scopes.pop_back();
        }

        void declare(const Token &name) {
            if (scopes.empty()) return;
            auto scope = scopes.back();
            if (scope.contains(name.getLexeme())) {
                lox::error(name, "Already a variable with this name in this scope.");
            }
            scope[name.getLexeme()] = false;
        }

        void define(const Token &name) {
            if (scopes.empty()) return;
            scopes.back()[name.getLexeme()] = true;
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
            } else if (returnStmt->expression.has_value() &&
                       currentFunction == FunctionType::INITIALIZER) {
                lox::error(returnStmt->keyword,
                           "Can't return a value from an initializer.");
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
            ClassType enclosingClass = currentClass;
            currentClass = ClassType::CLASS;
            declare(classStmt->name);
            define(classStmt->name);

            if (classStmt->superClass.has_value() &&
                classStmt->name.getLexeme() == classStmt->superClass.value()->name.getLexeme()) {
                lox::error(classStmt->superClass.value()->name,
                           "A class can't inherit from itself.");
            }

            if (classStmt->superClass.has_value()) {
                currentClass = ClassType::SUBCLASS;
                this->operator()(classStmt->superClass.value());
            }

            if (classStmt->superClass.has_value()) {
                beginScope();
                scopes.back()["super"] = true;
            }

            beginScope();
            scopes.back()["this"] = true;

            for (auto &method: classStmt->methods) {
                auto functionType = method->name.getLexeme() == "init" ? FunctionType::INITIALIZER : FunctionType::METHOD;
                resolveFunction(method, functionType);
            }

            endScope();

            if (classStmt->superClass.has_value()) {
                endScope();
            }

            currentClass = enclosingClass;
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

        void operator()(SetExprPtr &setExpr) {
            resolve(setExpr->object);
            resolve(setExpr->value);
        }

        void operator()(ThisExprPtr &thisExpr) {
            if (currentClass == ClassType::NONE) {
                lox::error(thisExpr->name,
                           "Can't use 'this' outside of a class.");
                return;
            }

            resolveLocal(*thisExpr, thisExpr->name);
        }

        void operator()(SuperExprPtr &superExpr) {
            if (currentClass == ClassType::NONE) {
                lox::error(superExpr->name,
                           "Can't use 'super' outside of a class.");
            } else if (currentClass != ClassType::SUBCLASS) {
                lox::error(superExpr->name,
                           "Can't use 'super' in a class with no superclass.");
            }
            resolveLocal(*superExpr, superExpr->name);
        }

        void operator()(VarExprPtr &varExpr) {
            if (!scopes.empty() &&
                scopes.back().contains(varExpr->name.getLexeme()) &&
                !scopes.back()[varExpr->name.getLexeme()]) {
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