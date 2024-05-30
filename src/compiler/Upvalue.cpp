#include "Upvalue.h"
#include "../Debug.h"
#include "FunctionCompiler.h"

#define DEBUG false

namespace lox {
    Value *LoxBuilder::AllocateUpvalue(Value *value) {
        const auto ptr = AllocateObj(ObjType::UPVALUE);

        CreateStore(value, CreateObjStructGEP(ObjType::UPVALUE, ptr, 1));
        CreateStore(getNullPtr(), CreateObjStructGEP(ObjType::UPVALUE, ptr, 2));
        CreateStore(getNilVal(), CreateObjStructGEP(ObjType::UPVALUE, ptr, 3));

        return ptr;
    }

    Value *FunctionCompiler::captureLocal(Value *local) {
        static auto *CaptureLocalFunction([this] {
            auto *const F = Function::Create(
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

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->args().begin();

            auto *const local = arguments;

            auto *const openUpvalues = B.getModule().getOpenUpvalues();
            auto *const upvalue = CreateEntryBlockAlloca(B.getFunction(), B.getPtrTy(), "upvalue");
            B.CreateStore(B.CreateLoad(B.getPtrTy(), openUpvalues), upvalue);

            auto *const WhileCond = B.CreateBasicBlock("while.cond");
            auto *const WhileBody = B.CreateBasicBlock("while.body");
            auto *const WhileEnd = B.CreateBasicBlock("while.end");

            B.CreateBr(WhileCond);
            B.SetInsertPoint(WhileCond);
            B.CreateCondBr(B.CreateIsNotNull(B.CreateLoad(B.getPtrTy(), upvalue)), WhileBody, WhileEnd);
            B.SetInsertPoint(WhileBody);
            {
                auto *const IsSameBlock1 = B.CreateBasicBlock("IsSame1");
                auto *const INotsSameBlock1 = B.CreateBasicBlock("NotIsSame1");

                auto *const upvalueLocation = B.CreateLoad(
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
                {
                    B.CreateRet(B.CreateLoad(B.getPtrTy(), upvalue));
                }

                B.SetInsertPoint(INotsSameBlock1);
                {
                    LoadInst *ptr = B.CreateLoad(B.getPtrTy(), upvalue);

                    B.CreateStore(
                        B.CreateLoad(
                            B.getPtrTy(),
                            B.CreateObjStructGEP(ObjType::UPVALUE, ptr, 2, "next")
                        ),
                        upvalue
                    );
                    B.CreateBr(WhileCond);
                }
            }
            B.SetInsertPoint(WhileEnd);

            auto *const upvaluePtr = B.AllocateUpvalue(local);

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
        static auto *CloseUpvalueFunction([&Builder] {
            auto *const F = Function::Create(
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

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->args().begin();
            auto *const local = arguments;

            auto *const openUpvalues = B.getModule().getOpenUpvalues();
            auto *const upvalue = CreateEntryBlockAlloca(F, B.getPtrTy(), "upvalue");
            auto *const previous = CreateEntryBlockAlloca(F, B.getPtrTy(), "previous");

            if constexpr (DEBUG_UPVALUES) {
                B.PrintF({B.CreateGlobalCachedString("closing upvalue(%p)\n"), local});
                B.PrintF({B.CreateGlobalCachedString("openUpvalues = %p\n"), B.CreateLoad(B.getPtrTy(), openUpvalues)});
            }

            B.CreateStore(B.CreateLoad(B.getPtrTy(), openUpvalues), upvalue);
            B.CreateStore(B.getNullPtr(), previous);

            auto *const WhileCond = B.CreateBasicBlock("while.cond");
            auto *const WhileBody = B.CreateBasicBlock("while.body");
            auto *const WhileEnd = B.CreateBasicBlock("while.end");


            B.CreateBr(WhileCond);
            B.SetInsertPoint(WhileCond);
            B.CreateCondBr(B.CreateIsNotNull(B.CreateLoad(B.getPtrTy(), upvalue)), WhileBody, WhileEnd);
            B.SetInsertPoint(WhileBody);
            {
                auto *const FoundBlock = B.CreateBasicBlock("IsSame1");
                auto *const ContinueBlock = B.CreateBasicBlock("NotIsSame1");

                auto *const upvalueLocation = B.CreateLoad(
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

                auto *const foundUpvalue = B.CreateLoad(B.getPtrTy(), upvalue);
                auto *const closed = B.CreateObjStructGEP(ObjType::UPVALUE, foundUpvalue, 3, "closed");
                auto *const loc = B.CreateObjStructGEP(ObjType::UPVALUE, foundUpvalue, 1, "loc");

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
                auto *const IsFirstBlock = B.CreateBasicBlock("first");
                auto *const IsFirstElseBlock = B.CreateBasicBlock("first.else");
                auto *const EndIsFirstBlock = B.CreateBasicBlock("first.end");
                B.CreateCondBr(B.CreateIsNull(B.CreateLoad(B.getPtrTy(), previous)), IsFirstBlock, IsFirstElseBlock);
                B.SetInsertPoint(IsFirstBlock);
                {
                    // openupvalues = upvalue->next
                    B.CreateStore(B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::UPVALUE, B.CreateLoad(B.getPtrTy(), upvalue), 2, "next")), openUpvalues);

                    B.CreateBr(EndIsFirstBlock);
                }
                B.SetInsertPoint(IsFirstElseBlock);
                {
                    // previous->next = upvalue->next;
                    B.CreateStore(
                        B.CreateLoad(
                            B.getPtrTy(),
                            B.CreateObjStructGEP(ObjType::UPVALUE, B.CreateLoad(B.getPtrTy(), upvalue), 2, "next")
                        ),
                        B.CreateObjStructGEP(ObjType::UPVALUE, previous, 2, "next")
                    );

                    B.CreateBr(EndIsFirstBlock);
                }
                B.SetInsertPoint(EndIsFirstBlock);
                {
                    B.CreateStore(
                        B.CreateLoad(
                            B.getPtrTy(),
                            B.CreateObjStructGEP(ObjType::UPVALUE, B.CreateLoad(B.getPtrTy(), upvalue), 2, "next")
                        ),
                        upvalue
                    );

                    B.CreateBr(WhileCond);
                }
                B.SetInsertPoint(ContinueBlock);
                {
                    B.CreateStore(B.CreateLoad(B.getPtrTy(), upvalue), previous);
                    B.CreateStore(
                        B.CreateLoad(
                            B.getPtrTy(),
                            B.CreateObjStructGEP(ObjType::UPVALUE, B.CreateLoad(B.getPtrTy(), upvalue), 2, "next")
                        ),
                        upvalue
                    );

                    B.CreateBr(WhileCond);
                }
            }
            B.SetInsertPoint(WhileEnd);

            if constexpr (DEBUG_UPVALUES) {
                B.PrintF({B.CreateGlobalCachedString("openUpvalues end = %p\n"), B.CreateLoad(B.getPtrTy(), openUpvalues)});
            }

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(CloseUpvalueFunction, {local});
    }

    void IterateUpvalues(LoxBuilder &Builder, Function *FunctionPointer) {
        static auto *IterateUpvaluesFunction([&Builder] {
            auto *const F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$iterateUpvalues",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            if constexpr (DEBUG_LOG_GC) {
                B.PrintString("--iterate upvalues--");
            }

            auto *const openUpvalues = B.getModule().getOpenUpvalues();
            auto *const upvalue = CreateEntryBlockAlloca(B.getFunction(), B.getPtrTy(), "upvalue");
            B.CreateStore(B.CreateLoad(B.getPtrTy(), openUpvalues), upvalue);

            auto *const WhileCond = B.CreateBasicBlock("while.cond");
            auto *const WhileBody = B.CreateBasicBlock("while.body");
            auto *const WhileEnd = B.CreateBasicBlock("while.end");

            B.CreateBr(WhileCond);
            B.SetInsertPoint(WhileCond);
            B.CreateCondBr(B.CreateIsNotNull(B.CreateLoad(B.getPtrTy(), upvalue)), WhileBody, WhileEnd);
            B.SetInsertPoint(WhileBody);
            {
                auto *const ptr = B.CreateLoad(B.getPtrTy(), upvalue);
                B.CreateCall(
                    FunctionType::get(B.getVoidTy(), {B.getPtrTy()}, false),
                    F->arg_begin(),
                    {ptr}
                );

                B.CreateStore(
                    B.CreateLoad(
                        B.getPtrTy(),
                        B.CreateObjStructGEP(ObjType::UPVALUE, ptr, 2, "next")
                    ),
                    upvalue
                );
                B.CreateBr(WhileCond);
            }
            B.SetInsertPoint(WhileEnd);

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(IterateUpvaluesFunction, {FunctionPointer});
    }
}// namespace lox
