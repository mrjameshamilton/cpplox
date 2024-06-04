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

        beginScope();
        {
            // The default return value is nil except for the script and initializers.
            if (type != LoxFunctionType::NONE && type != LoxFunctionType::INITIALIZER) {
                insertVariable("$returnVal", Builder.getNilVal());
            }

            if (entryBlockBuilder) entryBlockBuilder(Builder);

            beginScope();
            {
                if constexpr (DEBUG_LOG_GC) {
                    Builder.PrintF({Builder.CreateGlobalCachedString("## start function inner scope %s (sp %d)\n"), Builder.CreateGlobalCachedString(Builder.getFunction()->getName()), Builder.CreateLoad(Builder.getInt32Ty(), sp)});
                }
                // Declare parameters and store them in local variables.
                auto *arg = Builder.getFunction()->arg_begin() + 2 /* second arg is receiver, first is upvalues array */;
                for (auto &p: parameters) {
                    insertVariable(p.getLexeme(), arg++);
                }

                for (auto &stmt: statements) {
                    evaluate(stmt);
                }

                if (Builder.GetInsertBlock()->getTerminator() == nullptr) {
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

            auto *const ReturnBlock = Builder.CreateBasicBlock("exit");
            Builder.CreateBr(ReturnBlock);
            Builder.SetInsertPoint(ReturnBlock);
        }

        AllocaInst *returnVal = nullptr;
        if (type != LoxFunctionType::NONE && type != LoxFunctionType::INITIALIZER) {
            returnVal = CreateEntryBlockAlloca(Builder.getFunction(), Builder.getInt64Ty(), "returnValTemp");
            // Store a copy of the return value, before the $returnVal
            // local goes out of scope.
            if (const auto value = variables.lookup("$returnVal")) {
                Builder.CreateStore(Builder.CreateLoad(Builder.getInt64Ty(), value->value), returnVal);
                Builder.CreateInvariantStart(returnVal, Builder.getInt64(64));
            }
        }

        endScope();

        // At the beginning of the function, remember the current local variable stack pointer.
        IRBuilder EntryBlockBuilder(&Builder.getFunction()->getEntryBlock(), Builder.getFunction()->getEntryBlock().begin());
        const auto locals = Builder.getModule().getLocalsStack();
        // Ensure the alloca for the sp comes before any of its uses.
        sp->moveBefore(EntryBlockBuilder.CreateStore(locals.CreateGetCount(EntryBlockBuilder), sp));
        // Create new slots for the required number of locals.
        locals.CreatePushN(Builder.getModule(), EntryBlockBuilder, Constant::getNullValue(EntryBlockBuilder.getPtrTy()), EntryBlockBuilder.getInt32(localsCount));

        // At the end of the function, reset the stack pointer then any variables allocated
        // in the function are no longer accessible as GC roots and can be freed.
        locals.CreatePopN(Builder, Builder.getInt32(localsCount));

        if constexpr (DEBUG_LOG_GC) {
            Builder.PrintF({Builder.CreateGlobalCachedString("# end function scope %s (sp %d)\n"), Builder.CreateGlobalCachedString(Builder.getFunction()->getName()), Builder.CreateLoad(Builder.getInt32Ty(), sp)});
        }

        assert(scopes.empty());

        if (type == LoxFunctionType::NONE) {
            Builder.CreateRetVoid();
        } else if (type == LoxFunctionType::INITIALIZER) {
            Builder.CreateRet(Builder.getFunction()->arg_begin() + 1);
        } else {
            Builder.CreateRet(Builder.CreateLoad(Builder.getInt64Ty(), returnVal));
        }
    }

}// namespace lox
