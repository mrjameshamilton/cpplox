#include "LoxCompiler.h"
#include "Value.h"

#include <llvm/IR/Value.h>

#define DEBUG_LOG_GC true

namespace lox {

    AllocaInst *CreateEntryBlockAlloca(Function *TheFunction, Type *type, const std::string_view &VarName) {
        IRBuilder TmpB(&TheFunction->getEntryBlock(), TheFunction->getEntryBlock().begin());
        return TmpB.CreateAlloca(type, nullptr, VarName);
    }

    Value *LoxBuilder::AllocateObj(Value *objects, enum ObjType objType, const std::string_view name) {
        Type *StructType = getStructType(objType);

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
            getInt8(static_cast<uint8_t>(objType)),
            CreateStructGEP(ObjStructType, NewObjMalloc, 0, "ObjType")
        );

        CreateStore(
            getFalse(),
            CreateStructGEP(ObjStructType, NewObjMalloc, 1, "isMarked")
        );

#ifdef DEBUG_LOG_GC
        static const auto fmt0 = CreateGlobalStringPtr("\tobjects: %p => ");
        PrintF({fmt0, CreateLoad(getPtrTy(), objects)});
#endif

        CreateStore(
            CreateLoad(getPtrTy(), objects),
            CreateStructGEP(ObjStructType, NewObjMalloc, 2, "next")
        );

        CreateStore(NewObjMalloc, objects);

#ifdef DEBUG_LOG_GC
        static const auto fmt = CreateGlobalStringPtr("%p\n");
        PrintF({fmt, CreateLoad(getPtrTy(), objects)});
        static const auto fmt2 = CreateGlobalStringPtr("\t%p allocate %zu.\n");
        PrintF({fmt2, NewObjMalloc, allocsize});
        static const auto fmt3 = CreateGlobalStringPtr("\tobject.next = %p\n");
        PrintF({fmt3, CreateLoad(getPtrTy(), CreateStructGEP(ObjStructType, NewObjMalloc, 2, "next"))});
#endif

        const auto NewObj = CreateEntryBlockAlloca(GetInsertBlock()->getParent(), StructType, name);
        CreateStore(NewObjMalloc, NewObj);

        return CreateBitCast(NewObj, StructType->getPointerTo());
    }

    void LoxCompiler::FreeObjects() const {
        const auto global = LoxModule->getNamedGlobal("objects");
        const auto object = CreateEntryBlockAlloca(Builder->getFunction(), Builder->getPtrTy(), "object");
        const auto next = CreateEntryBlockAlloca(Builder->getFunction(), Builder->getPtrTy(), "next");
        Builder->CreateStore(
            Builder->CreateLoad(Builder->getPtrTy(), global),
            object
        );

        const auto WhileCond = BasicBlock::Create(Builder->getContext(), "while.cond", Builder->getFunction());
        const auto WhileBody = BasicBlock::Create(Builder->getContext(), "while.body", Builder->getFunction());
        const auto WhileEnd = BasicBlock::Create(Builder->getContext(), "while.end", Builder->getFunction());

        Builder->CreateBr(WhileCond);
        Builder->SetInsertPoint(WhileCond);
        Builder->CreateCondBr(Builder->CreateIsNotNull(Builder->CreateLoad(Builder->getPtrTy(), object)), WhileBody, WhileEnd);
        Builder->SetInsertPoint(WhileBody);

        Builder->CreateStore(
            Builder->CreateLoad(
                Builder->getPtrTy(),
                Builder->CreateStructGEP(Builder->getObjStructType(), Builder->CreateLoad(Builder->getPtrTy(), object), 2, "next")
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

    void LoxCompiler::FreeObject(Value *value) const {
        const auto IsStringBlock = BasicBlock::Create(Builder->getContext(), "string", Builder->getFunction());
        const auto DefaultBlock = BasicBlock::Create(Builder->getContext(), "default", Builder->getFunction());

        const auto Switch = Builder->CreateSwitch(Builder->ObjType(value), DefaultBlock);
        Switch->addCase(Builder->getInt8(static_cast<uint8_t>(ObjType::STRING)), IsStringBlock);


        Builder->SetInsertPoint(IsStringBlock);
#ifdef DEBUG_LOG_GC
        static const auto fmt = Builder->CreateGlobalStringPtr("free '%s' @ %p\n");
        Builder->PrintF({fmt, Builder->AsCString(value), value});
        //Builder->CreateFree(); TODO: free string chars? but they're not allocated by malloc.
        Builder->CreateFree(value);
#endif

        Builder->CreateBr(DefaultBlock);
        Builder->SetInsertPoint(DefaultBlock);
    }
}// namespace lox
