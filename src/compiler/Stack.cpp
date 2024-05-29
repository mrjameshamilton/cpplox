#include "Stack.h"

#include "../Debug.h"
#include "Memory.h"

namespace lox {

    static void ensureCapacity(LoxModule &M, IRBuilder<> &Builder, Value *stack, StructType *type, Value *size) {
        assert(size->getType() == Builder.getInt32Ty());

        static auto EnsureCapacityFunction([&Builder, &M] {
            const auto F = Function::Create(
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

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();
            const auto $stack = arguments + 0;
            const auto $count = arguments + 1;
            const auto $capacity = arguments + 2;
            const auto size = arguments + 3;

            if (DEBUG_STACK) {
                B.PrintF({B.CreateGlobalCachedString("ensure cap stack with %d\n"), size});
            }

            const auto stack = B.CreateLoad(B.getPtrTy(), $stack);
            const auto count = B.CreateLoad(B.getInt32Ty(), $count);
            const auto capacity = B.CreateLoad(B.getInt32Ty(), $capacity);
            if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("realloc stack: %p (count: %d, capacity: %d)\n"), stack, count, capacity});
            }
            const auto GrowBlock = B.CreateBasicBlock("grow");
            const auto EndBlock = B.CreateBasicBlock("end");

            B.CreateCondBr(B.CreateICmpSLT(capacity, size), GrowBlock, EndBlock);

            B.SetInsertPoint(GrowBlock);
            const auto newCapacity = B.CreateSelect(
                B.CreateICmpSLT(size, B.getInt32(8)),
                B.getInt32(8),
                B.CreateMul(size, B.getInt32(GROWTH_FACTOR))
            );
            B.CreateStore(newCapacity, $capacity);

            auto newSize = B.getSizeOf(B.getPtrTy(), newCapacity);

            if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("realloc stack: %p with new size %d\n"), stack, newSize});
            }

            const auto result = B.CreateRealloc(stack, newSize, "stack");

            const auto IsNullBlock = B.CreateBasicBlock("error.realloc");
            const auto OkBlock = B.CreateBasicBlock("ok.realloc");

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

        const auto $stack = Builder.CreateStructGEP(type, stack, 0);
        const auto $count = Builder.CreateStructGEP(type, stack, 1);
        const auto $capacity = Builder.CreateStructGEP(type, stack, 2);

        Builder.CreateCall(EnsureCapacityFunction, {$stack, $count, $capacity, size});
    }

    Value *GlobalStack::CreateGetCount(IRBuilder<> &B) const {
        return B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(StackStruct, stack, 1));
    }

    Value *GlobalStack::CreateGet(LoxBuilder &B, Value *index) const {
        const auto $stack = B.CreateStructGEP(StackStruct, stack, 0);
        const auto stack = B.CreateLoad(B.getPtrTy(), $stack);
        const auto addr = B.CreateGEP(ArrayType::get(B.getPtrTy(), 1), stack, {B.getInt32(0), index});

        return B.CreateLoad(B.getPtrTy(), addr);
    }

    void GlobalStack::CreateSet(LoxBuilder &B, Value *index, Value *value) const {
        const auto $stack = B.CreateStructGEP(StackStruct, stack, 0);
        const auto stack = B.CreateLoad(B.getPtrTy(), $stack);
        const auto addr = B.CreateGEP(ArrayType::get(B.getPtrTy(), 1), stack, {B.getInt32(0), index});

        if constexpr (DEBUG_STACK) {
            B.PrintF({B.CreateGlobalCachedString("set value: %d %p -> %p\n"), index, value, addr});
        }

        B.CreateStore(value, addr);
    }

    void GlobalStack::CreatePushN(LoxModule &M, IRBuilder<> &Builder, Value *Object, Value *N) const {
        static auto PushFunction([&Builder, &M, this] {
            const auto F = Function::Create(
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

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();
            const auto stackGlobal = arguments;
            const auto $stack = B.CreateStructGEP(StackStruct, stackGlobal, 0);
            const auto objPtr = arguments + 1;
            const auto N = arguments + 2;

            if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("push stack %p; ptr %p\n"), $stack, stackGlobal});
            }

            const auto i = CreateEntryBlockAlloca(F, B.getInt32Ty(), "i");

            B.CreateStore(B.getInt32(1), i);

            const auto ForCond = B.CreateBasicBlock("for.cond");
            const auto ForBody = B.CreateBasicBlock("for.body");
            const auto ForInc = B.CreateBasicBlock("for.inc");
            const auto ForEnd = B.CreateBasicBlock("for.end");

            B.CreateBr(ForCond);
            B.SetInsertPoint(ForCond);
            B.CreateCondBr(B.CreateICmpSLE(B.CreateLoad(B.getInt32Ty(), i), N), ForBody, ForEnd);
            B.SetInsertPoint(ForBody);

            CreatePush(M, B, objPtr);

            B.CreateBr(ForInc);
            B.SetInsertPoint(ForInc);
            B.CreateStore(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), i), B.getInt32(1)), i);
            B.CreateBr(ForCond);

            B.SetInsertPoint(ForEnd);
            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(PushFunction, {stack, Object, N});
    }

    void GlobalStack::CreatePush(LoxModule &M, IRBuilder<> &Builder, Value *Object) const {
        static auto PushFunction([&Builder, &M, this] {
            const auto F = Function::Create(
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

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto stackGlobal = F->args().begin();
            const auto $stack = B.CreateStructGEP(StackStruct, stackGlobal, 0);
            const auto $count = B.CreateStructGEP(StackStruct, stackGlobal, 1);
            const auto objPtr = stackGlobal + 1;

            if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("push stack %p; ptr %p\n"), $stack, stackGlobal});
            }

            const auto count = B.CreateLoad(B.getInt32Ty(), $count);

            ensureCapacity(M, B, stackGlobal, StackStruct, B.CreateAdd(B.getInt32(1), count));

            const auto ptr = B.CreateLoad(B.getPtrTy(), $stack);
            const auto addr = B.CreateGEP(B.getPtrTy(), ptr, count);
            if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("push (%p, %p) = %p\n"), ptr, addr, objPtr});
            }
            B.CreateStore(objPtr, addr);

            const auto newCount = B.CreateAdd(B.getInt32(1), count);
            B.CreateStore(newCount, $count);

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(PushFunction, {stack, Object});
    }

    void GlobalStack::CreatePopN(LoxBuilder &Builder, Value *N) const {
        static auto PopFunction([&] {
            const auto F = Function::Create(
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

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();
            const auto $count = B.CreateStructGEP(StackStruct, arguments, 1);
            const auto N = arguments + 1;

            const auto count = B.CreateLoad(B.getInt32Ty(), $count);

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

    void GlobalStack::CreateIterateObjectValues(LoxBuilder &Builder, Function *FunctionPointer) const {
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

            if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
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

            if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("iter ptr: %p %p\n"), addr, ptr});
            }

            const auto IsObjBlock = B.CreateBasicBlock("is.obj");
            const auto EndBlock = B.CreateBasicBlock("end.obj");
            const auto IsNotNull = B.CreateBasicBlock("is.notnull");
            const auto IsNotObjBlock = DEBUG_LOG_GC ? B.CreateBasicBlock("is.not.obj") : EndBlock;

            B.CreateCondBr(B.CreateIsNull(ptr), IsNotObjBlock, IsNotNull);
            B.SetInsertPoint(IsNotNull);

            const auto value = B.CreateLoad(B.getInt64Ty(), ptr);

            if constexpr (DEBUG_STACK || DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("iter value: %d\n"), value});
            }
            const auto CheckObjBlock = B.CreateBasicBlock("check.obj");

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

    void PushGlobal(LoxBuilder &Builder, GlobalVariable *global, const std::string_view name) {
        Builder.getModule().getGlobalsStack()->CreatePush(Builder.getModule(), Builder, global);
    }

    void IterateGlobals(LoxBuilder &Builder, Function *FunctionPointer) {
        if constexpr (DEBUG_LOG_GC) {
            Builder.PrintString("--iterate globals--");
        }
        Builder.getModule().getGlobalsStack()->CreateIterateObjectValues(Builder, FunctionPointer);
    }

    void IterateLocals(LoxBuilder &Builder, Function *FunctionPointer) {
        if constexpr (DEBUG_LOG_GC) {
            const auto sp = Builder.getModule().getLocalsStack()->CreateGetCount(Builder);
            Builder.PrintF({Builder.CreateGlobalCachedString("--iterate locals (%d)--\n"), sp});
        }

        Builder.getModule().getLocalsStack()->CreateIterateObjectValues(Builder, FunctionPointer);
    }
}// namespace lox