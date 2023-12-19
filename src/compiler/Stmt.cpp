#include "Compiler.h"
namespace lox {

    void Compiler::evaluate(const Stmt &stmt) {
        std::visit(*this, stmt);
    }

    void Compiler::operator()(const BlockStmtPtr &blockStmt) {
        beginScope();
        for (auto &statement: blockStmt->statements) {
            evaluate(statement);
        }
        endScope();
    }

    void Compiler::operator()(const FunctionStmtPtr &functionStmt) const {
    }

    void Compiler::operator()(const ExpressionStmtPtr &expressionStmt) {
        evaluate(expressionStmt->expression);
    }

    void Compiler::operator()(const PrintStmtPtr &printStmt) {
        const auto value = evaluate(printStmt->expression);

        const auto BoolBlock = BasicBlock::Create(*Context, "if.bool", MainFunction);
        const auto EndBoolBlock = BasicBlock::Create(*Context, "if.bool.end", MainFunction);
        const auto NilBlock = BasicBlock::Create(*Context, "if.nil", MainFunction);
        const auto EndNilBlock = BasicBlock::Create(*Context, "if.nil.end", MainFunction);
        const auto NumBlock = BasicBlock::Create(*Context, "if.num", MainFunction);
        const auto ObjBlock = BasicBlock::Create(*Context, "if.obj", MainFunction);
        const auto EndBlock = BasicBlock::Create(*Context, "if.end", MainFunction);

        Builder->CreateCondBr(IsBool(value), BoolBlock, EndBoolBlock);
        Builder->SetInsertPoint(BoolBlock);
        PrintBool(value);
        Builder->CreateBr(EndBlock);
        Builder->SetInsertPoint(EndBoolBlock);

        Builder->CreateCondBr(IsNil(value), NilBlock, EndNilBlock);
        Builder->SetInsertPoint(NilBlock);
        PrintNil();
        Builder->CreateBr(EndBlock);
        Builder->SetInsertPoint(EndNilBlock);

        Builder->CreateCondBr(IsNumber(value), NumBlock, ObjBlock);
        Builder->SetInsertPoint(NumBlock);
        PrintNumber(value);
        Builder->CreateBr(EndBlock);

        Builder->SetInsertPoint(ObjBlock);
        PrintObject(value);
        Builder->CreateBr(EndBlock);

        Builder->SetInsertPoint(EndBlock);
    }

    void Compiler::operator()(const ReturnStmtPtr &returnStmt) const {
    }

    void Compiler::operator()(const VarStmtPtr &varStmt) {
        const auto alloca = CreateEntryBlockAlloca(MainFunction, Builder->getInt64Ty(), varStmt->name.getLexeme());
        Builder->CreateStore(evaluate(varStmt->initializer), alloca);
        variables.insert(varStmt->name.getLexeme(), alloca);
        /*LoxModule->getOrInsertGlobal(varStmt->name.getLexeme(), Builder->getInt64Ty());
                const auto global = LoxModule->getNamedGlobal(varStmt->name.getLexeme());
                global->setLinkage(GlobalValue::PrivateLinkage);
                global->setAlignment(Align(8));
                global->setConstant(false);
                global->setInitializer(Builder->getInt64(NIL_VAL));

                Builder->CreateStore(evaluate(varStmt->initializer), global);*/
    }

    void Compiler::operator()(const WhileStmtPtr &whileStmt) {
        const auto Cond = BasicBlock::Create(*Context, "Cond", MainFunction);
        const auto Body = BasicBlock::Create(*Context, "Loop", MainFunction);
        const auto Exit = BasicBlock::Create(*Context, "Exit", MainFunction);

        Builder->CreateBr(Cond);
        Builder->SetInsertPoint(Cond);
        Builder->CreateCondBr(IsTruthy(evaluate(whileStmt->condition)), Body, Exit);
        Builder->SetInsertPoint(Body);
        evaluate(whileStmt->body);
        Builder->CreateBr(Cond);
        Builder->SetInsertPoint(Exit);
    }

    void Compiler::operator()(const IfStmtPtr &ifStmt) {
        const auto TrueBlock = BasicBlock::Create(*Context, "if.true", MainFunction);
        const auto FalseBlock = BasicBlock::Create(*Context, "else", MainFunction);
        const auto EndBlock = BasicBlock::Create(*Context, "if.end", MainFunction);
        Builder->CreateCondBr(IsTruthy(evaluate(ifStmt->condition)), TrueBlock, FalseBlock);
        Builder->SetInsertPoint(TrueBlock);
        evaluate(ifStmt->thenBranch);
        Builder->CreateBr(EndBlock);
        Builder->SetInsertPoint(FalseBlock);
        if (ifStmt->elseBranch.has_value()) {
            evaluate(ifStmt->elseBranch.value());
        }
        Builder->CreateBr(EndBlock);
        Builder->SetInsertPoint(EndBlock);
    }

    void Compiler::operator()(const ClassStmtPtr &classStmt) const {
    }
}// namespace lox