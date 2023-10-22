#include "AST.h"
#include "Error.h"
#include "Util.h"
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <utility>

namespace lox {


    struct Interpreter;
    struct LoxCallable;
    struct LoxFunction;
    struct Environment;
    using LoxCallablePtr = LoxCallable *;
    LoxCallablePtr createLoxFunction(FunctionStmtPtr &functionStmt, std::shared_ptr<Environment> &);
    using LoxObject = std::variant<std::string, double, bool, LoxCallablePtr, std::nullptr_t>;

    struct LoxCallable {
        int arity = 0;

        explicit LoxCallable(int arity) : arity{arity} {}

        virtual LoxObject operator()(Interpreter &interpreter, const std::vector<LoxObject> &arguments) = 0;
        virtual std::string to_string() = 0;

        bool operator==(const LoxObject &other) const {
            return false;
        }
    };

    struct NativeFunction : public LoxCallable {
        std::function<LoxObject(const std::vector<LoxObject> &)> function;

        explicit NativeFunction(std::function<LoxObject(const std::vector<LoxObject> &)> function) : LoxCallable(0), function{std::move(function)} {
        }

        ~NativeFunction() = default;

        LoxObject operator()(Interpreter &, const std::vector<LoxObject> &arguments) override {
            return function(arguments);
        }

        std::string to_string() override {
            return "<native fn>";
        }
    };

    class Environment {
    private:
        std::shared_ptr<Environment> enclosing;
        std::map<std::string_view, LoxObject> values = {};

    public:
        explicit Environment() = default;
        explicit Environment(std::shared_ptr<Environment> environment) : enclosing{std::move(environment)} {
        }

        void define(const std::string_view &name, const LoxObject &value) {
            values[name] = value;
        }

        LoxObject &get(const Token name) {
            if (values.contains(name.getLexeme())) {
                return values[name.getLexeme()];
            }

            if (enclosing != nullptr) return enclosing->get(name);

            throw std::invalid_argument("Undefined variable '" + std::string(name.getLexeme()) + "'.");
        }

        void assign(Token name, const LoxObject &value) {
            if (values.contains(name.getLexeme())) {
                values[name.getLexeme()] = value;
                return;
            }

            if (enclosing != nullptr) {
                enclosing->assign(name, value);
                return;
            }

            throw std::invalid_argument("Undefined variable '" + std::string(name.getLexeme()) + "'.");
        }
    };


    static std::string to_string(LoxObject &object) {
        return std::visit(overloaded{
                                  [](bool value) -> std::string { return value ? "true" : "false"; },
                                  [](double value) -> std::string { return std::to_string(value); },
                                  [](std::string value) -> std::string { return value; },
                                  [](const LoxCallablePtr &callable) -> std::string { return callable->to_string(); },
                                  [](std::nullptr_t) -> std::string { return "nil"; }},
                          object);
    }

    struct ReturnException : std::runtime_error {
        LoxObject value;
        explicit ReturnException(LoxObject value) : runtime_error("return exception: " + lox::to_string(value)), value{std::move(value)} {
        }
    };

    struct Interpreter {
        std::shared_ptr<Environment> globals = std::make_shared<Environment>();
        std::shared_ptr<Environment> environment = globals;

        Interpreter() {
            globals->define("clock", new NativeFunction{[](const std::vector<LoxObject> &) -> LoxObject {
                                auto now = std::chrono::system_clock::now().time_since_epoch();
                                return (double) std::chrono::duration_cast<std::chrono::seconds>(now).count();
                            }});
        }

        void operator()(ExpressionStmtPtr &expressionStmt) {
            evaluate(expressionStmt->expression);
        }

        void operator()(IfStmtPtr &ifStmtPtr) {
            if (isTruthy(evaluate(ifStmtPtr->condition))) {
                evaluate(ifStmtPtr->thenBranch);
            } else if (ifStmtPtr->elseBranch.has_value()) {
                evaluate(ifStmtPtr->elseBranch.value());
            }
        }

        void operator()(PrintStmtPtr &printStmt) {
            LoxObject object = evaluate(printStmt->expression);
            std::cout << to_string(object) << "\n";
        }

        void operator()(VarStmtPtr &varStmt) {
            LoxObject value = evaluate(varStmt->initializer);
            environment->define(varStmt->name.getLexeme(), value);
        }

        void operator()(FunctionStmtPtr &functionStmt) {
            auto name = functionStmt->name.getLexeme();
            auto value = createLoxFunction(functionStmt, this->environment);
            environment->define(name, value);
        }

        void operator()(ReturnStmtPtr &returnStmt) {
            LoxObject value;
            if (returnStmt->expression.has_value())
                value = evaluate(returnStmt->expression.value());

            throw ReturnException(value);
        }

        void operator()(BlockStmtPtr &blockStmt) {
            auto newEnvironment = std::make_shared<Environment>(environment);
            executeBlock(blockStmt->statements, newEnvironment);
        }

        void executeBlock(StmtList &statements, std::shared_ptr<Environment> &newEnvironment) {
            auto previous = environment;
            environment = newEnvironment;

            try {
                for (auto &statement: statements) {
                    evaluate(statement);
                }
            } catch (...) {
                // simulate try-finally used in Java version; re-throws but resets environment.
                environment = previous;
                throw;
            }

            environment = previous;
        }

        void operator()(WhileStmtPtr &whileStmt) {
            while (isTruthy(evaluate(whileStmt->condition))) {
                evaluate(whileStmt->body);
            }
        }

        LoxObject operator()(BinaryExprPtr &binaryExpr) {
            auto left = evaluate(binaryExpr->left);
            auto right = evaluate(binaryExpr->right);

            switch (binaryExpr->op) {
                case BinaryOp::PLUS: {
                    if (std::holds_alternative<double>(left) &&
                        std::holds_alternative<double>(right)) {
                        return std::get<double>(left) + std::get<double>(right);
                    }

                    if (std::holds_alternative<std::string>(left) &&
                        std::holds_alternative<std::string>(right)) {
                        return std::get<std::string>(left) + std::get<std::string>(right);
                    }

                    // TODO
                    return nullptr;
                }
                case BinaryOp::MINUS:
                    return std::get<double>(left) - std::get<double>(right);
                case BinaryOp::SLASH:
                    return std::get<double>(left) / std::get<double>(right);
                case BinaryOp::STAR:
                    return std::get<double>(left) * std::get<double>(right);
                case BinaryOp::GREATER:
                    return std::get<double>(left) > std::get<double>(right);
                case BinaryOp::GREATER_EQUAL:
                    return std::get<double>(left) >= std::get<double>(right);
                case BinaryOp::LESS:
                    return std::get<double>(left) < std::get<double>(right);
                case BinaryOp::LESS_EQUAL:
                    return std::get<double>(left) <= std::get<double>(right);
                case BinaryOp::BANG:
                    return left == right;
                case BinaryOp::BANG_EQUAL:
                    return left != right;
                case BinaryOp::EQUAL_EQUAL:
                    return left == right;
            }

            throw std::invalid_argument("Unknown binary expression.");
        }

        LoxObject operator()(CallExprPtr &callExpr) {
            auto callee = evaluate(callExpr->callee);

            std::vector<LoxObject> arguments;
            for (auto &argument: callExpr->arguments) {
                arguments.push_back(evaluate(argument));
            }

            if (std::holds_alternative<LoxCallablePtr>(callee)) {
                auto callable = std::get<LoxCallablePtr>(callee);
                if ((int) arguments.size() != callable->arity) {
                    throw std::invalid_argument("Expected " +
                                                std::to_string(callable->arity) + " arguments but got " +
                                                std::to_string(arguments.size()) + ".");
                }
                return (*callable)(*this, arguments);
            } else {
                throw std::invalid_argument("Can only call functions and classes.");
            }
        }

        LoxObject operator()(GroupingExprPtr &groupingExpr) {
            return evaluate(groupingExpr->expression);
        }

        LoxObject operator()(LiteralExprPtr &literalExpr) {
            return std::visit(
                    overloaded{
                            [](bool value) -> LoxObject { return value; },
                            [](double value) -> LoxObject { return value; },
                            [](std::string_view value) -> LoxObject { return std::string(value); },
                            [](std::nullptr_t value) -> LoxObject { return value; }},
                    literalExpr->literal);
        }

        LoxObject operator()(LogicalExprPtr &logicalExpr) {
            auto left = evaluate(logicalExpr->left);

            if (logicalExpr->op == LogicalOp::OR) {
                if (isTruthy(left)) return left;
            } else {
                if (!isTruthy(left)) return left;
            }

            return evaluate(logicalExpr->right);
        }

        LoxObject operator()(UnaryExprPtr &unaryExpr) {
            auto result = evaluate(unaryExpr->expression);
            switch (unaryExpr->op) {
                case UnaryOp::MINUS: {
                    if (std::holds_alternative<double>(result)) {
                        return -std::get<double>(result);
                    } else {
                        // TODO
                    }
                    break;
                }
                case UnaryOp::BANG:
                    return !isTruthy(result);
            }

            // Unreachable.
            return nullptr;
        }

        LoxObject operator()(VarExprPtr &varExpr) const {
            return environment->get(varExpr->name);
        }

        LoxObject operator()(AssignExprPtr &assignExpr) {
            LoxObject value = evaluate(assignExpr->value);
            environment->assign(assignExpr->name, value);
            return value;
        }

        static inline bool isTruthy(const LoxObject &object) {
            if (std::holds_alternative<std::nullptr_t>(object)) return false;
            if (std::holds_alternative<bool>(object)) return std::get<bool>(object);
            return true;
        }

        LoxObject operator()(std::nullptr_t n) {
            return n;
        }

        LoxObject evaluate(Expr &expr) {
            return std::visit(*this, expr);
        }

        void evaluate(Stmt &stmt) {
            std::visit(*this, stmt);
        }

        void evaluate(Program &program) {
            for (auto &stmt: program) {
                evaluate(stmt);
            }
        }
    };

    struct LoxFunction : public LoxCallable {
        std::unique_ptr<FunctionStmt> declaration;
        std::shared_ptr<Environment> closure;

        explicit LoxFunction(std::unique_ptr<FunctionStmt> &declaration, std::shared_ptr<Environment> &closure)
            : LoxCallable((int) declaration->parameters.size()), declaration{std::move(declaration)}, closure{closure} {
        }

        LoxObject operator()(Interpreter &interpreter, const std::vector<LoxObject> &arguments) override {
            auto environment = std::make_shared<Environment>(closure);
            for (int i = 0; i < (int) declaration->parameters.size(); i++) {
                environment->define(declaration->parameters[i].getLexeme(),
                                    arguments[i]);
            }

            auto &statements = declaration->body;
            try {
                interpreter.executeBlock(statements, environment);
            } catch (ReturnException &e) {
                return e.value;
            }
            return nullptr;
        }

        std::string to_string() override {
            return "<fn " + std::string(declaration->name.getLexeme()) + ">";
        }
    };

    inline LoxCallablePtr createLoxFunction(FunctionStmtPtr &functionStmt, std::shared_ptr<Environment> &environment) {
        return new LoxFunction(functionStmt, environment);
    }

}// namespace lox