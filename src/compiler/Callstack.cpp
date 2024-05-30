#include "Callstack.h"
#include "Memory.h"

namespace lox {
    void PushCall(LoxBuilder &Builder, Value *line, Value *name) {
        static auto *PushFunction([&Builder] {
            auto *const F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getInt32Ty(), Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$push",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->args().begin();
            auto *const line = arguments;
            auto *const name = arguments + 1;

            auto *const $sp = B.getModule().getCallStackPointer();
            auto *const $cs = B.getModule().getCallStack();

            auto *const sp = B.CreateLoad(B.getInt32Ty(), $sp);

            auto *const addr = B.CreateGEP($cs->getValueType(), $cs, {B.getInt32(0), sp});
            B.CreateStore(line, B.CreateGEP(B.getModule().getCallStruct(), addr, {B.getInt32(0), B.getInt32(0)}));
            B.CreateStore(name, B.CreateGEP(B.getModule().getCallStruct(), addr, {B.getInt32(0), B.getInt32(1)}));

            B.CreateStore(B.CreateAdd(sp, B.getInt32(1)), $sp);

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(PushFunction, {line, name});
    }

    void PopCall(LoxBuilder &Builder) {
        static auto *PopFunction([&Builder] {
            auto *const F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {},
                    false
                ),
                Function::InternalLinkage,
                "$pop",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const $sp = B.getModule().getCallStackPointer();
            auto *const sp = B.CreateLoad(B.getInt32Ty(), $sp);
            B.CreateStore(B.CreateSub(sp, B.getInt32(1)), $sp);

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(PopFunction, {});
    }

    void PrintStackTrace(LoxBuilder &Builder) {
        static auto *PrintStackTraceFunction([&Builder] {
            auto *const F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {},
                    false
                ),
                Function::InternalLinkage,
                "$printStackTrace",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const $sp = B.getModule().getCallStackPointer();
            auto *const $cs = B.getModule().getCallStack();

            auto *const sp = B.CreateLoad(B.getInt32Ty(), $sp);

            auto *const i = CreateEntryBlockAlloca(F, B.getInt32Ty(), "i");

            B.CreateStore(B.getInt32(1), i);

            auto *const ForCond = B.CreateBasicBlock("for.cond");
            auto *const ForBody = B.CreateBasicBlock("for.body");
            auto *const ForInc = B.CreateBasicBlock("for.inc");
            auto *const ForEnd = B.CreateBasicBlock("for.end");

            B.CreateBr(ForCond);
            B.SetInsertPoint(ForCond);
            B.CreateCondBr(B.CreateICmpSLE(B.CreateLoad(B.getInt32Ty(), i), sp), ForBody, ForEnd);
            B.SetInsertPoint(ForBody);

            auto *const top = B.CreateSub(B.CreateLoad(B.getInt32Ty(), $sp), B.CreateLoad(B.getInt32Ty(), i));
            auto *const addr = B.CreateGEP($cs->getValueType(), $cs, {B.getInt32(0), top});

            auto *const line = B.CreateLoad(B.getInt32Ty(), B.CreateGEP(B.getModule().getCallStruct(), addr, {B.getInt32(0), B.getInt32(0)}));
            auto *const name = B.CreateLoad(B.getPtrTy(), B.CreateGEP(B.getModule().getCallStruct(), addr, {B.getInt32(0), B.getInt32(1)}));

            auto *const IsScriptBlock = B.CreateBasicBlock("is.script");
            auto *const IsNotScriptBlock = B.CreateBasicBlock("isnot.script");

            B.CreateCondBr(B.CreateICmpEQ(sp, B.CreateLoad(B.getInt32Ty(), i)), IsScriptBlock, IsNotScriptBlock);

            B.SetInsertPoint(IsScriptBlock);
            B.PrintFErr(B.CreateGlobalCachedString("[line %d] in script\n"), {line});
            B.CreateBr(ForInc);
            B.SetInsertPoint(IsNotScriptBlock);
            B.PrintFErr(B.CreateGlobalCachedString("[line %d] in %s()\n"), {line, name});
            B.CreateBr(ForInc);

            B.SetInsertPoint(ForInc);
            B.CreateStore(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), i), B.getInt32(1)), i);
            B.CreateBr(ForCond);

            B.SetInsertPoint(ForEnd);

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(PrintStackTraceFunction, {});
    }

    void CheckStackOverflow(LoxBuilder &Builder, Value *line, Value *name) {
        static auto *CheckStackOverflowFunction([&Builder] {
            auto *const F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getInt32Ty(), Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$checkStackOverflow",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);
            auto *const arguments = F->args().begin();
            auto *const line = arguments;
            auto *const name = arguments + 1;

            auto *const $sp = B.getModule().getCallStackPointer();

            auto *const sp = B.CreateLoad(B.getInt32Ty(), $sp);

            auto *const IsStackOverFlow = B.CreateBasicBlock("is.stackoverflow");
            auto *const IsNotStackOverFlow = B.CreateBasicBlock("isnot.stackoverflow");

            B.CreateCondBr(B.CreateICmpSGE(sp, B.getInt32(MAX_CALL_STACK_SIZE - 1)), IsStackOverFlow, IsNotStackOverFlow);

            B.SetInsertPoint(IsStackOverFlow);
            B.RuntimeError(line, "Stack overflow.\n", {}, name);
            B.CreateUnreachable();

            B.SetInsertPoint(IsNotStackOverFlow);
            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(CheckStackOverflowFunction, {line, name});
    }
}// namespace lox