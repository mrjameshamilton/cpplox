#include "Localstack.h"
#include "Memory.h"
#include "ModuleCompiler.h"
#include "Value.h"
#include <llvm/IR/Value.h>

namespace lox {

    // Use a hash table for string interning.
    Value *FindStringEntry(LoxBuilder &Builder, Value *Table, Value *String, Value *Length, Value *Hash) {
        static auto FindStringFunction([&Builder] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder.getPtrTy(),
                    {Builder.getPtrTy(), Builder.getPtrTy(), Builder.getInt32Ty(), Builder.getInt32Ty()},
                    false
                ),
                Function::InternalLinkage,
                "$tableFindString",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->arg_begin();
            const auto table = arguments;
            const auto string = arguments + 1;
            const auto length = arguments + 2;
            const auto hash = arguments + 3;

            const auto count = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 0));

            const auto IsEmptyBlock = B.CreateBasicBlock("table.empty");
            const auto NotEmptyBlock = B.CreateBasicBlock("table.notempty");

            B.CreateCondBr(B.CreateICmpEQ(B.getInt32(0), count), IsEmptyBlock, NotEmptyBlock);

            B.SetInsertPoint(IsEmptyBlock);
            B.CreateRet(B.getNullPtr());

            B.SetInsertPoint(NotEmptyBlock);

            const auto index = CreateEntryBlockAlloca(F, B.getInt32Ty(), "index");
            const auto capacity = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 1));
            B.CreateStore(B.CreateURem(hash, capacity), index);

            const auto ForStartBlock = B.CreateBasicBlock("for.start");

            B.CreateBr(ForStartBlock);
            B.SetInsertPoint(ForStartBlock);
            const auto entries = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 2));
            const auto entry = B.CreateInBoundsGEP(B.getModule().getEntryStructType(), entries, B.CreateLoad(B.getInt32Ty(), index), "entry");
            const auto entryKey = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getEntryStructType(), entry, 0));

            const auto KeyIsNullBlock = B.CreateBasicBlock("key.null");
            const auto KeyIsNotNullBlock = B.CreateBasicBlock("key.notnull");
            const auto CheckKeyIsSameBlock = B.CreateBasicBlock("key.issame?");
            const auto EndIfBlock = B.CreateBasicBlock("key.endif");

            B.CreateCondBr(B.CreateIsNull(entryKey), KeyIsNullBlock, KeyIsNotNullBlock);

            B.SetInsertPoint(KeyIsNullBlock);

            const auto IsNilBlock = B.CreateBasicBlock("value.isnil");

            const auto entryValue = B.CreateLoad(B.getInt64Ty(), B.CreateStructGEP(B.getModule().getEntryStructType(), entry, 1));
            B.CreateCondBr(B.IsNil(entryValue), IsNilBlock, EndIfBlock);

            B.SetInsertPoint(IsNilBlock);
            B.CreateRet(B.getNullPtr());

            B.SetInsertPoint(KeyIsNotNullBlock);
            B.CreateBr(CheckKeyIsSameBlock);

            B.SetInsertPoint(CheckKeyIsSameBlock);

            const auto SameLengthBlock = B.CreateBasicBlock("same.length");
            const auto SameHashBlock = B.CreateBasicBlock("same.hash");
            const auto SameStringBlock = B.CreateBasicBlock("same.string");

            const auto keyLength = B.CreateLoad(B.getInt32Ty(), B.CreateObjStructGEP(ObjType::STRING, entryKey, 2));
            B.CreateCondBr(B.CreateICmpEQ(keyLength, length), SameLengthBlock, EndIfBlock);

            B.SetInsertPoint(SameLengthBlock);
            const auto keyHash = B.CreateLoad(B.getInt32Ty(), B.CreateObjStructGEP(ObjType::STRING, entryKey, 3));
            B.CreateCondBr(B.CreateICmpEQ(keyHash, hash), SameHashBlock, EndIfBlock);

            B.SetInsertPoint(SameHashBlock);
            const auto keyString = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::STRING, entryKey, 1));
            static const auto MemCmp = B.getModule().getOrInsertFunction(
                "memcmp",
                FunctionType::get(B.getInt32Ty(), {B.getPtrTy(), B.getPtrTy(), B.getInt64Ty()}, false)
            );
            B.CreateCondBr(B.CreateICmpEQ(B.CreateCall(MemCmp, {string, keyString, length}), B.getInt32(0)), SameStringBlock, EndIfBlock);

            B.SetInsertPoint(SameStringBlock);
            B.CreateRet(B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getEntryStructType(), entry, 0)));

            B.SetInsertPoint(EndIfBlock);

            // index = (index + 1) & (capacity - 1) === index = index % capacity
            B.CreateStore(
                B.CreateAnd(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), index), B.getInt32(1)), B.CreateSub(capacity, B.getInt32(1))),
                index
            );

            B.CreateBr(ForStartBlock);

            return F;
        }());

        return Builder.CreateCall(FindStringFunction, {Table, String, Length, Hash});
    }

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
                "$strHash",
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
        static auto AllocateStringFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    getPtrTy(),
                    {getPtrTy(), getInt32Ty()},
                    false
                ),
                Function::InternalLinkage,
                "$allocateString",
                getModule()
            );

            LoxBuilder B(getContext(), getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();

            const auto String = arguments;
            const auto Length = arguments + 1;

            const auto ptr = B.AllocateObj(ObjType::STRING);

            B.CreateStore(String, B.CreateObjStructGEP(ObjType::STRING, ptr, 1));
            B.CreateStore(Length, B.CreateObjStructGEP(ObjType::STRING, ptr, 2));
            B.CreateStore(StringHash(B, String, Length), B.CreateObjStructGEP(ObjType::STRING, ptr, 3));

            B.TableSet(B.CreateLoad(B.getPtrTy(), B.getModule().getRuntimeStrings()), ptr, B.getNilVal());

            B.CreateRet(ptr);

            return F;
        }());

        return CreateCall(AllocateStringFunction, {String, Length}, name);
    }

    Value *LoxBuilder::AllocateString(const StringRef String, const std::string_view name) {
        static auto AllocateStringFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    getPtrTy(),
                    {getPtrTy(), getInt32Ty(), getInt32Ty()},
                    false
                ),
                Function::InternalLinkage,
                "$allocateString",
                getModule()
            );

            LoxBuilder B(getContext(), getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();

            const auto String = arguments;
            const auto Length = arguments + 1;
            const auto hash = arguments + 2;

            const auto interned = FindStringEntry(B, B.CreateLoad(B.getPtrTy(), B.getModule().getRuntimeStrings()), String, Length, hash);

            const auto IsInternedBlock = B.CreateBasicBlock("is.interned");
            const auto NotInternedBlock = B.CreateBasicBlock("end");

            B.CreateCondBr(B.CreateIsNull(interned), NotInternedBlock, IsInternedBlock);

            B.SetInsertPoint(IsInternedBlock);
            B.CreateRet(interned);

            B.SetInsertPoint(NotInternedBlock);

            const auto ptr = B.AllocateObj(ObjType::STRING);

            B.CreateStore(String, B.CreateObjStructGEP(ObjType::STRING, ptr, 1));
            B.CreateStore(Length, B.CreateObjStructGEP(ObjType::STRING, ptr, 2));
            B.CreateStore(hash, B.CreateObjStructGEP(ObjType::STRING, ptr, 3));

            B.TableSet(B.CreateLoad(B.getPtrTy(), B.getModule().getRuntimeStrings()), ptr, B.getNilVal());

            B.CreateRet(ptr);

            return F;
        }());

        // FNV-1a hash function.
        unsigned int hash = -2128831035;
        for (const char i: String) {
            hash ^= i;
            hash *= 16777619;
        }

        return CreateCall(AllocateStringFunction, {CreateGlobalCachedString(String), getInt32(String.size()), getInt32(hash)}, name);
    }

    Value *LoxBuilder::Concat(Value *a, Value *b) {
        static auto ConcatFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    getPtrTy(),
                    {getInt64Ty(), getInt64Ty()},
                    false
                ),
                Function::InternalLinkage,
                "$concat",
                getModule()
            );

            LoxBuilder B(getContext(), getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();

            const auto a = B.AsObj(arguments);
            const auto b = B.AsObj(arguments + 1);

            const auto String0Length = B.CreateLoad(B.getInt32Ty(), B.CreateObjStructGEP(ObjType::STRING, a, 2), "length");
            const auto String1Length = B.CreateLoad(B.getInt32Ty(), B.CreateObjStructGEP(ObjType::STRING, b, 2), "length");
            const auto String0String = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::STRING, a, 1), "string");
            const auto String1String = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::STRING, b, 1), "string");

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

            B.CreateMemCpy(
                StringMalloc,
                Align(1),
                String0String,
                Align(1),
                B.CreateSExt(String0Length, B.getInt64Ty())
            );

            B.CreateMemCpy(
                B.CreateInBoundsGEP(
                    B.getInt8Ty(),
                    StringMalloc,
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

            const auto interned = FindStringEntry(B, B.CreateLoad(B.getPtrTy(), B.getModule().getRuntimeStrings()), StringMalloc, NewLength, StringHash(B, StringMalloc, NewLength));

            const auto IsInternedBlock = B.CreateBasicBlock("is.interned");
            const auto NotInternedBlock = B.CreateBasicBlock("end");

            B.CreateCondBr(B.CreateIsNull(interned), NotInternedBlock, IsInternedBlock);

            B.SetInsertPoint(IsInternedBlock);
            // Temporary string not required anymore.
            B.IRBuilder::CreateFree(StringMalloc);
            B.CreateRet(interned);

            B.SetInsertPoint(NotInternedBlock);

            const auto NewString = B.AllocateString(
                StringMalloc,
                NewLength,
                "NewString"
            );

            B.CreateRet(NewString);

            return F;
        }());

        return ObjVal(PushTemp(*this, CreateCall(ConcatFunction, {a, b}), "concat string"));
    }
}// namespace lox
