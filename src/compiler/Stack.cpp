#include "Stack.h"

#include "../Debug.h"
#include "Memory.h"

namespace lox {

    Value *GlobalStack::getCount(LoxBuilder &B) const {
        return B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(StackStruct, stack, 1));
    }

    void GlobalStack::setCount(LoxBuilder &B, Value *count) const {
        B.CreateStore(count, B.CreateStructGEP(StackStruct, stack, 1));
    }

    void GlobalStack::CreatePush(LoxBuilder &Builder, Value *Object) const {
        static auto PushFunction([&] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy(), Builder.getPtrTy(), Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$stackPush",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();
            const auto $stack = B.CreateStructGEP(StackStruct, arguments, 0);
            const auto $count = B.CreateStructGEP(StackStruct, arguments, 1);
            const auto $capacity = B.CreateStructGEP(StackStruct, arguments, 2);
            const auto objPtr = arguments + 1;
            const auto name = arguments + 2;

            if (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("push stack %s ptr %p\n"), name, arguments});
            }

            const auto stack = B.CreateLoad(B.getPtrTy(), $stack);
            const auto count = B.CreateLoad(B.getInt32Ty(), $count);
            const auto capacity = B.CreateLoad(B.getInt32Ty(), $capacity);
            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("realloc stack %s: %p (count: %d, capacity: %d)\n"), name, stack, count, capacity});
            }
            const auto GrowBlock = B.CreateBasicBlock("grow");
            const auto EndBlock = B.CreateBasicBlock("end");

            B.CreateCondBr(B.CreateICmpSLT(capacity, B.CreateAdd(B.getInt32(1), count)), GrowBlock, EndBlock);

            B.SetInsertPoint(GrowBlock);
            const auto newCapacity = B.CreateSelect(
                B.CreateICmpSLT(capacity, B.getInt32(8)),
                B.getInt32(8),
                B.CreateMul(capacity, B.getInt32(GROWTH_FACTOR))
            );
            B.CreateStore(newCapacity, $capacity);

            // TODO: handle null return
            const auto result = B.CreateRealloc(stack, B.getSizeOf(B.getPtrTy(), newCapacity));
            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("realloc stack: %p -> %p (count: %d, capacity: %d, newCapacity: %d)\n"), stack, result, count, capacity, newCapacity});
            }
            B.CreateStore(result, $stack);

            B.CreateBr(EndBlock);
            B.SetInsertPoint(EndBlock);

            auto ptr = B.CreateLoad(B.getPtrTy(), $stack);
            const auto addr = B.CreateGEP(B.getPtrTy(), ptr, count);
            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("push(%p, %p) = %p\n"), ptr, addr, objPtr});
            }
            B.CreateStore(objPtr, addr);

            const auto newCount = B.CreateAdd(B.getInt32(1), count);
            B.CreateStore(newCount, $count);

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(PushFunction, {stack, Object, Builder.CreateGlobalCachedString(name)});
    }

    void GlobalStack::CreatePop(LoxBuilder &Builder) const {
        static auto PopFunction([&] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$stackPop",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();
            const auto $count = B.CreateStructGEP(StackStruct, arguments, 1);

            const auto count = B.CreateLoad(B.getInt32Ty(), $count);

            B.CreateStore(B.CreateSub(count, B.getInt32(1)), $count);

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(PopFunction, {stack});
    }

    void GlobalStack::CreatePopAll(LoxBuilder &Builder, Function *FunctionPointer) const {
        static auto IterateFunction([&] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy(), Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$stackPopAll",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();
            const auto $stack = B.CreateStructGEP(StackStruct, arguments, 0);
            const auto $count = B.CreateStructGEP(StackStruct, arguments, 1);
            const auto function = arguments + 1;

            const auto count = B.CreateLoad(B.getInt32Ty(), $count);

            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("--iterate stack (%d)--\n"), count});
            }

            const auto WhileCond = B.CreateBasicBlock("while.cond");
            const auto WhileBody = B.CreateBasicBlock("while.body");
            const auto WhileEnd = B.CreateBasicBlock("while.end");

            B.CreateBr(WhileCond);
            B.SetInsertPoint(WhileCond);
            B.CreateCondBr(B.CreateICmpSGT(B.CreateLoad(B.getInt32Ty(), $count), B.getInt32(0)), WhileBody, WhileEnd);
            B.SetInsertPoint(WhileBody);
            {
                const auto newCount = B.CreateSub(B.CreateLoad(B.getInt32Ty(), $count), B.getInt32(1));
                B.CreateStore(newCount, $count);
                const auto addr = B.CreateGEP(B.getPtrTy(), B.CreateLoad(B.getPtrTy(), $stack), newCount);
                const auto ptr = B.CreateLoad(B.getPtrTy(), addr);

                B.CreateCall(
                    FunctionType::get(B.getVoidTy(), {B.getPtrTy()}, false),
                    function,
                    {ptr}
                );

                B.CreateBr(WhileCond);
            }
            B.SetInsertPoint(WhileEnd);
            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(IterateFunction, {stack, FunctionPointer});
    }

    void GlobalStack::CreateIterateValues(LoxBuilder &Builder, Function *FunctionPointer) const {
        static auto IterateFunction([&] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy(), Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$iterateStack",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();
            const auto $stack = B.CreateStructGEP(StackStruct, arguments, 0);
            const auto $count = B.CreateStructGEP(StackStruct, arguments, 1);
            const auto function = arguments + 1;

            const auto stack = B.CreateLoad(B.getPtrTy(), $stack);
            const auto count = B.CreateLoad(B.getInt32Ty(), $count);

            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("--iterate stack values (%d)--\n"), count});
            }
            const auto i = CreateEntryBlockAlloca(F, B.getInt32Ty(), "i");

            B.CreateStore(B.getInt32(1), i);

            const auto ForCond = B.CreateBasicBlock("for.cond");
            const auto ForBody = B.CreateBasicBlock("for.body");
            const auto ForInc = B.CreateBasicBlock("for.inc");
            const auto ForEnd = B.CreateBasicBlock("for.end");

            B.CreateBr(ForCond);
            B.SetInsertPoint(ForCond);
            B.CreateCondBr(B.CreateICmpSLE(B.CreateLoad(B.getInt32Ty(), i), count), ForBody, ForEnd);
            B.SetInsertPoint(ForBody);

            const auto top = B.CreateSub(B.CreateLoad(B.getInt32Ty(), $count), B.CreateLoad(B.getInt32Ty(), i));
            const auto addr = B.CreateGEP(ArrayType::get(B.getPtrTy(), 1), stack, {B.getInt32(0), top});

            const auto ptr = B.CreateLoad(B.getPtrTy(), addr);
            const auto value = B.CreateLoad(B.getInt64Ty(), ptr);

            const auto IsObjBlock = B.CreateBasicBlock("is.obj");
            const auto EndBlock = B.CreateBasicBlock("end.obj");

            B.CreateCondBr(B.IsObj(value), IsObjBlock, EndBlock);
            B.SetInsertPoint(IsObjBlock);
            B.CreateCall(
                FunctionType::get(B.getVoidTy(), {B.getPtrTy()}, false),
                function,
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

        Builder.CreateCall(IterateFunction, {stack, FunctionPointer});
    }

    void GlobalStack::CreateFree(LoxBuilder &Builder) const {
        Builder.IRBuilder::CreateFree(Builder.CreateLoad(Builder.getPtrTy(), stack));
    }

    void PushGlobal(LoxBuilder &Builder, GlobalVariable *global) {
        Builder.getModule().getGlobalsStack()->CreatePush(Builder, global);
    }

    void IterateGlobals(LoxBuilder &Builder, Function *FunctionPointer) {
        if constexpr (DEBUG_LOG_GC) {
            Builder.PrintString("--iterate globals--");
        }
        Builder.getModule().getGlobalsStack()->CreateIterateValues(Builder, FunctionPointer);
    }

    void PushLocal(LoxBuilder &Builder, Value *local, const StringRef what) {
        if constexpr (DEBUG_LOG_GC) {
            const auto sp = Builder.getModule().getLocalsStack()->getCount(Builder);
            Builder.PrintF({Builder.CreateGlobalCachedString("pushlocal(%p [%s, %d])\n"), local, Builder.CreateGlobalCachedString(what), sp});
            //B.PrintObject(B.CreateLoad(B.getInt64Ty(), local));
        }
        Builder.getModule().getLocalsStack()->CreatePush(Builder, local);
    }

    void IterateLocals(LoxBuilder &Builder, Function *FunctionPointer) {
        if constexpr (DEBUG_LOG_GC) {
            const auto sp = Builder.getModule().getLocalsStack()->getCount(Builder);
            Builder.PrintF({Builder.CreateGlobalCachedString("--iterate locals (%d)--\n"), sp});
        }

        Builder.getModule().getLocalsStack()->CreateIterateValues(Builder, FunctionPointer);
    }
}// namespace lox