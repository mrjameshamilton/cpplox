#include "FunctionCompiler.h"

namespace lox {

    void FunctionCompiler::compile(const std::vector<Stmt> &statements) {
        beginScope();
        const auto EntryBasicBlock = Builder.CreateBasicBlock("entry");
        Builder.SetInsertPoint(EntryBasicBlock);
        for (auto &stmt: statements) {
            evaluate(stmt);
        }
        // TODO: return statements.
        Builder.CreateRet(Builder.getNilVal());
        endScope();
    }

}// namespace lox
