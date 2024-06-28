#include "Interpreter.h"
#include "LoxClass.h"
#include "LoxFunction.h"
#include "LoxInstance.h"
#include "NativeFunction.h"

#include <chrono>

namespace lox {

    static LoxNumber checkNumberOperand(const Token &op, const LoxObject &operand) {
        if (std::holds_alternative<LoxNumber>(operand)) return std::get<LoxNumber>(operand);
        throw runtime_error(op, "Operand must be a number.");
    }

    static void checkNumberOperands(const Token &op, const LoxObject &left, const LoxObject &right) {
        if (std::holds_alternative<LoxNumber>(left) && std::holds_alternative<LoxNumber>(right)) return;

        throw runtime_error(op, "Operands must be numbers.");
    }

    Interpreter::Interpreter() {
        globals->define("clock", std::make_shared<NativeFunction>([](const std::vector<LoxObject> &) -> LoxObject {
                            const auto now = std::chrono::system_clock::now().time_since_epoch();
                            return LoxNumber(std::chrono::duration_cast<std::chrono::seconds>(now).count());
                        }));
        globals->define(
            "exit", std::make_shared<NativeFunction>(
                        [](const std::vector<LoxObject> &arguments) -> LoxObject {
                            const auto token = Token(IDENTIFIER, "", nullptr, 0);
                            exit(static_cast<int>(checkNumberOperand(token, arguments.at(0))));
                        },
                        1
                    )
        );
        globals->define("read", std::make_shared<NativeFunction>([](const std::vector<LoxObject> &) -> LoxObject {
                            const int c = getchar();
                            if (c == -1) { return LoxNil(); }
                            return LoxNumber(static_cast<uint8_t>(c));
                        }));
        globals->define(
            "utf",
            std::make_shared<NativeFunction>(
                [](const std::vector<LoxObject> &args) -> LoxObject {
                    int byte_count = 0;
                    for (int i = 0; i < 4; i++) {
                        if (i > 0 && std::holds_alternative<LoxNil>(args[i])) continue;

                        if (!std::holds_alternative<LoxNumber>(args[i]) ||
                            (std::get<LoxNumber>(args[i]) < 0 || std::get<LoxNumber>(args[i]) > 255)) {
                            const auto token = Token(IDENTIFIER, "", nullptr, 0);
                            throw lox::runtime_error(token, "utf parameter should be a number between 0 and 255.");
                        }

                        byte_count++;
                    }

                    char bytes[byte_count];
                    std::transform(
                        args.begin(), args.end() - 4 + byte_count, bytes,
                        [](const LoxObject &value) -> char {
                            return std::holds_alternative<LoxNil>(value)
                                       ? 0
                                       : static_cast<char>(std::get<LoxNumber>(value));
                        }
                    );

                    return LoxString(bytes, byte_count);
                },
                4
            )
        );
        globals->define(
            "printerr", std::make_shared<NativeFunction>(
                            [](const std::vector<LoxObject> &arguments) -> LoxObject {
                                std::cerr << lox::to_string(arguments[0]) << std::endl;
                                return LoxNil();
                            },
                            1
                        )
        );
    }

    [[nodiscard]] LoxObject &Interpreter::lookUpVariable(const Token &name, const Assignable &expr) const {
        if (expr.distance == -1) { return globals->get(name); }
        return environment->getAt(expr.distance, name.getLexeme());
    }

    StmtResult Interpreter::operator()(const ClassStmtPtr &classStmt) {
        std::optional<std::shared_ptr<LoxClass>> super_class;
        if (classStmt->super_class.has_value()) {
            if (const auto &s = (*this)(classStmt->super_class.value());
                std::holds_alternative<LoxCallablePtr>(s) &&
                dynamic_cast<LoxClass *>(std::get<LoxCallablePtr>(s).get())) {
                super_class = std::reinterpret_pointer_cast<LoxClass>(std::get<LoxCallablePtr>(s));
            } else {
                throw runtime_error(classStmt->super_class.value()->name, "Superclass must be a class.");
            }
        }

        environment->define(classStmt->name.getLexeme());

        if (super_class.has_value()) {
            environment = std::make_shared<Environment>(environment);
            environment->define("super", super_class.value());
        }

        std::unordered_map<std::string_view, LoxFunctionPtr> methods;

        for (auto &method: classStmt->methods) {
            methods[method->name.getLexeme()] =
                std::make_shared<LoxFunction>(method, environment, method->type == LoxFunctionType::INITIALIZER);
        }

        if (super_class.has_value()) { environment = environment->get_enclosing(); }

        environment->assign(
            classStmt->name, std::make_shared<LoxClass>(classStmt->name.getLexeme(), super_class, std::move(methods))
        );

        return Nothing();
    }

    LoxObject Interpreter::operator()(const CallExprPtr &callExpr) {
        if (function_depth > 512) { throw lox::runtime_error(callExpr->keyword, "Stack overflow."); }

        const auto &callee = evaluate(callExpr->callee);

        std::vector<LoxObject> arguments;
        for (auto &argument: callExpr->arguments) { arguments.push_back(evaluate(argument)); }

        if (std::holds_alternative<LoxCallablePtr>(callee)) {
            const auto &callable = std::get<LoxCallablePtr>(callee);
            if (static_cast<int>(arguments.size()) != callable->arity()) {
                throw runtime_error(
                    callExpr->keyword,
                    std::format("Expected {} arguments but got {}.", callable->arity(), arguments.size())
                );
            }
            function_depth++;
            auto lox_object = (*callable)(*this, arguments);
            function_depth--;
            return lox_object;
        }

        throw runtime_error(callExpr->keyword, "Can only call functions and classes.");
    }

    StmtResult Interpreter::operator()(const FunctionStmtPtr &functionStmt) {
        const auto name = functionStmt->name.getLexeme();
        auto function = std::make_shared<LoxFunction>(functionStmt, environment);
        environment->define(name, std::move(function));
        return Nothing();
    }

    StmtResult Interpreter::executeBlock(const StmtList &statements, const EnvironmentPtr &newEnvironment) {
        const auto previous = environment;
        environment = newEnvironment;

        for (const auto &statement: statements) {
            if (auto result = evaluate(statement); std::holds_alternative<Return>(result)) {
                environment = previous;
                return result;
            }
        }

        environment = previous;

        return Nothing();
    }

    LoxObject Interpreter::operator()(const GetExprPtr &getExpr) {
        if (const auto &object = evaluate(getExpr->object); std::holds_alternative<LoxInstancePtr>(object)) {
            return std::get<LoxInstancePtr>(object)->get(getExpr->name);
        }

        throw runtime_error(getExpr->name, "Only instances have properties.");
    }

    LoxObject Interpreter::operator()(const SetExprPtr &setExpr) {
        const auto &object = evaluate(setExpr->object);

        if (!std::holds_alternative<LoxInstancePtr>(object)) {
            throw runtime_error(setExpr->name, "Only instances have fields.");
        }

        auto value = evaluate(setExpr->value);
        std::get<LoxInstancePtr>(object)->set(setExpr->name, value);
        return value;
    }

    LoxObject Interpreter::operator()(const SuperExprPtr &superExpr) const {
        const auto &callable = std::get<LoxCallablePtr>(environment->getAt(superExpr->distance, "super"));
        const auto &super_class = std::reinterpret_pointer_cast<LoxClass>(callable);
        const auto &instance = std::get<LoxInstancePtr>(environment->getAt(superExpr->distance - 1, "this"));
        const auto &method = super_class->findMethod(superExpr->method.getLexeme());
        if (method == nullptr) {
            throw runtime_error(
                superExpr->method, std::format("Undefined property '{}'.", std::string(superExpr->method.getLexeme()))
            );
        }
        return method->bind(instance);
    }

    StmtResult Interpreter::operator()(const IfStmtPtr &ifStmtPtr) {
        if (isTruthy(evaluate(ifStmtPtr->condition))) { return std::move(evaluate(ifStmtPtr->thenBranch)); }

        if (ifStmtPtr->elseBranch.has_value()) { return evaluate(ifStmtPtr->elseBranch.value()); }

        return Nothing();
    }

    StmtResult Interpreter::operator()(const ExpressionStmtPtr &expressionStmt) {
        evaluate(expressionStmt->expression);
        return Nothing();
    }

    StmtResult Interpreter::operator()(const PrintStmtPtr &printStmt) {
        const auto object = evaluate(printStmt->expression);
        std::cout << lox::to_string(object) << std::endl;
        return Nothing();
    }

    StmtResult Interpreter::operator()(const VarStmtPtr &varStmt) {
        const auto value = evaluate(varStmt->initializer);
        environment->define(varStmt->name.getLexeme(), value);
        return Nothing();
    }

    StmtResult Interpreter::operator()(const WhileStmtPtr &whileStmt) {
        while (isTruthy(evaluate(whileStmt->condition))) {
            if (auto result = evaluate(whileStmt->body); std::holds_alternative<Return>(result)) { return result; }
        }
        return Nothing();
    }

    StmtResult Interpreter::operator()(const ReturnStmtPtr &returnStmt) {
        LoxObject value = LoxNil();

        if (returnStmt->expression.has_value()) { value = evaluate(returnStmt->expression.value()); }

        return Return(value);
    }

    StmtResult Interpreter::operator()(const BlockStmtPtr &blockStmt) {
        return executeBlock(blockStmt->statements, std::make_shared<Environment>(environment));
    }

    LoxObject Interpreter::operator()(const BinaryExprPtr &binaryExpr) {
        const auto &left = evaluate(binaryExpr->left);
        const auto &right = evaluate(binaryExpr->right);

        switch (binaryExpr->op) {
            case BinaryOp::PLUS: {
                if (std::holds_alternative<LoxNumber>(left) && std::holds_alternative<LoxNumber>(right)) {
                    return std::get<LoxNumber>(left) + std::get<LoxNumber>(right);
                }

                if (std::holds_alternative<LoxString>(left) && std::holds_alternative<LoxString>(right)) {
                    return std::get<LoxString>(left) + std::get<LoxString>(right);
                }

                throw runtime_error(binaryExpr->token, "Operands must be two numbers or two strings.");
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
            case BinaryOp::BANG_EQUAL:
                return left != right;
            case BinaryOp::EQUAL_EQUAL:
                return left == right;
        }

        std::unreachable();
    }
    LoxObject Interpreter::operator()(const ThisExprPtr &thisExpr) const {
        return lookUpVariable(thisExpr->name, *thisExpr);
    }

    LoxObject Interpreter::operator()(const GroupingExprPtr &groupingExpr) {
        return evaluate(groupingExpr->expression);
    }

    LoxObject Interpreter::operator()(const LiteralExprPtr &literalExpr) const {
        return std::visit(
            overloaded{
                [](const bool value) -> LoxObject { return value; },
                [](const double value) -> LoxObject { return value; },
                [](const std::string_view value) -> LoxObject { return std::string(value); },
                [](const std::nullptr_t) -> LoxObject { return LoxNil(); },
            },
            literalExpr->literal
        );
    }

    LoxObject Interpreter::operator()(const LogicalExprPtr &logicalExpr) {
        auto left = evaluate(logicalExpr->left);

        if (logicalExpr->op == LogicalOp::OR) {
            if (isTruthy(left)) return left;
        } else {
            if (!isTruthy(left)) return left;
        }

        return evaluate(logicalExpr->right);
    }

    LoxObject Interpreter::operator()(const UnaryExprPtr &unaryExpr) {
        const auto &result = evaluate(unaryExpr->expression);
        switch (unaryExpr->op) {
            case UnaryOp::MINUS: {
                return -checkNumberOperand(unaryExpr->token, result);
            }
            case UnaryOp::BANG:
                return !isTruthy(result);
        }

        std::unreachable();
    }

    LoxObject Interpreter::operator()(const VarExprPtr &varExpr) const {
        return lookUpVariable(varExpr->name, *varExpr);
    }

    LoxObject Interpreter::operator()(const AssignExprPtr &assignExpr) {
        const auto &value = evaluate(assignExpr->value);
        if (assignExpr->distance == -1) {
            globals->assign(assignExpr->name, value);
        } else {
            environment->assignAt(assignExpr->distance, assignExpr->name, value);
        }
        return value;
    }

}// namespace lox
