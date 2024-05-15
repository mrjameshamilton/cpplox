#include "FunctionCompiler.h"
#include "../Debug.h"
#include "ModuleCompiler.h"

namespace lox {

    void FunctionCompiler::compile(const std::vector<Stmt> &statements, const std::vector<Token> &parameters, const std::function<void(LoxBuilder &)> &entryBlockBuilder) {

        Builder.SetInsertPoint(EntryBasicBlock);
        if constexpr (DEBUG_LOG_GC) {
            Builder.PrintF({
                Builder.CreateGlobalCachedString("# start function %s\n"),
                Builder.CreateGlobalCachedString(Builder.getFunction()->getName()),
            });
        }

        // At the beginning of the function, remember the current local variable stack pointer.
        Builder.CreateStore(Builder.getModule().getLocalsStack()->getCount(Builder), sp);

        if constexpr (DEBUG_LOG_GC) {
            Builder.PrintF({Builder.CreateGlobalCachedString("\tset sp = %d\n"), Builder.CreateLoad(Builder.getInt32Ty(), sp)});
        }

        beginScope();
        {
            // The default return value is nil.
            Builder.CreateStore(Builder.getNilVal(), returnVal);
            // Push the returnVal so that it's reachable until closing this outermost scope.
            PushLocal(Builder, returnVal, "$returnVal");

            if (entryBlockBuilder) entryBlockBuilder(Builder);

            beginScope();
            {
                if constexpr (DEBUG_LOG_GC) {
                    Builder.PrintF({Builder.CreateGlobalCachedString("## start function inner scope %s (sp %d)\n"), Builder.CreateGlobalCachedString(Builder.getFunction()->getName()), Builder.CreateLoad(Builder.getInt32Ty(), sp)});
                }
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

            if constexpr (DEBUG_LOG_GC) {
                Builder.PrintF({Builder.CreateGlobalCachedString("## end function inner scope %s (sp %d)\n"), Builder.CreateGlobalCachedString(Builder.getFunction()->getName()), Builder.CreateLoad(Builder.getInt32Ty(), sp)});
            }

            const auto ReturnBlock = Builder.CreateBasicBlock("exit");
            Builder.CreateBr(ReturnBlock);
            Builder.SetInsertPoint(ReturnBlock);
        }
        endScope();

        // At the end of the function, reset the stack pointer then any variables allocated
        // in the function are no longer accessible as GC roots and can be freed.
        // This is especially important for early returns from scopes, since the endScope()
        // won't be called for scopes they would normally close after the return.
        Builder.getModule().getLocalsStack()->setCount(Builder, Builder.CreateLoad(Builder.getInt32Ty(), sp));

        if constexpr (DEBUG_LOG_GC) {
            Builder.PrintF({Builder.CreateGlobalCachedString("# end function scope %s (sp %d)\n"), Builder.CreateGlobalCachedString(Builder.getFunction()->getName()), Builder.CreateLoad(Builder.getInt32Ty(), sp)});
        }

        assert(scopes.empty());

        if (type == LoxFunctionType::INITIALIZER) {
            Builder.CreateRet(Builder.getFunction()->arg_begin() + 1);
        } else {
            Builder.CreateRet(Builder.CreateLoad(Builder.getInt64Ty(), returnVal));
        }
    }

}// namespace lox
