#include "FunctionCompiler.h"
#include "ModuleCompiler.h"

namespace lox {

    void FunctionCompiler::compile(const std::vector<Stmt> &statements, const std::vector<Token> &parameters, const std::function<void(LoxBuilder &)> &entryBlockBuilder) {
        beginScope();

        BasicBlock *EntryBasicBlock = Builder.CreateBasicBlock("entry");
        Builder.SetInsertPoint(EntryBasicBlock);

        if (entryBlockBuilder) entryBlockBuilder(Builder);

        // Declare parameters and store them in local variables.
        auto arg = Builder.getFunction()->arg_begin();

        // TODO: support captured variables in general.
        // Self-referencing functions are supported by passing the function obj as the first parameter
        // and re-declaring a local variable with the same name.
        insertVariable(Builder.getFunction()->getName(), arg++);

        for (auto &p: parameters) {
            insertVariable(p.getLexeme(), arg++);
        }

        for (auto &stmt: statements) {
            evaluate(stmt);
        }

        endScope();

        // Default return value.
        BasicBlock *ExitBasicBlock = Builder.CreateBasicBlock("exit");
        Builder.CreateBr(ExitBasicBlock);
        Builder.SetInsertPoint(ExitBasicBlock);
        Builder.CreateRet(Builder.getNilVal());
    }

}// namespace lox
