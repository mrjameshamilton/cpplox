#include "Upvalue.h"
#include "FunctionCompiler.h"

#define DEBUG false

namespace lox {
    Value *LoxBuilder::AllocateUpvalue(Value *value) {
        const auto ptr = AllocateObj(ObjType::UPVALUE);

        CreateStore(value,CreateObjStructGEP(ObjType::UPVALUE, ptr, 1));
        CreateStore(getNullPtr(),CreateObjStructGEP(ObjType::UPVALUE, ptr, 2));
        CreateStore(getNilVal(),CreateObjStructGEP(ObjType::UPVALUE, ptr, 3));

        return ptr;
    }

    Value *FunctionCompiler::captureLocal(Value *local) {
        static auto CaptureLocalFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder.getPtrTy(),
                    {Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$captureLocal",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();

            const auto local = arguments;

            const auto openUpvalues = B.getModule().getOpenUpvalues();
            const auto upvalue = CreateEntryBlockAlloca(B.getFunction(), B.getPtrTy(), "upvalue");
            B.CreateStore(B.CreateLoad(B.getPtrTy(), openUpvalues), upvalue);

            const auto WhileCond = B.CreateBasicBlock("while.cond");
            const auto WhileBody = B.CreateBasicBlock("while.body");
            const auto WhileEnd = B.CreateBasicBlock("while.end");

            B.CreateBr(WhileCond);
            B.SetInsertPoint(WhileCond);
            B.CreateCondBr(B.CreateIsNotNull(B.CreateLoad(B.getPtrTy(), upvalue)), WhileBody, WhileEnd);
            B.SetInsertPoint(WhileBody);

            const auto IsSameBlock1 = B.CreateBasicBlock("IsSame1");
            const auto INotsSameBlock1 = B.CreateBasicBlock("NotIsSame1");

            const auto upvalueLocation = B.CreateLoad(
                B.getPtrTy(),
                B.CreateObjStructGEP(ObjType::UPVALUE, B.CreateLoad(B.getPtrTy(), upvalue), 1, "location")
            );

            B.CreateCondBr(
                B.CreateICmpEQ(
                    B.getInt64(0), B.CreatePtrDiff(B.getPtrTy(), upvalueLocation, local)
                ),
                IsSameBlock1,
                INotsSameBlock1
            );

            B.SetInsertPoint(IsSameBlock1);

            B.CreateRet(B.CreateLoad(B.getPtrTy(), upvalue));

            B.SetInsertPoint(INotsSameBlock1);

            LoadInst *ptr = B.CreateLoad(B.getPtrTy(), upvalue);

            B.CreateStore(
                B.CreateLoad(
                    B.getPtrTy(),
                    B.CreateObjStructGEP(ObjType::UPVALUE, ptr, 2, "next")
                ),
                upvalue
            );
            B.CreateBr(WhileCond);

            B.SetInsertPoint(WhileEnd);

            const auto upvaluePtr = B.AllocateUpvalue(local);

            B.CreateStore(
                B.CreateLoad(B.getPtrTy(), openUpvalues),
                B.CreateObjStructGEP(ObjType::UPVALUE, upvaluePtr, 2, "next")
            );

            B.CreateStore(
                upvaluePtr,
                openUpvalues
            );

            B.CreateRet(upvaluePtr);

            return F;
        }());

        return Builder.CreateCall(CaptureLocalFunction, {local});
    }

    void closeUpvalues(LoxBuilder &Builder, Value *local) {
        static auto CloseUpvalueFunction([&Builder] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$closeUpvalue",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();
            const auto local = arguments;

            const auto openUpvalues = B.getModule().getOpenUpvalues();
            const auto upvalue = CreateEntryBlockAlloca(B.getFunction(), B.getPtrTy(), "upvalue");
            const auto previous = CreateEntryBlockAlloca(B.getFunction(), B.getPtrTy(), "previous");

            if constexpr (DEBUG_UPVALUES) {
                B.PrintF({B.CreateGlobalCachedString("closing upvalue(%p)\n"), local});
                B.PrintF({B.CreateGlobalCachedString("openUpvalues = %p\n"), B.CreateLoad(B.getPtrTy(), openUpvalues)});
            }

            B.CreateStore(B.CreateLoad(B.getPtrTy(), openUpvalues), upvalue);
            B.CreateStore(B.getNullPtr(), previous);

            const auto WhileCond = B.CreateBasicBlock("while.cond");
            const auto WhileBody = B.CreateBasicBlock("while.body");
            const auto WhileEnd = B.CreateBasicBlock("while.end");


            B.CreateBr(WhileCond);
            B.SetInsertPoint(WhileCond);
            B.CreateCondBr(B.CreateIsNotNull(B.CreateLoad(B.getPtrTy(), upvalue)), WhileBody, WhileEnd);
            B.SetInsertPoint(WhileBody);

            const auto FoundBlock = B.CreateBasicBlock("IsSame1");
            const auto ContinueBlock = B.CreateBasicBlock("NotIsSame1");

            const auto upvalueLocation = B.CreateLoad(
                B.getPtrTy(),
                B.CreateObjStructGEP(ObjType::UPVALUE, B.CreateLoad(B.getPtrTy(), upvalue), 1, "location")
            );

            if constexpr (DEBUG_UPVALUES) {
                B.PrintF({B.CreateGlobalCachedString("current = ")});
                B.Print(B.ObjVal(B.CreateLoad(B.getPtrTy(), upvalue)));
                B.PrintF({B.CreateGlobalCachedString("%p == %p?\n"), upvalueLocation, local});
            }

            B.CreateCondBr(
                B.CreateICmpEQ(
                    B.getInt64(0), B.CreatePtrDiff(B.getPtrTy(), upvalueLocation, local)
                ),
                FoundBlock,
                ContinueBlock
            );

            B.SetInsertPoint(FoundBlock);

            const auto foundUpvalue = B.CreateLoad(B.getPtrTy(), upvalue);
            const auto closed = B.CreateObjStructGEP(ObjType::UPVALUE, foundUpvalue, 3, "closed");
            const auto loc = B.CreateObjStructGEP(ObjType::UPVALUE, foundUpvalue, 1, "loc");

            if constexpr (DEBUG_UPVALUES) {
                B.PrintF({B.CreateGlobalCachedString("before = ")});
                B.Print(B.ObjVal(B.CreateLoad(B.getPtrTy(), upvalue)));
            }

            // Close the upvalue by copying the value from the current location.
            B.CreateStore(
                B.CreateLoad(Builder.getInt64Ty(), B.CreateLoad(Builder.getPtrTy(), loc)),
                closed
            );

            // And then setting the new location to the "closed" field.
            B.CreateStore(
                closed,
                B.CreateObjStructGEP(ObjType::UPVALUE, B.CreateLoad(Builder.getPtrTy(), upvalue), 1, "loc")
            );

            if constexpr (DEBUG_UPVALUES) {
                // The upvalue should now be closed: the location value
                // should point to the value inside its own struct.
                B.PrintF({B.CreateGlobalCachedString("after = ")});
                B.Print(B.ObjVal(B.CreateLoad(B.getPtrTy(), upvalue)));
            }

            // Remove the closed upvalue from the open upvalues list.
            const auto IsFirstBlock = B.CreateBasicBlock("first");
            const auto IsFirstElseBlock = B.CreateBasicBlock("first.else");
            const auto EndIsFirstBlock = B.CreateBasicBlock("first.end");
            B.CreateCondBr(B.CreateIsNull(B.CreateLoad(B.getPtrTy(), previous)), IsFirstBlock, IsFirstElseBlock);
            B.SetInsertPoint(IsFirstBlock);

            // openupvalues = upvalue->next
            B.CreateStore(B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::UPVALUE, B.CreateLoad(B.getPtrTy(), upvalue), 2, "next")), openUpvalues);

            B.CreateBr(EndIsFirstBlock);
            B.SetInsertPoint(IsFirstElseBlock);

            // previous->next = upvalue->next;
            B.CreateStore(
                B.CreateLoad(
                    B.getPtrTy(),
                    B.CreateObjStructGEP(ObjType::UPVALUE, B.CreateLoad(B.getPtrTy(), upvalue), 2, "next")
                ),
                B.CreateObjStructGEP(ObjType::UPVALUE, previous, 2, "next")
            );

            B.CreateBr(EndIsFirstBlock);
            B.SetInsertPoint(EndIsFirstBlock);

            B.CreateStore(
                B.CreateLoad(
                    B.getPtrTy(),
                    B.CreateObjStructGEP(ObjType::UPVALUE, B.CreateLoad(B.getPtrTy(), upvalue), 2, "next")
                ),
                upvalue
            );

            B.CreateBr(WhileCond);

            B.SetInsertPoint(ContinueBlock);

            B.CreateStore(B.CreateLoad(B.getPtrTy(), upvalue), previous);
            B.CreateStore(
                B.CreateLoad(
                    B.getPtrTy(),
                    B.CreateObjStructGEP(ObjType::UPVALUE, B.CreateLoad(B.getPtrTy(), upvalue), 2, "next")
                ),
                upvalue
            );

            B.CreateBr(WhileCond);

            B.SetInsertPoint(WhileEnd);

            if constexpr (DEBUG_UPVALUES) {
                B.PrintF({B.CreateGlobalCachedString("openUpvalues end = %p\n"), B.CreateLoad(B.getPtrTy(), openUpvalues)});
            }

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(CloseUpvalueFunction, {local});
    }

}// namespace lox
