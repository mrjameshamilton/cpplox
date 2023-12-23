#include "LoxCompiler.h"
namespace lox {

    void LoxCompiler::evaluate(const Stmt &stmt) {
        std::visit(*this, stmt);
    }

    void LoxCompiler::operator()(const BlockStmtPtr &blockStmt) {
        beginScope();
        for (auto &statement: blockStmt->statements) {
            evaluate(statement);
        }
        endScope();
    }

    void LoxCompiler::operator()(const FunctionStmtPtr &functionStmt) const {
    }

    void LoxCompiler::operator()(const ExpressionStmtPtr &expressionStmt) {
        evaluate(expressionStmt->expression);
    }

    void LoxCompiler::operator()(const PrintStmtPtr &printStmt) {
        const auto value = evaluate(printStmt->expression);

        const auto BoolBlock = BasicBlock::Create(Builder->getContext(), "if.bool", Builder->getFunction());
        const auto EndBoolBlock = BasicBlock::Create(Builder->getContext(), "if.bool.end", Builder->getFunction());
        const auto NilBlock = BasicBlock::Create(Builder->getContext(), "if.nil", Builder->getFunction());
        const auto EndNilBlock = BasicBlock::Create(Builder->getContext(), "if.nil.end", Builder->getFunction());
        const auto NumBlock = BasicBlock::Create(Builder->getContext(), "if.num", Builder->getFunction());
        const auto ObjBlock = BasicBlock::Create(Builder->getContext(), "if.obj", Builder->getFunction());
        const auto EndBlock = BasicBlock::Create(Builder->getContext(), "if.end", Builder->getFunction());

        Builder->CreateCondBr(Builder->IsBool(value), BoolBlock, EndBoolBlock);
        Builder->SetInsertPoint(BoolBlock);
        Builder->PrintBool(value);
        Builder->CreateBr(EndBlock);
        Builder->SetInsertPoint(EndBoolBlock);

        Builder->CreateCondBr(Builder->IsNil(value), NilBlock, EndNilBlock);
        Builder->SetInsertPoint(NilBlock);
        Builder->PrintNil();
        Builder->CreateBr(EndBlock);
        Builder->SetInsertPoint(EndNilBlock);

        Builder->CreateCondBr(Builder->IsNumber(value), NumBlock, ObjBlock);
        Builder->SetInsertPoint(NumBlock);
        Builder->PrintNumber(value);
        Builder->CreateBr(EndBlock);

        Builder->SetInsertPoint(ObjBlock);
        Builder->PrintObject(value);
        Builder->CreateBr(EndBlock);

        Builder->SetInsertPoint(EndBlock);
    }

    void LoxCompiler::operator()(const ReturnStmtPtr &returnStmt) const {
    }

    void LoxCompiler::operator()(const VarStmtPtr &varStmt) {
        const auto alloca = CreateEntryBlockAlloca(Builder->getFunction(), Builder->getInt64Ty(), varStmt->name.getLexeme());
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

    void LoxCompiler::operator()(const WhileStmtPtr &whileStmt) {
        const auto Cond = BasicBlock::Create(Builder->getContext(), "Cond", Builder->getFunction());
        const auto Body = BasicBlock::Create(Builder->getContext(), "Loop", Builder->getFunction());
        const auto Exit = BasicBlock::Create(Builder->getContext(), "Exit", Builder->getFunction());

        Builder->CreateBr(Cond);
        Builder->SetInsertPoint(Cond);
        Builder->CreateCondBr(Builder->IsTruthy(evaluate(whileStmt->condition)), Body, Exit);
        Builder->SetInsertPoint(Body);
        evaluate(whileStmt->body);
        Builder->CreateBr(Cond);
        Builder->SetInsertPoint(Exit);
    }

    void LoxCompiler::operator()(const IfStmtPtr &ifStmt) {
        const auto TrueBlock = BasicBlock::Create(Builder->getContext(), "if.true", Builder->getFunction());
        const auto FalseBlock = BasicBlock::Create(Builder->getContext(), "else", Builder->getFunction());
        const auto EndBlock = BasicBlock::Create(Builder->getContext(), "if.end", Builder->getFunction());
        Builder->CreateCondBr(Builder->IsTruthy(evaluate(ifStmt->condition)), TrueBlock, FalseBlock);
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

    void LoxCompiler::operator()(const ClassStmtPtr &classStmt) const {
    }
}// namespace lox