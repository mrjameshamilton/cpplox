#pragma once

#include "AST.h"
#include "Error.h"
#include "Util.h"
#include <chrono>
#include <format>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <utility>

namespace lox {


    struct Interpreter;
    struct LoxCallable;
    struct LoxFunction;
    struct LoxClass;
    struct LoxInstance;
    class Environment;
    using LoxCallablePtr = std::shared_ptr<LoxCallable>;
    using LoxInstancePtr = std::shared_ptr<LoxInstance>;
    LoxCallablePtr createLoxFunction(FunctionStmtPtr &functionStmt, std::shared_ptr<Environment> &);
    using LoxString = std::string;
    using LoxNumber = double;
    using LoxBoolean = bool;
    using LoxNil = std::nullptr_t;
    using LoxObject = std::variant<LoxNil, LoxString, LoxNumber, LoxBoolean, LoxCallablePtr, LoxInstancePtr>;

    struct LoxCallable {
        int arity = 0;

        explicit LoxCallable(int arity) : arity{arity} {}

        virtual LoxObject operator()(Interpreter &interpreter, const std::vector<LoxObject> &arguments) = 0;
        virtual std::string to_string() = 0;

        bool operator==(const LoxObject &other) const {
            // TODO
            return false;
        }
    };

    struct NativeFunction : public LoxCallable {
        std::function<LoxObject(const std::vector<LoxObject> &)> function;

        explicit NativeFunction(std::function<LoxObject(const std::vector<LoxObject> &)> function) : LoxCallable(0), function{std::move(function)} {
        }

        virtual ~NativeFunction() = default;

        LoxObject operator()(Interpreter &, const std::vector<LoxObject> &arguments) override {
            return function(arguments);
        }

        std::string to_string() override {
            return "<native fn>";
        }
    };

    struct LoxInstance {
        LoxClass *klass;
        std::unordered_map<std::string_view, LoxObject> fields;

        explicit LoxInstance(LoxClass *klass) : klass{klass} {
        }

        LoxObject get(const Token name) {
            if (fields.contains(name.getLexeme())) {
                return fields[name.getLexeme()];
            }

            throw lox::runtime_error(name, "Undefined property '" + std::string(name.getLexeme()) + "'.");
        }
    };

    struct LoxClass : public LoxCallable {
        std::string_view name;

        explicit LoxClass(const std::string_view &name, int arity = 0) : LoxCallable(arity), name{name} {}

        virtual ~LoxClass() = default;

        LoxObject operator()(Interpreter &, const std::vector<LoxObject> &arguments) override {
            auto instance = std::make_shared<LoxInstance>(this);
            return instance;
        }

        std::string to_string() override {
            return std::string(name);
        }
    };

    static std::string to_string(LoxObject &object) {
        return std::visit(overloaded{
                                  [](LoxBoolean value) -> std::string { return value ? "true" : "false"; },
                                  [](LoxNumber value) -> std::string { return std::format("{:g}", value); },
                                  [](LoxString value) -> std::string { return value; },
                                  [](const LoxCallablePtr &callable) -> std::string { return callable->to_string(); },
                                  [](const LoxInstancePtr &instance) -> std::string { return std::string(instance->klass->name) + " instance"; },
                                  [](LoxNil) -> std::string { return "nil"; }},
                          object);
    }

    class Environment {
    private:
        std::shared_ptr<Environment> enclosing;
        std::unordered_map<std::string_view, LoxObject> values;

    public:
        explicit Environment() = default;
        explicit Environment(std::shared_ptr<Environment> environment) : enclosing{std::move(environment)} {
        }

        void define(const std::string_view &name, const LoxObject &value = LoxNil{}) {
            values[name] = value;
        }

        LoxObject getAt(const unsigned long distance, const std::string_view &name) {
            return ancestor(distance)->values[name];
        }

        Environment *ancestor(const unsigned long distance) {
            auto environment = this;
            for (unsigned long i = 0; i < distance; i++) {
                environment = environment->enclosing.get();
            }
            return environment;
        }

        LoxObject get(const Token name) {
            if (values.contains(name.getLexeme())) {
                return values[name.getLexeme()];
            }

            if (enclosing != nullptr) return enclosing->get(name);

            throw std::invalid_argument("Undefined variable '" + std::string(name.getLexeme()) + "'.");
        }

        void assign(const Token name, const LoxObject &value) {
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

        void assignAt(const unsigned long distance, const Token name, const LoxObject &value) {
            ancestor(distance)->values[name.getLexeme()] = value;
        }
    };

    struct ReturnException : std::runtime_error {
        LoxObject value;
        explicit ReturnException(LoxObject value) : runtime_error("return exception: " + lox::to_string(value)), value{std::move(value)} {
        }
    };

    struct Interpreter {
        std::shared_ptr<Environment> globals = std::make_shared<Environment>();
        std::shared_ptr<Environment> environment = globals;

        Interpreter() {
            globals->define("clock", std::make_shared<NativeFunction>([](const std::vector<LoxObject> &) -> LoxObject {
                                auto now = std::chrono::system_clock::now().time_since_epoch();
                                return (double) std::chrono::duration_cast<std::chrono::seconds>(now).count();
                            }));
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
            auto value = evaluate(varStmt->initializer);
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

        void operator()(ClassStmtPtr &classStmt) {
            environment->define(classStmt->name.getLexeme());
            auto klass = std::make_shared<LoxClass>(classStmt->name.getLexeme());
            environment->assign(classStmt->name, klass);
        }

        LoxObject operator()(BinaryExprPtr &binaryExpr) {
            auto left = evaluate(binaryExpr->left);
            auto right = evaluate(binaryExpr->right);

            switch (binaryExpr->op) {
                case BinaryOp::PLUS: {
                    if (std::holds_alternative<LoxNumber>(left) &&
                        std::holds_alternative<LoxNumber>(right)) {
                        return std::get<LoxNumber>(left) + std::get<LoxNumber>(right);
                    }

                    if (std::holds_alternative<LoxString>(left) &&
                        std::holds_alternative<LoxString>(right)) {
                        return std::get<LoxString>(left) + std::get<LoxString>(right);
                    }

                    // TODO
                    return nullptr;
                }
                case BinaryOp::MINUS:
                    return std::get<LoxNumber>(left) - std::get<LoxNumber>(right);
                case BinaryOp::SLASH:
                    return std::get<LoxNumber>(left) / std::get<LoxNumber>(right);
                case BinaryOp::STAR:
                    return std::get<LoxNumber>(left) * std::get<LoxNumber>(right);
                case BinaryOp::GREATER:
                    return std::get<LoxNumber>(left) > std::get<LoxNumber>(right);
                case BinaryOp::GREATER_EQUAL:
                    return std::get<LoxNumber>(left) >= std::get<LoxNumber>(right);
                case BinaryOp::LESS:
                    return std::get<LoxNumber>(left) < std::get<LoxNumber>(right);
                case BinaryOp::LESS_EQUAL:
                    return std::get<LoxNumber>(left) <= std::get<LoxNumber>(right);
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

        LoxObject operator()(GetExprPtr &getExpr) {
            auto object = evaluate(getExpr->object);
            if (std::holds_alternative<LoxInstancePtr>(object)) {
                return std::get<LoxInstancePtr>(object)->get(getExpr->name);
            }

            throw lox::runtime_error(getExpr->name, "Only instances have properties.");
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
                    if (std::holds_alternative<LoxNumber>(result)) {
                        return -std::get<LoxNumber>(result);
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

        LoxObject operator()(VarExprPtr &varExpr) {
            return lookUpVariable(varExpr->name, varExpr.get());
        }

        LoxObject lookUpVariable(const Token &name, Assignable *expr) {
            if (expr->distance == -1) {
                return globals->get(name);
            }
            return environment->getAt(expr->distance, name.getLexeme());
        }

        LoxObject operator()(AssignExprPtr &assignExpr) {
            LoxObject value = evaluate(assignExpr->value);
            if (assignExpr->distance == -1) {
                environment->assign(assignExpr.get()->name, value);
            } else {
                environment->assignAt(assignExpr->distance, assignExpr.get()->name, value);
            }
            return value;
        }

        static inline bool isTruthy(const LoxObject &object) {
            if (std::holds_alternative<LoxNil>(object)) return false;
            if (std::holds_alternative<LoxBoolean>(object)) return std::get<LoxBoolean>(object);
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

        virtual ~LoxFunction() = default;

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
            return std::format("<fn {}>", std::string(declaration->name.getLexeme()));
        }
    };

    inline LoxCallablePtr createLoxFunction(FunctionStmtPtr &functionStmt, std::shared_ptr<Environment> &environment) {
        return std::make_shared<LoxFunction>(functionStmt, environment);
    }

}// namespace lox