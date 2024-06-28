#ifndef INTERPRETER1_H
#define INTERPRETER1_H
#include "../frontend/AST.h"
#include "Environment.h"

namespace lox {
    struct Return {
        LoxObject &value;
        ~Return() = default;
    };
    struct Nothing {};
    using StmtResult = std::variant<LoxObject, Return, Nothing>;

    class Interpreter {
        EnvironmentPtr globals = std::make_shared<Environment>();
        EnvironmentPtr environment = globals;
        int function_depth = 0;

    public:
        Interpreter();
        StmtResult operator()(const ExpressionStmtPtr &expressionStmt);
        StmtResult operator()(const IfStmtPtr &ifStmtPtr);
        StmtResult operator()(const PrintStmtPtr &printStmt);
        StmtResult operator()(const VarStmtPtr &varStmt);
        StmtResult operator()(const FunctionStmtPtr &functionStmt);
        StmtResult operator()(const ReturnStmtPtr &returnStmt);
        StmtResult operator()(const BlockStmtPtr &blockStmt);
        StmtResult operator()(const WhileStmtPtr &whileStmt);
        StmtResult operator()(const ClassStmtPtr &classStmt);
        LoxObject operator()(const BinaryExprPtr &binaryExpr);
        LoxObject operator()(const CallExprPtr &callExpr);
        LoxObject operator()(const GetExprPtr &getExpr);
        LoxObject operator()(const SetExprPtr &setExpr);
        LoxObject operator()(const ThisExprPtr &thisExpr) const;
        LoxObject operator()(const SuperExprPtr &superExpr) const;
        LoxObject operator()(const GroupingExprPtr &groupingExpr);
        LoxObject operator()(const LiteralExprPtr &literalExpr) const;
        LoxObject operator()(const LogicalExprPtr &logicalExpr);
        LoxObject operator()(const UnaryExprPtr &unaryExpr);
        LoxObject operator()(const VarExprPtr &varExpr) const;
        LoxObject operator()(const AssignExprPtr &assignExpr);


        StmtResult executeBlock(const StmtList &statements, const EnvironmentPtr &newEnvironment);

        [[nodiscard]] LoxObject &lookUpVariable(const Token &name, const Assignable &expr) const;

        LoxObject evaluate(const Expr &expr) { return std::visit(*this, expr); }
        StmtResult evaluate(const Stmt &stmt) { return std::visit(*this, stmt); }
        void evaluate(const Program &program) {
            try {
                for (const auto &stmt: program) { evaluate(stmt); }
            } catch (const runtime_error &e) { runtimeError(e); }
        }
    };
}// namespace lox
#endif//INTERPRETER1_H
