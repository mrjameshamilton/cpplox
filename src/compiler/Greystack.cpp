#include "Greystack.h"

#include "../Debug.h"
#include "Memory.h"

namespace lox {

    void PushGrey(LoxBuilder &Builder, Value *Object) {
        static auto PushFunction([&Builder] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$pushGrey",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();
            const auto objPtr = arguments;

            const auto $stack = B.getModule().getGrayStack();
            const auto $count = B.getModule().getGreyCount();
            const auto $capacity = B.getModule().getGreyCapacity();

            const auto stack = B.CreateLoad(B.getPtrTy(), $stack);
            const auto count = B.CreateLoad(B.getInt32Ty(), $count);
            const auto capacity = B.CreateLoad(B.getInt32Ty(), $capacity);

            const auto GrowBlock = B.CreateBasicBlock("grow");
            const auto EndBlock = B.CreateBasicBlock("end");

            if constexpr (DEBUG_LOG_GC) {
                //B.PrintF({B.CreateGlobalCachedString("grow? %d < %d + 1?\n"), capacity, count});
            }

            B.CreateCondBr(B.CreateICmpSLT(capacity, B.CreateAdd(B.getInt32(1), count)), GrowBlock, EndBlock);

            B.SetInsertPoint(GrowBlock);
            const auto newCapacity = B.CreateSelect(
                B.CreateICmpSLT(capacity, B.getInt32(8)),
                B.getInt32(8),
                B.CreateMul(capacity, B.getInt32(2))
            );
            B.CreateStore(newCapacity, $capacity);

            // TODO: handle null return
            const auto result = B.CreateRealloc(stack, B.getSizeOf(B.getPtrTy(), newCapacity));
            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("realloc grey: %p -> %p (count: %d, capacity: %d, newCapacity: %d)\n"), stack, result, count, capacity, newCapacity});
            }
            B.CreateStore(result, $stack);

            B.CreateBr(EndBlock);
            B.SetInsertPoint(EndBlock);

            auto ptr = B.CreateLoad(B.getPtrTy(), $stack);
            const auto addr = B.CreateGEP(B.getPtrTy(), ptr, count);
            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("pushgrey(%p, %p) = %p\n"), ptr, addr, objPtr});
            }
            B.CreateStore(objPtr, addr);

            const auto newCount = B.CreateAdd(B.getInt32(1), count);
            B.CreateStore(newCount, $count);

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(PushFunction, {Object});
    }

}// namespace lox