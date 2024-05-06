
#include "Memory.h"
#include "ModuleCompiler.h"
#include "Value.h"
#include <llvm/IR/Value.h>

#include "../Debug.h"
#include "GC.h"
#include "Stack.h"


namespace lox {

    AllocaInst *CreateEntryBlockAlloca(Function *TheFunction, Type *type, const std::string_view VarName, Value *initialValue) {
        IRBuilder TmpB(&TheFunction->getEntryBlock(), TheFunction->getEntryBlock().begin());
        const auto alloca = TmpB.CreateAlloca(type, nullptr, VarName);
        if (initialValue) {
            TmpB.CreateStore(initialValue, alloca);
        }
        return alloca;
    }

    Value *LoxBuilder::getSizeOf(Type *type, Value *arraySize) {
        Type *IntPtrTy = IntegerType::getInt32Ty(getContext());
        // The IR that is generated with getSizeOf uses a hack described here:
        // https://mukulrathi.com/create-your-own-programming-language/concurrency-runtime-language-tutorial/#malloc
        Constant *allocsize = ConstantExpr::getSizeOf(type);
        allocsize = ConstantExpr::getTruncOrBitCast(allocsize, IntPtrTy);
        return arraySize ? CreateMul(allocsize, arraySize) : allocsize;
    }

    Value *LoxBuilder::CreateRealloc(Value *ptr, Value *newSize) {
        static const auto realloc = getModule().getOrInsertFunction(
            "realloc",
            FunctionType::get(getPtrTy(), {getPtrTy(), getInt64Ty()}, false)
        );
        return CreateCall(realloc, {ptr, newSize});
    }

    Value *LoxBuilder::CreateReallocate(Value *ptr, Value *oldSize, Value *newSize) {
        static auto ReallocFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    getPtrTy(),
                    {getPtrTy(), getInt32Ty(), getInt32Ty()},
                    false
                ),
                Function::InternalLinkage,
                "$realloc",
                getModule()
            );

            F->addFnAttr(Attribute::getWithAllocSizeArgs(getContext(), 1, {}));
            F->addFnAttr(Attribute::get(getContext(), Attribute::AllocKind, static_cast<uint64_t>(AllocFnKind::Realloc)));

            LoxBuilder B(getContext(), getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();
            const auto ptr = arguments;
            const auto oldSize = arguments + 1;
            const auto newSize = arguments + 2;

            B.CreateStore(
                B.CreateAdd(B.CreateLoad(B.getInt32Ty(), B.getModule().getAllocatedBytes()), B.CreateSub(newSize, oldSize)),
                B.getModule().getAllocatedBytes()
            );

            const auto GcBlock = B.CreateBasicBlock("gc");
            const auto NoGcBlock = B.CreateBasicBlock("no.gc");

            B.CreateCondBr(
                B.CreateAnd(B.CreateICmpSGT(newSize, oldSize), B.CreateICmpSGT(B.CreateLoad(B.getInt32Ty(), B.getModule().getAllocatedBytes()), B.CreateLoad(B.getInt32Ty(), B.getModule().getNextGC()))),
                GcBlock,
                NoGcBlock
            );

            B.SetInsertPoint(GcBlock);
            {
                B.CollectGarbage();
                B.CreateBr(NoGcBlock);
            }
            B.SetInsertPoint(NoGcBlock);
            {
                const auto IsFreeBlock = B.CreateBasicBlock("is.free");
                const auto IsAllocBlock = B.CreateBasicBlock("is.alloc");

                B.CreateCondBr(B.CreateICmpEQ(B.getInt32(0), newSize), IsFreeBlock, IsAllocBlock);
                B.SetInsertPoint(IsFreeBlock);
                {
                    B.CreateRealloc(ptr, B.getInt32(0));
                    B.CreateRet(B.getNullPtr());
                }
                B.SetInsertPoint(IsAllocBlock);
                {
                    B.CreateRet(B.CreateRealloc(ptr, B.getSizeOf(PointerType::getUnqual(getContext()), newSize)));
                }
            }
            return F;
        }());

        return CreateCall(ReallocFunction, {ptr, oldSize, newSize});
    }

    void LoxBuilder::CreateFree(Value *ptr, Type *type, Value *arraySize = nullptr) {
        CreateReallocate(ptr, getSizeOf(type, arraySize ? arraySize : getInt32(1)), getInt32(0));
    }

    void LoxBuilder::CreateFree(Value *ptr, const lox::ObjType type, Value *arraySize = nullptr) {
        CreateFree(ptr, getModule().getStructType(type), arraySize);
    }

    Value *LoxBuilder::AllocateObj(const enum ObjType objType, const std::string_view name) {
        static auto AllocateObjectFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    getPtrTy(),
                    {getInt8Ty()},
                    false
                ),
                Function::InternalLinkage,
                "$allocateObject",
                getModule()
            );

            LoxBuilder B(getContext(), getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();

            const auto objType = arguments;

            const auto objects = B.getModule().getObjects();
            const auto DefaultBlock = B.CreateBasicBlock("default");
            const auto EndBlock = B.CreateBasicBlock("end");

            const auto Switch = B.CreateSwitch(objType, DefaultBlock);

            const std::initializer_list<lox::ObjType> ObjTypes{
                ObjType::STRING,
                ObjType::FUNCTION,
                ObjType::CLOSURE,
                ObjType::UPVALUE,
                ObjType::CLASS,
                ObjType::INSTANCE,
                ObjType::BOUND_METHOD
            };

            const auto CurrentBlock = B.GetInsertBlock();

            B.SetInsertPoint(EndBlock);
            const auto allocsize = B.CreatePHI(B.getInt32Ty(), ObjTypes.size());

            B.SetInsertPoint(CurrentBlock);

            // The malloc size IR that is generated with getSizeOf uses a hack described here:
            // https://mukulrathi.com/create-your-own-programming-language/concurrency-runtime-language-tutorial/#malloc

            Type *IntPtrTy = IntegerType::getInt32Ty(B.getContext());
            for (auto type: ObjTypes) {
                const auto Block = B.CreateBasicBlock("obj_" + std::to_string(static_cast<uint8_t>(type)));
                Switch->addCase(B.ObjTypeInt(type), Block);
                B.SetInsertPoint(Block);
                allocsize->addIncoming(
                    ConstantExpr::getTruncOrBitCast(ConstantExpr::getSizeOf(B.getModule().getStructType(type)), IntPtrTy),
                    Block
                );
                B.CreateBr(EndBlock);
            }

            B.SetInsertPoint(DefaultBlock);
            B.CreateUnreachable();

            B.SetInsertPoint(EndBlock);

            const auto NewObjMalloc = B.CreateReallocate(
                B.getNullPtr(),
                B.getInt32(0),
                allocsize
            );

            B.CreateStore(
                objType,
                B.CreateStructGEP(B.getModule().getObjStructType(), NewObjMalloc, 0, "ObjType")
            );

            B.CreateStore(
                B.getFalse(),
                B.CreateStructGEP(B.getModule().getObjStructType(), NewObjMalloc, 1, "isMarked")
            );

            B.CreateStore(
                B.CreateLoad(B.getPtrTy(), objects),
                B.CreateStructGEP(B.getModule().getObjStructType(), NewObjMalloc, 2, "next")
            );

            B.CreateStore(NewObjMalloc, objects);

            if constexpr (DEBUG_LOG_GC) {
                static const auto fmt = B.CreateGlobalCachedString("%p\n");
                B.PrintF({fmt, B.CreateLoad(B.getPtrTy(), objects)});
                static const auto fmt2 = B.CreateGlobalCachedString("\t%p allocate %zu.\n");
                B.PrintF({fmt2, NewObjMalloc, allocsize});
                static const auto fmt3 = B.CreateGlobalCachedString("\tobject.next = %p\n");
                B.PrintF({fmt3, B.CreateLoad(getPtrTy(), B.CreateStructGEP(B.getModule().getObjStructType(), NewObjMalloc, 2, "next"))});
            }

            B.CreateRet(NewObjMalloc);

            return F;
        }());

        if constexpr (STRESS_GC) {
            this->CollectGarbage();
        }

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

    Value *LoxBuilder::AllocateArray(Type *type, Value *arraySize) {
        return CreateRealloc(getNullPtr(), getSizeOf(type, arraySize));
    }

    void FreeObject(LoxBuilder &Builder, Value *value) {
        assert(value->getType() == Builder.getInt64Ty());

        static auto FreeObjectFunction([&Builder] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {Builder.getInt64Ty()},
                    false
                ),
                Function::InternalLinkage,
                "$freeObject",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto value = F->arg_begin();

            const auto IsStringBlock = B.CreateBasicBlock("string");
            const auto IsFunctionBlock = B.CreateBasicBlock("function");
            const auto IsClosureBlock = B.CreateBasicBlock("closure");
            const auto IsUpvalueBlock = B.CreateBasicBlock("upvalue");
            const auto IsClassBlock = B.CreateBasicBlock("class");
            const auto IsBoundMethodBlock = B.CreateBasicBlock("boundmethod");
            const auto IsInstanceBlock = B.CreateBasicBlock("instance");
            const auto DefaultBlock = B.CreateBasicBlock("default");
            const auto EndBlock = B.CreateBasicBlock("end");

            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("free %p (objtype: %d): "), value, B.ObjType(value)});
                B.PrintObject(value);
            }

            const auto Switch = B.CreateSwitch(B.ObjType(value), DefaultBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::STRING), IsStringBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::FUNCTION), IsFunctionBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::CLOSURE), IsClosureBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::UPVALUE), IsUpvalueBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::CLASS), IsClassBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::BOUND_METHOD), IsBoundMethodBlock);
            Switch->addCase(B.ObjTypeInt(ObjType::INSTANCE), IsInstanceBlock);

            B.SetInsertPoint(IsStringBlock);
            {
                B.CreateFree(B.AsObj(value), ObjType::STRING);
                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(IsFunctionBlock);
            {
                B.CreateFree(B.AsObj(value), ObjType::FUNCTION);
                // Don't need to free the name, because it will be freed as a String obj anyway.
                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(IsClosureBlock);
            {
                const auto closure = B.AsObj(value);
                const auto size = B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(B.getModule().getStructType(ObjType::CLOSURE), closure, 3));
                const auto IsNotNull = B.CreateBasicBlock("NotNullArray");
                const auto NullArray = B.CreateBasicBlock("NullArray");
                B.CreateCondBr(B.CreateICmpEQ(size, B.getInt32(0)), NullArray, IsNotNull);
                B.SetInsertPoint(IsNotNull);
                {
                    const auto array = B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(B.getModule().getStructType(ObjType::CLOSURE), closure, 2));
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
                B.CreateFree(B.AsObj(value), ObjType::CLASS);
                B.CreateBr(EndBlock);
            }
            B.SetInsertPoint(IsInstanceBlock);
            {
                B.CreateFree(B.AsObj(value), ObjType::INSTANCE);
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
                    B.PrintF({B.CreateGlobalCachedString("not freeing {{object %d}} %p\n"), B.ObjType(value), B.AsObj(value)});
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
        static auto FreeObjectsFunction([&Builder] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder.getVoidTy(),
                    {},
                    false
                ),
                Function::InternalLinkage,
                "$freeObjects",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            const auto objects = B.getModule().getObjects();

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto before = B.CreateLoad(B.getInt32Ty(), B.getModule().getAllocatedBytes());

            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("--free objects (%p)--\n"), B.CreateLoad(B.getPtrTy(), objects)});
            }

            const auto object = CreateEntryBlockAlloca(F, B.getPtrTy(), "object");
            const auto next = CreateEntryBlockAlloca(F, B.getPtrTy(), "next");
            B.CreateStore(
                B.CreateLoad(B.getPtrTy(), objects),
                object
            );
            B.CreateStore(B.getNullPtr(), next);

            const auto WhileCond = B.CreateBasicBlock("while.cond");
            const auto WhileBody = B.CreateBasicBlock("while.body");
            const auto WhileEnd = B.CreateBasicBlock("while.end");

            B.CreateBr(WhileCond);
            B.SetInsertPoint(WhileCond);
            {
                B.CreateCondBr(B.CreateIsNotNull(B.CreateLoad(B.getPtrTy(), object)), WhileBody, WhileEnd);
                B.SetInsertPoint(WhileBody);
                {
                    const auto ptr = B.CreateLoad(B.getPtrTy(), object);
                    B.CreateStore(
                        B.CreateLoad(B.getPtrTy(), B.CreateStructGEP(Builder.getModule().getObjStructType(), ptr, 2, "next")),
                        next
                    );

                    const auto objectPtr = B.AsObj(B.CreateLoad(B.getInt64Ty(), object));
                    const auto value = B.ObjVal(objectPtr);

                    if constexpr (ENABLE_RUNTIME_ASSERTS) {
                        // At this point all upvalues should be closed.
                        const auto IsUpvalueBlock = B.CreateBasicBlock("is.upvalue");
                        const auto NotUpvalueBlock = B.CreateBasicBlock("not.upvalue");

                        B.CreateCondBr(B.IsUpvalue(value), IsUpvalueBlock, NotUpvalueBlock);
                        B.SetInsertPoint(IsUpvalueBlock);
                        {
                            const auto upvalue = objectPtr;
                            const auto closed = B.CreateLoad(B.getInt64Ty(), B.CreateObjStructGEP(ObjType::UPVALUE, upvalue, 3));

                            const auto IsNotClosedBlock = B.CreateBasicBlock("notclosed");

                            B.CreateCondBr(B.IsNil(closed), IsNotClosedBlock, NotUpvalueBlock);
                            B.SetInsertPoint(IsNotClosedBlock);
                            {
                                B.RuntimeError(B.getInt32(0), "upvalue not closed %p\n", {upvalue}, B.CreateGlobalCachedString("assert"));
                                B.CreateUnreachable();
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

            // Free greystack
            B.getModule().getGrayStack()->CreateFree(B);

            if constexpr (DEBUG_LOG_GC) {
                B.PrintF({B.CreateGlobalCachedString("--end free objects (%p)--\n"), (B.CreateLoad(B.getPtrTy(), objects))});
                const auto current = B.CreateLoad(B.getInt32Ty(), B.getModule().getAllocatedBytes());
                B.PrintF({B.CreateGlobalCachedString("     collected %zu bytes (from %zu to %zu)\n"), B.CreateSub(before, current), before, current});
            }

            B.CreateRetVoid();

            return F;
        }());

        Builder.CreateCall(FreeObjectsFunction);
    }
}// namespace lox
