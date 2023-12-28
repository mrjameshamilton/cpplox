#include "ModuleCompiler.h"
#include "Value.h"
#include <llvm/IR/Value.h>

namespace lox {

#define LOAD_STRING_LENGTH(PTR) \
    CreateLoad(getInt32Ty(), CreateStructGEP(getModule().getStructType(ObjType::STRING), CreateLoad(getPtrTy(), PTR), 2), "length")
#define LOAD_STRING_STRING(PTR) \
    CreateLoad(getPtrTy(), CreateStructGEP(getModule().getStructType(ObjType::STRING), CreateLoad(getPtrTy(), PTR), 1), "string")
#define STORE_STRING_LENGTH(PTR, LENGTH) \
    CreateStore(LENGTH, CreateStructGEP(getModule().getStructType(ObjType::STRING), CreateLoad(getPtrTy(), PTR), 2))
#define STORE_STRING_STRING(PTR, STR) \
    CreateStore(STR, CreateStructGEP(getModule().getStructType(ObjType::STRING), CreateLoad(getPtrTy(), PTR), 1))

    Value *LoxBuilder::AllocateString(Value *objects, Value *String, Value *Length, const std::string_view name) {
        const auto NewString = AllocateObj(objects, ObjType::STRING, name);

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

            const auto InsertPoint = GetInsertBlock();
            const auto EntryBasicBlock = CreateBasicBlock("entry", F);
            SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();

            const auto p0str = CreateEntryBlockAlloca(F, getPtrTy(), "p0str");
            CreateStore(AsString(arguments), p0str);
            const auto p1str = CreateEntryBlockAlloca(F, getPtrTy(), "p1str");
            CreateStore(AsString(arguments + 1), p1str);

            const auto String0Length = LOAD_STRING_LENGTH(p0str);
            const auto String1Length = LOAD_STRING_LENGTH(p1str);
            const auto String0String = LOAD_STRING_STRING(p0str);
            const auto String1String = LOAD_STRING_STRING(p1str);

            const auto NotEqualBlock = CreateBasicBlock("not.equal", F);
            const auto CheckContents = CreateBasicBlock("check.contents", F);
            CreateCondBr(CreateICmpNE(String0Length, String1Length), NotEqualBlock, CheckContents);

            SetInsertPoint(CheckContents);

            static const auto MemCmp = this->getModule().getOrInsertFunction(
                "memcmp",
                FunctionType::get(getInt32Ty(), {getPtrTy(), getPtrTy(), getInt64Ty()}, false)
            );

            CreateRet(
                CreateICmpEQ(
                    CreateCall(MemCmp, {String0String, String1String, String0Length}),
                    getInt32(0)
                )
            );

            SetInsertPoint(NotEqualBlock);
            CreateRet(getFalse());

            SetInsertPoint(InsertPoint);

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

            const auto InsertPoint = GetInsertBlock();
            const auto EntryBasicBlock = CreateBasicBlock("entry", F);
            SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();

            const auto p0str = CreateEntryBlockAlloca(F, getPtrTy(), "p0str");
            CreateStore(AsString(arguments), p0str);
            const auto p1str = CreateEntryBlockAlloca(F, getPtrTy(), "p1str");
            CreateStore(AsString(arguments + 1), p1str);

            const auto String0Length = LOAD_STRING_LENGTH(p0str);
            const auto String1Length = LOAD_STRING_LENGTH(p1str);
            const auto String0String = LOAD_STRING_STRING(p0str);
            const auto String1String = LOAD_STRING_STRING(p1str);

            const auto NewLength = CreateNSWAdd(
                String0Length,
                String1Length,
                "NewLength"
            );

            const auto StringMalloc = CreateMalloc(
                getInt32Ty(),
                getInt8PtrTy(),
                CreateSExt(CreateNSWAdd(getInt32(1), NewLength), getInt64Ty()),
                nullptr
            );

            const auto StringTemp = CreateEntryBlockAlloca(F, getPtrTy(), "StringTemp");
            CreateStore(StringMalloc, StringTemp);

            CreateMemCpy(
                CreateLoad(getPtrTy(), StringTemp),
                Align(1),
                String0String,
                Align(1),
                CreateSExt(String0Length, getInt64Ty())
            );

            CreateMemCpy(
                CreateInBoundsGEP(
                    getInt8Ty(),
                    CreateLoad(getPtrTy(), StringTemp),
                    {CreateSExt(String0Length, getInt64Ty())}
                ),
                Align(1),
                String1String,
                Align(1),
                CreateSExt(
                    CreateNSWAdd(
                        String1Length,
                        getInt32(1)
                    ),
                    getInt64Ty()
                )
            );

            const auto NewString = AllocateString(
                this->getModule().getNamedGlobal("objects"),
                CreateLoad(getPtrTy(), StringTemp),
                NewLength, "NewString"
            );

            CreateRet(NewString);

            SetInsertPoint(InsertPoint);

            return F;
        }());

        return CreateCall(ConcatFunction, {a, b});
    }
#undef LOAD_STRING_LENGTH
#undef LOAD_STRING_STRING
#undef STORE_STRING_LENGTH
#undef STORE_STRING_STRING
}// namespace lox
