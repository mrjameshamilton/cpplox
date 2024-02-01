#include "Upvalue.h"
#include "FunctionCompiler.h"

#define DEBUG false

namespace lox {
    Value *LoxBuilder::AllocateUpvalue(Value *value) {
        const auto obj = AllocateObj(ObjType::UPVALUE);
        const auto ptr = CreateLoad(getPtrTy(), obj);
        CreateStore(
            value,
            CreateStructGEP(getModule().getStructType(ObjType::UPVALUE), ptr, 1)
        );
        CreateStore(
            Constant::getNullValue(PointerType::getUnqual(getContext())),
            CreateStructGEP(getModule().getStructType(ObjType::UPVALUE), ptr, 2)
        );
        CreateStore(
            getNilVal(),
            CreateStructGEP(getModule().getStructType(ObjType::UPVALUE), ptr, 3)
        );
        return ObjVal(
            CreatePtrToInt(
                ptr,
                getInt64Ty()
            )
        );
    }

    Value *FunctionCompiler::captureLocal(Value *local) {
        auto UpvalueStructType = Builder.getModule().getStructType(ObjType::UPVALUE);

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

        Builder.CreateBr(WhileCond);
        Builder.SetInsertPoint(WhileCond);
        Builder.CreateCondBr(Builder.CreateIsNotNull(Builder.CreateLoad(Builder.getPtrTy(), upvalue)), WhileBody, WhileEnd);
        Builder.SetInsertPoint(WhileBody);

        Builder.CreateStore(
            Builder.CreateLoad(
                Builder.getPtrTy(),
                Builder.CreateStructGEP(UpvalueStructType, Builder.CreateLoad(Builder.getPtrTy(), upvalue), 2, "next")
            ),
            next
        );

        const auto IsSameBlock = Builder.CreateBasicBlock("IsSame");
        const auto IsDifferentBlock = Builder.CreateBasicBlock("IsDifferent");
        const auto EndBlock = Builder.CreateBasicBlock("End");

        const auto upvalueLocation = Builder.CreateLoad(
            Builder.getPtrTy(),
            Builder.CreateStructGEP(UpvalueStructType, Builder.CreateLoad(Builder.getPtrTy(), upvalue), 1, "location")
        );

        Builder.CreateCondBr(
            Builder.CreateICmpEQ(
                Builder.getInt64(0), Builder.CreatePtrDiff(Builder.getPtrTy(), upvalueLocation, local)
            ),
            IsSameBlock,
            IsDifferentBlock
        );

        Builder.SetInsertPoint(IsDifferentBlock);
        Builder.CreateStore(
            Builder.CreateLoad(Builder.getPtrTy(), next),
            upvalue
        );

        Builder.CreateBr(WhileCond);

        Builder.SetInsertPoint(WhileEnd);

        const auto newUpvalue = Builder.AllocateUpvalue(local);
        const auto asUpvalue = Builder.AsUpvalue(newUpvalue);

        Builder.CreateStore(
            Builder.CreateLoad(Builder.getPtrTy(), openUpvalues),
            Builder.CreateStructGEP(UpvalueStructType, asUpvalue, 2, "next")
        );

        Builder.CreateStore(asUpvalue, openUpvalues);
        const auto Y = asUpvalue;
        Builder.CreateBr(EndBlock);

        Builder.SetInsertPoint(IsSameBlock);
        const auto X = Builder.CreateLoad(Builder.getPtrTy(), upvalue);
        Builder.CreateBr(EndBlock);

        Builder.SetInsertPoint(EndBlock);

        const auto Result = Builder.CreatePHI(Builder.getPtrTy(), 2);
        Result->addIncoming(X, IsSameBlock);
        Result->addIncoming(Y, WhileEnd);

        return Result;
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
        const auto X = Builder.CreateLoad(Builder.getPtrTy(), upvalue);
        const auto closed = Builder.CreateStructGEP(UpValueStructType, X, 3, "closed");
        const auto v = Builder.CreateStructGEP(UpValueStructType, X, 1, "loc");

#if DEBUG
        Builder.PrintString("Before");
        Builder.Print(Builder.ObjVal(
            Builder.CreatePtrToInt(
                Builder.CreateLoad(Builder.getPtrTy(), upvalue),
                Builder.getInt64Ty()
            )
        ));
#endif

        Builder.CreateStore(
            Builder.CreateLoad(Builder.getInt64Ty(), Builder.CreateLoad(Builder.getPtrTy(), v)),
            closed
        );

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