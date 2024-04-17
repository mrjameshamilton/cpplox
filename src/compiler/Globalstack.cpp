#include "Globalstack.h"
#include "../Debug.h"
#include "Memory.h"
#include "ModuleCompiler.h"

namespace lox {
    void PushGlobal(LoxBuilder &Builder, Value *global) {
        static auto PushFunction([&Builder] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$pushGlobal",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();
            const auto global = arguments;

            const auto $sp = B.getModule().getGlobalsCount();
            const auto $cs = B.getModule().getGlobals();
            const auto sp = B.CreateLoad(B.getInt32Ty(), $sp);

            const auto addr = B.CreateGEP($cs->getValueType(), $cs, {B.getInt32(0), sp});
            //B.PrintF({B.CreateGlobalCachedString("pushglobal(%p, %p)\n"), global, addr});
            B.CreateStore(global, addr);

            B.CreateStore(B.CreateAdd(sp, B.getInt32(1)), $sp);

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(PushFunction, {global});
    }

    void IterateGlobals(LoxBuilder &Builder, Function *FunctionPointer) {
        static auto IterateGlobalsFunction([&Builder] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$iterateGlobals",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            if constexpr (DEBUG_LOG_GC) {
                B.PrintString("--iterate globals--");
            }
            const auto $sp = B.getModule().getGlobalsCount();
            const auto $cs = B.getModule().getGlobals();

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

            const auto ptr = B.CreateLoad(B.getPtrTy(), addr);
            const auto value = B.CreateLoad(B.getInt64Ty(), ptr);

            const auto IsObjBlock = B.CreateBasicBlock("is.obj");
            const auto EndBlock = B.CreateBasicBlock("end.obj");

            B.CreateCondBr(B.IsObj(value), IsObjBlock, EndBlock);
            B.SetInsertPoint(IsObjBlock);
            B.CreateCall(
                FunctionType::get(B.getVoidTy(), {B.getPtrTy()}, false),
                F->arg_begin(),
                {(B.AsObj(value))}
            );
            B.CreateBr(EndBlock);
            B.SetInsertPoint(EndBlock);

            B.CreateBr(ForInc);

            B.SetInsertPoint(ForInc);
            B.CreateStore(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), i), B.getInt32(1)), i);
            B.CreateBr(ForCond);

            B.SetInsertPoint(ForEnd);

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(IterateGlobalsFunction, {FunctionPointer});
    }
}// namespace lox