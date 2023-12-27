#include "FunctionCompiler.h"
#include "ModuleCompiler.h"

namespace lox {

    void FunctionCompiler::compile(const std::vector<Token> &parameters, const std::vector<Stmt> &statements) {
        beginScope();
        const auto EntryBasicBlock = Builder.CreateBasicBlock("entry");
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

        // TODO: return statements.
        Builder.CreateRet(Builder.getNilVal());
        endScope();
    }

}// namespace lox
