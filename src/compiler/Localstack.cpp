#include "Localstack.h"
#include "../Debug.h"
#include "Memory.h"
#include "ModuleCompiler.h"

namespace lox {
    void PushLocal(LoxBuilder &Builder, Value *local, const StringRef what) {
        static auto PushFunction([&Builder] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy(), Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$pushLocal",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();
            const auto local = arguments;
            const auto what = arguments + 1;

            const auto $sp = B.getModule().getLocalsStackPointer();
            const auto $cs = B.getModule().getLocalsStack();

            const auto sp = B.CreateLoad(B.getInt32Ty(), $sp);

            const auto addr = B.CreateGEP($cs->getValueType(), $cs, {B.getInt32(0), sp});
            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("pushlocal(%p, %p [%s, %d])\n"), local, addr, what, sp});
                //B.PrintObject(B.CreateLoad(B.getInt64Ty(), local));
            }
            B.CreateStore(local, addr);

            B.CreateStore(B.CreateAdd(sp, B.getInt32(1)), $sp);

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(PushFunction, {local, Builder.CreateGlobalCachedString(what)});
    }

    Value *PushTemp(LoxBuilder &Builder, Value *value, const StringRef what) {
        assert(value->getType() == Builder.getPtrTy());
        const auto temp = CreateEntryBlockAlloca(Builder.getFunction(), Builder.getPtrTy(), "temp");
        Builder.CreateStore(Builder.ObjVal(value), temp);
        PushLocal(Builder, temp, ("temp: " + what).str());
        return value;
    }

    void PopLocal(LoxBuilder &Builder, const StringRef what) {
        static auto PopFunction([&Builder] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$popLocal",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto what = F->arg_begin();

            const auto $sp = B.getModule().getLocalsStackPointer();
            const auto sp = B.CreateLoad(B.getInt32Ty(), $sp);

            B.CreateStore(B.CreateSub(sp, B.getInt32(1)), $sp);

            if constexpr (DEBUG_LOG_GC) {
                const auto $cs = B.getModule().getLocalsStack();
                const auto addr = B.CreateGEP($cs->getValueType(), $cs, {B.getInt32(0), B.CreateLoad(B.getInt32Ty(), $sp)});
                B.PrintF({B.CreateGlobalCachedString("poplocal(%p, %p [%s])\n"), B.CreateLoad(B.getPtrTy(), addr), addr, what});
            }

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(PopFunction, {Builder.CreateGlobalCachedString(what)});
    }

    void IterateLocals(LoxBuilder &Builder, Function *FunctionPointer) {
        static auto IterateLocalsFunction([&Builder] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$iterateLocals",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);


            const auto $sp = B.getModule().getLocalsStackPointer();
            const auto $cs = B.getModule().getLocalsStack();

            const auto sp = B.CreateLoad(B.getInt32Ty(), $sp);

            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("--iterate locals (%d)--\n"), sp});
            }

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

            auto ptr = B.CreateLoad(B.getPtrTy(), addr);
            auto value = B.CreateLoad(B.getInt64Ty(), ptr);

            const auto IsObjBlock = B.CreateBasicBlock("is.obj");
            const auto IsNotObjBlock = B.CreateBasicBlock("is.notobj");
            const auto EndBlock = B.CreateBasicBlock("end.obj");

            B.CreateCondBr(B.IsObj(value), IsObjBlock, IsNotObjBlock);
            B.SetInsertPoint(IsObjBlock);
            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("local(%p, %p, %d, %p) = "), addr, ptr, value, (B.AsObj(value))});
                B.Print(B.CreateLoad(B.getInt64Ty(), ptr));
            }
            B.CreateCall(
                FunctionType::get(B.getVoidTy(), {B.getPtrTy()}, false),
                F->arg_begin(),
                {B.AsObj(value)}
            );
            B.CreateBr(EndBlock);

            B.SetInsertPoint(IsNotObjBlock);
            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("not object %p %p %p = "), addr, ptr, B.AsObj(value)});
                B.Print(value);
            }

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

        Builder.CreateCall(IterateLocalsFunction, {FunctionPointer});
    }

}// namespace lox