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

    static Value *StringHash(LoxBuilder &Builder, Value *String, Value *Length) {
        static auto StrHashFunction([&Builder] {
            // FNV-1a hash function.
            const auto F = Function::Create(
                FunctionType::get(
                    Builder.getInt32Ty(),
                    {Builder.getPtrTy(), Builder.getInt32Ty()},
                    false
                ),
                Function::InternalLinkage,
                "strHash",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();

            const auto str = arguments;
            const auto len = arguments + 1;

            const auto hash = CreateEntryBlockAlloca(F, B.getInt32Ty(), "hash");
            const auto i = CreateEntryBlockAlloca(F, B.getInt32Ty(), "i");

            B.CreateStore(B.getInt32(-2128831035), hash);
            B.CreateStore(B.getInt32(0), i);

            const auto ForCond = B.CreateBasicBlock("for.cond");
            const auto ForBody = B.CreateBasicBlock("for.body");
            const auto ForInc = B.CreateBasicBlock("for.inc");
            const auto ForEnd = B.CreateBasicBlock("for.end");

            B.CreateBr(ForCond);
            B.SetInsertPoint(ForCond);
            B.CreateCondBr(B.CreateICmpSLT(B.CreateLoad(B.getInt32Ty(), i), len), ForBody, ForEnd);
            B.SetInsertPoint(ForBody);

            const auto char_index = B.CreateInBoundsGEP(B.getInt8Ty(), str, B.CreateLoad(B.getInt32Ty(), i));
            const auto char_ = B.CreateLoad(B.getInt8Ty(), char_index);
            const auto xor_ = B.CreateXor(B.CreateZExt(char_, B.getInt32Ty()), B.CreateLoad(B.getInt32Ty(), hash));
            const auto mul = B.CreateMul(xor_, B.getInt32(16777619));

            B.CreateStore(mul, hash);

            B.CreateBr(ForInc);
            B.SetInsertPoint(ForInc);
            B.CreateStore(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), i), B.getInt32(1)), i);
            B.CreateBr(ForCond);

            B.SetInsertPoint(ForEnd);
            B.CreateRet(B.CreateLoad(B.getInt32Ty(), hash));

            return F;
        }());

        return Builder.CreateCall(StrHashFunction, {String, Length});
    }

    Value *LoxBuilder::AllocateString(Value *String, Value *Length, const std::string_view name) {
        const auto NewString = AllocateObj(ObjType::STRING, name);

        const auto ptr = CreateLoad(getPtrTy(), NewString);
        CreateStore(String, CreateStructGEP(getModule().getStructType(ObjType::STRING), ptr, 1));
        CreateStore(Length, CreateStructGEP(getModule().getStructType(ObjType::STRING), ptr, 2));
        CreateStore(StringHash(*this, String, Length), CreateStructGEP(getModule().getStructType(ObjType::STRING), ptr, 3));

        return ptr;
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
                getModule()
            );

            LoxBuilder B(getContext(), getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();

            const auto p0str = CreateEntryBlockAlloca(F, B.getPtrTy(), "p0str");
            B.CreateStore(B.AsObj(arguments), p0str);
            const auto p1str = CreateEntryBlockAlloca(F, B.getPtrTy(), "p1str");
            B.CreateStore(B.AsObj(arguments + 1), p1str);

            const auto String0Length = LOAD_STRING_LENGTH(p0str);
            const auto String1Length = LOAD_STRING_LENGTH(p1str);
            const auto String0String = LOAD_STRING_STRING(p0str);
            const auto String1String = LOAD_STRING_STRING(p1str);

            const auto NotEqualBlock = B.CreateBasicBlock("not.equal");
            const auto CheckContents = B.CreateBasicBlock("check.contents");
            B.CreateCondBr(B.CreateICmpNE(String0Length, String1Length), NotEqualBlock, CheckContents);

            B.SetInsertPoint(CheckContents);

            static const auto MemCmp = getModule().getOrInsertFunction(
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
                getModule()
            );

            LoxBuilder B(getContext(), getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();

            const auto p0str = CreateEntryBlockAlloca(F, B.getPtrTy(), "p0str");
            B.CreateStore(B.AsObj(arguments), p0str);
            const auto p1str = CreateEntryBlockAlloca(F, B.getPtrTy(), "p1str");
            B.CreateStore(B.AsObj(arguments + 1), p1str);

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

            B.CreateRet(B.ObjVal(NewString));

            return F;
        }());

        return CreateCall(ConcatFunction, {a, b});
    }
#undef LOAD_STRING_LENGTH
#undef LOAD_STRING_STRING
#undef STORE_STRING_LENGTH
#undef STORE_STRING_STRING
}// namespace lox
