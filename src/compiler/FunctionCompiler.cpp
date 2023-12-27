#include "FunctionCompiler.h"

namespace lox {

    void FunctionCompiler::compile(const std::vector<Stmt> &statements) {
        beginScope();
        const auto EntryBasicBlock = Builder.CreateBasicBlock("entry");
        Builder.SetInsertPoint(EntryBasicBlock);
        for (auto &stmt: statements) {
            evaluate(stmt);
        }
        Builder.CreateRet(Builder.getInt32(0));
        endScope();
    }

}// namespace lox
