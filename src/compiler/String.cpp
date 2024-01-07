#include "ModuleCompiler.h"
#include "Value.h"
#include <llvm/IR/Value.h>

namespace lox {

#define LOAD_STRING_LENGTH(PTR) \
    B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getStructType(ObjType::STRING), B.CreateLoad(B.getPtrTy(), PTR), 2), "length")
#define LOAD_STRING_STRING(PTR) \
    B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getStructType(ObjType::STRING), B.CreateLoad(B.getPtrTy(), PTR), 1), "string")
#define STORE_STRING_LENGTH(PTR, LENGTH) \
    CreateStore(LENGTH, CreateStructGEP(getModule().getStructType(ObjType::STRING), CreateLoad(getPtrTy(), PTR), 2))
#define STORE_STRING_STRING(PTR, STR) \
    CreateStore(STR, CreateStructGEP(getModule().getStructType(ObjType::STRING), CreateLoad(getPtrTy(), PTR), 1))

    Value *LoxBuilder::AllocateString(Value *String, Value *Length, const std::string_view name) {
        const auto NewString = AllocateObj(ObjType::STRING, name);

        STORE_STRING_STRING(NewString, String);
        STORE_STRING_LENGTH(NewString, Length);

        return ObjVal(
            CreatePtrToInt(
                CreateLoad(getPtrTy(), NewString),
                getInt64Ty()
            )
        );
    }

    Value *LoxBuilder::StrEquals(Value *a, Value *b) {
        static auto StrEqualsFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    getInt1Ty(),
                    {getInt64Ty(), getInt64Ty()},
                    false
                ),
                Function::InternalLinkage,
                "strEquals",
                this->getModule()
            );

            LoxBuilder B(getContext(), getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();

            const auto p0str = CreateEntryBlockAlloca(F, B.getPtrTy(), "p0str");
            B.CreateStore(B.AsString(arguments), p0str);
            const auto p1str = CreateEntryBlockAlloca(F, B.getPtrTy(), "p1str");
            B.CreateStore(B.AsString(arguments + 1), p1str);

            const auto String0Length = LOAD_STRING_LENGTH(p0str);
            const auto String1Length = LOAD_STRING_LENGTH(p1str);
            const auto String0String = LOAD_STRING_STRING(p0str);
            const auto String1String = LOAD_STRING_STRING(p1str);

            const auto NotEqualBlock = B.CreateBasicBlock("not.equal");
            const auto CheckContents = B.CreateBasicBlock("check.contents");
            B.CreateCondBr(B.CreateICmpNE(String0Length, String1Length), NotEqualBlock, CheckContents);

            B.SetInsertPoint(CheckContents);

            static const auto MemCmp = this->getModule().getOrInsertFunction(
                "memcmp",
                FunctionType::get(B.getInt32Ty(), {B.getPtrTy(), B.getPtrTy(), B.getInt64Ty()}, false)
            );

            B.CreateRet(
                B.CreateICmpEQ(
                    B.CreateCall(MemCmp, {String0String, String1String, String0Length}),
                    B.getInt32(0)
                )
            );

            B.SetInsertPoint(NotEqualBlock);
            B.CreateRet(B.getFalse());

            return F;
        }());

        return CreateCall(StrEqualsFunction, {a, b});
    }

    Value *LoxBuilder::Concat(Value *a, Value *b) {
        static auto ConcatFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    getInt64Ty(),
                    {getInt64Ty(), getInt64Ty()},
                    false
                ),
                Function::InternalLinkage,
                "concat",
                this->getModule()
            );

            LoxBuilder B(getContext(), getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();

            const auto p0str = CreateEntryBlockAlloca(F, B.getPtrTy(), "p0str");
            B.CreateStore(B.AsString(arguments), p0str);
            const auto p1str = CreateEntryBlockAlloca(F, B.getPtrTy(), "p1str");
            B.CreateStore(B.AsString(arguments + 1), p1str);

            const auto String0Length = LOAD_STRING_LENGTH(p0str);
            const auto String1Length = LOAD_STRING_LENGTH(p1str);
            const auto String0String = LOAD_STRING_STRING(p0str);
            const auto String1String = LOAD_STRING_STRING(p1str);

            const auto NewLength = B.CreateNSWAdd(
                String0Length,
                String1Length,
                "NewLength"
            );

            const auto StringMalloc = B.CreateMalloc(
                B.getInt32Ty(),
                B.getPtrTy(),
                B.CreateSExt(B.CreateNSWAdd(B.getInt32(1), NewLength), B.getInt64Ty()),
                nullptr
            );

            const auto StringTemp = CreateEntryBlockAlloca(F, B.getPtrTy(), "StringTemp");
            B.CreateStore(StringMalloc, StringTemp);

            B.CreateMemCpy(
                B.CreateLoad(B.getPtrTy(), StringTemp),
                Align(1),
                String0String,
                Align(1),
                B.CreateSExt(String0Length, B.getInt64Ty())
            );

            B.CreateMemCpy(
                B.CreateInBoundsGEP(
                    B.getInt8Ty(),
                    B.CreateLoad(B.getPtrTy(), StringTemp),
                    {B.CreateSExt(String0Length, B.getInt64Ty())}
                ),
                Align(1),
                String1String,
                Align(1),
                B.CreateSExt(
                    B.CreateNSWAdd(
                        String1Length,
                        B.getInt32(1)
                    ),
                    B.getInt64Ty()
                )
            );

            const auto NewString = B.AllocateString(
                B.CreateLoad(B.getPtrTy(), StringTemp),
                NewLength,
                "NewString"
            );

            B.CreateRet(NewString);

            return F;
        }());

        return CreateCall(ConcatFunction, {a, b});
    }
#undef LOAD_STRING_LENGTH
#undef LOAD_STRING_STRING
#undef STORE_STRING_LENGTH
#undef STORE_STRING_STRING
}// namespace lox
