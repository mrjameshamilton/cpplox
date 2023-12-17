#include "Compiler.h"
#include "Value.h"
#include <llvm/IR/Value.h>

namespace lox {

#define LOAD_STRING_LENGTH(PTR) \
    Builder->CreateLoad(Builder->getInt32Ty(), Builder->CreateStructGEP(StringStructType, Builder->CreateLoad(Builder->getPtrTy(), PTR), 2), "length")
#define LOAD_STRING_STRING(PTR) \
    Builder->CreateLoad(Builder->getPtrTy(), Builder->CreateStructGEP(StringStructType, Builder->CreateLoad(Builder->getPtrTy(), PTR), 1), "string")

    Value *Compiler::StrEquals(Value *a, Value *b) const {
        static auto StrEqualsFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder->getInt1Ty(),
                    {Builder->getInt64Ty(), Builder->getInt64Ty()},
                    false
                ),
                Function::InternalLinkage,
                "strEquals",
                *LoxModule
            );

            const auto InsertPoint = Builder->GetInsertBlock();
            const auto EntryBasicBlock = BasicBlock::Create(*Context, "entry", F);
            Builder->SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();

            const auto p0str = CreateEntryBlockAlloca(F, Builder->getPtrTy(), "p0str");
            Builder->CreateStore(AsString(arguments), p0str);
            const auto p1str = CreateEntryBlockAlloca(F, Builder->getPtrTy(), "p1str");
            Builder->CreateStore(AsString(arguments + 1), p1str);

            const auto String0Length = LOAD_STRING_LENGTH(p0str);
            const auto String1Length = LOAD_STRING_LENGTH(p1str);
            const auto String0String = LOAD_STRING_STRING(p0str);
            const auto String1String = LOAD_STRING_STRING(p1str);

            const auto NotEqualBlock = BasicBlock::Create(*Context, "not.equal", F);
            const auto CheckContents = BasicBlock::Create(*Context, "check.contents", F);
            Builder->CreateCondBr(Builder->CreateICmpNE(String0Length, String1Length), NotEqualBlock, CheckContents);

            Builder->SetInsertPoint(CheckContents);

            static const auto MemCmp = LoxModule->getOrInsertFunction(
                "memcmp",
                FunctionType::get(Builder->getInt32Ty(), {Builder->getPtrTy(), Builder->getPtrTy(), Builder->getInt64Ty()}, false)
            );

            Builder->CreateRet(
                Builder->CreateICmpEQ(
                    Builder->CreateCall(MemCmp, {String0String, String1String, String0Length}),
                    Builder->getInt32(0)
                )
            );

            Builder->SetInsertPoint(NotEqualBlock);
            Builder->CreateRet(Builder->getFalse());

            Builder->SetInsertPoint(InsertPoint);

            return F;
        }());

        return Builder->CreateCall(StrEqualsFunction, {a, b});
    }

    Value *Compiler::Concat(Value *a, Value *b) const {
        static auto ConcatFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder->getInt64Ty(),
                    {Builder->getInt64Ty(), Builder->getInt64Ty()},
                    false
                ),
                Function::InternalLinkage,
                "concat",
                *LoxModule
            );

            const auto InsertPoint = Builder->GetInsertBlock();
            const auto EntryBasicBlock = BasicBlock::Create(*Context, "entry", F);
            Builder->SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();

            const auto p0str = CreateEntryBlockAlloca(F, Builder->getPtrTy(), "p0str");
            Builder->CreateStore(AsString(arguments), p0str);
            const auto p1str = CreateEntryBlockAlloca(F, Builder->getPtrTy(), "p1str");
            Builder->CreateStore(AsString(arguments + 1), p1str);

            const auto String0Length = LOAD_STRING_LENGTH(p0str);
            const auto String1Length = LOAD_STRING_LENGTH(p1str);
            const auto String0String = LOAD_STRING_STRING(p0str);
            const auto String1String = LOAD_STRING_STRING(p1str);

            const auto NewLength = Builder->CreateNSWAdd(
                String0Length,
                String1Length,
                "NewLength"
            );

            const auto StringMalloc = Builder->CreateMalloc(
                Builder->getInt32Ty(),
                Builder->getInt8PtrTy(),
                Builder->CreateSExt(Builder->CreateNSWAdd(Builder->getInt32(1), NewLength), Builder->getInt64Ty()),
                nullptr
            );

            const auto StringTemp = CreateEntryBlockAlloca(F, Builder->getPtrTy(), "StringTemp");
            Builder->CreateStore(StringMalloc, StringTemp);

            Builder->CreateMemCpy(
                Builder->CreateLoad(Builder->getPtrTy(), StringTemp),
                Align(1),
                String0String,
                Align(1),
                Builder->CreateSExt(String0Length, Builder->getInt64Ty())
            );

            Builder->CreateMemCpy(
                Builder->CreateInBoundsGEP(
                    Builder->getInt8Ty(),
                    Builder->CreateLoad(Builder->getPtrTy(), StringTemp),
                    {Builder->CreateSExt(String0Length, Builder->getInt64Ty())}
                ),
                Align(1),
                String1String,
                Align(1),
                Builder->CreateSExt(
                    Builder->CreateNSWAdd(
                        String1Length,
                        Builder->getInt32(1)
                    ),
                    Builder->getInt64Ty()
                )
            );

            const auto NewObj = CreateEntryBlockAlloca(F, ObjStructType, "NewString");


            Type *IntPtrTy = IntegerType::getInt32Ty(*Context);
            // The malloc size IR that is generated with getSizeOf uses a hack described here:
            // https://mukulrathi.com/create-your-own-programming-language/concurrency-runtime-language-tutorial/#malloc
            Constant *allocsize = ConstantExpr::getSizeOf(StringStructType);
            allocsize = ConstantExpr::getTruncOrBitCast(allocsize, IntPtrTy);

            const auto NewObjMalloc = Builder->CreateMalloc(
                IntPtrTy,
                StringStructType,
                allocsize,
                nullptr
            );

            Builder->CreateStore(
                Builder->getInt8(static_cast<uint8_t>(ObjType::STRING)),
                Builder->CreateStructGEP(ObjStructType, NewObjMalloc, 0)
            );
            Builder->CreateStore(NewObjMalloc, NewObj);

            const auto NewString = Builder->CreateBitCast(NewObj, StringStructType->getPointerTo());
            Builder->CreateStore(
                Builder->CreateLoad(Builder->getPtrTy(), StringTemp),
                Builder->CreateStructGEP(StringStructType, Builder->CreateLoad(Builder->getPtrTy(), NewString), 1)
            );
            Builder->CreateStore(
                NewLength,
                Builder->CreateStructGEP(StringStructType, Builder->CreateLoad(Builder->getPtrTy(), NewString), 2)
            );
            Builder->CreateRet(
                ObjVal(
                    Builder->CreatePtrToInt(
                        Builder->CreateLoad(Builder->getPtrTy(), NewString),
                        Builder->getInt64Ty()
                    )
                )
            );

            Builder->SetInsertPoint(InsertPoint);

            return F;
        }());

        return Builder->CreateCall(ConcatFunction, {a, b});
    }
#undef LOAD_STRING_LENGTH
#undef LOAD_STRING_STRING

}// namespace lox