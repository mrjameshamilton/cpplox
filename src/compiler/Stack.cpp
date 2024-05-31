#include "Stack.h"

#include "../Debug.h"
#include "Memory.h"

namespace lox {

    static void ensureCapacity(LoxModule &M, IRBuilder<> &Builder, Value *stack, StructType *type, Value *size) {
        assert(size->getType() == Builder.getInt32Ty());

        static auto *EnsureCapacityFunction([&Builder, &M] {
            auto *const F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy(), Builder.getPtrTy(), Builder.getPtrTy(), Builder.getInt32Ty()},
                    false
                ),
                Function::InternalLinkage,
                "$stackEnsureCapacity",
                M
            );

            F->addFnAttr(Attribute::AlwaysInline);

            LoxBuilder B(Builder.getContext(), M, *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->args().begin();
            auto *const $stack = arguments + 0;
            auto *const $count = arguments + 1;
            auto *const $capacity = arguments + 2;
            auto *const size = arguments + 3;

            if (DEBUG_STACK) {
                B.PrintF({B.CreateGlobalCachedString("ensure cap stack with %d\n"), size});
            }

            auto *const stack = B.CreateLoad(B.getPtrTy(), $stack);
            auto *const count = B.CreateLoad(B.getInt32Ty(), $count);
            auto *const capacity = B.CreateLoad(B.getInt32Ty(), $capacity);
            if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("realloc stack: %p (count: %d, capacity: %d)\n"), stack, count, capacity});
            }
            auto *const GrowBlock = B.CreateBasicBlock("grow");
            auto *const EndBlock = B.CreateBasicBlock("end");

            B.CreateCondBr(B.CreateICmpSLT(capacity, size), GrowBlock, EndBlock);

            B.SetInsertPoint(GrowBlock);
            auto *const newCapacity = B.CreateSelect(
                B.CreateICmpSLT(size, B.getInt32(8)),
                B.getInt32(8),
                B.CreateMul(size, B.getInt32(GROWTH_FACTOR))
            );
            B.CreateStore(newCapacity, $capacity);

            auto *const newSize = B.getSizeOf(B.getPtrTy(), newCapacity);

            if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("realloc stack: %p with new size %d\n"), stack, newSize});
            }

            auto *const result = B.CreateRealloc(stack, newSize, "stack");

            auto *const IsNullBlock = B.CreateBasicBlock("error.realloc");
            auto *const OkBlock = B.CreateBasicBlock("ok.realloc");

            B.CreateCondBr(B.CreateIsNull(result), IsNullBlock, OkBlock);
            B.SetInsertPoint(IsNullBlock);
            {
                B.RuntimeError(B.getInt32(0), "Could not reallocate %d for %p\n", {newSize, stack}, B.CreateGlobalCachedString("ensureCapacity"));
                B.CreateUnreachable();
            }
            B.SetInsertPoint(OkBlock);
            {
                if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
                    B.PrintF({B.CreateGlobalCachedString("realloc stack: %p; %p -> %p (count: %d, capacity: %d, newCapacity: %d)\n"), $stack, stack, result, count, capacity, newCapacity});
                }
                B.CreateStore(result, $stack);
                B.CreateBr(EndBlock);
            }

            B.SetInsertPoint(EndBlock);
            B.CreateRetVoid();

            return F;
        }());

        auto *const $stack = Builder.CreateStructGEP(type, stack, 0);
        auto *const $count = Builder.CreateStructGEP(type, stack, 1);
        auto *const $capacity = Builder.CreateStructGEP(type, stack, 2);

        Builder.CreateCall(EnsureCapacityFunction, {$stack, $count, $capacity, size});
    }

    Value *GlobalStack::CreateGetCount(IRBuilder<> &B) const {
        return B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(StackStruct, stack, 1));
    }

    Value *GlobalStack::CreateGet(LoxBuilder &B, Value *index) const {
        auto *const $stack = B.CreateStructGEP(StackStruct, stack, 0);
        auto *const stack = B.CreateLoad(B.getPtrTy(), $stack);
        auto *const addr = B.CreateInBoundsGEP(B.getPtrTy(), stack, index);

        return B.CreateLoad(B.getPtrTy(), addr);
    }

    void GlobalStack::CreateSet(LoxBuilder &B, Value *index, Value *value) const {
        auto *const $stack = B.CreateStructGEP(StackStruct, stack, 0);
        auto *const stack = B.CreateLoad(B.getPtrTy(), $stack);
        auto *const addr = B.CreateInBoundsGEP(B.getPtrTy(), stack, index);

        if constexpr (DEBUG_STACK) {
            B.PrintF({B.CreateGlobalCachedString("set value: %d %p -> %p\n"), index, value, addr});
        }

        B.CreateStore(value, addr);
    }

    void GlobalStack::CreatePushN(LoxModule &M, IRBuilder<> &Builder, Value *Object, Value *N) const {
        static auto *PushFunction([&Builder, &M, this] {
            auto *const F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy(), Builder.getPtrTy(), Builder.getInt32Ty()},
                    false
                ),
                Function::InternalLinkage,
                "$stackPushN",
                M
            );

            LoxBuilder B(Builder.getContext(), M, *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->args().begin();
            auto *const stackGlobal = arguments;
            auto *const $stack = B.CreateStructGEP(StackStruct, stackGlobal, 0);
            auto *const objPtr = arguments + 1;
            auto *const N = arguments + 2;

            if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("push stack %p; ptr %p\n"), $stack, stackGlobal});
            }

            auto *const i = CreateEntryBlockAlloca(F, B.getInt32Ty(), "i");

            B.CreateStore(B.getInt32(1), i);

            auto *const ForCond = B.CreateBasicBlock("for.cond");
            auto *const ForBody = B.CreateBasicBlock("for.body");
            auto *const ForInc = B.CreateBasicBlock("for.inc");
            auto *const ForEnd = B.CreateBasicBlock("for.end");

            B.CreateBr(ForCond);
            B.SetInsertPoint(ForCond);
            B.CreateCondBr(B.CreateICmpSLE(B.CreateLoad(B.getInt32Ty(), i), N), ForBody, ForEnd);
            B.SetInsertPoint(ForBody);

            CreatePush(M, B, objPtr);

            B.CreateBr(ForInc);
            B.SetInsertPoint(ForInc);
            B.CreateStore(B.CreateNSWAdd(B.CreateLoad(B.getInt32Ty(), i), B.getInt32(1)), i);
            B.CreateBr(ForCond);

            B.SetInsertPoint(ForEnd);
            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(PushFunction, {stack, Object, N});
    }

    void GlobalStack::CreatePush(LoxModule &M, IRBuilder<> &Builder, Value *Object) const {
        static auto *PushFunction([&Builder, &M, this] {
            auto *const F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy(), Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$stackPush",
                M
            );

            LoxBuilder B(Builder.getContext(), M, *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const stackGlobal = F->args().begin();
            auto *const $stack = B.CreateStructGEP(StackStruct, stackGlobal, 0);
            auto *const $count = B.CreateStructGEP(StackStruct, stackGlobal, 1);
            auto *const objPtr = stackGlobal + 1;

            if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("push stack %p; ptr %p\n"), $stack, stackGlobal});
            }

            auto *const count = B.CreateLoad(B.getInt32Ty(), $count);

            ensureCapacity(M, B, stackGlobal, StackStruct, B.CreateNSWAdd(B.getInt32(1), count));

            auto *const ptr = B.CreateLoad(B.getPtrTy(), $stack);
            auto *const addr = B.CreateInBoundsGEP(B.getPtrTy(), ptr, count);
            if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("push (%p, %p) = %p\n"), ptr, addr, objPtr});
            }
            B.CreateStore(objPtr, addr);

            auto *const newCount = B.CreateNSWAdd(B.getInt32(1), count);
            B.CreateStore(newCount, $count);

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(PushFunction, {stack, Object});
    }

    void GlobalStack::CreatePopN(LoxBuilder &Builder, Value *N) const {
        static auto *PopFunction([&] {
            auto *const F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy(), Builder.getInt32Ty()},
                    false
                ),
                Function::InternalLinkage,
                "$stackPopN",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->args().begin();
            auto *const $count = B.CreateStructGEP(StackStruct, arguments, 1);
            auto *const N = arguments + 1;

            auto *const count = B.CreateLoad(B.getInt32Ty(), $count);

            B.CreateStore(B.CreateSub(count, N), $count);

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(PopFunction, {stack, N});
    }

    void GlobalStack::CreatePop(LoxBuilder &Builder) const {
        CreatePopN(Builder, Builder.getInt32(1));
    }

    void GlobalStack::CreatePopAll(LoxBuilder &Builder, Function *FunctionPointer) const {
        static auto *IterateFunction([&] {
            auto *const F = Function::Create(
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

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->args().begin();
            auto *const $stack = B.CreateStructGEP(StackStruct, arguments, 0);
            auto *const $count = B.CreateStructGEP(StackStruct, arguments, 1);
            auto *const function = arguments + 1;

            auto *const count = B.CreateLoad(B.getInt32Ty(), $count);

            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("--iterate stack (%d)--\n"), count});
            }

            auto *const WhileCond = B.CreateBasicBlock("while.cond");
            auto *const WhileBody = B.CreateBasicBlock("while.body");
            auto *const WhileEnd = B.CreateBasicBlock("while.end");

            B.CreateBr(WhileCond);
            B.SetInsertPoint(WhileCond);
            B.CreateCondBr(B.CreateICmpSGT(B.CreateLoad(B.getInt32Ty(), $count), B.getInt32(0)), WhileBody, WhileEnd);
            B.SetInsertPoint(WhileBody);
            {
                auto *const newCount = B.CreateSub(B.CreateLoad(B.getInt32Ty(), $count), B.getInt32(1));
                B.CreateStore(newCount, $count);
                auto *const addr = B.CreateInBoundsGEP(B.getPtrTy(), B.CreateLoad(B.getPtrTy(), $stack), newCount);
                auto *const ptr = B.CreateLoad(B.getPtrTy(), addr);

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

    void GlobalStack::CreateIterateObjectValues(LoxBuilder &Builder, Function *FunctionPointer) const {
        static auto *IterateFunction([&] {
            auto *const F = Function::Create(
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

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->args().begin();
            auto *const $stack = B.CreateStructGEP(StackStruct, arguments, 0);
            auto *const $count = B.CreateStructGEP(StackStruct, arguments, 1);
            auto *const function = arguments + 1;

            auto *const stack = B.CreateLoad(B.getPtrTy(), $stack);
            auto *const count = B.CreateLoad(B.getInt32Ty(), $count);

            if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("--iterate stack values (%d)--\n"), count});
            }
            auto *const i = CreateEntryBlockAlloca(F, B.getInt32Ty(), "i");

            B.CreateStore(B.getInt32(1), i);

            auto *const ForCond = B.CreateBasicBlock("for.cond");
            auto *const ForBody = B.CreateBasicBlock("for.body");
            auto *const ForInc = B.CreateBasicBlock("for.inc");
            auto *const ForEnd = B.CreateBasicBlock("for.end");

            B.CreateBr(ForCond);
            B.SetInsertPoint(ForCond);
            B.CreateCondBr(B.CreateICmpSLE(B.CreateLoad(B.getInt32Ty(), i), count), ForBody, ForEnd);
            B.SetInsertPoint(ForBody);

            auto *const top = B.CreateSub(B.CreateLoad(B.getInt32Ty(), $count), B.CreateLoad(B.getInt32Ty(), i));
            auto *const addr = B.CreateInBoundsGEP(B.getPtrTy(), stack, top);

            auto *const ptr = B.CreateLoad(B.getPtrTy(), addr);

            if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("iter ptr: %p %p\n"), addr, ptr});
            }

            auto *const IsObjBlock = B.CreateBasicBlock("is.obj");
            auto *const EndBlock = B.CreateBasicBlock("end.obj");
            auto *const IsNotNull = B.CreateBasicBlock("is.notnull");
            auto *const IsNotObjBlock = DEBUG_LOG_GC ? B.CreateBasicBlock("is.not.obj") : EndBlock;

            B.CreateCondBr(B.CreateIsNull(ptr), IsNotObjBlock, IsNotNull);
            B.SetInsertPoint(IsNotNull);

            auto *const value = B.CreateLoad(B.getInt64Ty(), ptr);

            if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("iter value: %d\n"), value});
            }
            auto *const CheckObjBlock = B.CreateBasicBlock("check.obj");

            B.CreateCondBr(B.IsNil(value), IsNotObjBlock, CheckObjBlock);

            B.SetInsertPoint(CheckObjBlock);
            B.CreateCondBr(B.IsObj(value), IsObjBlock, IsNotObjBlock);
            B.SetInsertPoint(IsObjBlock);
            if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("calling function %p(%d, %p)\n"), function, value, B.AsObj(value)});
            }
            B.CreateCall(
                FunctionType::get(B.getVoidTy(), {B.getPtrTy()}, false),
                function,
                {(B.AsObj(value))}
            );
            B.CreateBr(EndBlock);

            if constexpr (DEBUG_LOG_GC) {
                B.SetInsertPoint(IsNotObjBlock);
                B.PrintF({B.CreateGlobalCachedString("not object %p\n"), ptr});
                B.CreateBr(EndBlock);
            }

            B.SetInsertPoint(EndBlock);

            B.CreateBr(ForInc);

            B.SetInsertPoint(ForInc);
            B.CreateStore(B.CreateNSWAdd(B.CreateLoad(B.getInt32Ty(), i), B.getInt32(1)), i);
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

    void PushGlobal(LoxBuilder &Builder, GlobalVariable *global, const std::string_view name) {
        Builder.getModule().getGlobalsStack().CreatePush(Builder.getModule(), Builder, global);
    }

    void IterateGlobals(LoxBuilder &Builder, Function *FunctionPointer) {
        if constexpr (DEBUG_LOG_GC) {
            Builder.PrintString("--iterate globals--");
        }
        Builder.getModule().getGlobalsStack().CreateIterateObjectValues(Builder, FunctionPointer);
    }

    void IterateLocals(LoxBuilder &Builder, Function *FunctionPointer) {
        if constexpr (DEBUG_LOG_GC) {
            auto *const sp = Builder.getModule().getLocalsStack().CreateGetCount(Builder);
            Builder.PrintF({Builder.CreateGlobalCachedString("--iterate locals (%d)--\n"), sp});
        }

        Builder.getModule().getLocalsStack().CreateIterateObjectValues(Builder, FunctionPointer);
    }
}// namespace lox