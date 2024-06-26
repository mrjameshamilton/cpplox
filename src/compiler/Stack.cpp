#include "Stack.h"

#include "../Debug.h"
#include "GC.h"
#include "Memory.h"

#include <llvm/Transforms/Utils/BasicBlockUtils.h>

namespace lox {

    static void ensureCapacity(LoxModule &M, IRBuilder<> &Builder, Value *stack, StructType *type, Value *size) {
        assert(size->getType() == Builder.getInt32Ty());

        static auto *EnsureCapacityFunction([&Builder, &M] {
            auto *const F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy(), Builder.getPtrTy(), Builder.getPtrTy(), Builder.getInt32Ty()}, false
                ),
                Function::InternalLinkage, "$stackEnsureCapacity", M
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

            if (DEBUG_STACK) { B.PrintF({B.CreateGlobalCachedString("ensure cap stack with %d\n"), size}); }

            auto *const stack = B.CreateLoad(B.getPtrTy(), $stack);
            auto *const count = B.CreateLoad(B.getInt32Ty(), $count);
            auto *const capacity = B.CreateLoad(B.getInt32Ty(), $capacity);

            auto *const GrowBlock = B.CreateBasicBlock("grow");
            auto *const EndBlock = B.CreateBasicBlock("end");
            auto *const DontGrowBlock = DEBUG_STACK ? B.CreateBasicBlock("dont.grow") : EndBlock;

            B.CreateCondBr(B.CreateICmpSLT(capacity, size), GrowBlock, DontGrowBlock);

            if constexpr (DEBUG_STACK) {
                B.SetInsertPoint(DontGrowBlock);
                B.PrintF(
                    {B.CreateGlobalCachedString("don't grow realloc stack: %p (count: %d, capacity: %d)\n"), stack,
                     count, capacity}
                );
                B.CreateBr(EndBlock);
            }

            B.SetInsertPoint(GrowBlock);
            auto *const newCapacity = B.CreateSelect(
                B.CreateICmpSLT(size, B.getInt32(8)), B.getInt32(8),
                B.CreateMul(size, B.getInt32(GROWTH_FACTOR), "newcapacity", true, true)
            );
            B.CreateStore(newCapacity, $capacity);

            auto *const newSize = B.getSizeOf(B.getPtrTy(), newCapacity);

            if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("grow realloc stack: %p with new size %d\n"), stack, newSize});
            }

            auto *const result = B.CreateRealloc(stack, newSize, "stack");

            auto *const IsNullBlock = B.CreateBasicBlock("error.realloc");
            auto *const OkBlock = B.CreateBasicBlock("ok.realloc");

            B.CreateCondBr(B.CreateIsNull(result), IsNullBlock, OkBlock);
            B.SetInsertPoint(IsNullBlock);
            {
                B.RuntimeError(
                    B.getInt32(0), "Could not reallocate %d for %p\n", {newSize, stack},
                    B.CreateGlobalCachedString("ensureCapacity")
                );
            }
            B.SetInsertPoint(OkBlock);
            {
                if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
                    B.PrintF(
                        {B.CreateGlobalCachedString(
                             "realloc stack: %p; %p -> %p (count: %d, capacity: %d, newCapacity: %d)\n"
                         ),
                         $stack, stack, result, count, capacity, newCapacity}
                    );
                }
                B.CreateStore(result, $stack);

                // Initialize the new entries with nullptr.
                auto *const i = CreateEntryBlockAlloca(F, B.getInt32Ty(), "i");

                B.CreateStore(count, i);

                auto *const ForCond = B.CreateBasicBlock("for.cond");
                auto *const ForBody = B.CreateBasicBlock("for.body");
                auto *const ForInc = B.CreateBasicBlock("for.inc");
                auto *const ForEnd = B.CreateBasicBlock("for.end");

                B.CreateBr(ForCond);
                B.SetInsertPoint(ForCond);
                {
                    B.CreateCondBr(B.CreateICmpSLT(B.CreateLoad(B.getInt32Ty(), i), newCapacity), ForBody, ForEnd);
                    B.SetInsertPoint(ForBody);

                    auto *const addr = B.CreateInBoundsGEP(B.getPtrTy(), result, B.CreateLoad(B.getInt32Ty(), i));

                    if constexpr (DEBUG_STACK) {
                        B.PrintF(
                            {B.CreateGlobalCachedString("set value: (result: %p) %d %p -> %p\n"), result,
                             B.CreateLoad(B.getInt32Ty(), i), B.getNullPtr(), addr}
                        );
                    }

                    B.CreateStore(B.getNullPtr(), addr);

                    B.CreateBr(ForInc);
                    B.SetInsertPoint(ForInc);
                    {
                        B.CreateStore(
                            B.CreateAdd(B.CreateLoad(B.getInt32Ty(), i), B.getInt32(1), "i+1", true, true), i
                        );
                        B.CreateBr(ForCond);
                    }
                }
                B.SetInsertPoint(ForEnd);
                { B.CreateBr(EndBlock); }
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

    void GlobalStack::CreateSet(LoxBuilder &B, Value *index, Value *value) const {
        auto *const $stack = B.CreateStructGEP(StackStruct, stack, 0);
        auto *const stack = B.CreateLoad(B.getPtrTy(), $stack);
        auto *const addr = B.CreateInBoundsGEP(B.getPtrTy(), stack, index);

        if constexpr (DEBUG_STACK) {
            B.PrintF({B.CreateGlobalCachedString("set value: %d %p -> %p\n"), index, value, addr});
        }

        B.CreateStore(value, addr);
    }

    // Create N new slots on the stack
    // The new slots will be initialized to nullptr only if the stack storage
    // needs to be reallocated.
    void GlobalStack::CreatePushN(LoxModule &M, IRBuilder<> &Builder, Value *N) const {
        auto *const count = CreateGetCount(Builder);
        auto *const size = Builder.CreateAdd(N, count, "newCount", true, true);
        ensureCapacity(M, Builder, stack, StackStruct, size);
        Builder.CreateStore(size, Builder.CreateStructGEP(StackStruct, stack, 1));
    }


    void GlobalStack::CreatePush(LoxModule &M, IRBuilder<> &Builder, Value *Object) const {
        static auto *PushFunction([&Builder, &M, this] {
            auto *const F = Function::Create(
                FunctionType::get(Builder.getVoidTy(), {Builder.getPtrTy(), Builder.getPtrTy()}, false),
                Function::InternalLinkage, "$stackPush", M
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

            ensureCapacity(M, B, stackGlobal, StackStruct, B.CreateAdd(B.getInt32(1), count, "count+1", true, true));

            auto *const ptr = B.CreateLoad(B.getPtrTy(), $stack);
            auto *const addr = B.CreateInBoundsGEP(B.getPtrTy(), ptr, count);
            if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("push (%p, %p) = %p\n"), ptr, addr, objPtr});
            }
            B.CreateStore(objPtr, addr);

            auto *const newCount = B.CreateAdd(B.getInt32(1), count, "newcount", true, true);
            B.CreateStore(newCount, $count);

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(PushFunction, {stack, Object});
    }

    void GlobalStack::CreatePopN(LoxBuilder &Builder, Value *N) const {
        static auto *PopFunction([&] {
            auto *const F = Function::Create(
                FunctionType::get(Builder.getVoidTy(), {Builder.getPtrTy(), Builder.getInt32Ty()}, false),
                Function::InternalLinkage, "$stackPopN", Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->args().begin();
            auto *const $count = B.CreateStructGEP(StackStruct, arguments, 1);
            auto *const N = arguments + 1;

            auto *const count = B.CreateLoad(B.getInt32Ty(), $count);

            B.CreateStore(B.CreateSub(count, N, "count", true, true), $count);

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(PopFunction, {stack, N});
    }

    void GlobalStack::CreatePopAll(LoxBuilder &Builder, Function *FunctionPointer) const {
        static auto *IterateFunction([&] {
            auto *const F = Function::Create(
                FunctionType::get(Builder.getVoidTy(), {Builder.getPtrTy(), Builder.getPtrTy()}, false),
                Function::InternalLinkage, "$stackPopAll", Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->args().begin();
            auto *const $stack = B.CreateStructGEP(StackStruct, arguments, 0);
            auto *const $count = B.CreateStructGEP(StackStruct, arguments, 1);
            auto *const function = arguments + 1;

            auto *const count = B.CreateLoad(B.getInt32Ty(), $count);

            if constexpr (DEBUG_LOG_GC) { B.PrintF({B.CreateGlobalCachedString("--iterate stack (%d)--\n"), count}); }

            auto *const WhileCond = B.CreateBasicBlock("while.cond");
            auto *const WhileBody = B.CreateBasicBlock("while.body");
            auto *const WhileEnd = B.CreateBasicBlock("while.end");

            B.CreateBr(WhileCond);
            B.SetInsertPoint(WhileCond);
            B.CreateCondBr(B.CreateICmpSGT(B.CreateLoad(B.getInt32Ty(), $count), B.getInt32(0)), WhileBody, WhileEnd);
            B.SetInsertPoint(WhileBody);
            {
                auto *const newCount =
                    B.CreateSub(B.CreateLoad(B.getInt32Ty(), $count), B.getInt32(1), "newCount", true, true);
                B.CreateStore(newCount, $count);
                auto *const addr = B.CreateInBoundsGEP(B.getPtrTy(), B.CreateLoad(B.getPtrTy(), $stack), newCount);
                auto *const ptr = B.CreateLoad(B.getPtrTy(), addr);

                B.CreateCall(FunctionType::get(B.getVoidTy(), {B.getPtrTy()}, false), function, {ptr});

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
                FunctionType::get(Builder.getVoidTy(), {Builder.getPtrTy(), Builder.getPtrTy()}, false),
                Function::InternalLinkage, "$iterateStack", Builder.getModule()
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
                B.PrintF(
                    {B.CreateGlobalCachedString("iter ptr %d: %p %p\n"), B.CreateLoad(B.getInt32Ty(), i), addr, ptr}
                );
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
                B.PrintF({B.CreateGlobalCachedString("calling function %p(%d, %p)\n"), function, value, value});
            }
            B.CreateCall(FunctionType::get(B.getVoidTy(), {B.getPtrTy()}, false), function, {(B.AsObj(value))});
            B.CreateBr(EndBlock);

            if constexpr (DEBUG_LOG_GC) {
                B.SetInsertPoint(IsNotObjBlock);
                B.PrintF({B.CreateGlobalCachedString("not object %p\n"), ptr});
                B.CreateBr(EndBlock);
            }

            B.SetInsertPoint(EndBlock);

            B.CreateBr(ForInc);

            B.SetInsertPoint(ForInc);
            B.CreateStore(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), i), B.getInt32(1), "i+1", true, true), i);
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
}// namespace lox