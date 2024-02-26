#include "Upvalue.h"
#include "FunctionCompiler.h"

#define DEBUG false

namespace lox {
    Value *LoxBuilder::AllocateUpvalue(Value *value) {
        const auto ptr = AllocateObj(ObjType::UPVALUE);

        CreateStore(
            value,
            CreateObjStructGEP(ObjType::UPVALUE, ptr, 1)
        );
        CreateStore(
            getNullPtr(),
            CreateObjStructGEP(ObjType::UPVALUE, ptr, 2)
        );
        CreateStore(
            getNilVal(),
            CreateObjStructGEP(ObjType::UPVALUE, ptr, 3)
        );
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
            //B.PrintF({B.CreateGlobalCachedString("captureLocal(%p), openupvalues = %p\n"), local, openUpvalues});
            const auto upvalue = CreateEntryBlockAlloca(B.getFunction(), B.getPtrTy(), "upvalue");
            //const auto next = CreateEntryBlockAlloca(B.getFunction(), B.getPtrTy(), "next");
            B.CreateStore(B.CreateLoad(B.getPtrTy(), openUpvalues), upvalue);

            const auto WhileCond = B.CreateBasicBlock("while.cond");
            const auto WhileBody = B.CreateBasicBlock("while.body");
            const auto WhileEnd = B.CreateBasicBlock("while.end");

            //B.PrintF({B.CreateGlobalCachedString("%p?\n"), B.CreateLoad(B.getPtrTy(), upvalue)});

            B.CreateBr(WhileCond);
            B.SetInsertPoint(WhileCond);
            B.CreateCondBr(B.CreateIsNotNull(B.CreateLoad(B.getPtrTy(), upvalue)), WhileBody, WhileEnd);
            B.SetInsertPoint(WhileBody);
            //B.PrintString(Twine("WhileBody"));

            const auto IsSameBlock1 = B.CreateBasicBlock("IsSame1");
            const auto INotsSameBlock1 = B.CreateBasicBlock("NotIsSame1");

            const auto upvalueLocation = B.CreateLoad(
                B.getPtrTy(),
                B.CreateObjStructGEP(ObjType::UPVALUE, B.CreateLoad(B.getPtrTy(), upvalue), 1, "location")
            );

            //B.PrintF({B.CreateGlobalCachedString("%p == %p?\n"), upvalueLocation, local});
            B.CreateCondBr(
                B.CreateICmpEQ(
                    B.getInt64(0), B.CreatePtrDiff(B.getPtrTy(), upvalueLocation, local)
                ),
                IsSameBlock1,
                INotsSameBlock1
            );

            B.SetInsertPoint(IsSameBlock1);
            //B.PrintF({B.CreateGlobalCachedString("Returning same: %p = "), B.CreateLoad(B.getPtrTy(), upvalue)});
            //B.Print(B.ObjVal(B.CreateLoad(B.getPtrTy(), upvalue)));

            B.CreateRet(B.CreateLoad(B.getPtrTy(), upvalue));

            B.SetInsertPoint(INotsSameBlock1);

            LoadInst *ptr = B.CreateLoad(B.getPtrTy(), upvalue);
            //B.PrintString(Twine("WhileBody2"));
            //B.Print(B.ObjVal(ptr));

            B.CreateStore(
                B.CreateLoad(
                    B.getPtrTy(),
                    B.CreateObjStructGEP(ObjType::UPVALUE, ptr, 2, "next")
                ),
                upvalue
            );
            B.CreateBr(WhileCond);

            B.SetInsertPoint(WhileEnd);

            //B.PrintString(Twine("WhileEnd"));
            const auto upvaluePtr = B.AllocateUpvalue(local);

            B.CreateStore(
                B.CreateLoad(B.getPtrTy(), openUpvalues),
                B.CreateObjStructGEP(ObjType::UPVALUE, upvaluePtr, 2, "next")
            );

            B.CreateStore(
                upvaluePtr,
                openUpvalues
            );

            //B.PrintString(Twine("returning:"));
            //B.Print(B.ObjVal(upvaluePtr));
            // B.SetInsertPoint(EndBlock);

            B.CreateRet(upvaluePtr);

            return F;
        }());

        return Builder.CreateCall(CaptureLocalFunction, {local});
    }

    void closeUpvalues(LoxBuilder &Builder, Value *local) {
        const auto UpValueStructType = Builder.getModule().getStructType(ObjType::UPVALUE);

        const auto openUpvalues = Builder.getModule().getOpenUpvalues();
        const auto upvalue = CreateEntryBlockAlloca(Builder.getFunction(), Builder.getPtrTy(), "upvalue");
        const auto next = CreateEntryBlockAlloca(Builder.getFunction(), Builder.getPtrTy(), "next");
        Builder.CreateStore(
            Builder.CreateLoad(Builder.getPtrTy(), openUpvalues),
            upvalue
        );

        const auto WhileCond = Builder.CreateBasicBlock("while.cond");
        const auto WhileBody = Builder.CreateBasicBlock("while.body");
        const auto WhileEnd = Builder.CreateBasicBlock("while.end");

        // Find the upvalue that points to the local.
        Builder.CreateBr(WhileCond);
        Builder.SetInsertPoint(WhileCond);
        Builder.CreateCondBr(Builder.CreateIsNotNull(Builder.CreateLoad(Builder.getPtrTy(), upvalue)), WhileBody, WhileEnd);
        Builder.SetInsertPoint(WhileBody);

        Builder.CreateStore(
            Builder.CreateLoad(
                Builder.getPtrTy(),
                Builder.CreateStructGEP(UpValueStructType, Builder.CreateLoad(Builder.getPtrTy(), upvalue), 2, "next")
            ),
            next
        );

        const auto IsSameBlock = Builder.CreateBasicBlock("IsSame");
        const auto IsDifferentBlock = Builder.CreateBasicBlock("IsDifferent");
        const auto EndBlock = Builder.CreateBasicBlock("End");

        const auto upvalueLocation = Builder.CreateLoad(
            Builder.getPtrTy(),
            Builder.CreateStructGEP(UpValueStructType, Builder.CreateLoad(Builder.getPtrTy(), upvalue), 1, "location")
        );

        Builder.CreateCondBr(
            Builder.CreateICmpEQ(
                Builder.getInt64(0), Builder.CreatePtrDiff(Builder.getPtrTy(), upvalueLocation, local)
            ),
            IsSameBlock,
            IsDifferentBlock
        );

        Builder.SetInsertPoint(IsDifferentBlock);
        // Continue to next...
        Builder.CreateStore(
            Builder.CreateLoad(Builder.getPtrTy(), next),
            upvalue
        );

        Builder.CreateBr(WhileCond);

        Builder.SetInsertPoint(WhileEnd);

        Builder.CreateBr(EndBlock);

        Builder.SetInsertPoint(IsSameBlock);
        const auto foundUpvalue = Builder.CreateLoad(Builder.getPtrTy(), upvalue);
        const auto closed = Builder.CreateStructGEP(UpValueStructType, foundUpvalue, 3, "closed");
        const auto loc = Builder.CreateStructGEP(UpValueStructType, foundUpvalue, 1, "loc");

#if DEBUG
        Builder.PrintString("Before");
        Builder.Print(Builder.ObjVal(
            Builder.CreatePtrToInt(
                Builder.CreateLoad(Builder.getPtrTy(), upvalue),
                Builder.getInt64Ty()
            )
        ));
#endif

        // Close the upvalue by copying the value from the current location.
        Builder.CreateStore(
            Builder.CreateLoad(Builder.getInt64Ty(), Builder.CreateLoad(Builder.getPtrTy(), loc)),
            closed
        );

        // And then setting the new location to the "closed" field.
        Builder.CreateStore(
            closed,
            Builder.CreateStructGEP(UpValueStructType, Builder.CreateLoad(Builder.getPtrTy(), upvalue), 1, "loc")
        );

#if DEBUG
        Builder.PrintString("After");
        Builder.Print(Builder.ObjVal(
            Builder.CreatePtrToInt(
                Builder.CreateLoad(Builder.getPtrTy(), upvalue),
                Builder.getInt64Ty()
            )
        ));
#endif

        Builder.CreateBr(EndBlock);
        Builder.SetInsertPoint(EndBlock);
    }

}// namespace lox
