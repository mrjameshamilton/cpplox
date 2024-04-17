#include "FunctionCompiler.h"
#include "ModuleCompiler.h"

namespace lox {

    void FunctionCompiler::compile(const std::vector<Stmt> &statements, const std::vector<Token> &parameters, const std::function<void(LoxBuilder &)> &entryBlockBuilder) {

        Builder.SetInsertPoint(EntryBasicBlock);

        beginScope();
        {
            // The default return value is nil.
            Builder.CreateStore(Builder.getNilVal(), returnVal);
            // Push the returnVal so that it's reachable until closing this outermost scope.
            PushLocal(Builder, returnVal, "$returnVal");

            if (entryBlockBuilder) entryBlockBuilder(Builder);

            beginScope();
            {
                // Declare parameters and store them in local variables.
                auto arg = Builder.getFunction()->arg_begin() + 2 /* second arg is receiver, first is upvalues array */;
                for (auto &p: parameters) {
                    insertVariable(p.getLexeme(), arg++);
                }

                for (auto &stmt: statements) {
                    evaluate(stmt);
                }

                if (!Builder.GetInsertBlock()->getTerminator()) {
                    // In the case where there was no return statement in the Lox code,
                    // then the current block at this point will be unterminated.
                    Builder.CreateBr(ExitBasicBlock);
                }

                Builder.SetInsertPoint(ExitBasicBlock);

                // Code can be generated here to close open upvalues,
                // since parameters go out of scope at the end of a function.
            }
            endScope();

            const auto ReturnBlock = Builder.CreateBasicBlock("exit");
            Builder.CreateBr(ReturnBlock);
            Builder.SetInsertPoint(ReturnBlock);
        }
        endScope();
        assert(scopes.empty());

        if (type == LoxFunctionType::INITIALIZER) {
            Builder.CreateRet(Builder.getFunction()->arg_begin() + 1);
        } else {
            Builder.CreateRet(Builder.CreateLoad(Builder.getInt64Ty(), returnVal));
        }
    }

}// namespace lox
