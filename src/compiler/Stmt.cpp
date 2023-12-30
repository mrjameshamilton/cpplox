#include "FunctionCompiler.h"
#include "ModuleCompiler.h"
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <ranges>
#include <vector>
namespace lox {

    void FunctionCompiler::evaluate(const Stmt &stmt) {
        std::visit(*this, stmt);
    }

    void FunctionCompiler::operator()(const BlockStmtPtr &blockStmt) {
        beginScope();
        for (auto &statement: blockStmt->statements) {
            evaluate(statement);
        }
        endScope();
    }

    void FunctionCompiler::operator()(const FunctionStmtPtr &functionStmt) {
        const auto objects = Builder.getModule().getObjects();

        const std::vector<Type *> paramTypes(functionStmt->parameters.size() + 1, Builder.getInt64Ty());
        FunctionType *FT = FunctionType::get(IntegerType::getInt64Ty(Builder.getContext()), paramTypes, false);

        Function *F = Function::Create(
            FT,
            Function::InternalLinkage,
            functionStmt->name.getLexeme(),
            Builder.getModule()
        );

        Value *func = Builder.AllocateFunction(F);

        const auto alloca = CreateEntryBlockAlloca(Builder.getFunction(), Builder.getInt64Ty(), functionStmt->name.getLexeme());
        Builder.CreateStore(func, alloca);
        variables.insert(functionStmt->name.getLexeme(), alloca);

        FunctionCompiler C(Builder.getContext(), Builder.getModule(), *F);

        C.compile(functionStmt->body, functionStmt->parameters);
    }

    void FunctionCompiler::operator()(const ExpressionStmtPtr &expressionStmt) {
        evaluate(expressionStmt->expression);
    }

    void FunctionCompiler::operator()(const PrintStmtPtr &printStmt) {
        Builder.Print(evaluate(printStmt->expression));
    }

    void FunctionCompiler::operator()(const ReturnStmtPtr &returnStmt) {
        BasicBlock *ExitBasicBlock = Builder.CreateBasicBlock("return");
        BasicBlock *NewBasicBlock = Builder.CreateBasicBlock("return.unreachable");
        Builder.CreateBr(ExitBasicBlock);
        Builder.SetInsertPoint(ExitBasicBlock);
        Builder.CreateRet(returnStmt->expression.has_value() ? evaluate(returnStmt->expression.value()) : Builder.getNilVal());
        Builder.SetInsertPoint(NewBasicBlock);
    }

    void FunctionCompiler::operator()(const VarStmtPtr &varStmt) {
        const auto alloca = CreateEntryBlockAlloca(Builder.getFunction(), Builder.getInt64Ty(), varStmt->name.getLexeme());
        Builder.CreateStore(evaluate(varStmt->initializer), alloca);
        variables.insert(varStmt->name.getLexeme(), alloca);
    }

    void FunctionCompiler::operator()(const WhileStmtPtr &whileStmt) {
        const auto Cond = Builder.CreateBasicBlock("Cond");
        const auto Body = Builder.CreateBasicBlock("Loop");
        const auto Exit = Builder.CreateBasicBlock("Exit");

        Builder.CreateBr(Cond);
        Builder.SetInsertPoint(Cond);
        Builder.CreateCondBr(Builder.IsTruthy(evaluate(whileStmt->condition)), Body, Exit);
        Builder.SetInsertPoint(Body);
        evaluate(whileStmt->body);
        Builder.CreateBr(Cond);
        Builder.SetInsertPoint(Exit);
    }

    void FunctionCompiler::operator()(const IfStmtPtr &ifStmt) {
        const auto TrueBlock = Builder.CreateBasicBlock("if.true");
        const auto FalseBlock = Builder.CreateBasicBlock("else");
        const auto EndBlock = Builder.CreateBasicBlock("if.end");
        Builder.CreateCondBr(Builder.IsTruthy(evaluate(ifStmt->condition)), TrueBlock, FalseBlock);
        Builder.SetInsertPoint(TrueBlock);
        evaluate(ifStmt->thenBranch);
        Builder.CreateBr(EndBlock);
        Builder.SetInsertPoint(FalseBlock);
        if (ifStmt->elseBranch.has_value()) {
            evaluate(ifStmt->elseBranch.value());
        }
        Builder.CreateBr(EndBlock);
        Builder.SetInsertPoint(EndBlock);
    }

    void FunctionCompiler::operator()(const ClassStmtPtr &classStmt) const {
    }
}// namespace lox