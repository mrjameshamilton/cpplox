#include "ModuleCompiler.h"
#include "Value.h"

#include <llvm/IR/Value.h>

#define DEBUG_LOG_GC false

namespace lox {

    AllocaInst *CreateEntryBlockAlloca(Function *TheFunction, Type *type, const std::string_view &VarName) {
        IRBuilder TmpB(&TheFunction->getEntryBlock(), TheFunction->getEntryBlock().begin());
        return TmpB.CreateAlloca(type, nullptr, VarName);
    }

    Value *LoxBuilder::AllocateObj(const enum ObjType objType, const std::string_view name) {
        const auto objects = getModule().getObjects();
        Type *StructType = getModule().getStructType(objType);

        Type *IntPtrTy = IntegerType::getInt32Ty(this->getContext());
        // The malloc size IR that is generated with getSizeOf uses a hack described here:
        // https://mukulrathi.com/create-your-own-programming-language/concurrency-runtime-language-tutorial/#malloc
        Constant *allocsize = ConstantExpr::getSizeOf(StructType);
        allocsize = ConstantExpr::getTruncOrBitCast(allocsize, IntPtrTy);

        const auto NewObjMalloc = CreateMalloc(
            IntPtrTy,
            StructType,
            allocsize,
            nullptr
        );

        CreateStore(
            ObjTypeInt(objType),
            CreateStructGEP(getModule().getObjStructType(), NewObjMalloc, 0, "ObjType")
        );

        CreateStore(
            getFalse(),
            CreateStructGEP(getModule().getObjStructType(), NewObjMalloc, 1, "isMarked")
        );

#if DEBUG_LOG_GC
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
            default:
                PrintString("Allocate object");
        }
        static const auto fmt0 = CreateGlobalStringPtr("\tobjects: %p => ");
        PrintF({fmt0, CreateLoad(getPtrTy(), objects)});
#endif

        CreateStore(
            CreateLoad(getPtrTy(), objects),
            CreateStructGEP(getModule().getObjStructType(), NewObjMalloc, 2, "next")
        );

        CreateStore(NewObjMalloc, objects);

#if DEBUG_LOG_GC
        static const auto fmt = CreateGlobalStringPtr("%p\n");
        PrintF({fmt, CreateLoad(getPtrTy(), objects)});
        static const auto fmt2 = CreateGlobalStringPtr("\t%p allocate %zu.\n");
        PrintF({fmt2, NewObjMalloc, allocsize});
        static const auto fmt3 = CreateGlobalStringPtr("\tobject.next = %p\n");
        PrintF({fmt3, CreateLoad(getPtrTy(), CreateStructGEP(getModule().getObjStructType(), NewObjMalloc, 2, "next"))});
#endif

        const auto NewObj = CreateEntryBlockAlloca(GetInsertBlock()->getParent(), StructType, name);
        CreateStore(NewObjMalloc, NewObj);

        return CreateBitCast(NewObj, StructType->getPointerTo());
    }

    Value *LoxBuilder::AllocateArray(llvm::Type *type, int size, const std::string_view &name) {
        Type *IntPtrTy = IntegerType::getInt32Ty(getContext());
        // The malloc size IR that is generated with getSizeOf uses a hack described here:
        // https://mukulrathi.com/create-your-own-programming-language/concurrency-runtime-language-tutorial/#malloc
        Constant *allocsize = ConstantExpr::getSizeOf(type->getPointerTo());
        allocsize = ConstantExpr::getTruncOrBitCast(allocsize, IntPtrTy);
        Constant *arraySize = ConstantInt::get(IntPtrTy, size);
        return CreateMalloc(
            IntPtrTy,
            getPtrTy(),
            allocsize,
            arraySize,
            nullptr,
            name
        );
    }

    void ModuleCompiler::FreeObjects() const {
        const auto object = CreateEntryBlockAlloca(Builder->getFunction(), Builder->getPtrTy(), "object");
        const auto next = CreateEntryBlockAlloca(Builder->getFunction(), Builder->getPtrTy(), "next");
        Builder->CreateStore(
            Builder->CreateLoad(Builder->getPtrTy(), M->getObjects()),
            object
        );

        const auto WhileCond = Builder->CreateBasicBlock("while.cond");
        const auto WhileBody = Builder->CreateBasicBlock("while.body");
        const auto WhileEnd = Builder->CreateBasicBlock("while.end");

        Builder->CreateBr(WhileCond);
        Builder->SetInsertPoint(WhileCond);
        Builder->CreateCondBr(Builder->CreateIsNotNull(Builder->CreateLoad(Builder->getPtrTy(), object)), WhileBody, WhileEnd);
        Builder->SetInsertPoint(WhileBody);

        Builder->CreateStore(
            Builder->CreateLoad(
                Builder->getPtrTy(),
                Builder->CreateStructGEP(M->getObjStructType(), Builder->CreateLoad(Builder->getPtrTy(), object), 2, "next")
            ),
            next
        );

        FreeObject(Builder->CreateLoad(Builder->getInt64Ty(), object));

        Builder->CreateStore(
            Builder->CreateLoad(Builder->getPtrTy(), next),
            object
        );

        Builder->CreateBr(WhileCond);

        Builder->SetInsertPoint(WhileEnd);
    }

    void ModuleCompiler::FreeObject(Value *value) const {
        const auto IsStringBlock = Builder->CreateBasicBlock("string");
        const auto IsFunctionBlock = Builder->CreateBasicBlock("function");
        const auto IsClosureBlock = Builder->CreateBasicBlock("closure");
        const auto IsUpvalueBlock = Builder->CreateBasicBlock("upvalue");
        const auto DefaultBlock = Builder->CreateBasicBlock("default");

#if DEBUG_LOG_GC
        static const auto fmt = Builder->CreateGlobalStringPtr("free %p: ");
        Builder->PrintF({fmt, value});
        Builder->PrintObject(value);
#endif

        const auto Switch = Builder->CreateSwitch(Builder->ObjType(value), DefaultBlock);
        Switch->addCase(Builder->ObjTypeInt(ObjType::STRING), IsStringBlock);
        Switch->addCase(Builder->ObjTypeInt(ObjType::FUNCTION), IsFunctionBlock);
        Switch->addCase(Builder->ObjTypeInt(ObjType::CLOSURE), IsClosureBlock);
        Switch->addCase(Builder->ObjTypeInt(ObjType::UPVALUE), IsUpvalueBlock);

        Builder->SetInsertPoint(IsStringBlock);
        Builder->CreateFree(value);
        //Builder->CreateFree(); TODO: free string chars? but they're not allocated by malloc.

        Builder->CreateBr(DefaultBlock);

        Builder->SetInsertPoint(IsFunctionBlock);
        Builder->CreateFree(Builder->AsFunction(value));
        // Don't need to free the name, because it will be freed as a String obj anyway.
        Builder->CreateBr(DefaultBlock);

        Builder->SetInsertPoint(IsClosureBlock);
        const auto closure = Builder->AsClosure(value);
        const auto size = Builder->CreateLoad(Builder->getInt32Ty(), Builder->CreateStructGEP(Builder->getModule().getStructType(ObjType::CLOSURE), closure, 3));
        const auto IsNotNull = Builder->CreateBasicBlock("NotNullArray");
        const auto NullArray = Builder->CreateBasicBlock("NullArray");
        Builder->CreateCondBr(Builder->CreateICmpEQ(size, Builder->getInt32(0)), NullArray, IsNotNull);
        Builder->SetInsertPoint(IsNotNull);
        const auto array = Builder->CreateLoad(Builder->getPtrTy(), Builder->CreateStructGEP(Builder->getModule().getStructType(ObjType::CLOSURE), closure, 2));
        Builder->CreateFree(array);
        Builder->CreateBr(NullArray);
        Builder->SetInsertPoint(NullArray);
        Builder->CreateFree(closure);
        Builder->CreateBr(DefaultBlock);

        Builder->SetInsertPoint(IsUpvalueBlock);
        Builder->CreateFree(Builder->AsUpvalue(value));

        Builder->CreateBr(DefaultBlock);
        Builder->SetInsertPoint(DefaultBlock);
    }
}// namespace lox
