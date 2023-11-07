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

using namespace std::literals;

namespace lox {

    struct Interpreter;
    struct LoxCallable;
    struct LoxFunction;
    struct LoxClass;
    struct LoxInstance;
    using LoxCallablePtr = std::shared_ptr<LoxCallable>;
    using LoxFunctionPtr = std::shared_ptr<LoxFunction>;
    using LoxInstancePtr = std::shared_ptr<LoxInstance>;
    using LoxString = std::string;
    using LoxNumber = double;
    using LoxBoolean = bool;
    using LoxNil = std::nullptr_t;
    using LoxObject = std::variant<LoxNil, LoxString, LoxNumber, LoxBoolean, LoxCallablePtr, LoxInstancePtr>;
    static inline std::string to_string(LoxObject &);
    static inline LoxFunctionPtr bind(LoxFunctionPtr &, LoxInstancePtr &);
    static inline LoxObject execute(LoxFunctionPtr &, Interpreter &, const std::vector<LoxObject> &);
    static inline int functionArity(LoxFunctionPtr &);

    struct ReturnException : std::runtime_error {
        LoxObject value;
        explicit ReturnException(LoxObject value) : runtime_error("return exception: " + lox::to_string(value)), value{std::move(value)} {
        }
    };

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

    struct LoxClass : public LoxCallable {
        std::string_view name;
        std::unordered_map<std::string_view, LoxFunctionPtr> methods;
        LoxFunctionPtr initializer;

        explicit LoxClass(const std::string_view &name, const std::unordered_map<std::string_view, LoxFunctionPtr> &methods)
            : LoxCallable(0), name{name}, methods{methods} {
            this->initializer = findMethod("init");
            this->arity = functionArity(initializer);
        }

        virtual ~LoxClass() = default;

        LoxObject operator()(Interpreter &interpreter, const std::vector<LoxObject> &arguments) override {
            auto instance = std::make_shared<LoxInstance>(this);
            if (initializer != nullptr) {
                auto function = bind(initializer, instance);
                execute(function, interpreter, arguments);
            }
            return instance;
        }

        LoxFunctionPtr findMethod(const std::string_view &method_name) {
            if (methods.contains(method_name)) {
                return methods[method_name];
            }

            return nullptr;
        }

        std::string to_string() override {
            return std::string(name);
        }
    };

    struct LoxInstance : public std::enable_shared_from_this<LoxInstance> {
        LoxClass *klass;
        std::unordered_map<std::string_view, LoxObject> fields;

        explicit LoxInstance(LoxClass *klass) : klass{klass} {
        }

        LoxObject get(const Token name) {
            if (fields.contains(name.getLexeme())) {
                return fields[name.getLexeme()];
            }

            auto method = klass->findMethod(name.getLexeme());

            if (method != nullptr) {
                auto instance = shared_from_this();
                return std::reinterpret_pointer_cast<LoxCallable>(bind(method, instance));
            }

            throw lox::runtime_error(name, "Undefined property '" + std::string(name.getLexeme()) + "'.");
        }

        void set(const Token name, const LoxObject &value) {
            fields[name.getLexeme()] = value;
        }
    };

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

    inline void executeBlock(Interpreter &interpreter, StmtList &statements, std::shared_ptr<Environment> &newEnvironment);

    struct LoxFunction : public LoxCallable {
        std::shared_ptr<FunctionStmt> declaration;
        std::shared_ptr<Environment> closure;
        bool isInitializer;

        explicit LoxFunction(std::shared_ptr<FunctionStmt> declaration, std::shared_ptr<Environment> &closure, const bool isInitializer = false)
            : LoxCallable((int) declaration->parameters.size()), declaration{std::move(declaration)}, closure{closure}, isInitializer{isInitializer} {
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
                executeBlock(interpreter, statements, environment);
            } catch (ReturnException &e) {
                if (isInitializer) return closure->getAt(0, "this");

                return e.value;
            }

            if (isInitializer) {
                return closure->getAt(0, "this");
            }

            return nullptr;
        }

        LoxFunctionPtr bind(LoxInstancePtr &instance) {
            auto environment = std::make_shared<Environment>(closure);
            environment->define("this", instance);
            return std::make_shared<LoxFunction>(declaration, environment, isInitializer);
        }

        inline std::string to_string() override {
            return std::format("<fn {}>", std::string(declaration->name.getLexeme()));
        }
    };

    static inline LoxFunctionPtr bind(LoxFunctionPtr &function, LoxInstancePtr &instance) {
        return function->bind(instance);
    }

    static inline LoxObject execute(LoxFunctionPtr &function, Interpreter &interpreter, const std::vector<LoxObject> &arguments) {
        return (*function)(interpreter, arguments);
    }

    static inline int functionArity(LoxFunctionPtr &function) {
        return function == nullptr ? 0 : function->arity;
    }

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
            std::cout << lox::to_string(object) << "\n";
        }

        void operator()(VarStmtPtr &varStmt) {
            auto value = evaluate(varStmt->initializer);
            environment->define(varStmt->name.getLexeme(), value);
        }

        void operator()(FunctionStmtPtr &functionStmt) {
            auto name = functionStmt->name.getLexeme();
            std::shared_ptr<FunctionStmt> decl = std::move(functionStmt);
            auto function = std::make_shared<LoxFunction>(decl, environment);
            environment->define(name, std::move(function));
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
            std::unordered_map<std::string_view, LoxFunctionPtr> methods;

            for (auto &method: classStmt->methods) {
                auto name = method->name.getLexeme();
                std::shared_ptr<FunctionStmt> m = std::move(method);
                methods[name] = std::make_shared<LoxFunction>(m, environment, name == "init");
            }

            auto klass = std::make_shared<LoxClass>(classStmt->name.getLexeme(), std::move(methods));
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
                auto &ptr = std::get<LoxInstancePtr>(object);
                return ptr->get(getExpr->name);
            }

            throw lox::runtime_error(getExpr->name, "Only instances have properties.");
        }

        LoxObject operator()(SetExprPtr &setExpr) {
            auto object = evaluate(setExpr->object);

            if (!std::holds_alternative<LoxInstancePtr>(object)) {
                throw lox::runtime_error(setExpr->name, "Only instances have fields.");
            }

            auto value = evaluate(setExpr->value);
            std::get<LoxInstancePtr>(object)->set(setExpr->name, value);
            return value;
        }

        LoxObject operator()(ThisExprPtr &thisExpr) {
            return lookUpVariable(thisExpr->name, *thisExpr);
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
            return lookUpVariable(varExpr->name, *varExpr);
        }

        LoxObject lookUpVariable(const Token &name, Assignable &expr) {
            if (expr.distance == -1) {
                return globals->get(name);
            }
            return environment->getAt(expr.distance, name.getLexeme());
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

    inline void executeBlock(Interpreter &interpreter, StmtList &statements, std::shared_ptr<Environment> &newEnvironment) {
        interpreter.executeBlock(statements, newEnvironment);
    }

    static inline std::string to_string(LoxObject &object) {
        return std::visit(overloaded{
                                  [](LoxBoolean value) -> std::string { return value ? "true" : "false"; },
                                  [](LoxNumber value) -> std::string { return std::format("{:g}", value); },
                                  [](LoxString value) -> std::string { return value; },
                                  [](const LoxCallablePtr &callable) -> std::string { return callable->to_string(); },
                                  [](const LoxInstancePtr &instance) -> std::string { return std::string(instance->klass->name) + " instance"; },
                                  [](LoxNil) -> std::string { return "nil"; }},
                          object);
    }


}// namespace lox