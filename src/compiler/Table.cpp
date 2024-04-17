#include "Table.h"
#include "LoxBuilder.h"
#include "Memory.h"
#include "ModuleCompiler.h"

#include <llvm/IR/Value.h>

namespace lox {
    // Port from clox: https://github.com/mrjameshamilton/clox/blob/master/src/table.c

    Value *LoxBuilder::AllocateTable() {
        static auto AllocateTableFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    getPtrTy(),
                    {},
                    false
                ),
                Function::InternalLinkage,
                "$allocateTable",
                getModule()
            );

            LoxBuilder B(getContext(), getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto ptr = B.CreateRealloc(
                B.getNullPtr(),
                B.getSizeOf(getModule().getTableStructType())
            );

            B.CreateStore(B.getInt32(0), B.CreateStructGEP(B.getModule().getTableStructType(), ptr, 0));
            B.CreateStore(B.getInt32(0), B.CreateStructGEP(B.getModule().getTableStructType(), ptr, 1));
            B.CreateStore(B.getNullPtr(), B.CreateStructGEP(B.getModule().getTableStructType(), ptr, 2));

            B.CreateRet(ptr);

            return F;
        }());

        return CreateCall(AllocateTableFunction);
    }

    Value *FindEntry(LoxBuilder &Builder, Value *Entries, Value *Capacity, Value *Key) {
        static auto FindEntryFunction([&Builder] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder.getPtrTy(),
                    {Builder.getPtrTy(), Builder.getInt32Ty(), Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$tableFindEntry",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->arg_begin();
            const auto entries = arguments;
            const auto capacity = arguments + 1;
            const auto key = arguments + 2;

            const auto hash = B.CreateLoad(B.getInt32Ty(), B.CreateObjStructGEP(ObjType::STRING, key, 3, "hash"));
            const auto index = CreateEntryBlockAlloca(F, B.getInt32Ty(), "index");
            const auto tombstone = CreateEntryBlockAlloca(F, B.getPtrTy(), "tombstone");
            B.CreateStore(Constant::getNullValue(PointerType::getUnqual(B.getContext())), tombstone);
            B.CreateStore(B.CreateURem(hash, capacity), index);

            const auto ForStartBlock = B.CreateBasicBlock("for.start");

            B.CreateBr(ForStartBlock);
            B.SetInsertPoint(ForStartBlock);
            const auto entry = B.CreateInBoundsGEP(B.getModule().getEntryStructType(), entries, B.CreateLoad(B.getInt32Ty(), index), "entryx");
            const auto entryKey = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getEntryStructType(), entry, 0));

            const auto KeyIsNullBlock = B.CreateBasicBlock("key.null");
            const auto KeyIsNotNullBlock = B.CreateBasicBlock("key.notnull");
            const auto CheckKeyIsSameBlock = B.CreateBasicBlock("key.issame?");
            const auto KeyIsSameBlock = B.CreateBasicBlock("key.issame");
            const auto EndIfBlock = B.CreateBasicBlock("key.endif");

            B.CreateCondBr(B.CreateIsNull(entryKey), KeyIsNullBlock, KeyIsNotNullBlock);

            B.SetInsertPoint(KeyIsNullBlock);

            const auto IsNilBlock = B.CreateBasicBlock("value.isnil");
            const auto IsNotNilBlock = B.CreateBasicBlock("value.notnil");
            const auto EndNilCheckBlock = B.CreateBasicBlock("value.end");

            const auto entryValue = B.CreateLoad(B.getInt64Ty(), B.CreateStructGEP(B.getModule().getEntryStructType(), entry, 1));
            B.CreateCondBr(B.IsNil(entryValue), IsNilBlock, IsNotNilBlock);

            B.SetInsertPoint(IsNilBlock);
            const auto tombstonePtr = B.CreateLoad(B.getPtrTy(), tombstone);
            B.CreateRet(B.CreateSelect(B.CreateIsNotNull(tombstonePtr), tombstonePtr, entry));

            B.SetInsertPoint(IsNotNilBlock);

            const auto IsTombstoneNullBlock = B.CreateBasicBlock("tombstone.isnull");

            B.CreateCondBr(B.CreateIsNull(B.CreateLoad(B.getPtrTy(), tombstone)), IsTombstoneNullBlock, EndNilCheckBlock);
            B.SetInsertPoint(IsTombstoneNullBlock);

            B.CreateStore(entry, tombstone);

            B.CreateBr(EndNilCheckBlock);
            B.SetInsertPoint(EndNilCheckBlock);

            B.CreateBr(EndIfBlock);

            B.SetInsertPoint(KeyIsNotNullBlock);
            B.CreateBr(CheckKeyIsSameBlock);

            B.SetInsertPoint(CheckKeyIsSameBlock);
            B.CreateCondBr(B.CreateICmpEQ(B.CreatePtrDiff(B.getPtrTy(), entryKey, key), B.getInt64(0)), KeyIsSameBlock, EndIfBlock);

            B.SetInsertPoint(KeyIsSameBlock);
            B.CreateRet(entry);

            B.SetInsertPoint(EndIfBlock);

            // index = (index + 1) & (capacity - 1) === index = index % capacity
            B.CreateStore(
                B.CreateAnd(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), index), B.getInt32(1)), B.CreateSub(capacity, B.getInt32(1))),
                index
            );

            B.CreateBr(ForStartBlock);

            return F;
        }());

        return Builder.CreateCall(FindEntryFunction, {Entries, Capacity, Key});
    }

    void PrintEntry(LoxBuilder &B, Value *value) {
        const auto entryKey = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getEntryStructType(), value, 0));
        const auto entryKeyString = B.AsCString(B.ObjVal(entryKey));
        const auto entryValue = B.CreateLoad(B.getInt64Ty(), B.CreateStructGEP(B.getModule().getEntryStructType(), value, 1));

        B.PrintF({B.CreateGlobalCachedString("entry: %p = "), entryKeyString});
        B.Print(entryValue);
    }

    Value *LoxBuilder::TableSet(Value *Table, Value *Key, Value *V) {
        assert(V->getType() == IntegerType::get(getContext(), 64));
        static auto AdjustCapacityFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    getVoidTy(),
                    {getPtrTy(), getInt32Ty()},
                    false
                ),
                Function::InternalLinkage,
                "$adjustCapacity",
                getModule()
            );

            LoxBuilder B(getContext(), getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->arg_begin();
            const auto table = arguments;
            const auto capacity = arguments + 1;

            const auto entries = B.CreateRealloc(B.getNullPtr(), B.getSizeOf(B.getModule().getEntryStructType(), capacity));
            const auto i = CreateEntryBlockAlloca(F, B.getInt32Ty(), "i");

            B.CreateStore(B.getInt32(0), i);

            // First initialize all entries to null values.
            const auto ForCond = B.CreateBasicBlock("for.cond");
            const auto ForBody = B.CreateBasicBlock("for.body");
            const auto ForInc = B.CreateBasicBlock("for.inc");
            const auto ForEnd = B.CreateBasicBlock("for.end");

            B.CreateBr(ForCond);
            B.SetInsertPoint(ForCond);
            B.CreateCondBr(B.CreateICmpSLT(B.CreateLoad(B.getInt32Ty(), i), capacity), ForBody, ForEnd);
            B.SetInsertPoint(ForBody);

            const auto entry = B.CreateInBoundsGEP(B.getModule().getEntryStructType(), entries, B.CreateLoad(B.getInt32Ty(), i));

            // key
            B.CreateStore(
                B.getNullPtr(),
                B.CreateStructGEP(getModule().getEntryStructType(), entry, 0)
            );

            // value
            B.CreateStore(
                B.getNilVal(),
                B.CreateStructGEP(getModule().getEntryStructType(), entry, 1)
            );

            B.CreateBr(ForInc);
            B.SetInsertPoint(ForInc);
            B.CreateStore(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), i), B.getInt32(1)), i);
            B.CreateBr(ForCond);

            B.SetInsertPoint(ForEnd);

            B.CreateStore(
                B.getInt32(0),
                B.CreateStructGEP(B.getModule().getTableStructType(), table, 0)
            );

            // ...
            B.CreateStore(B.getInt32(0), i);

            const auto ForCond2 = B.CreateBasicBlock("for.cond");
            const auto ForBody2 = B.CreateBasicBlock("for.body");
            const auto ForInc2 = B.CreateBasicBlock("for.inc");
            const auto ForEnd2 = B.CreateBasicBlock("for.end");

            const auto tableCapacity = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 1));

            B.CreateBr(ForCond2);
            B.SetInsertPoint(ForCond2);

            B.CreateCondBr(
                B.CreateICmpSLT(
                    B.CreateLoad(B.getInt32Ty(), i),
                    tableCapacity
                ),
                ForBody2,
                ForEnd2
            );
            B.SetInsertPoint(ForBody2);

            const auto NotNullBlock = B.CreateBasicBlock("key.notnull");

            const auto entry2 = B.CreateInBoundsGEP(
                B.getModule().getEntryStructType(),
                B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 2)),
                B.CreateLoad(B.getInt32Ty(), i)
            );
            const auto entryKeyPtr = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(getModule().getEntryStructType(), entry2, 0));
            B.CreateCondBr(B.CreateIsNull(entryKeyPtr), ForInc2, NotNullBlock);

            B.SetInsertPoint(NotNullBlock);

            const auto dest = FindEntry(B, entries, capacity, entryKeyPtr);

            B.CreateStore(
                entryKeyPtr,
                B.CreateStructGEP(getModule().getEntryStructType(), dest, 0)
            );
            B.CreateStore(
                B.CreateLoad(B.getInt64Ty(), B.CreateStructGEP(getModule().getEntryStructType(), entry2, 1)),
                B.CreateStructGEP(getModule().getEntryStructType(), dest, 1)
            );

            const auto count = B.CreateStructGEP(B.getModule().getTableStructType(), table, 0);
            B.CreateStore(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), count), B.getInt32(1)), count);

            B.CreateBr(ForInc2);
            B.SetInsertPoint(ForInc2);
            B.CreateStore(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), i), B.getInt32(1)), i);
            B.CreateBr(ForCond2);

            B.SetInsertPoint(ForEnd2);

            B.IRBuilder::CreateFree(B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 2)));
            B.CreateStore(capacity, B.CreateStructGEP(B.getModule().getTableStructType(), table, 1));
            B.CreateStore(entries, B.CreateStructGEP(B.getModule().getTableStructType(), table, 2));

            B.CreateRetVoid();

            return F;
        }());

        static auto TableSetFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    getInt1Ty(),
                    {getPtrTy(), getPtrTy(), getInt64Ty()},
                    false
                ),
                Function::InternalLinkage,
                "$tableSet",
                getModule()
            );

            LoxBuilder B(getContext(), getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->arg_begin();
            const auto table = arguments;
            const auto key = arguments + 1;
            const auto value = arguments + 2;

            const auto count = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 0));
            const auto initialCapacity = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 1));

            const auto CheckCapacityBlock = B.CreateBasicBlock("initialCapacity.check");
            const auto EndCheckBlock = B.CreateBasicBlock("initialCapacity.checkend");

            B.CreateCondBr(
                B.CreateICmpSGT(
                    B.CreateAdd(count, B.getInt32(1)),
                    B.CreateFPToSI(B.CreateFMul(B.CreateSIToFP(initialCapacity, B.getDoubleTy()), ConstantFP::get(B.getDoubleTy(), 0.75)), B.getInt32Ty())
                ),
                CheckCapacityBlock,
                EndCheckBlock
            );
            B.SetInsertPoint(CheckCapacityBlock);

            const auto newCapacity = B.CreateSelect(
                B.CreateICmpSLT(initialCapacity, B.getInt32(8)),
                B.getInt32(8),
                B.CreateMul(initialCapacity, B.getInt32(2))
            );

            B.CreateCall(AdjustCapacityFunction, {table, newCapacity});

            B.CreateBr(EndCheckBlock);
            B.SetInsertPoint(EndCheckBlock);

            const auto capacity = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 1));
            const auto entry = FindEntry(B, B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 2)), capacity, key);
            const auto isNewKey = B.CreateIsNull(B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(getModule().getEntryStructType(), entry, 0)));

            const auto IsNewEntryBlock = B.CreateBasicBlock("newentry");
            const auto EndBlock = B.CreateBasicBlock("end");

            B.CreateCondBr(
                B.CreateAnd(
                    isNewKey,
                    B.IsNil(B.CreateLoad(B.getInt64Ty(), B.CreateStructGEP(getModule().getEntryStructType(), entry, 1)))
                ),
                IsNewEntryBlock,
                EndBlock
            );
            B.SetInsertPoint(IsNewEntryBlock);

            B.CreateStore(
                B.CreateAdd(B.getInt32(1), B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 0))),
                B.CreateStructGEP(B.getModule().getTableStructType(), table, 0)
            );

            B.CreateBr(EndBlock);
            B.SetInsertPoint(EndBlock);

            // key
            B.CreateStore(
                key,
                B.CreateStructGEP(getModule().getEntryStructType(), entry, 0)
            );

            // value
            B.CreateStore(
                value,
                B.CreateStructGEP(getModule().getEntryStructType(), entry, 1)
            );

            B.CreateRet(isNewKey);

            return F;
        }());

        return CreateCall(TableSetFunction, {Table, Key, V});
    }

    Value *LoxBuilder::TableGet(Value *Table, Value *Key) {
        assert(Table->getType() == getPtrTy());
        assert(Key->getType() == getPtrTy());

        static auto TableGetFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    getInt64Ty(),
                    {getPtrTy(), getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$tableGet",
                getModule()
            );

            LoxBuilder B(getContext(), getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->arg_begin();
            const auto table = arguments;
            const auto key = arguments + 1;

            const auto count = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 0));

            const auto IsEmptyBlock = B.CreateBasicBlock("table.empty");
            const auto NotEmptyBlock = B.CreateBasicBlock("table.notempty");

            B.CreateCondBr(B.CreateICmpEQ(B.getInt32(0), count), IsEmptyBlock, NotEmptyBlock);

            B.SetInsertPoint(IsEmptyBlock);
            B.CreateRet(B.getUninitializedVal());

            B.SetInsertPoint(NotEmptyBlock);
            const auto capacity = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 1));
            const auto entries = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 2));

            const auto entry = FindEntry(B, entries, capacity, key);
            const auto entryKey = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(getModule().getEntryStructType(), entry, 0));

            const auto EntryKeyNullBlock = B.CreateBasicBlock("entry.keynull");
            const auto EndBlock = B.CreateBasicBlock("entry.end");

            B.CreateCondBr(B.CreateIsNull(entryKey), EntryKeyNullBlock, EndBlock);

            B.SetInsertPoint(EntryKeyNullBlock);
            {
                if constexpr (DEBUG_TABLE_ENTRIES) {
                    B.PrintF({B.CreateGlobalCachedString("return entry key null %p (%s)\n"), entryKey, B.AsCString(B.ObjVal(key))});
                }
                B.CreateRet(B.getUninitializedVal());
            }
            B.SetInsertPoint(EndBlock);

            const auto entryValue = B.CreateLoad(B.getInt64Ty(), B.CreateStructGEP(getModule().getEntryStructType(), entry, 1));

            if constexpr (DEBUG_TABLE_ENTRIES) {
                B.PrintF({B.CreateGlobalCachedString("return entry value: %p\n"), B.AsObj(entryValue)});
            }

            B.CreateRet(entryValue);

            return F;
        }());

        return CreateCall(TableGetFunction, {Table, Key});
    }

    Value *LoxBuilder::TableAddAll(Value *FromTable, Value *ToTable) {
        assert(FromTable->getType() == getPtrTy());
        assert(ToTable->getType() == getPtrTy());

        static auto TableAddAllFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    getVoidTy(),
                    {getPtrTy(), getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$tableAddAll",
                getModule()
            );

            LoxBuilder B(getContext(), getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->arg_begin();
            const auto from = arguments;
            const auto to = arguments + 1;

            const auto capacity = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), from, 1));

            const auto entries = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getTableStructType(), from, 2), "entries");

            const auto i = CreateEntryBlockAlloca(F, B.getInt32Ty(), "i");

            B.CreateStore(B.getInt32(0), i);

            // First initialize all entries to null values.
            const auto ForCond = B.CreateBasicBlock("for.cond");
            const auto ForBody = B.CreateBasicBlock("for.body");
            const auto ForInc = B.CreateBasicBlock("for.inc");
            const auto ForEnd = B.CreateBasicBlock("for.end");

            B.CreateBr(ForCond);
            B.SetInsertPoint(ForCond);
            B.CreateCondBr(B.CreateICmpSLT(B.CreateLoad(B.getInt32Ty(), i), capacity), ForBody, ForEnd);
            B.SetInsertPoint(ForBody);

            const auto entry = B.CreateInBoundsGEP(B.getModule().getEntryStructType(), entries, B.CreateLoad(B.getInt32Ty(), i), "entry");
            const auto entryKey = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(getModule().getEntryStructType(), entry, 0));

            const auto KeyIsNotNullBlock = B.CreateBasicBlock("key.notnull");

            B.CreateCondBr(B.CreateIsNull(entryKey), ForInc, KeyIsNotNullBlock);

            B.SetInsertPoint(KeyIsNotNullBlock);

            B.TableSet(to, entryKey, B.CreateLoad(B.getInt64Ty(), B.CreateStructGEP(getModule().getEntryStructType(), entry, 1)));

            B.CreateBr(ForInc);
            B.SetInsertPoint(ForInc);
            B.CreateStore(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), i), B.getInt32(1)), i);
            B.CreateBr(ForCond);

            B.SetInsertPoint(ForEnd);

            B.CreateRetVoid();

            return F;
        }());


        return CreateCall(TableAddAllFunction, {FromTable, ToTable});
    }

    Value *TableDelete(LoxBuilder &Builder, Value *Table, Value *Key) {

        static auto TableDeleteFunction([&Builder] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder.getInt1Ty(),
                    {Builder.getPtrTy(), Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$tableDelete",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->arg_begin();
            const auto table = arguments;
            const auto key = arguments + 1;

            const auto count = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 0));

            const auto IsEmptyBlock = B.CreateBasicBlock("table.empty");
            const auto NotEmptyBlock = B.CreateBasicBlock("table.notempty");

            B.CreateCondBr(B.CreateICmpEQ(B.getInt32(0), count), IsEmptyBlock, NotEmptyBlock);

            B.SetInsertPoint(IsEmptyBlock);
            B.CreateRet(B.getFalse());

            B.SetInsertPoint(NotEmptyBlock);
            const auto capacity = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 1));
            const auto entries = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 2));

            const auto entry = FindEntry(B, entries, capacity, key);
            const auto entryKey = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getEntryStructType(), entry, 0));

            const auto EntryKeyNullBlock = B.CreateBasicBlock("entry.keynull");
            const auto EndBlock = B.CreateBasicBlock("entry.end");

            B.CreateCondBr(B.CreateIsNull(entryKey), EntryKeyNullBlock, EndBlock);

            B.SetInsertPoint(EntryKeyNullBlock);
            B.CreateRet(B.getFalse());

            B.SetInsertPoint(EndBlock);

            // Place a tombstone.
            B.CreateStore(B.getNullPtr(), B.CreateStructGEP(B.getModule().getEntryStructType(), entry, 0));
            B.CreateStore(B.getTrueVal(), B.CreateStructGEP(B.getModule().getEntryStructType(), entry, 1));

            B.CreateRet(B.getTrue());

            return F;
        }());

        return Builder.CreateCall(TableDeleteFunction, {Table, Key});
    }

    void IterateTable(LoxBuilder &Builder, Value *Table, Function *FunctionPtr) {
        assert(Table->getType() == Builder.getPtrTy());

        static auto IterateTableFunction([&Builder] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy(), Builder.getPtrTy(), Builder.getInt1Ty()},
                    false
                ),
                Function::InternalLinkage,
                "$iterateTable",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->arg_begin();
            const auto table = arguments;
            const auto function = arguments + 1;

            const auto capacity = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 1));
            const auto entries = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 2), "entries");
            const auto i = CreateEntryBlockAlloca(F, B.getInt32Ty(), "i");

            B.CreateStore(B.getInt32(0), i);

            const auto ForCond = B.CreateBasicBlock("for.cond");
            const auto ForBody = B.CreateBasicBlock("for.body");
            const auto ForInc = B.CreateBasicBlock("for.inc");
            const auto ForEnd = B.CreateBasicBlock("for.end");

            B.CreateBr(ForCond);
            B.SetInsertPoint(ForCond);
            B.CreateCondBr(B.CreateICmpSLT(B.CreateLoad(B.getInt32Ty(), i), capacity), ForBody, ForEnd);
            B.SetInsertPoint(ForBody);
            const auto entry = B.CreateInBoundsGEP(B.getModule().getEntryStructType(), entries, B.CreateLoad(B.getInt32Ty(), i), "entry");
            const auto entryKey = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getEntryStructType(), entry, 0));
            const auto entryValue = B.CreateLoad(B.getInt64Ty(), B.CreateStructGEP(B.getModule().getEntryStructType(), entry, 1));

            const auto KeyIsNotNullBlock = B.CreateBasicBlock("key.notnull");
            if constexpr (DEBUG_TABLE_ENTRIES) {
                B.PrintF({B.CreateGlobalCachedString("entry: %p => %p\n"), entryKey, B.AsObj(entryValue)});
            }

            B.CreateCondBr(B.CreateIsNull(entryKey), ForInc, KeyIsNotNullBlock);
            B.SetInsertPoint(KeyIsNotNullBlock);

            B.CreateCall(
                FunctionType::get(B.getVoidTy(), {B.getPtrTy(), B.getPtrTy(), B.getInt64Ty()}, false),
                function,
                {table, entryKey, entryValue}
            );

            B.CreateBr(ForInc);
            B.SetInsertPoint(ForInc);

            B.CreateStore(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), i), B.getInt32(1)), i);

            B.CreateBr(ForCond);

            B.SetInsertPoint(ForEnd);

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(IterateTableFunction, {Table, FunctionPtr});
    }

}// namespace lox