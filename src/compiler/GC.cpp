#include "GC.h"

#include "../Debug.h"
#include "Memory.h"
#include "ModuleCompiler.h"
#include "Upvalue.h"

#include "Stack.h"
#include "Table.h"

#include <memory>

namespace lox {

    static void MarkObject(LoxBuilder &B, Value *ObjectPtr) {
        assert(ObjectPtr->getType() == B.getPtrTy());

        const auto IsNotNull = B.CreateBasicBlock("is.notnull");
        const auto IsNotMarkedBlock = B.CreateBasicBlock("is.notmarked");
        const auto IsMarkedBlock = DEBUG_LOG_GC ? B.CreateBasicBlock("is.marked") : nullptr;
        const auto EndBlock = B.CreateBasicBlock("end.obj");

        B.CreateCondBr(B.CreateIsNull(ObjectPtr), EndBlock, IsNotNull);
        B.SetInsertPoint(IsNotNull);

        const auto isMarked = B.CreateLoad(B.getInt1Ty(), B.CreateStructGEP(B.getModule().getObjStructType(), ObjectPtr, 1, "isMarked"));

        if constexpr (DEBUG_LOG_GC) {
            B.CreateCondBr(B.CreateICmpEQ(B.getTrue(), isMarked), IsMarkedBlock, IsNotMarkedBlock);
            B.SetInsertPoint(IsMarkedBlock);
            B.PrintF({B.CreateGlobalCachedString("already marked(%p) = "), ObjectPtr});
            B.Print(B.ObjVal(ObjectPtr));
            B.CreateBr(EndBlock);
        } else {
            B.CreateCondBr(B.CreateICmpEQ(B.getTrue(), isMarked), EndBlock, IsNotMarkedBlock);
        }

        B.SetInsertPoint(IsNotMarkedBlock);

        if constexpr (DEBUG_LOG_GC) {
            B.PrintF({B.CreateGlobalCachedString("mark(%p, type: %d) = "), ObjectPtr, B.ObjType(B.ObjVal(ObjectPtr))});
            B.Print(B.ObjVal(ObjectPtr));
        }

        B.getModule().getGrayStack()->CreatePush(B, ObjectPtr, "grey");

        B.CreateStore(
            B.getTrue(),
            B.CreateStructGEP(B.getModule().getObjStructType(), ObjectPtr, 1, "isMarked")
        );

        B.CreateBr(EndBlock);
        B.SetInsertPoint(EndBlock);
    }

    static void MarkValue(LoxBuilder &B, Value *value) {
        assert(value->getType() == B.getInt64Ty());

        const auto IsObjBlock = B.CreateBasicBlock("is.obj");
        const auto EndBlock = B.CreateBasicBlock("end.obj");

        B.CreateCondBr(B.IsObj(value), IsObjBlock, EndBlock);
        B.SetInsertPoint(IsObjBlock);
        MarkObject(B, B.AsObj(value));
        B.CreateBr(EndBlock);
        B.SetInsertPoint(EndBlock);
    }

    static void MarkTable(LoxBuilder &Builder, Value *Table) {
        assert(Table->getType() == Builder.getPtrTy());

        static auto MarkTableEntryFunction([&Builder] {
            const auto F = Function::Create(
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

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->arg_begin();
            [[unused]] const auto table = arguments;
            const auto key = arguments + 1;
            const auto value = arguments + 2;

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

        static auto BlackObjectFunction([&Builder] {
            const auto F = Function::Create(
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

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto value = B.ObjVal(F->arg_begin());
            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("blacken(%p, type: %d) = "), F->arg_begin(), B.ObjType(value)});
                B.Print(value);
            }

            const auto IsClosureBlock = B.CreateBasicBlock("print.closure");
            const auto IsFunctionBlock = B.CreateBasicBlock("print.function");
            const auto IsUpvalueBlock = B.CreateBasicBlock("print.upvalue");
            const auto IsClassBlock = B.CreateBasicBlock("print.class");
            const auto IsInstanceBlock = B.CreateBasicBlock("print.instance");
            const auto IsBoundMethod = B.CreateBasicBlock("print.boundmethod");
            const auto DefaultBlock = B.CreateBasicBlock("print.default");
            const auto EndBlock = B.CreateBasicBlock("print.end");

            const auto Switch = B.CreateSwitch(B.ObjType(value), DefaultBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::STRING), EndBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::CLOSURE), IsClosureBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::FUNCTION), IsFunctionBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::UPVALUE), IsUpvalueBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::CLASS), IsClassBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::INSTANCE), IsInstanceBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::BOUND_METHOD), IsBoundMethod);

            B.SetInsertPoint(IsFunctionBlock);
            {
                const auto function = B.AsObj(value);
                const auto name = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::FUNCTION, function, 3));
                MarkObject(B, function);
                MarkObject(B, name);
                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(IsClosureBlock);
            {
                const auto closure = B.AsObj(value);
                const auto function = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::CLOSURE, closure, 1));
                const auto name = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::FUNCTION, function, 3));

                MarkObject(B, function);
                MarkObject(B, name);

                const auto i = CreateEntryBlockAlloca(F, B.getInt32Ty(), "i", B.getInt32(0));
                const auto size = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getStructType(ObjType::CLOSURE), closure, 3));
                const auto array = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getStructType(ObjType::CLOSURE), closure, 2));

                const auto WhileCond = B.CreateBasicBlock("while.cond");
                const auto WhileBody = B.CreateBasicBlock("while.body");

                B.CreateBr(WhileCond);
                B.SetInsertPoint(WhileCond);
                B.CreateCondBr(B.CreateICmpSLT(B.CreateLoad(B.getInt32Ty(), i), size), WhileBody, EndBlock);
                B.SetInsertPoint(WhileBody);

                const auto addr = B.CreateGEP(B.getPtrTy(), array, B.CreateLoad(B.getInt32Ty(), i));
                const auto object_ptr = B.CreateLoad(B.getPtrTy(), addr);

                MarkObject(B, object_ptr);

                B.CreateStore(B.CreateAdd(B.CreateLoad(B.getInt32Ty(), i), B.getInt32(1)), i);
                B.CreateBr(WhileCond);
            }
            B.SetInsertPoint(IsUpvalueBlock);
            {
                const auto upvalue = B.AsObj(value);
                const auto closed = B.CreateLoad(B.getInt64Ty(), B.CreateObjStructGEP(ObjType::UPVALUE, upvalue, 3));
                if constexpr (DEBUG_LOG_GC) {
                    B.PrintF({B.CreateGlobalCachedString("upvalue.closed(%d, %p) = "), closed, B.AsObj(closed)});
                    B.Print(closed);
                }
                MarkValue(B, closed);
                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(IsClassBlock);
            {
                const auto klass = B.AsObj(value);
                const auto className = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::CLASS, klass, 1));
                MarkObject(B, className);
                const auto methods = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::CLASS, klass, 2));
                MarkTable(B, methods);
                B.CreateBr(EndBlock);
            }

            B.SetInsertPoint(IsInstanceBlock);
            {
                const auto instance = B.AsObj(value);
                const auto instanceKlass = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::INSTANCE, instance, 1));
                const auto fields = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::INSTANCE, instance, 2));
                if constexpr (DEBUG_LOG_GC) {
                    B.PrintF({B.CreateGlobalCachedString("black instance %s\n"), B.AsCString(B.CreateLoad(B.getInt64Ty(), B.CreateObjStructGEP(ObjType::CLASS, instanceKlass, 1)))});
                }
                MarkObject(B, instanceKlass);
                MarkTable(B, fields);

                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(IsBoundMethod);
            {
                const auto bound = B.AsObj(value);
                const auto receiver = B.CreateLoad(B.getInt64Ty(), B.CreateObjStructGEP(ObjType::BOUND_METHOD, bound, 1));
                const auto methodClosure = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::BOUND_METHOD, bound, 2));
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
                    B.PrintF({B.CreateGlobalCachedString("not blackening {{object %d}}\n"), B.ObjType(value)});
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
        static auto BlackenFunction([&Builder] {
            const auto F = Function::Create(
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

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);
            BlackenObject(B, F->arg_begin());
            B.CreateRetVoid();

            return F;
        }());

        if constexpr (DEBUG_LOG_GC) {
            Builder.PrintString("-- trace refs --");
        }

        Builder.getModule().getGrayStack()->CreatePopAll(Builder, BlackenFunction);
    }

    static void MarkRoots(LoxBuilder &Builder) {
        static auto MarkObjectFunction([&Builder] {
            const auto F = Function::Create(
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

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            MarkObject(B, F->arg_begin());

            B.CreateRetVoid();

            return F;
        }());

        if constexpr (DEBUG_LOG_GC) {
            Builder.PrintString("--mark roots--");
        }
        IterateLocals(Builder, MarkObjectFunction);
        IterateGlobals(Builder, MarkObjectFunction);
        IterateUpvalues(Builder, MarkObjectFunction);
        if constexpr (DEBUG_LOG_GC) {
            Builder.PrintString("--end mark roots--");
        }
    }

    static void Sweep(LoxBuilder &Builder) {
        static auto TraceRefsFunction([&Builder] {
            const auto F = Function::Create(
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
            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto object = CreateEntryBlockAlloca(F, B.getPtrTy(), "object");
            const auto previous = CreateEntryBlockAlloca(F, B.getPtrTy(), "next");
            B.CreateStore(
                B.CreateLoad(B.getPtrTy(), B.getModule().getObjects()),
                object
            );
            B.CreateStore(B.getNullPtr(), previous);

            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("--sweep (objects @ %p)--\n"), B.CreateLoad(B.getPtrTy(), object)});
            }

            const auto WhileCond = B.CreateBasicBlock("while.cond");
            const auto WhileBody = B.CreateBasicBlock("while.body");
            const auto WhileEnd = B.CreateBasicBlock("while.end");

            B.CreateBr(WhileCond);
            B.SetInsertPoint(WhileCond);
            B.CreateCondBr(B.CreateIsNotNull(B.CreateLoad(B.getPtrTy(), object)), WhileBody, WhileEnd);
            B.SetInsertPoint(WhileBody);
            {
                const auto IsNotMarkedBlock = B.CreateBasicBlock("is.notmarked");
                const auto IsMarkedBlock = B.CreateBasicBlock("is.marked");

                const auto isMarked = B.CreateLoad(B.getInt1Ty(), B.CreateStructGEP(B.getModule().getObjStructType(), B.CreateLoad(B.getPtrTy(), object), 1, "isMarked"));
                B.CreateCondBr(B.CreateICmpEQ(B.getTrue(), isMarked), IsMarkedBlock, IsNotMarkedBlock);

                B.SetInsertPoint(IsMarkedBlock);
                {
                    B.CreateStore(B.getFalse(), B.CreateStructGEP(B.getModule().getObjStructType(), B.CreateLoad(B.getPtrTy(), object), 1, "isMarked"));
                    B.CreateStore(B.CreateLoad(B.getPtrTy(), object), previous);
                    const auto nextptr = B.CreateLoad(
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
                    const auto unreached = CreateEntryBlockAlloca(F, B.getPtrTy(), "unreached");

                    B.CreateStore(B.CreateLoad(B.getPtrTy(), object), unreached);
                    B.CreateStore(
                        B.CreateLoad(
                            B.getPtrTy(),
                            B.CreateStructGEP(B.getModule().getObjStructType(), B.CreateLoad(B.getPtrTy(), object), 2, "next")
                        ),
                        object
                    );

                    const auto IsNull = B.CreateBasicBlock("is.notmarked.null");
                    const auto IsNotNull = B.CreateBasicBlock("is.notmarked.notnull");
                    const auto End = B.CreateBasicBlock("end");

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
        static auto RemoveWhiteFunction([&Builder] {
            const auto F = Function::Create(
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

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->arg_begin();
            const auto table = arguments;
            const auto key = arguments + 1;


            const auto ShouldDeleteBlock = B.CreateBasicBlock("should.delete");
            const auto EndBlock = B.CreateBasicBlock("end");

            const auto isMarked = B.CreateStructGEP(B.getModule().getObjStructType(), key, 1, "isMarked");
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

    Function *CreateGcFunction(LoxBuilder &Builder) {
        // TODO: shrink stacks as well
        static auto GCFunction([&Builder] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {},
                    false
                ),
                Function::InternalLinkage,
                "$gc",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            if constexpr (DEBUG_LOG_GC) {
                B.PrintString("-- start GC ---");
            }

            const auto before = B.CreateLoad(B.getInt32Ty(), B.getModule().getAllocatedBytes());

            MarkRoots(B);
            TraceReferences(B);
            RemoveWhiteStrings(B);
            Sweep(B);

            B.CreateStore(
                B.CreateMul(B.getInt32(GC_GROWTH_FACTOR), B.CreateLoad(B.getInt32Ty(), B.getModule().getAllocatedBytes())),
                B.getModule().getNextGC()
            );

            if constexpr (DEBUG_LOG_GC) {
                B.PrintString("-- end GC ---");
                B.PrintF({B.CreateGlobalCachedString("     collected %zu bytes (from %zu to %zu) next at %zu\n"), B.CreateSub(before, B.CreateLoad(B.getInt32Ty(), B.getModule().getAllocatedBytes())), before, B.CreateLoad(B.getInt32Ty(), B.getModule().getAllocatedBytes()), B.CreateLoad(B.getInt32Ty(), B.getModule().getNextGC())});
            }

            B.CreateRetVoid();

            return F;
        }());

        return GCFunction;
    }

    void LoxBuilder::CollectGarbage() {
        CreateCall(getModule().getOrInsertFunction("$gc", FunctionType::get(getVoidTy(), {}, false)));
    }
}// namespace lox
