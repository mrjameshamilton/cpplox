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
        const auto objects = Builder.getModule().getNamedGlobal("objects");

        const std::vector<Type *> paramTypes(functionStmt->parameters.size() + 1, Builder.getInt64Ty());
        FunctionType *FT = FunctionType::get(IntegerType::getInt64Ty(Builder.getContext()), paramTypes, false);

        Function *M = Function::Create(
            FT,
            Function::InternalLinkage,
            functionStmt->name.getLexeme(),
            Builder.getModule()
        );

        Value *func = Builder.AllocateFunction(objects, M);

        const auto alloca = CreateEntryBlockAlloca(Builder.getFunction(), Builder.getInt64Ty(), functionStmt->name.getLexeme());
        Builder.CreateStore(func, alloca);
        variables.insert(functionStmt->name.getLexeme(), alloca);

        LoxBuilder FBuilder(Builder.getContext(), Builder.getModule(), *M);
        FunctionCompiler C(FBuilder);

        C.compile(functionStmt->parameters, functionStmt->body);
    }

    void FunctionCompiler::operator()(const ExpressionStmtPtr &expressionStmt) {
        evaluate(expressionStmt->expression);
    }

    void FunctionCompiler::operator()(const PrintStmtPtr &printStmt) {
        const auto value = evaluate(printStmt->expression);

        //const auto start = Builder.CreateBasicBlock("print.start");
        const auto BoolBlock = Builder.CreateBasicBlock("if.print.bool");
        const auto EndBoolBlock = Builder.CreateBasicBlock("if.print.bool.end");
        const auto NilBlock = Builder.CreateBasicBlock("if.print.nil");
        const auto EndNilBlock = Builder.CreateBasicBlock("if.print.nil.end");
        const auto NumBlock = Builder.CreateBasicBlock("if.print.num");
        const auto ObjBlock = Builder.CreateBasicBlock("if.print.obj");
        const auto EndBlock = Builder.CreateBasicBlock("if.print.end");

        //Builder.CreateBr(start);
        //Builder.SetInsertPoint(start);

        Builder.CreateCondBr(Builder.IsBool(value), BoolBlock, EndBoolBlock);
        Builder.SetInsertPoint(BoolBlock);
        Builder.PrintBool(value);
        Builder.CreateBr(EndBlock);
        Builder.SetInsertPoint(EndBoolBlock);

        Builder.CreateCondBr(Builder.IsNil(value), NilBlock, EndNilBlock);
        Builder.SetInsertPoint(NilBlock);
        Builder.PrintNil();
        Builder.CreateBr(EndBlock);
        Builder.SetInsertPoint(EndNilBlock);

        Builder.CreateCondBr(Builder.IsNumber(value), NumBlock, ObjBlock);
        Builder.SetInsertPoint(NumBlock);
        Builder.PrintNumber(value);
        Builder.CreateBr(EndBlock);

        Builder.SetInsertPoint(ObjBlock);
        Builder.PrintObject(value);
        Builder.CreateBr(EndBlock);

        Builder.SetInsertPoint(EndBlock);
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
        /*LoxModule->getOrInsertGlobal(varStmt->name.getLexeme(), Builder->getInt64Ty());
                const auto global = LoxModule->getNamedGlobal(varStmt->name.getLexeme());
                global->setLinkage(GlobalValue::PrivateLinkage);
                global->setAlignment(Align(8));
                global->setConstant(false);
                global->setInitializer(Builder->getInt64(NIL_VAL));

                Builder->CreateStore(evaluate(varStmt->initializer), global);*/
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