#pragma once

#include "AST.h"
#include "Error.h"
#include "Util.h"

#include <chrono>
#include <format>
#include <functional>
#include <iostream>
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
    using LoxClassPtr = std::shared_ptr<LoxClass>;
    using LoxString = std::string;
    using LoxNumber = double;
    using LoxBoolean = bool;
    using LoxNil = std::nullptr_t;
    using LoxObject = std::variant<LoxNil, LoxString, LoxNumber, LoxBoolean, LoxCallablePtr, LoxInstancePtr>;
    static std::string to_string(const LoxObject &);
    static LoxInstancePtr createLoxInstance(const LoxClassPtr &klass);
    inline LoxFunctionPtr bind(LoxFunctionPtr &, LoxInstancePtr &);
    inline LoxObject execute(const LoxFunctionPtr &, Interpreter &, const std::vector<LoxObject> &);
    inline int functionArity(const LoxFunctionPtr &);

    struct ReturnException final : std::runtime_error {
        LoxObject value;
        explicit ReturnException(LoxObject value) : runtime_error("return exception: " + lox::to_string(value)), value{std::move(value)} {
        }
    };

    struct LoxCallable {
        int arity = 0;

        explicit LoxCallable(const int arity) : arity{arity} {}
        virtual ~LoxCallable() = default;

        virtual LoxObject operator()(Interpreter &interpreter, const std::vector<LoxObject> &arguments) = 0;
        virtual std::string to_string() = 0;
    };

    struct NativeFunction final : LoxCallable {
        std::function<LoxObject(const std::vector<LoxObject> &)> function;

        explicit NativeFunction(std::function<LoxObject(const std::vector<LoxObject> &)> function) : LoxCallable(0), function{std::move(function)} {
        }

        ~NativeFunction() override = default;

        LoxObject operator()(Interpreter &, const std::vector<LoxObject> &arguments) override {
            return function(arguments);
        }

        std::string to_string() override {
            return "<native fn>";
        }
    };

    struct LoxClass final : LoxCallable, std::enable_shared_from_this<LoxClass> {
        std::string_view name;
        std::optional<std::shared_ptr<LoxClass>> superClass;
        std::unordered_map<std::string_view, LoxFunctionPtr> methods;
        LoxFunctionPtr initializer;

        explicit LoxClass(const std::string_view &name, const std::optional<std::shared_ptr<LoxClass>> &superClass, const std::unordered_map<std::string_view, LoxFunctionPtr> &methods)
            : LoxCallable(0), name{name}, superClass{superClass}, methods{methods} {
            this->initializer = findMethod("init");
            this->arity = functionArity(initializer);
        }

        ~LoxClass() override = default;

        LoxObject operator()(Interpreter &interpreter, const std::vector<LoxObject> &arguments) override {
            auto instance = createLoxInstance(shared_from_this());
            if (initializer != nullptr) {
                const auto function = bind(initializer, instance);
                execute(function, interpreter, arguments);
            }
            return instance;
        }

        LoxFunctionPtr findMethod(const std::string_view &method_name) {
            if (methods.contains(method_name)) {
                return methods[method_name];
            }

            if (superClass.has_value()) {
                return superClass.value()->findMethod(method_name);
            }

            return nullptr;
        }

        std::string to_string() override {
            return std::string(name);
        }
    };

    struct LoxInstance : std::enable_shared_from_this<LoxInstance> {
        LoxClassPtr klass;
        std::unordered_map<std::string_view, LoxObject> fields;

        explicit LoxInstance(LoxClassPtr klass) : klass{std::move(klass)} {
        }

        LoxObject get(const Token &name) {
            if (fields.contains(name.getLexeme())) {
                return fields[name.getLexeme()];
            }

            if (auto method = klass->findMethod(name.getLexeme()); method != nullptr) {
                auto instance = shared_from_this();
                return std::reinterpret_pointer_cast<LoxCallable>(bind(method, instance));
            }

            throw runtime_error(name, "Undefined property '" + std::string(name.getLexeme()) + "'.");
        }

        void set(const Token &name, const LoxObject &value) {
            fields[name.getLexeme()] = value;
        }
    };

    static LoxInstancePtr createLoxInstance(const LoxClassPtr &klass) {
        return std::make_shared<LoxInstance>(klass);
    }

    class Environment : public std::enable_shared_from_this<Environment> {
        std::unordered_map<std::string_view, LoxObject> values;
        std::shared_ptr<Environment> enclosing;

    public:
        explicit Environment() = default;
        explicit Environment(std::shared_ptr<Environment> environment) : enclosing{std::move(environment)} {
        }

        std::shared_ptr<Environment> getEnclosing() const { return enclosing; }

        void define(const std::string_view &name, const LoxObject &value = LoxNil{}) {
            values[name] = value;
        }

        LoxObject &getAt(const unsigned long distance, const std::string_view &name) {
            return ancestor(distance)->values[name];
        }

        std::shared_ptr<Environment> ancestor(const unsigned long distance) {
            auto environment = shared_from_this();
            for (unsigned long i = 0; i < distance; i++) {
                environment = environment->enclosing;
            }
            return environment;
        }

        LoxObject &get(const Token &name) {
            if (values.contains(name.getLexeme())) {
                return values[name.getLexeme()];
            }

            if (enclosing != nullptr) return enclosing->get(name);

            throw runtime_error(name, "Undefined variable '" + std::string(name.getLexeme()) + "'.");
        }

        void assign(const Token &name, const LoxObject &value) {
            if (values.contains(name.getLexeme())) {
                values[name.getLexeme()] = value;
                return;
            }

            if (enclosing != nullptr) {
                enclosing->assign(name, value);
                return;
            }

            throw runtime_error(name, "Undefined variable '" + std::string(name.getLexeme()) + "'.");
        }

        void assignAt(const unsigned long distance, const Token &name, const LoxObject &value) {
            ancestor(distance)->values[name.getLexeme()] = value;
        }
    };

    inline void executeBlock(Interpreter &interpreter, StmtList &statements, const std::shared_ptr<Environment> &newEnvironment);

    struct LoxFunction final : LoxCallable {
        std::shared_ptr<FunctionStmt> declaration;
        std::shared_ptr<Environment> closure;
        bool isInitializer;

        explicit LoxFunction(std::shared_ptr<FunctionStmt> declaration, const std::shared_ptr<Environment> &closure, const bool isInitializer = false)
            : LoxCallable(static_cast<int>(declaration->parameters.size())), declaration{std::move(declaration)}, closure{closure}, isInitializer{isInitializer} {
        }

        ~LoxFunction() override = default;

        LoxObject operator()(Interpreter &interpreter, const std::vector<LoxObject> &arguments) override {
            const auto environment = std::make_shared<Environment>(closure);
            for (int i = 0; i < static_cast<int>(declaration->parameters.size()); i++) {
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

        std::string to_string() override {
            return std::format("<fn {}>", std::string(declaration->name.getLexeme()));
        }
    };

    inline LoxFunctionPtr bind(LoxFunctionPtr &function, LoxInstancePtr &instance) {
        return function->bind(instance);
    }

    inline LoxObject execute(const LoxFunctionPtr &function, Interpreter &interpreter, const std::vector<LoxObject> &arguments) {
        return (*function)(interpreter, arguments);
    }

    inline int functionArity(const LoxFunctionPtr &function) {
        return function == nullptr ? 0 : function->arity;
    }

    struct Interpreter {
        std::shared_ptr<Environment> globals = std::make_shared<Environment>();
        std::shared_ptr<Environment> environment = globals;
        int function_depth = 0;

        Interpreter() {
            globals->define("clock", std::make_shared<NativeFunction>([](const std::vector<LoxObject> &) -> LoxObject {
                                const auto now = std::chrono::system_clock::now().time_since_epoch();
                                return static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(now).count());
                            }));
        }

        void operator()(const ExpressionStmtPtr &expressionStmt) {
            evaluate(expressionStmt->expression);
        }

        void operator()(const IfStmtPtr &ifStmtPtr) {
            if (isTruthy(evaluate(ifStmtPtr->condition))) {
                evaluate(ifStmtPtr->thenBranch);
            } else if (ifStmtPtr->elseBranch.has_value()) {
                evaluate(ifStmtPtr->elseBranch.value());
            }
        }

        void operator()(const PrintStmtPtr &printStmt) {
            const auto object = evaluate(printStmt->expression);
            std::cout << lox::to_string(object) << "\n";
        }

        void operator()(const VarStmtPtr &varStmt) {
            const auto value = evaluate(varStmt->initializer);
            environment->define(varStmt->name.getLexeme(), value);
        }

        void operator()(const FunctionStmtPtr &functionStmt) {
            const auto name = functionStmt->name.getLexeme();
            auto function = std::make_shared<LoxFunction>(functionStmt, environment);
            environment->define(name, std::move(function));
        }

        void operator()(const ReturnStmtPtr &returnStmt) {
            LoxObject value;
            if (returnStmt->expression.has_value())
                value = evaluate(returnStmt->expression.value());

            throw ReturnException(value);
        }

        void operator()(const BlockStmtPtr &blockStmt) {
            executeBlock(blockStmt->statements, std::make_shared<Environment>(environment));
        }

        void executeBlock(const StmtList &statements, const std::shared_ptr<Environment> &newEnvironment) {
            const auto previous = environment;
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

        void operator()(const WhileStmtPtr &whileStmt) {
            while (isTruthy(evaluate(whileStmt->condition))) {
                evaluate(whileStmt->body);
            }
        }

        void operator()(const ClassStmtPtr &classStmt) {
            std::optional<std::shared_ptr<LoxClass>> superKlass;
            if (classStmt->super_class.has_value()) {
                const auto superclass = this->operator()(classStmt->super_class.value());
                if (!std::holds_alternative<LoxCallablePtr>(superclass) ||
                    !dynamic_cast<LoxClass *>(std::get<LoxCallablePtr>(superclass).get())) {
                    throw runtime_error(classStmt->super_class.value()->name, "Superclass must be a class.");
                }
                superKlass = std::reinterpret_pointer_cast<LoxClass>(std::get<LoxCallablePtr>(superclass));
            }

            environment->define(classStmt->name.getLexeme());

            if (superKlass.has_value()) {
                environment = std::make_shared<Environment>(environment);
                environment->define("super", superKlass.value());
            }

            std::unordered_map<std::string_view, LoxFunctionPtr> methods;

            for (auto &method: classStmt->methods) {
                auto name = method->name.getLexeme();
                methods[name] = std::make_shared<LoxFunction>(std::move(method), environment, name == "init");
            }

            auto klass = std::make_shared<LoxClass>(classStmt->name.getLexeme(), superKlass, std::move(methods));
            if (superKlass.has_value()) {
                environment = environment->getEnclosing();
            }

            environment->assign(classStmt->name, klass);
        }

        LoxObject operator()(const BinaryExprPtr &binaryExpr) {
            const auto left = evaluate(binaryExpr->left);
            const auto right = evaluate(binaryExpr->right);

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

                    throw runtime_error(binaryExpr->token,
                                        "Operands must be two numbers or two strings.");
                }
                case BinaryOp::MINUS:
                    checkNumberOperands(binaryExpr->token, left, right);
                    return std::get<LoxNumber>(left) - std::get<LoxNumber>(right);
                case BinaryOp::SLASH:
                    checkNumberOperands(binaryExpr->token, left, right);
                    return std::get<LoxNumber>(left) / std::get<LoxNumber>(right);
                case BinaryOp::STAR:
                    checkNumberOperands(binaryExpr->token, left, right);
                    return std::get<LoxNumber>(left) * std::get<LoxNumber>(right);
                case BinaryOp::GREATER:
                    checkNumberOperands(binaryExpr->token, left, right);
                    return std::get<LoxNumber>(left) > std::get<LoxNumber>(right);
                case BinaryOp::GREATER_EQUAL:
                    checkNumberOperands(binaryExpr->token, left, right);
                    return std::get<LoxNumber>(left) >= std::get<LoxNumber>(right);
                case BinaryOp::LESS:
                    checkNumberOperands(binaryExpr->token, left, right);
                    return std::get<LoxNumber>(left) < std::get<LoxNumber>(right);
                case BinaryOp::LESS_EQUAL:
                    checkNumberOperands(binaryExpr->token, left, right);
                    return std::get<LoxNumber>(left) <= std::get<LoxNumber>(right);
                case BinaryOp::BANG:
                    return left == right;
                case BinaryOp::BANG_EQUAL:
                    return left != right;
                case BinaryOp::EQUAL_EQUAL:
                    return left == right;
            }

            std::unreachable();
        }

        LoxObject operator()(const CallExprPtr &callExpr) {
            if (function_depth > 512) {
                throw lox::runtime_error(callExpr->keyword, "Stack overflow.");
            }

            const auto callee = evaluate(callExpr->callee);

            std::vector<LoxObject> arguments;
            for (auto &argument: callExpr->arguments) {
                arguments.push_back(evaluate(argument));
            }

            if (std::holds_alternative<LoxCallablePtr>(callee)) {
                const auto callable = std::get<LoxCallablePtr>(callee);
                if (static_cast<int>(arguments.size()) != callable->arity) {
                    throw runtime_error(callExpr->keyword, "Expected " +
                                                                   std::to_string(callable->arity) + " arguments but got " +
                                                                   std::to_string(arguments.size()) + ".");
                }
                function_depth++;
                auto lox_object = (*callable)(*this, arguments);
                function_depth--;
                return lox_object;
            }

            throw runtime_error(callExpr->keyword, "Can only call functions and classes.");
        }

        LoxObject operator()(const GetExprPtr &getExpr) {
            if (const auto object = evaluate(getExpr->object); std::holds_alternative<LoxInstancePtr>(object)) {
                return std::get<LoxInstancePtr>(object)->get(getExpr->name);
            }

            throw runtime_error(getExpr->name, "Only instances have properties.");
        }

        LoxObject operator()(const SetExprPtr &setExpr) {
            const auto object = evaluate(setExpr->object);

            if (!std::holds_alternative<LoxInstancePtr>(object)) {
                throw runtime_error(setExpr->name, "Only instances have fields.");
            }

            auto value = evaluate(setExpr->value);
            std::get<LoxInstancePtr>(object)->set(setExpr->name, value);
            return value;
        }

        LoxObject operator()(const ThisExprPtr &thisExpr) const {
            return lookUpVariable(thisExpr->name, *thisExpr);
        }

        LoxObject operator()(const SuperExprPtr &superExpr) const {
            const auto callable = std::get<LoxCallablePtr>(environment->getAt(superExpr->distance, "super"));
            const auto superClass = std::reinterpret_pointer_cast<LoxClass>(callable);
            auto instance = std::get<LoxInstancePtr>(environment->getAt(superExpr->distance - 1, "this"));
            const auto method = superClass->findMethod(superExpr->method.getLexeme());
            if (method == nullptr) {
                throw runtime_error(superExpr->method, "Undefined property '" + std::string(superExpr->method.getLexeme()) + "'.");
            }
            return method->bind(instance);
        }

        LoxObject operator()(const GroupingExprPtr &groupingExpr) {
            return evaluate(groupingExpr->expression);
        }

        LoxObject operator()(const LiteralExprPtr &literalExpr) const {
            return std::visit(
                    overloaded{
                            [](const bool value) -> LoxObject { return value; },
                            [](const double value) -> LoxObject { return value; },
                            [](const std::string_view value) -> LoxObject { return std::string(value); },
                            [](const std::nullptr_t value) -> LoxObject { return nullptr; }},
                    literalExpr->literal);
        }

        LoxObject operator()(const LogicalExprPtr &logicalExpr) {
            auto left = evaluate(logicalExpr->left);

            if (logicalExpr->op == LogicalOp::OR) {
                if (isTruthy(left)) return left;
            } else {
                if (!isTruthy(left)) return left;
            }

            return evaluate(logicalExpr->right);
        }

        LoxObject operator()(const UnaryExprPtr &unaryExpr) {
            const auto result = evaluate(unaryExpr->expression);
            switch (unaryExpr->op) {
                case UnaryOp::MINUS: {
                    checkNumberOperand(unaryExpr->token, result);
                    return -std::get<LoxNumber>(result);
                }
                case UnaryOp::BANG:
                    return !isTruthy(result);
            }

            std::unreachable();
        }

        static void checkNumberOperand(const Token &op, const LoxObject &operand) {
            if (std::holds_alternative<LoxNumber>(operand)) return;
            throw runtime_error(op, "Operand must be a number.");
        }

        static void checkNumberOperands(const Token &op, const LoxObject &left, const LoxObject &right) {
            if (std::holds_alternative<LoxNumber>(left) && std::holds_alternative<LoxNumber>(right)) return;

            throw runtime_error(op, "Operands must be numbers.");
        }

        LoxObject operator()(const VarExprPtr &varExpr) const {
            return lookUpVariable(varExpr->name, *varExpr);
        }

        [[nodiscard]] LoxObject& lookUpVariable(const Token &name, const Assignable &expr) const {
            if (expr.distance == -1) {
                return globals->get(name);
            }
            return environment->getAt(expr.distance, name.getLexeme());
        }

        LoxObject operator()(const AssignExprPtr &assignExpr) {
            LoxObject value = evaluate(assignExpr->value);
            if (assignExpr->distance == -1) {
                globals->assign(assignExpr->name, value);
            } else {
                environment->assignAt(assignExpr->distance, assignExpr->name, value);
            }
            return value;
        }

        static bool isTruthy(const LoxObject &object) {
            if (std::holds_alternative<LoxNil>(object)) return false;
            if (std::holds_alternative<LoxBoolean>(object)) return std::get<LoxBoolean>(object);
            return true;
        }

        LoxObject evaluate(const Expr &expr) {
            return std::visit(*this, expr);
        }

        void evaluate(const Stmt &stmt) {
            std::visit(*this, stmt);
        }

        void evaluate(const Program &program) {
            try {
                for (auto &stmt: program) {
                    evaluate(stmt);
                }
            } catch (const runtime_error &e) {
                runtimeError(e);
            }
        }
    };

    inline void executeBlock(Interpreter &interpreter, StmtList &statements, const std::shared_ptr<Environment> &newEnvironment) {
        interpreter.executeBlock(statements, newEnvironment);
    }

    static std::string to_string(const LoxObject &object) {
        return std::visit(overloaded{
                                  [](const LoxBoolean value) -> std::string { return value ? "true" : "false"; },
                                  [](LoxNumber value) -> std::string { return std::format("{:g}", value); },
                                  [](LoxString value) -> std::string { return value; },
                                  [](const LoxCallablePtr &callable) -> std::string { return callable->to_string(); },
                                  [](const LoxInstancePtr &instance) -> std::string { return std::string(instance->klass->name) + " instance"; },
                                  [](LoxNil) -> std::string { return "nil"; }},
                          object);
    }


}// namespace lox