#include "Table.h"
#include "../Debug.h"
#include "LoxBuilder.h"
#include "Memory.h"
#include "ModuleCompiler.h"

#include <llvm/IR/Value.h>

namespace lox {
    // Port from clox: https://github.com/mrjameshamilton/clox/blob/master/src/table.c

    Value *LoxBuilder::AllocateTable() {
        static auto *AllocateTableFunction([this] {
            auto *const F = Function::Create(
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

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const ptr = B.CreateRealloc(
                B.getNullPtr(),
                B.getSizeOf(getModule().getTableStructType()), "table"
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
        static auto *FindEntryFunction([&Builder] {
            auto *const F = Function::Create(
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

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->arg_begin();
            auto *const entries = arguments;
            auto *const capacity = arguments + 1;
            auto *const key = arguments + 2;

            auto *const hash = B.CreateLoad(B.getInt32Ty(), B.CreateObjStructGEP(ObjType::STRING, key, 3, "hash"));
            auto *const index = CreateEntryBlockAlloca(F, B.getInt32Ty(), "index");
            auto *const tombstone = CreateEntryBlockAlloca(F, B.getPtrTy(), "tombstone");
            B.CreateStore(Constant::getNullValue(PointerType::getUnqual(B.getContext())), tombstone);
            B.CreateStore(B.CreateURem(hash, capacity), index);

            auto *const ForStartBlock = B.CreateBasicBlock("for.start");

            B.CreateBr(ForStartBlock);
            B.SetInsertPoint(ForStartBlock);
            {
                auto *const entry = B.CreateInBoundsGEP(B.getModule().getEntryStructType(), entries, B.CreateLoad(B.getInt32Ty(), index), "entryx");
                auto *const entryKey = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getEntryStructType(), entry, 0));

                auto *const KeyIsNullBlock = B.CreateBasicBlock("key.null");
                auto *const KeyIsNotNullBlock = B.CreateBasicBlock("key.notnull");
                auto *const CheckKeyIsSameBlock = B.CreateBasicBlock("key.issame?");
                auto *const KeyIsSameBlock = B.CreateBasicBlock("key.issame");
                auto *const EndIfBlock = B.CreateBasicBlock("key.endif");

                B.CreateCondBr(B.CreateIsNull(entryKey), KeyIsNullBlock, KeyIsNotNullBlock);

                B.SetInsertPoint(KeyIsNullBlock);
                {
                    auto *const IsNilBlock = B.CreateBasicBlock("value.isnil");
                    auto *const IsNotNilBlock = B.CreateBasicBlock("value.notnil");
                    auto *const EndNilCheckBlock = B.CreateBasicBlock("value.end");

                    auto *const entryValue = B.CreateLoad(B.getInt64Ty(), B.CreateStructGEP(B.getModule().getEntryStructType(), entry, 1));
                    B.CreateCondBr(B.IsNil(entryValue), IsNilBlock, IsNotNilBlock);

                    B.SetInsertPoint(IsNilBlock);
                    auto *const tombstonePtr = B.CreateLoad(B.getPtrTy(), tombstone);
                    B.CreateRet(B.CreateSelect(B.CreateIsNotNull(tombstonePtr), tombstonePtr, entry));

                    B.SetInsertPoint(IsNotNilBlock);
                    {
                        auto *const IsTombstoneNullBlock = B.CreateBasicBlock("tombstone.isnull");

                        B.CreateCondBr(B.CreateIsNull(B.CreateLoad(B.getPtrTy(), tombstone)), IsTombstoneNullBlock, EndNilCheckBlock);
                        B.SetInsertPoint(IsTombstoneNullBlock);
                        {
                            B.CreateStore(entry, tombstone);
                            B.CreateBr(EndNilCheckBlock);
                        }
                        B.SetInsertPoint(EndNilCheckBlock);
                        {
                            B.CreateBr(EndIfBlock);
                        }
                    }
                }
                B.SetInsertPoint(KeyIsNotNullBlock);
                {
                    B.CreateBr(CheckKeyIsSameBlock);
                }
                B.SetInsertPoint(CheckKeyIsSameBlock);
                {
                    B.CreateCondBr(B.CreateICmpEQ(B.CreatePtrDiff(B.getPtrTy(), entryKey, key), B.getInt64(0)), KeyIsSameBlock, EndIfBlock);
                    B.SetInsertPoint(KeyIsSameBlock);
                    {
                        B.CreateRet(entry);
                    }
                }
                B.SetInsertPoint(EndIfBlock);
                {
                    // index = (index + 1) & (capacity - 1) === index = index % capacity
                    B.CreateStore(
                        B.CreateAnd(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), index), B.getInt32(1), "index+1", true, true), B.CreateSub(capacity, B.getInt32(1), "capacity-1", true, true)),
                        index
                    );

                    B.CreateBr(ForStartBlock);
                }
            }

            return F;
        }());

        return Builder.CreateCall(FindEntryFunction, {Entries, Capacity, Key});
    }

    void PrintEntry(LoxBuilder &B, Value *value) {
        auto *const entryKey = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getEntryStructType(), value, 0));
        auto *const entryKeyString = B.AsCString(B.ObjVal(entryKey));
        auto *const entryValue = B.CreateLoad(B.getInt64Ty(), B.CreateStructGEP(B.getModule().getEntryStructType(), value, 1));

        B.PrintF({B.CreateGlobalCachedString("entry: %p = "), entryKeyString});
        B.Print(entryValue);
    }

    Value *LoxBuilder::TableSet(Value *Table, Value *Key, Value *V) {
        assert(V->getType() == IntegerType::get(getContext(), 64));
        static auto *AdjustCapacityFunction([this] {
            auto *const F = Function::Create(
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

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->arg_begin();
            auto *const table = arguments;
            auto *const capacity = arguments + 1;

            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("adjustCapcity: %d\n"), capacity});
            }
            auto *const entries = B.CreateRealloc(B.getNullPtr(), B.getSizeOf(B.getModule().getEntryStructType(), capacity), "entries");
            auto *const i = CreateEntryBlockAlloca(F, B.getInt32Ty(), "i");

            B.CreateStore(B.getInt32(0), i);

            // First initialize all entries to null values.
            auto *const ForCond = B.CreateBasicBlock("for.cond");
            auto *const ForBody = B.CreateBasicBlock("for.body");
            auto *const ForInc = B.CreateBasicBlock("for.inc");
            auto *const ForEnd = B.CreateBasicBlock("for.end");

            B.CreateBr(ForCond);
            B.SetInsertPoint(ForCond);
            B.CreateCondBr(B.CreateICmpSLT(B.CreateLoad(B.getInt32Ty(), i), capacity), ForBody, ForEnd);
            B.SetInsertPoint(ForBody);

            auto *const entry = B.CreateInBoundsGEP(B.getModule().getEntryStructType(), entries, B.CreateLoad(B.getInt32Ty(), i));

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
            B.CreateStore(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), i), B.getInt32(1), "i+1", true, true), i);
            B.CreateBr(ForCond);

            B.SetInsertPoint(ForEnd);

            B.CreateStore(
                B.getInt32(0),
                B.CreateStructGEP(B.getModule().getTableStructType(), table, 0)
            );

            // ...
            B.CreateStore(B.getInt32(0), i);

            auto *const ForCond2 = B.CreateBasicBlock("for.cond");
            auto *const ForBody2 = B.CreateBasicBlock("for.body");
            auto *const ForInc2 = B.CreateBasicBlock("for.inc");
            auto *const ForEnd2 = B.CreateBasicBlock("for.end");

            auto *const tableCapacity = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 1));

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

            auto *const NotNullBlock = B.CreateBasicBlock("key.notnull");

            auto *const entry2 = B.CreateInBoundsGEP(
                B.getModule().getEntryStructType(),
                B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 2)),
                B.CreateLoad(B.getInt32Ty(), i)
            );
            auto *const entryKeyPtr = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(getModule().getEntryStructType(), entry2, 0));
            B.CreateCondBr(B.CreateIsNull(entryKeyPtr), ForInc2, NotNullBlock);

            B.SetInsertPoint(NotNullBlock);

            auto *const dest = FindEntry(B, entries, capacity, entryKeyPtr);

            B.CreateStore(
                entryKeyPtr,
                B.CreateStructGEP(getModule().getEntryStructType(), dest, 0)
            );
            B.CreateStore(
                B.CreateLoad(B.getInt64Ty(), B.CreateStructGEP(getModule().getEntryStructType(), entry2, 1)),
                B.CreateStructGEP(getModule().getEntryStructType(), dest, 1)
            );

            auto *const count = B.CreateStructGEP(B.getModule().getTableStructType(), table, 0);
            B.CreateStore(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), count), B.getInt32(1), "count+1", true, true), count);

            B.CreateBr(ForInc2);
            B.SetInsertPoint(ForInc2);
            B.CreateStore(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), i), B.getInt32(1), "i+1", true, true), i);
            B.CreateBr(ForCond2);

            B.SetInsertPoint(ForEnd2);

            B.IRBuilder::CreateFree(B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 2)));
            B.CreateStore(capacity, B.CreateStructGEP(B.getModule().getTableStructType(), table, 1));
            B.CreateStore(entries, B.CreateStructGEP(B.getModule().getTableStructType(), table, 2));

            B.CreateRetVoid();

            return F;
        }());

        static auto *TableSetFunction([this] {
            auto *const F = Function::Create(
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

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->arg_begin();
            auto *const table = arguments;
            auto *const key = arguments + 1;
            auto *const value = arguments + 2;

            auto *const count = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 0));
            auto *const initialCapacity = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 1));

            auto *const CheckCapacityBlock = B.CreateBasicBlock("initialCapacity.check");
            auto *const EndCheckBlock = B.CreateBasicBlock("initialCapacity.checkend");

            B.CreateCondBr(
                B.CreateICmpSGT(
                    B.CreateAdd(count, B.getInt32(1), "count+1", true, true),
                    B.CreateFPToSI(B.CreateFMul(B.CreateSIToFP(initialCapacity, B.getDoubleTy()), ConstantFP::get(B.getDoubleTy(), 0.75)), B.getInt32Ty())
                ),
                CheckCapacityBlock,
                EndCheckBlock
            );
            B.SetInsertPoint(CheckCapacityBlock);

            auto *const newCapacity = B.CreateSelect(
                B.CreateICmpSLT(initialCapacity, B.getInt32(8)),
                B.getInt32(8),
                B.CreateMul(initialCapacity, B.getInt32(2), "newcapacity", true, true)
            );

            B.CreateCall(AdjustCapacityFunction, {table, newCapacity});

            B.CreateBr(EndCheckBlock);
            B.SetInsertPoint(EndCheckBlock);

            auto *const capacity = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 1));
            auto *const entry = FindEntry(B, B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 2)), capacity, key);
            auto *const isNewKey = B.CreateIsNull(B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(getModule().getEntryStructType(), entry, 0)));

            auto *const IsNewEntryBlock = B.CreateBasicBlock("newentry");
            auto *const EndBlock = B.CreateBasicBlock("end");

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
                B.CreateAdd(B.getInt32(1), B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 0)), "count", true, true),
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

        static auto *TableGetFunction([this] {
            auto *const F = Function::Create(
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

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->arg_begin();
            auto *const table = arguments;
            auto *const key = arguments + 1;

            auto *const count = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 0));

            auto *const IsEmptyBlock = B.CreateBasicBlock("table.empty");
            auto *const NotEmptyBlock = B.CreateBasicBlock("table.notempty");

            B.CreateCondBr(B.CreateICmpEQ(B.getInt32(0), count), IsEmptyBlock, NotEmptyBlock);

            B.SetInsertPoint(IsEmptyBlock);
            B.CreateRet(B.getUninitializedVal());

            B.SetInsertPoint(NotEmptyBlock);
            auto *const capacity = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 1));
            auto *const entries = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 2));

            auto *const entry = FindEntry(B, entries, capacity, key);
            auto *const entryKey = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(getModule().getEntryStructType(), entry, 0));

            auto *const EntryKeyNullBlock = B.CreateBasicBlock("entry.keynull");
            auto *const EndBlock = B.CreateBasicBlock("entry.end");

            B.CreateCondBr(B.CreateIsNull(entryKey), EntryKeyNullBlock, EndBlock);

            B.SetInsertPoint(EntryKeyNullBlock);
            {
                if constexpr (DEBUG_TABLE_ENTRIES) {
                    B.PrintF({B.CreateGlobalCachedString("return entry key null %p (%s)\n"), entryKey, B.AsCString(B.ObjVal(key))});
                }
                B.CreateRet(B.getUninitializedVal());
            }
            B.SetInsertPoint(EndBlock);

            auto *const entryValue = B.CreateLoad(B.getInt64Ty(), B.CreateStructGEP(getModule().getEntryStructType(), entry, 1));

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

        static auto *TableAddAllFunction([this] {
            auto *const F = Function::Create(
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

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->arg_begin();
            auto *const from = arguments;
            auto *const to = arguments + 1;

            auto *const capacity = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), from, 1));

            auto *const entries = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getTableStructType(), from, 2), "entries");

            auto *const i = CreateEntryBlockAlloca(F, B.getInt32Ty(), "i");

            B.CreateStore(B.getInt32(0), i);

            // First initialize all entries to null values.
            auto *const ForCond = B.CreateBasicBlock("for.cond");
            auto *const ForBody = B.CreateBasicBlock("for.body");
            auto *const ForInc = B.CreateBasicBlock("for.inc");
            auto *const ForEnd = B.CreateBasicBlock("for.end");

            B.CreateBr(ForCond);
            B.SetInsertPoint(ForCond);
            B.CreateCondBr(B.CreateICmpSLT(B.CreateLoad(B.getInt32Ty(), i), capacity), ForBody, ForEnd);
            B.SetInsertPoint(ForBody);

            auto *const entry = B.CreateInBoundsGEP(B.getModule().getEntryStructType(), entries, B.CreateLoad(B.getInt32Ty(), i), "entry");
            auto *const entryKey = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(getModule().getEntryStructType(), entry, 0));

            auto *const KeyIsNotNullBlock = B.CreateBasicBlock("key.notnull");

            B.CreateCondBr(B.CreateIsNull(entryKey), ForInc, KeyIsNotNullBlock);

            B.SetInsertPoint(KeyIsNotNullBlock);

            B.TableSet(to, entryKey, B.CreateLoad(B.getInt64Ty(), B.CreateStructGEP(getModule().getEntryStructType(), entry, 1)));

            B.CreateBr(ForInc);
            B.SetInsertPoint(ForInc);
            B.CreateStore(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), i), B.getInt32(1), "i+1", true, true), i);
            B.CreateBr(ForCond);

            B.SetInsertPoint(ForEnd);

            B.CreateRetVoid();

            return F;
        }());


        return CreateCall(TableAddAllFunction, {FromTable, ToTable});
    }

    Value *TableDelete(LoxBuilder &Builder, Value *Table, Value *Key) {

        static auto *TableDeleteFunction([&Builder] {
            auto *const F = Function::Create(
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

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->arg_begin();
            auto *const table = arguments;
            auto *const key = arguments + 1;

            auto *const count = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 0));

            auto *const IsEmptyBlock = B.CreateBasicBlock("table.empty");
            auto *const NotEmptyBlock = B.CreateBasicBlock("table.notempty");

            B.CreateCondBr(B.CreateICmpEQ(B.getInt32(0), count), IsEmptyBlock, NotEmptyBlock);

            B.SetInsertPoint(IsEmptyBlock);
            B.CreateRet(B.getFalse());

            B.SetInsertPoint(NotEmptyBlock);
            auto *const capacity = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 1));
            auto *const entries = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 2));

            auto *const entry = FindEntry(B, entries, capacity, key);
            auto *const entryKey = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getEntryStructType(), entry, 0));

            auto *const EntryKeyNullBlock = B.CreateBasicBlock("entry.keynull");
            auto *const EndBlock = B.CreateBasicBlock("entry.end");

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

        static auto *IterateTableFunction([&Builder] {
            auto *const F = Function::Create(
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

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->arg_begin();
            auto *const table = arguments;
            auto *const function = arguments + 1;

            auto *const capacity = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 1));
            auto *const entries = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getTableStructType(), table, 2), "entries");
            auto *const i = CreateEntryBlockAlloca(F, B.getInt32Ty(), "i");

            B.CreateStore(B.getInt32(0), i);

            auto *const ForCond = B.CreateBasicBlock("for.cond");
            auto *const ForBody = B.CreateBasicBlock("for.body");
            auto *const ForInc = B.CreateBasicBlock("for.inc");
            auto *const ForEnd = B.CreateBasicBlock("for.end");

            B.CreateBr(ForCond);
            B.SetInsertPoint(ForCond);
            B.CreateCondBr(B.CreateICmpSLT(B.CreateLoad(B.getInt32Ty(), i), capacity), ForBody, ForEnd);
            B.SetInsertPoint(ForBody);
            auto *const entry = B.CreateInBoundsGEP(B.getModule().getEntryStructType(), entries, B.CreateLoad(B.getInt32Ty(), i), "entry");
            auto *const entryKey = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getEntryStructType(), entry, 0));
            auto *const entryValue = B.CreateLoad(B.getInt64Ty(), B.CreateStructGEP(B.getModule().getEntryStructType(), entry, 1));

            auto *const KeyIsNotNullBlock = B.CreateBasicBlock("key.notnull");
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

            B.CreateStore(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), i), B.getInt32(1), "i+1", true, true), i);

            B.CreateBr(ForCond);

            B.SetInsertPoint(ForEnd);

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(IterateTableFunction, {Table, FunctionPtr});
    }

}// namespace lox