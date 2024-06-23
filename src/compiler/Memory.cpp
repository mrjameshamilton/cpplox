
#include "Memory.h"
#include "ModuleCompiler.h"
#include "Value.h"
#include <llvm/IR/Value.h>

#include "../Debug.h"
#include "GC.h"
#include "Stack.h"

namespace lox {

    AllocaInst *CreateEntryBlockAlloca(
        Function *TheFunction, Type *type, const std::string_view VarName,
        const std::function<void(IRBuilder<> &, AllocaInst *)> &entryBuilder
    ) {
        IRBuilder TmpB(&TheFunction->getEntryBlock(), TheFunction->getEntryBlock().begin());
        auto *const alloca = TmpB.CreateAlloca(type, nullptr, VarName);

        if constexpr (DEBUG_LOG_GC) {
            FunctionCallee PrintF = TheFunction->getParent()->getOrInsertFunction(
                "printf", FunctionType::get(
                              IntegerType::getInt8Ty(TheFunction->getContext()),
                              {PointerType::getUnqual(TheFunction->getContext())}, true
                          )
            );
            TmpB.CreateCall(
                PrintF, {TmpB.CreateGlobalString("local: %s@%p\n"), TmpB.CreateGlobalString(VarName), alloca}
            );
        }
        if (entryBuilder) { entryBuilder(TmpB, alloca); }
        return alloca;
    }

    Value *LoxBuilder::getSizeOf(Type *type, Value *arraySize) {
        Type *IntPtrTy = IntegerType::getInt32Ty(getContext());
        // The IR that is generated with getSizeOf uses a hack described here:
        // https://mukulrathi.com/create-your-own-programming-language/concurrency-runtime-language-tutorial/#malloc
        Constant *allocsize = ConstantExpr::getSizeOf(type);
        allocsize = ConstantExpr::getTruncOrBitCast(allocsize, IntPtrTy);
        return arraySize != nullptr ? CreateMul(allocsize, arraySize, "size", true, true) : allocsize;
    }

    ConstantInt *LoxBuilder::getSizeOf(const lox::ObjType type) const {
        return getSizeOf(getModule().getStructType(type), 1);
    }

    ConstantInt *LoxBuilder::getSizeOf(Type *type, const unsigned int arraySize = 1) const {
        const uint64_t size = getModule().getDataLayout().getTypeStoreSize(type).getKnownMinValue() * arraySize;
        return ConstantInt::get(Type::getInt64Ty(Context), size);
    }

    Value *LoxBuilder::CreateRealloc(Value *ptr, Value *newSize, const StringRef what) {
        static const auto realloc = getModule().getOrInsertFunction(
            "realloc", FunctionType::get(getPtrTy(), {getPtrTy(), getInt64Ty()}, false)
        );

        auto *const call = CreateCall(realloc, {ptr, newSize});

        if constexpr (DEBUG_LOG_GC) {
            PrintF(
                {CreateGlobalCachedString("realloc %s (%p, %d) = %p\n"), CreateGlobalCachedString(what), ptr, newSize,
                 call}
            );
        }
        return call;
    }

    Value *LoxBuilder::CreateReallocate(Value *ptr, Value *oldSize, Value *newSize) {
        assert(ptr->getType() == getPtrTy());
        assert(oldSize->getType() == getInt32Ty());
        assert(newSize->getType() == getInt32Ty());

        static auto *ReallocFunction([this] {
            auto *const F = Function::Create(
                FunctionType::get(getPtrTy(), {getPtrTy(), getInt32Ty(), getInt32Ty()}, false),
                Function::InternalLinkage, "$realloc", getModule()
            );

            F->addFnAttr(Attribute::getWithAllocSizeArgs(getContext(), 1, {}));
            F->addFnAttr(Attribute::get(getContext(), Attribute::AllocKind, static_cast<uint64_t>(AllocFnKind::Realloc))
            );
            F->addParamAttr(0, Attribute::AllocatedPointer);

            LoxBuilder B(getContext(), getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->args().begin();
            auto *const ptr = arguments;
            auto *const oldSize = arguments + 1;
            auto *const newSize = arguments + 2;

            B.CreateStore(
                B.CreateAdd(
                    B.CreateLoad(B.getInt32Ty(), B.getModule().getAllocatedBytes()),
                    B.CreateSub(newSize, oldSize, "diff", true, true), "allocatedBytes", true, true
                ),
                B.getModule().getAllocatedBytes()
            );

            auto *const GcBlock = B.CreateBasicBlock("gc");
            auto *const NoGcBlock = B.CreateBasicBlock("no.gc");

            B.CreateCondBr(B.CreateICmpSGT(newSize, oldSize), GcBlock, NoGcBlock);

            B.SetInsertPoint(GcBlock);
            {
                B.CollectGarbage(false);
                B.CreateBr(NoGcBlock);
            }
            B.SetInsertPoint(NoGcBlock);
            {
                auto *const IsFreeBlock = B.CreateBasicBlock("is.free");
                auto *const IsAllocBlock = B.CreateBasicBlock("is.alloc");

                B.CreateCondBr(B.CreateICmpEQ(B.getInt32(0), newSize), IsFreeBlock, IsAllocBlock);
                B.SetInsertPoint(IsFreeBlock);
                {
                    B.IRBuilder::CreateFree(ptr);
                    B.CreateRet(B.getNullPtr());
                }
                B.SetInsertPoint(IsAllocBlock);
                { B.CreateRet(B.CreateRealloc(ptr, newSize, "alloc")); }
            }
            return F;
        }());

        return CreateCall(ReallocFunction, {ptr, oldSize, newSize});
    }

    void LoxBuilder::CreateFree(Value *ptr, const lox::ObjType type, Value *arraySize = nullptr) {
        if (arraySize != nullptr) {
            CreateReallocate(ptr, getSizeOf(getModule().getStructType(type), arraySize), getInt32(0));
        } else {
            CreateReallocate(ptr, CreateTrunc(getSizeOf(type), getInt32Ty()), getInt32(0));
        }
    }

    Value *LoxBuilder::AllocateObj(const enum ObjType objType, const std::string_view name) {
        static auto *AllocateObjectFunction([this] {
            auto *const F = Function::Create(
                FunctionType::get(getPtrTy(), {getInt8Ty()}, false), Function::InternalLinkage, "$allocateObject",
                getModule()
            );

            LoxBuilder B(getContext(), getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->args().begin();

            auto *const objType = arguments;

            auto *const objects = B.getModule().getObjects();
            auto *const DefaultBlock = B.CreateBasicBlock("default");
            auto *const EndBlock = B.CreateBasicBlock("end");

            auto *const Switch = B.CreateSwitch(objType, DefaultBlock);

            // clang-format off
            const std::initializer_list<lox::ObjType> ObjTypes{
                ObjType::STRING,
                ObjType::FUNCTION,
                ObjType::CLOSURE,
                ObjType::UPVALUE,
                ObjType::CLASS,
                ObjType::INSTANCE,
                ObjType::BOUND_METHOD
            };
            // clang-format on

            auto *const CurrentBlock = B.GetInsertBlock();

            B.SetInsertPoint(EndBlock);
            auto *const allocsize = B.CreatePHI(B.getInt32Ty(), ObjTypes.size());

            B.SetInsertPoint(CurrentBlock);

            // The malloc size IR that is generated with getSizeOf uses a hack described here:
            // https://mukulrathi.com/create-your-own-programming-language/concurrency-runtime-language-tutorial/#malloc

            Type *IntPtrTy = IntegerType::getInt32Ty(B.getContext());
            for (const auto &type: ObjTypes) {
                auto *const Block = B.CreateBasicBlock("obj_" + std::to_string(static_cast<uint8_t>(type)));
                Switch->addCase(B.ObjTypeInt(type), Block);
                B.SetInsertPoint(Block);
                allocsize->addIncoming(
                    ConstantExpr::getTruncOrBitCast(
                        ConstantExpr::getSizeOf(B.getModule().getStructType(type)), IntPtrTy
                    ),
                    Block
                );
                B.CreateBr(EndBlock);
            }

            B.SetInsertPoint(DefaultBlock);
            B.CreateUnreachable();

            B.SetInsertPoint(EndBlock);

            auto *const NewObjMalloc = B.CreateReallocate(B.getNullPtr(), B.getInt32(0), allocsize);

            B.CreateStore(objType, B.CreateStructGEP(B.getModule().getObjStructType(), NewObjMalloc, 0, "ObjType"));

            B.CreateStore(
                B.getFalse(), B.CreateStructGEP(B.getModule().getObjStructType(), NewObjMalloc, 1, "isMarked")
            );

            B.CreateStore(
                B.CreateLoad(B.getPtrTy(), objects),
                B.CreateStructGEP(B.getModule().getObjStructType(), NewObjMalloc, 2, "next")
            );

            B.CreateStore(NewObjMalloc, objects);

            if constexpr (DEBUG_LOG_GC) {
                static auto *const fmt = B.CreateGlobalCachedString("%p\n");
                B.PrintF({fmt, B.CreateLoad(B.getPtrTy(), objects)});
                static auto *const fmt2 = B.CreateGlobalCachedString("\t%p allocate %zu.\n");
                B.PrintF({fmt2, NewObjMalloc, allocsize});
                static auto *const fmt3 = B.CreateGlobalCachedString("\tobject.next = %p\n");
                B.PrintF(
                    {fmt3, B.CreateLoad(
                               getPtrTy(), B.CreateStructGEP(B.getModule().getObjStructType(), NewObjMalloc, 2, "next")
                           )}
                );
            }

            B.CreateRet(NewObjMalloc);

            return F;
        }());

        if constexpr (STRESS_GC) { this->CollectGarbage(true); }

        if constexpr (DEBUG_LOG_GC) {
            switch (objType) {
                case ObjType::STRING:
                    PrintString("Allocate string");
                    break;
                case ObjType::FUNCTION:
                    PrintString("Allocate function");
                    break;
                case ObjType::CLOSURE:
                    PrintString("Allocate closure");
                    break;
                case ObjType::UPVALUE:
                    PrintString("Allocate upvalue");
                    break;
                case ObjType::CLASS:
                    PrintString("Allocate class");
                    break;
                case ObjType::INSTANCE:
                    PrintString("Allocate instance");
                    break;
                case ObjType::BOUND_METHOD:
                    PrintString("Allocate bound method");
                    break;
                default:
                    std::unreachable();
            }
            PrintF({CreateGlobalCachedString("\tobjects: %p => "), CreateLoad(getPtrTy(), getModule().getObjects())});
        }

        return CreateCall(AllocateObjectFunction, {ObjTypeInt(objType)}, name);
    }

    void FreeObject(LoxBuilder &Builder, Value *value) {
        assert(value->getType() == Builder.getInt64Ty());

        static auto *FreeObjectFunction([&Builder] {
            auto *const F = Function::Create(
                FunctionType::get(Builder.getVoidTy(), {Builder.getInt64Ty()}, false), Function::InternalLinkage,
                "$freeObject", Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const value = F->arg_begin();

            auto *const IsStringBlock = B.CreateBasicBlock("string");
            auto *const IsFunctionBlock = B.CreateBasicBlock("function");
            auto *const IsClosureBlock = B.CreateBasicBlock("closure");
            auto *const IsUpvalueBlock = B.CreateBasicBlock("upvalue");
            auto *const IsClassBlock = B.CreateBasicBlock("class");
            auto *const IsBoundMethodBlock = B.CreateBasicBlock("boundmethod");
            auto *const IsInstanceBlock = B.CreateBasicBlock("instance");
            auto *const DefaultBlock = B.CreateBasicBlock("default");
            auto *const EndBlock = B.CreateBasicBlock("end");

            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("free %p (objtype: %d): "), value, B.ObjType(value)});
                B.PrintObject(value);
            }

            auto *const Switch = B.CreateSwitch(B.ObjType(value), DefaultBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::STRING), IsStringBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::FUNCTION), IsFunctionBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::CLOSURE), IsClosureBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::UPVALUE), IsUpvalueBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::CLASS), IsClassBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::BOUND_METHOD), IsBoundMethodBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::INSTANCE), IsInstanceBlock);

            B.SetInsertPoint(IsStringBlock);
            {
                auto *const DynamicStringBlock = B.CreateBasicBlock("dynamic.string");
                auto *const StaticStringBlock = B.CreateBasicBlock("static.string");

                auto *const string = B.AsObj(value);
                auto *const isDynamic = B.CreateLoad(B.getInt1Ty(), B.CreateObjStructGEP(ObjType::STRING, string, 4));
                B.CreateCondBr(isDynamic, DynamicStringBlock, StaticStringBlock);
                B.SetInsertPoint(DynamicStringBlock);
                {
                    B.IRBuilder::CreateFree(B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::STRING, string, 1))
                    );
                    B.CreateBr(StaticStringBlock);
                }
                B.SetInsertPoint(StaticStringBlock);
                {
                    B.CreateFree(string, ObjType::STRING);
                    B.CreateBr(EndBlock);
                }
            }
            B.SetInsertPoint(IsFunctionBlock);
            {
                B.CreateFree(B.AsObj(value), ObjType::FUNCTION);
                // Don't need to free the name, because it will be freed as a String obj anyway.
                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(IsClosureBlock);
            {
                auto *const closure = B.AsObj(value);
                auto *const size = B.CreateLoad(
                    B.getInt32Ty(), B.CreateStructGEP(B.getModule().getStructType(ObjType::CLOSURE), closure, 3)
                );
                auto *const IsNotNull = B.CreateBasicBlock("NotNullArray");
                auto *const NullArray = B.CreateBasicBlock("NullArray");
                B.CreateCondBr(B.CreateICmpEQ(size, B.getInt32(0)), NullArray, IsNotNull);
                B.SetInsertPoint(IsNotNull);
                {
                    auto *const array = B.CreateLoad(
                        B.getPtrTy(), B.CreateStructGEP(B.getModule().getStructType(ObjType::CLOSURE), closure, 2)
                    );
                    B.CreateFree(array, ObjType::UPVALUE, size);
                    B.CreateBr(NullArray);
                }
                B.SetInsertPoint(NullArray);
                {
                    B.CreateFree(closure, ObjType::CLOSURE);
                    B.CreateBr(EndBlock);
                }
            }
            B.SetInsertPoint(IsUpvalueBlock);
            {
                B.CreateFree(B.AsObj(value), ObjType::UPVALUE);
                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(IsClassBlock);
            {
                auto *const klass = B.AsObj(value);
                // TODO: CreateFreeTable function.
                auto *const methods = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::CLASS, klass, 2));
                auto *const entries =
                    B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getTableStructType(), methods, 2));
                B.IRBuilder::CreateFree(entries);
                B.IRBuilder::CreateFree(methods);
                B.CreateFree(klass, ObjType::CLASS);
                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(IsInstanceBlock);
            {
                auto *const instance = B.AsObj(value);
                auto *const fields = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::INSTANCE, instance, 2));
                auto *const entries =
                    B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getTableStructType(), fields, 2));
                B.IRBuilder::CreateFree(entries);
                B.IRBuilder::CreateFree(fields);
                B.CreateFree(instance, ObjType::INSTANCE);
                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(IsBoundMethodBlock);
            {
                B.CreateFree(B.AsObj(value), ObjType::BOUND_METHOD);
                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(DefaultBlock);
            {
                if constexpr (DEBUG_LOG_GC) {
                    B.PrintF(
                        {B.CreateGlobalCachedString("not freeing {{object %d}} %p\n"), B.ObjType(value), B.AsObj(value)}
                    );
                    B.CreateBr(EndBlock);
                } else {
                    B.CreateUnreachable();
                }
            }

            B.SetInsertPoint(EndBlock);
            B.CreateRetVoid();
            return F;
        }());

        Builder.CreateCall(FreeObjectFunction, {value});
    }

    void FreeObjects(LoxBuilder &Builder) {
        static auto *FreeObjectsFunction([&Builder] {
            auto *const F = Function::Create(
                FunctionType::get(Builder.getVoidTy(), {}, false), Function::InternalLinkage, "$freeObjects",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            auto *const objects = B.getModule().getObjects();

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const before = B.CreateLoad(B.getInt32Ty(), B.getModule().getAllocatedBytes());

            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("--free objects (%p)--\n"), B.CreateLoad(B.getPtrTy(), objects)});
            }

            auto *const object = CreateEntryBlockAlloca(F, B.getPtrTy(), "object");
            auto *const next = CreateEntryBlockAlloca(F, B.getPtrTy(), "next");
            B.CreateStore(B.CreateLoad(B.getPtrTy(), objects), object);
            B.CreateStore(B.getNullPtr(), next);

            auto *const WhileCond = B.CreateBasicBlock("while.cond");
            auto *const WhileBody = B.CreateBasicBlock("while.body");
            auto *const WhileEnd = B.CreateBasicBlock("while.end");

            B.CreateBr(WhileCond);
            B.SetInsertPoint(WhileCond);
            {
                B.CreateCondBr(B.CreateIsNotNull(B.CreateLoad(B.getPtrTy(), object)), WhileBody, WhileEnd);
                B.SetInsertPoint(WhileBody);
                {
                    auto *const ptr = B.CreateLoad(B.getPtrTy(), object);
                    B.CreateStore(
                        B.CreateLoad(
                            B.getPtrTy(), B.CreateStructGEP(Builder.getModule().getObjStructType(), ptr, 2, "next")
                        ),
                        next
                    );

                    auto *const objectPtr = B.AsObj(B.CreateLoad(B.getInt64Ty(), object));
                    auto *const value = B.ObjVal(objectPtr);

                    if constexpr (ENABLE_RUNTIME_ASSERTS) {
                        // At this point all upvalues should be closed.
                        auto *const IsUpvalueBlock = B.CreateBasicBlock("is.upvalue");
                        auto *const NotUpvalueBlock = B.CreateBasicBlock("not.upvalue");

                        B.CreateCondBr(B.IsUpvalue(value), IsUpvalueBlock, NotUpvalueBlock);
                        B.SetInsertPoint(IsUpvalueBlock);
                        {
                            auto *const upvalue = objectPtr;
                            auto *const closed =
                                B.CreateLoad(B.getInt64Ty(), B.CreateObjStructGEP(ObjType::UPVALUE, upvalue, 3));

                            auto *const IsNotClosedBlock = B.CreateBasicBlock("notclosed");

                            B.CreateCondBr(B.IsNil(closed), IsNotClosedBlock, NotUpvalueBlock);
                            B.SetInsertPoint(IsNotClosedBlock);
                            {
                                B.RuntimeError(
                                    B.getInt32(0), "upvalue not closed %p\n", {upvalue},
                                    B.CreateGlobalCachedString("assert"), false
                                );
                            }
                        }
                        B.SetInsertPoint(NotUpvalueBlock);
                    }

                    FreeObject(B, value);

                    B.CreateStore(B.CreateLoad(B.getPtrTy(), next), object);

                    B.CreateBr(WhileCond);
                }
            }
            B.SetInsertPoint(WhileEnd);

            if constexpr (DEBUG_LOG_GC) {
                B.PrintF(
                    {B.CreateGlobalCachedString("--end free objects (%p)--\n"), (B.CreateLoad(B.getPtrTy(), objects))}
                );
                auto *const current = B.CreateLoad(B.getInt32Ty(), B.getModule().getAllocatedBytes());
                B.PrintF(
                    {B.CreateGlobalCachedString("     collected %zu bytes (from %zu to %zu)\n"),
                     B.CreateSub(before, current), before, current}
                );
            }

            const auto &M = B.getModule();
            M.getGrayStack().CreateFree(B);
            M.getLocalsStack().CreateFree(B);
            auto *const runtimeStringsTable = B.CreateLoad(B.getPtrTy(), M.getRuntimeStrings());
            B.IRBuilder::CreateFree(B.CreateLoad(
                B.getPtrTy(), B.CreateStructGEP(B.getModule().getTableStructType(), runtimeStringsTable, 2)
            ));
            B.IRBuilder::CreateFree(runtimeStringsTable);

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(FreeObjectsFunction);
    }
}// namespace lox
