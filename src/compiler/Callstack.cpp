#include "Callstack.h"
#include "Memory.h"

namespace lox {
    void PushCall(LoxBuilder &Builder, Value *line, Value *name) {
        static auto PushFunction([&Builder] {
            const auto F = Function::Create(
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

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();
            const auto line = arguments;
            const auto name = arguments + 1;

            const auto $sp = B.getModule().getCallStackPointer();
            const auto $cs = B.getModule().getCallStack();

            const auto sp = B.CreateLoad(B.getInt32Ty(), $sp);

            const auto addr = B.CreateGEP($cs->getValueType(), $cs, {B.getInt32(0), sp});
            B.CreateStore(line, B.CreateGEP(B.getModule().getCallStruct(), addr, {B.getInt32(0), B.getInt32(0)}));
            B.CreateStore(name, B.CreateGEP(B.getModule().getCallStruct(), addr, {B.getInt32(0), B.getInt32(1)}));

            B.CreateStore(B.CreateAdd(sp, B.getInt32(1)), $sp);

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(PushFunction, {line, name});
    }

    void PopCall(LoxBuilder &Builder) {
        static auto PopFunction([&Builder] {
            const auto F = Function::Create(
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

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto $sp = B.getModule().getCallStackPointer();
            const auto sp = B.CreateLoad(B.getInt32Ty(), $sp);
            B.CreateStore(B.CreateSub(sp, B.getInt32(1)), $sp);

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(PopFunction, {});
    }

    void PrintStackTrace(LoxBuilder &Builder) {
        static auto PrintStackTraceFunction([&Builder] {
            const auto F = Function::Create(
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

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto $sp = B.getModule().getCallStackPointer();
            const auto $cs = B.getModule().getCallStack();

            const auto sp = B.CreateLoad(B.getInt32Ty(), $sp);

            const auto i = CreateEntryBlockAlloca(F, B.getInt32Ty(), "i");

            B.CreateStore(B.getInt32(1), i);

            const auto ForCond = B.CreateBasicBlock("for.cond");
            const auto ForBody = B.CreateBasicBlock("for.body");
            const auto ForInc = B.CreateBasicBlock("for.inc");
            const auto ForEnd = B.CreateBasicBlock("for.end");

            B.CreateBr(ForCond);
            B.SetInsertPoint(ForCond);
            B.CreateCondBr(B.CreateICmpSLE(B.CreateLoad(B.getInt32Ty(), i), sp), ForBody, ForEnd);
            B.SetInsertPoint(ForBody);

            const auto top = B.CreateSub(B.CreateLoad(B.getInt32Ty(), $sp), B.CreateLoad(B.getInt32Ty(), i));
            const auto addr = B.CreateGEP($cs->getValueType(), $cs, {B.getInt32(0), top});

            const auto line = B.CreateLoad(B.getInt32Ty(), B.CreateGEP(B.getModule().getCallStruct(), addr, {B.getInt32(0), B.getInt32(0)}));
            const auto name = B.CreateLoad(B.getPtrTy(), B.CreateGEP(B.getModule().getCallStruct(), addr, {B.getInt32(0), B.getInt32(1)}));

            const auto IsScriptBlock = B.CreateBasicBlock("is.script");
            const auto IsNotScriptBlock = B.CreateBasicBlock("isnot.script");

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
        static auto CheckStackOverflowFunction([&Builder] {
            const auto F = Function::Create(
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

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);
            const auto arguments = F->args().begin();
            const auto line = arguments;
            const auto name = arguments + 1;

            const auto $sp = B.getModule().getCallStackPointer();

            const auto sp = B.CreateLoad(B.getInt32Ty(), $sp);

            const auto IsStackOverFlow = B.CreateBasicBlock("is.stackoverflow");
            const auto IsNotStackOverFlow = B.CreateBasicBlock("isnot.stackoverflow");

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