#include "FunctionCompiler.h"
#include "ModuleCompiler.h"

namespace lox {

    void FunctionCompiler::compile(Value *func, const std::vector<Token> &parameters, const std::vector<Stmt> &statements) {
        beginScope();

        // TODO: self-referencing functions.
        if (func) {
            /*
            const auto alloca = CreateEntryBlockAlloca(Builder.getFunction(), Builder.getInt64Ty(), Builder.getFunction()->getName());
            Builder.CreateStore(func, alloca);
            variables.insert(Builder.getFunction()->getName(), alloca);
            */
            //variables.insert(Builder.getFunction()->getName(), Builder.getFunction());
        }

        BasicBlock *EntryBasicBlock = Builder.CreateBasicBlock("entry");
        Builder.SetInsertPoint(EntryBasicBlock);

        // Declare parameters and store them in local variables.
        auto arg = Builder.getFunction()->arg_begin();
        for (auto &p: parameters) {
            const auto alloca = CreateEntryBlockAlloca(Builder.getFunction(), Builder.getInt64Ty(), p.getLexeme());
            Builder.CreateStore(arg++, alloca);
            variables.insert(p.getLexeme(), alloca);
        }

        for (auto &stmt: statements) {
            evaluate(stmt);
        }
        endScope();

        BasicBlock *ExitBasicBlock = Builder.CreateBasicBlock("exit");
        Builder.CreateBr(ExitBasicBlock);
        Builder.SetInsertPoint(ExitBasicBlock);
        Builder.CreateRet(Builder.getNilVal());
    }

}// namespace lox
