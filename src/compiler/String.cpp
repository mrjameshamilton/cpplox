#include "Memory.h"
#include "ModuleCompiler.h"
#include "Stack.h"
#include "Value.h"
#include <llvm/IR/Value.h>

namespace lox {

    // Use a hash table for string interning.
    Value *FindStringEntry(LoxBuilder &Builder, Value *Table, Value *String, Value *Length, Value *Hash) {
        static auto *FindStringFunction([&Builder] {
            auto *const F = Function::Create(
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

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->arg_begin();
            auto *const table = arguments;
            auto *const string = arguments + 1;
            auto *const length = arguments + 2;
            auto *const hash = arguments + 3;

            auto *const count = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 0));

            auto *const IsEmptyBlock = B.CreateBasicBlock("table.empty");
            auto *const NotEmptyBlock = B.CreateBasicBlock("table.notempty");

            B.CreateCondBr(B.CreateICmpEQ(B.getInt32(0), count), IsEmptyBlock, NotEmptyBlock);

            B.SetInsertPoint(IsEmptyBlock);
            B.CreateRet(B.getNullPtr());

            B.SetInsertPoint(NotEmptyBlock);

            auto *const index = CreateEntryBlockAlloca(F, B.getInt32Ty(), "index");
            auto *const capacity = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 1));
            B.CreateStore(B.CreateURem(hash, capacity), index);

            auto *const ForStartBlock = B.CreateBasicBlock("for.start");

            B.CreateBr(ForStartBlock);
            B.SetInsertPoint(ForStartBlock);
            auto *const entries = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 2));
            auto *const entry = B.CreateInBoundsGEP(B.getModule().getEntryStructType(), entries, B.CreateLoad(B.getInt32Ty(), index), "entry");
            auto *const entryKey = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getEntryStructType(), entry, 0));

            auto *const KeyIsNullBlock = B.CreateBasicBlock("key.null");
            auto *const KeyIsNotNullBlock = B.CreateBasicBlock("key.notnull");
            auto *const CheckKeyIsSameBlock = B.CreateBasicBlock("key.issame?");
            auto *const EndIfBlock = B.CreateBasicBlock("key.endif");

            B.CreateCondBr(B.CreateIsNull(entryKey), KeyIsNullBlock, KeyIsNotNullBlock);

            B.SetInsertPoint(KeyIsNullBlock);

            auto *const IsNilBlock = B.CreateBasicBlock("value.isnil");

            auto *const entryValue = B.CreateLoad(B.getInt64Ty(), B.CreateStructGEP(B.getModule().getEntryStructType(), entry, 1));
            B.CreateCondBr(B.IsNil(entryValue), IsNilBlock, EndIfBlock);

            B.SetInsertPoint(IsNilBlock);
            B.CreateRet(B.getNullPtr());

            B.SetInsertPoint(KeyIsNotNullBlock);
            B.CreateBr(CheckKeyIsSameBlock);

            B.SetInsertPoint(CheckKeyIsSameBlock);

            auto *const SameLengthBlock = B.CreateBasicBlock("same.length");
            auto *const SameHashBlock = B.CreateBasicBlock("same.hash");
            auto *const SameStringBlock = B.CreateBasicBlock("same.string");

            auto *const keyLength = B.CreateLoad(B.getInt32Ty(), B.CreateObjStructGEP(ObjType::STRING, entryKey, 2));
            B.CreateCondBr(B.CreateICmpEQ(keyLength, length), SameLengthBlock, EndIfBlock);

            B.SetInsertPoint(SameLengthBlock);
            auto *const keyHash = B.CreateLoad(B.getInt32Ty(), B.CreateObjStructGEP(ObjType::STRING, entryKey, 3));
            B.CreateCondBr(B.CreateICmpEQ(keyHash, hash), SameHashBlock, EndIfBlock);

            B.SetInsertPoint(SameHashBlock);
            auto *const keyString = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::STRING, entryKey, 1));
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
                B.CreateAnd(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), index), B.getInt32(1), "index+1", true, true), B.CreateSub(capacity, B.getInt32(1), "capacity-1", true, true)),
                index
            );

            B.CreateBr(ForStartBlock);

            return F;
        }());

        return Builder.CreateCall(FindStringFunction, {Table, String, Length, Hash});
    }

    static Value *StringHash(LoxBuilder &Builder, Value *String, Value *Length) {
        static auto *StrHashFunction([&Builder] {
            // FNV-1a hash function.
            auto *const F = Function::Create(
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

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->args().begin();

            auto *const str = arguments;
            auto *const len = arguments + 1;

            auto *const hash = CreateEntryBlockAlloca(F, B.getInt32Ty(), "hash");
            auto *const i = CreateEntryBlockAlloca(F, B.getInt32Ty(), "i");

            B.CreateStore(B.getInt32(-2128831035), hash);
            B.CreateStore(B.getInt32(0), i);

            auto *const ForCond = B.CreateBasicBlock("for.cond");
            auto *const ForBody = B.CreateBasicBlock("for.body");
            auto *const ForInc = B.CreateBasicBlock("for.inc");
            auto *const ForEnd = B.CreateBasicBlock("for.end");

            B.CreateBr(ForCond);
            B.SetInsertPoint(ForCond);
            B.CreateCondBr(B.CreateICmpSLT(B.CreateLoad(B.getInt32Ty(), i), len), ForBody, ForEnd);
            B.SetInsertPoint(ForBody);

            auto *const char_index = B.CreateInBoundsGEP(B.getInt8Ty(), str, B.CreateLoad(B.getInt32Ty(), i));
            auto *const char_ = B.CreateLoad(B.getInt8Ty(), char_index);
            auto *const xor_ = B.CreateXor(B.CreateZExt(char_, B.getInt32Ty()), B.CreateLoad(B.getInt32Ty(), hash));
            auto *const mul = B.CreateMul(xor_, B.getInt32(16777619), "xor", true, true);

            B.CreateStore(mul, hash);

            B.CreateBr(ForInc);
            B.SetInsertPoint(ForInc);
            B.CreateStore(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), i), B.getInt32(1), "i+1", true, true), i);
            B.CreateBr(ForCond);

            B.SetInsertPoint(ForEnd);
            B.CreateRet(B.CreateLoad(B.getInt32Ty(), hash));

            return F;
        }());

        return Builder.CreateCall(StrHashFunction, {String, Length});
    }

    Value *LoxBuilder::AllocateString(Value *String, Value *Length, const std::string_view name) {
        static auto *AllocateStringFunction([this] {
            auto *const F = Function::Create(
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

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->args().begin();

            auto *const String = arguments;
            auto *const Length = arguments + 1;

            auto *const ptr = B.AllocateObj(ObjType::STRING);

            B.CreateStore(String, B.CreateObjStructGEP(ObjType::STRING, ptr, 1));
            B.CreateStore(Length, B.CreateObjStructGEP(ObjType::STRING, ptr, 2));
            B.CreateStore(StringHash(B, String, Length), B.CreateObjStructGEP(ObjType::STRING, ptr, 3));
            B.CreateStore(B.getTrue(), B.CreateObjStructGEP(ObjType::STRING, ptr, 4));

            B.TableSet(B.CreateLoad(B.getPtrTy(), B.getModule().getRuntimeStrings()), ptr, B.getNilVal());

            B.CreateRet(ptr);

            return F;
        }());

        return CreateCall(AllocateStringFunction, {String, Length}, name);
    }

    Value *LoxBuilder::AllocateString(const StringRef String, const std::string_view name) {
        static auto *AllocateStringFunction([this] {
            auto *const F = Function::Create(
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

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->args().begin();

            auto *const String = arguments;
            auto *const Length = arguments + 1;
            auto *const hash = arguments + 2;

            auto *const interned = FindStringEntry(B, B.CreateLoad(B.getPtrTy(), B.getModule().getRuntimeStrings()), String, Length, hash);

            auto *const IsInternedBlock = B.CreateBasicBlock("is.interned");
            auto *const NotInternedBlock = B.CreateBasicBlock("end");

            B.CreateCondBr(B.CreateIsNull(interned), NotInternedBlock, IsInternedBlock);

            B.SetInsertPoint(IsInternedBlock);
            B.CreateRet(interned);

            B.SetInsertPoint(NotInternedBlock);

            auto *const ptr = B.AllocateObj(ObjType::STRING);

            B.CreateStore(String, B.CreateObjStructGEP(ObjType::STRING, ptr, 1));
            B.CreateStore(Length, B.CreateObjStructGEP(ObjType::STRING, ptr, 2));
            B.CreateStore(hash, B.CreateObjStructGEP(ObjType::STRING, ptr, 3));
            B.CreateStore(B.getFalse(), B.CreateObjStructGEP(ObjType::STRING, ptr, 4));

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

        auto *const ptr = CreateCall(AllocateStringFunction, {CreateGlobalCachedString(String), getInt32(String.size()), getInt32(hash)}, name);

        CreateInvariantStart(ptr, getSizeOf(ObjType::STRING));

        return ptr;
    }

    Value *LoxBuilder::Concat(Value *a, Value *b) {
        static auto *ConcatFunction([this] {
            auto *const F = Function::Create(
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

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->args().begin();

            auto *const a = B.AsObj(arguments);
            auto *const b = B.AsObj(arguments + 1);

            auto *const String0Length = B.CreateLoad(B.getInt32Ty(), B.CreateObjStructGEP(ObjType::STRING, a, 2), "length");
            auto *const String1Length = B.CreateLoad(B.getInt32Ty(), B.CreateObjStructGEP(ObjType::STRING, b, 2), "length");
            auto *const String0String = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::STRING, a, 1), "string");
            auto *const String1String = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::STRING, b, 1), "string");

            auto *const NewLength = B.CreateAdd(
                String0Length,
                String1Length,
                "NewLength",
                true,
                true
            );

            auto *const StringMalloc = B.CreateRealloc(
                B.getNullPtr(),
                B.CreateSExt(B.CreateAdd(B.getInt32(1), NewLength, "NewLength", true, true), B.getInt64Ty()), "concat string"
            );

            B.CreateMemCpy(
                StringMalloc,
                Align(8),
                String0String,
                Align(8),
                B.CreateSExt(String0Length, B.getInt64Ty())
            );

            B.CreateMemCpy(
                B.CreateInBoundsGEP(
                    B.getInt8Ty(),
                    StringMalloc,
                    {B.CreateSExt(String0Length, B.getInt64Ty())}
                ),
                Align(8),
                String1String,
                Align(8),
                B.CreateSExt(
                    B.CreateAdd(
                        String1Length,
                        B.getInt32(1),
                        "size",
                        true,
                        true
                    ),
                    B.getInt64Ty()
                )
            );

            auto *const interned = FindStringEntry(B, B.CreateLoad(B.getPtrTy(), B.getModule().getRuntimeStrings()), StringMalloc, NewLength, StringHash(B, StringMalloc, NewLength));

            auto *const IsInternedBlock = B.CreateBasicBlock("is.interned");
            auto *const NotInternedBlock = B.CreateBasicBlock("end");

            B.CreateCondBr(B.CreateIsNull(interned), NotInternedBlock, IsInternedBlock);

            B.SetInsertPoint(IsInternedBlock);
            // Temporary string not required anymore.
            B.IRBuilder::CreateFree(StringMalloc);
            B.CreateRet(interned);

            B.SetInsertPoint(NotInternedBlock);

            auto *const NewString = B.AllocateString(
                StringMalloc,
                NewLength,
                "NewString"
            );

            B.CreateRet(NewString);

            return F;
        }());

        auto *const ptr = CreateCall(ConcatFunction, {a, b});

        CreateInvariantStart(ptr, getSizeOf(ObjType::STRING));

        return ptr;
    }
}// namespace lox
