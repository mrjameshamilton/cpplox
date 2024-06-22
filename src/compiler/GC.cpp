#include "GC.h"

#include "../Debug.h"
#include "Memory.h"
#include "ModuleCompiler.h"
#include "Upvalue.h"

#include "Stack.h"
#include "Table.h"

#include <llvm/Transforms/Utils/BasicBlockUtils.h>

namespace lox {

    void MarkObject(LoxBuilder &Builder, Value *ObjectPtr) {
        assert(ObjectPtr->getType() == Builder.getPtrTy());

        static auto *const MarkObjectFunction = Builder.getModule().getFunction("$markObject");
        Builder.CreateCall(MarkObjectFunction, {ObjectPtr});
    }

    static void MarkValue(LoxBuilder &B, Value *value) {
        assert(value->getType() == B.getInt64Ty());

        auto *const IsObjBlock = B.CreateBasicBlock("is.obj");
        auto *const EndBlock = B.CreateBasicBlock("end.obj");

        B.CreateCondBr(B.IsObj(value), IsObjBlock, EndBlock);
        B.SetInsertPoint(IsObjBlock);
        MarkObject(B, B.AsObj(value));
        B.CreateBr(EndBlock);
        B.SetInsertPoint(EndBlock);
    }

    static void MarkTable(LoxBuilder &Builder, Value *Table) {
        assert(Table->getType() == Builder.getPtrTy());

        static auto *MarkTableEntryFunction([&Builder] {
            auto *const F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy(), Builder.getPtrTy(), Builder.getInt64Ty()},
                    false
                ),
                Function::InternalLinkage,
                "$markTableEntry",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->arg_begin();
            [[unused]] auto *const table = arguments;
            auto *const key = arguments + 1;
            auto *const value = arguments + 2;

            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("mark table entry %p\n"), key});
                B.PrintString(B.ObjVal(key));
            }

            MarkObject(B, key);
            MarkValue(B, value);

            B.CreateRetVoid();

            return F;
        }());

        IterateTable(Builder, Table, MarkTableEntryFunction);
    }

    static void BlackenObject(LoxBuilder &Builder, Value *ObjectPtr) {
        assert(ObjectPtr->getType() == Builder.getPtrTy());

        static auto *BlackObjectFunction([&Builder] {
            auto *const F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$blackenObject",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const value = B.ObjVal(F->arg_begin());
            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("blacken(%p, type: %d) = \n"), F->arg_begin(), value});//B.ObjType(value)});
                //B.Print(value);
            }

            auto *const IsClosureBlock = B.CreateBasicBlock("print.closure");
            auto *const IsFunctionBlock = B.CreateBasicBlock("print.function");
            auto *const IsUpvalueBlock = B.CreateBasicBlock("print.upvalue");
            auto *const IsClassBlock = B.CreateBasicBlock("print.class");
            auto *const IsInstanceBlock = B.CreateBasicBlock("print.instance");
            auto *const IsBoundMethod = B.CreateBasicBlock("print.boundmethod");
            auto *const DefaultBlock = B.CreateBasicBlock("print.default");
            auto *const EndBlock = B.CreateBasicBlock("print.end");

            auto *const Switch = B.CreateSwitch(B.ObjType(value), DefaultBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::STRING), EndBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::CLOSURE), IsClosureBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::FUNCTION), IsFunctionBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::UPVALUE), IsUpvalueBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::CLASS), IsClassBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::INSTANCE), IsInstanceBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::BOUND_METHOD), IsBoundMethod);

            B.SetInsertPoint(IsFunctionBlock);
            {
                auto *const function = B.AsObj(value);
                auto *const name = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::FUNCTION, function, 3));
                MarkObject(B, function);
                MarkObject(B, name);
                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(IsClosureBlock);
            {
                auto *const closure = B.AsObj(value);
                auto *const function = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::CLOSURE, closure, 1));
                auto *const name = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::FUNCTION, function, 3));

                MarkObject(B, function);
                MarkObject(B, name);

                auto *const i = CreateEntryBlockAlloca(F, B.getInt32Ty(), "i", [](auto &B, auto *alloca) {
                    B.CreateStore(B.getInt32(0), alloca);
                });
                auto *const size = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getStructType(ObjType::CLOSURE), closure, 3));
                auto *const array = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getStructType(ObjType::CLOSURE), closure, 2));

                auto *const WhileCond = B.CreateBasicBlock("while.cond");
                auto *const WhileBody = B.CreateBasicBlock("while.body");

                B.CreateBr(WhileCond);
                B.SetInsertPoint(WhileCond);
                {
                    B.CreateCondBr(B.CreateICmpSLT(B.CreateLoad(B.getInt32Ty(), i), size), WhileBody, EndBlock);
                    B.SetInsertPoint(WhileBody);

                    auto *const addr = B.CreateInBoundsGEP(B.getPtrTy(), array, B.CreateLoad(B.getInt32Ty(), i));
                    auto *const object_ptr = B.CreateLoad(B.getPtrTy(), addr);

                    MarkObject(B, object_ptr);

                    B.CreateStore(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), i), B.getInt32(1), "i+1", true, true), i);
                    B.CreateBr(WhileCond);
                }
            }
            B.SetInsertPoint(IsUpvalueBlock);
            {
                auto *const upvalue = B.AsObj(value);
                auto *const closed = B.CreateLoad(B.getInt64Ty(), B.CreateObjStructGEP(ObjType::UPVALUE, upvalue, 3));
                if constexpr (DEBUG_LOG_GC) {
                    B.PrintF({B.CreateGlobalCachedString("upvalue.closed(%d, %p) = "), closed, B.AsObj(closed)});
                    B.Print(closed);
                }
                MarkValue(B, closed);
                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(IsClassBlock);
            {
                auto *const klass = B.AsObj(value);
                auto *const className = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::CLASS, klass, 1));
                MarkObject(B, className);
                auto *const methods = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::CLASS, klass, 2));
                MarkTable(B, methods);
                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(IsInstanceBlock);
            {
                auto *const instance = B.AsObj(value);
                auto *const instanceKlass = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::INSTANCE, instance, 1));
                auto *const fields = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::INSTANCE, instance, 2));
                if constexpr (DEBUG_LOG_GC) {
                    B.PrintF({B.CreateGlobalCachedString("black instance %s\n"), B.AsCString(B.CreateLoad(B.getInt64Ty(), B.CreateObjStructGEP(ObjType::CLASS, instanceKlass, 1)))});
                }
                MarkObject(B, instanceKlass);
                MarkTable(B, fields);

                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(IsBoundMethod);
            {
                auto *const bound = B.AsObj(value);
                auto *const receiver = B.CreateLoad(B.getInt64Ty(), B.CreateObjStructGEP(ObjType::BOUND_METHOD, bound, 1));
                auto *const methodClosure = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::BOUND_METHOD, bound, 2));
                if constexpr (DEBUG_LOG_GC) {
                    B.PrintF({B.CreateGlobalCachedString("black bound method(%p,%d,%p) = "), bound, receiver, methodClosure});
                    B.CreateCall(FunctionType::get(B.getVoidTy(), B.getInt64Ty(), false), B.getModule().getFunction("$print"), B.ObjVal(methodClosure));
                }
                MarkValue(B, receiver);
                MarkObject(B, methodClosure);
                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(DefaultBlock);
            {
                if constexpr (DEBUG_LOG_GC) {
                    //B.PrintF({B.CreateGlobalCachedString("not blackening {{object %d}}\n"), B.ObjType(value)});
                }
                B.CreateBr(EndBlock);
            }

            B.SetInsertPoint(EndBlock);
            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(BlackObjectFunction, {ObjectPtr});
    }

    static void TraceReferences(LoxBuilder &Builder) {
        static auto *BlackenFunction([&Builder] {
            auto *const F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$blacken",
                Builder.getModule()
            );

            F->addFnAttr(Attribute::AlwaysInline);

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);
            BlackenObject(B, F->arg_begin());
            B.CreateRetVoid();

            return F;
        }());

        if constexpr (DEBUG_LOG_GC) {
            Builder.PrintString("-- trace refs --");
        }

        Builder.getModule().getGrayStack().CreatePopAll(Builder, BlackenFunction);
    }

    static void MarkGlobalRoots(LoxBuilder &Builder) {
        if constexpr (DEBUG_LOG_GC) {
            Builder.PrintString("--iterate globals--");
        }

        static auto *const MarkGlobalRootsFunction = [&Builder] {
            auto *const F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {},
                    false
                ),
                Function::InternalLinkage,
                "$markGlobalRoots",
                Builder.getModule()
            );
            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);
            // The body of the function will be added to everytime
            // a global is created with code that will call the MarkObject
            // function.
            B.CreateRetVoid();

            return F;
        }();

        Builder.CreateCall(MarkGlobalRootsFunction);

        if constexpr (DEBUG_LOG_GC) {
            Builder.PrintString("--end iterate globals--");
        }
    }

    static void IterateLocals(LoxBuilder &Builder, Function *FunctionPointer) {
        if constexpr (DEBUG_LOG_GC) {
            auto *const sp = Builder.getModule().getLocalsStack().CreateGetCount(Builder);
            Builder.PrintF({Builder.CreateGlobalCachedString("--iterate locals (%d)--\n"), sp});
        }

        Builder.getModule().getLocalsStack().CreateIterateObjectValues(Builder, FunctionPointer);
    }

    static void MarkRoots(LoxBuilder &Builder) {
        static auto *const MarkObjectFunction = Builder.getModule().getFunction("$markObject");
        if constexpr (DEBUG_LOG_GC) {
            Builder.PrintString("--mark roots--");
        }
        IterateLocals(Builder, MarkObjectFunction);
        MarkGlobalRoots(Builder);
        IterateUpvalues(Builder, MarkObjectFunction);
        if constexpr (DEBUG_LOG_GC) {
            Builder.PrintString("--end mark roots--");
        }
    }

    static void Sweep(LoxBuilder &Builder) {
        static auto *TraceRefsFunction([&Builder] {
            auto *const F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {},
                    false
                ),
                Function::InternalLinkage,
                "$sweep",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);
            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const object = CreateEntryBlockAlloca(F, B.getPtrTy(), "object");
            auto *const previous = CreateEntryBlockAlloca(F, B.getPtrTy(), "next");
            B.CreateStore(
                B.CreateLoad(B.getPtrTy(), B.getModule().getObjects()),
                object
            );
            B.CreateStore(B.getNullPtr(), previous);

            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("--sweep (objects @ %p)--\n"), B.CreateLoad(B.getPtrTy(), object)});
            }

            auto *const WhileCond = B.CreateBasicBlock("while.cond");
            auto *const WhileBody = B.CreateBasicBlock("while.body");
            auto *const WhileEnd = B.CreateBasicBlock("while.end");

            B.CreateBr(WhileCond);
            B.SetInsertPoint(WhileCond);
            B.CreateCondBr(B.CreateIsNotNull(B.CreateLoad(B.getPtrTy(), object)), WhileBody, WhileEnd);
            B.SetInsertPoint(WhileBody);
            {
                auto *const IsNotMarkedBlock = B.CreateBasicBlock("is.notmarked");
                auto *const IsMarkedBlock = B.CreateBasicBlock("is.marked");

                auto *const isMarked = B.CreateLoad(B.getInt1Ty(), B.CreateStructGEP(B.getModule().getObjStructType(), B.CreateLoad(B.getPtrTy(), object), 1, "isMarked"));
                B.CreateCondBr(B.CreateICmpEQ(B.getTrue(), isMarked), IsMarkedBlock, IsNotMarkedBlock);

                B.SetInsertPoint(IsMarkedBlock);
                {
                    B.CreateStore(B.getFalse(), B.CreateStructGEP(B.getModule().getObjStructType(), B.CreateLoad(B.getPtrTy(), object), 1, "isMarked"));
                    B.CreateStore(B.CreateLoad(B.getPtrTy(), object), previous);
                    auto *const nextptr = B.CreateLoad(
                        B.getPtrTy(),
                        B.CreateStructGEP(B.getModule().getObjStructType(), B.CreateLoad(B.getPtrTy(), object), 2, "next")
                    );
                    B.CreateStore(
                        nextptr,
                        object
                    );
                    B.CreateBr(WhileCond);
                }
                B.SetInsertPoint(IsNotMarkedBlock);
                {
                    auto *const unreached = CreateEntryBlockAlloca(F, B.getPtrTy(), "unreached");

                    B.CreateStore(B.CreateLoad(B.getPtrTy(), object), unreached);
                    B.CreateStore(
                        B.CreateLoad(
                            B.getPtrTy(),
                            B.CreateStructGEP(B.getModule().getObjStructType(), B.CreateLoad(B.getPtrTy(), object), 2, "next")
                        ),
                        object
                    );

                    auto *const IsNull = B.CreateBasicBlock("is.notmarked.null");
                    auto *const IsNotNull = B.CreateBasicBlock("is.notmarked.notnull");
                    auto *const End = B.CreateBasicBlock("end");

                    B.CreateCondBr(B.CreateIsNull(B.CreateLoad(B.getPtrTy(), previous)), IsNull, IsNotNull);
                    B.SetInsertPoint(IsNull);
                    {
                        B.CreateStore(B.CreateLoad(B.getPtrTy(), object), B.getModule().getObjects());
                        B.CreateBr(End);
                    }
                    B.SetInsertPoint(IsNotNull);
                    {
                        B.CreateStore(
                            B.CreateLoad(B.getPtrTy(), object),
                            B.CreateStructGEP(B.getModule().getObjStructType(), B.CreateLoad(B.getPtrTy(), previous), 2, "next")
                        );
                        B.CreateBr(End);
                    }
                    B.SetInsertPoint(End);

                    if constexpr (DEBUG_LOG_GC) {
                        B.PrintF({B.CreateGlobalCachedString("unreached %p: "), B.CreateLoad(B.getPtrTy(), unreached)});
                    }

                    FreeObject(B, B.ObjVal(B.CreateLoad(B.getPtrTy(), unreached)));

                    B.CreateBr(WhileCond);
                }
            }
            B.SetInsertPoint(WhileEnd);

            if constexpr (DEBUG_LOG_GC) {
                B.PrintString("--end sweep--");
            }

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(TraceRefsFunction);
    }

    static void RemoveWhiteStrings(LoxBuilder &Builder) {
        static auto *RemoveWhiteFunction([&Builder] {
            auto *const F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy(), Builder.getPtrTy(), Builder.getInt64Ty()},
                    false
                ),
                Function::InternalLinkage,
                "$removeWhite",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->arg_begin();
            auto *const table = arguments;
            auto *const key = arguments + 1;


            auto *const ShouldDeleteBlock = B.CreateBasicBlock("should.delete");
            auto *const EndBlock = B.CreateBasicBlock("end");

            auto *const isMarked = B.CreateStructGEP(B.getModule().getObjStructType(), key, 1, "isMarked");
            B.CreateCondBr(B.CreateAnd(B.CreateIsNotNull(key), B.CreateNot(B.CreateLoad(B.getInt1Ty(), isMarked))), ShouldDeleteBlock, EndBlock);
            B.SetInsertPoint(ShouldDeleteBlock);
            {
                if constexpr (DEBUG_LOG_GC) {
                    B.PrintF({B.CreateGlobalCachedString("remove white ")});
                    B.PrintString(B.ObjVal(key));
                }
                TableDelete(B, table, key);
                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(EndBlock);

            B.CreateRetVoid();

            return F;
        }());

        IterateTable(Builder, Builder.CreateLoad(Builder.getPtrTy(), Builder.getModule().getRuntimeStrings()), RemoveWhiteFunction);
    }

    /**
     * Creates the $gc and $markObject functions.
     *
     * @return the $gc function.
     */
    Function *CreateGcFunction(LoxBuilder &Builder) {
        static auto *const GCFunction = Function::Create(
            FunctionType::get(
                Builder.getVoidTy(),
                {Builder.getInt1Ty(), Builder.getPtrTy()},
                false
            ),
            Function::InternalLinkage,
            "$gc",
            Builder.getModule()
        );

        [[maybe_unused]] static auto *MarkObjectFunction([&Builder] {
            auto *const F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$markObject",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->arg_begin();
            auto *const ObjectPtr = arguments;

            auto *const IsNotNull = B.CreateBasicBlock("is.notnull");
            auto *const IsNotMarkedBlock = B.CreateBasicBlock("is.notmarked");
            auto *const IsMarkedBlock = DEBUG_LOG_GC ? B.CreateBasicBlock("is.marked") : nullptr;
            auto *const EndBlock = B.CreateBasicBlock("end.obj");

            B.CreateCondBr(B.CreateIsNull(ObjectPtr), EndBlock, IsNotNull);
            B.SetInsertPoint(IsNotNull);
            {
                auto *const isMarked = B.CreateLoad(
                    B.getInt1Ty(),
                    B.CreateStructGEP(B.getModule().getObjStructType(), ObjectPtr, 1, "isMarked")
                );

                if constexpr (DEBUG_LOG_GC) {
                    B.CreateCondBr(B.CreateICmpEQ(B.getTrue(), isMarked), IsMarkedBlock, IsNotMarkedBlock);
                    B.SetInsertPoint(IsMarkedBlock);
                    B.PrintF({B.CreateGlobalCachedString("already marked(%p)\n"), ObjectPtr});
                    //B.Print(B.ObjVal(ObjectPtr));
                    B.CreateBr(EndBlock);
                } else {
                    B.CreateCondBr(B.CreateICmpEQ(B.getTrue(), isMarked), EndBlock, IsNotMarkedBlock);
                }
            }
            B.SetInsertPoint(IsNotMarkedBlock);
            {
                if constexpr (DEBUG_LOG_GC) {
                    B.PrintF({B.CreateGlobalCachedString("mark(%p, type: %d) = "), ObjectPtr, B.ObjType(B.ObjVal(ObjectPtr))});
                    B.Print(B.ObjVal(ObjectPtr));
                }

                B.getModule().getGrayStack().CreatePush(B.getModule(), B, ObjectPtr);

                B.CreateStore(
                    B.getTrue(),
                    B.CreateStructGEP(B.getModule().getObjStructType(), ObjectPtr, 1, "isMarked")
                );

                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(EndBlock);
            B.CreateRetVoid();

            return F;
        }());

        [[maybe_unused]] static auto *GCFunctionBuilder([&Builder] {
            LoxBuilder B(Builder.getContext(), Builder.getModule(), *GCFunction);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const force = B.getFunction()->arg_begin();
            auto *const extraRoot = B.getFunction()->arg_begin() + 1;

            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("-- start GC (force? %d)---\n"), force});
                B.PrintF({B.CreateGlobalCachedString("  extra root: %p\n"), extraRoot});
            }

            auto *const CollectBlock = B.CreateBasicBlock("collect");
            auto *const EndBlock = B.CreateBasicBlock("end");

            B.CreateCondBr(
                B.CreateAnd(
                    B.CreateLoad(B.getInt1Ty(), B.getModule().getEnableGC()),
                    B.CreateOr(
                        force,
                        B.CreateICmpSGT(B.CreateLoad(B.getInt32Ty(), B.getModule().getAllocatedBytes()), B.CreateLoad(B.getInt32Ty(), B.getModule().getNextGC()))
                    )
                ),
                CollectBlock, EndBlock
            );
            B.SetInsertPoint(EndBlock);
            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("skip GC\n")});
            }
            B.CreateRetVoid();

            B.SetInsertPoint(CollectBlock);
            auto *const before = B.CreateLoad(B.getInt32Ty(), B.getModule().getAllocatedBytes());

            // Mark the extra root, if any (maybe nullptr).
            B.CreateCall(MarkObjectFunction, {extraRoot});

            MarkRoots(B);
            TraceReferences(B);
            RemoveWhiteStrings(B);
            Sweep(B);

            B.CreateStore(
                B.CreateMul(B.getInt32(GC_GROWTH_FACTOR), B.CreateLoad(B.getInt32Ty(), B.getModule().getAllocatedBytes()), "nextGC", true, true),
                B.getModule().getNextGC()
            );

            if constexpr (DEBUG_LOG_GC) {
                B.PrintString("-- end GC ---");
                B.PrintF({B.CreateGlobalCachedString("     collected %zu bytes (from %zu to %zu) next at %zu\n"), B.CreateSub(before, B.CreateLoad(B.getInt32Ty(), B.getModule().getAllocatedBytes()), "from", true, true), before, B.CreateLoad(B.getInt32Ty(), B.getModule().getAllocatedBytes()), B.CreateLoad(B.getInt32Ty(), B.getModule().getNextGC())});
            }

            B.CreateRetVoid();

            return GCFunction;
        }());

        return GCFunction;
    }

    void LoxBuilder::CollectGarbage(const bool force = false, Value *extraRoot) {
        CreateCall(getModule().getFunction("$gc"), {force ? getTrue() : getFalse(), extraRoot ? extraRoot : getNullPtr()});
    }

    /**
     * Each call to this function will append code to the $markGlobalRoots
     * function to mark the value in the global as a root, if it's an object.
     */
    void AddGlobalGCRoot(LoxModule &Module, GlobalVariable *global) {
        auto *const F = Module.getFunction("$markGlobalRoots");
        auto *const EntryBlock = &F->getEntryBlock();
        auto *const TerminatorInstruction = EntryBlock->getTerminator();

        LoxBuilder B(Module.getContext(), Module, *F);
        B.SetInsertPoint(EntryBlock->begin());
        auto *const value = B.CreateLoad(B.getInt64Ty(), global);

        BasicBlock *IsObjBlock = nullptr;
        SplitBlockAndInsertIfThenElse(B.IsObj(value), TerminatorInstruction, &IsObjBlock, nullptr);

        B.SetInsertPoint(IsObjBlock->begin());
        MarkObject(B, B.AsObj(value));
    }

    Value *DelayGC(LoxBuilder &B, const std::function<Value *(LoxBuilder &)> &block) {
        if constexpr (DEBUG_LOG_GC) {
            B.PrintF({B.CreateGlobalCachedString("disable gc\n")});
        }
        auto *const original = B.CreateLoad(B.getInt1Ty(), B.getModule().getEnableGC());
        B.CreateStore(B.getFalse(), B.getModule().getEnableGC());
        auto *const result = block(B);
        if constexpr (DEBUG_LOG_GC) {
            B.PrintF({B.CreateGlobalCachedString("enable gc\n")});
        }
        B.CreateStore(original, B.getModule().getEnableGC());

        // Collect garbage if necessary, passing the result of the block
        // function as an extra GC root (because the result may not
        // be reachable yet via a local/global).
        B.CollectGarbage(false, result);

        return result;
    }
}// namespace lox
