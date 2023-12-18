#include "Compiler.h"
#include "String.h"
#include "Value.h"

#include <llvm/IR/Value.h>

#define DEBUG_LOG_GC true

namespace lox {

    Value *Compiler::AllocateObj(lox::ObjType objType, const std::string_view name) const {
        Type *StructType;

        switch (objType) {
            case ObjType::STRING:
#if DEBUG_LOG_GC
                PrintString("Allocate string:");
#endif
                StructType = StringStructType;
                break;
            // TODO: other types.
            default:
                throw std::runtime_error("Not implemented");
        }

        Type *IntPtrTy = IntegerType::getInt32Ty(*Context);
        // The malloc size IR that is generated with getSizeOf uses a hack described here:
        // https://mukulrathi.com/create-your-own-programming-language/concurrency-runtime-language-tutorial/#malloc
        Constant *allocsize = ConstantExpr::getSizeOf(StructType);
        allocsize = ConstantExpr::getTruncOrBitCast(allocsize, IntPtrTy);

        const auto NewObjMalloc = Builder->CreateMalloc(
            IntPtrTy,
            StructType,
            allocsize,
            nullptr
        );

        Builder->CreateStore(
            Builder->getInt8(static_cast<uint8_t>(objType)),
            Builder->CreateStructGEP(ObjStructType, NewObjMalloc, 0, "ObjType")
        );

        Builder->CreateStore(
            Builder->getFalse(),
            Builder->CreateStructGEP(ObjStructType, NewObjMalloc, 1, "isMarked")
        );

        // Linked-list of all allocated objects for the garbage collector check.
        const auto global = LoxModule->getNamedGlobal("objects");

#ifdef DEBUG_LOG_GC
        static const auto fmt0 = Builder->CreateGlobalStringPtr("\tobjects: %p => ");
        PrintF({fmt0, Builder->CreateLoad(Builder->getPtrTy(), global)});
#endif

        Builder->CreateStore(
            Builder->CreateLoad(Builder->getPtrTy(), global),
            Builder->CreateStructGEP(ObjStructType, NewObjMalloc, 2, "next")
        );

        Builder->CreateStore(NewObjMalloc, global);

#ifdef DEBUG_LOG_GC
        static const auto fmt = Builder->CreateGlobalStringPtr("%p\n");
        PrintF({fmt, Builder->CreateLoad(Builder->getPtrTy(), global)});
        static const auto fmt2 = Builder->CreateGlobalStringPtr("\t%p allocate %zu.\n");
        PrintF({fmt2, NewObjMalloc, allocsize});
        static const auto fmt3 = Builder->CreateGlobalStringPtr("\tobject.next = %p\n");
        PrintF({fmt3, Builder->CreateLoad(Builder->getPtrTy(), Builder->CreateStructGEP(ObjStructType, NewObjMalloc, 2, "next"))});
#endif

        const auto NewObj = CreateEntryBlockAlloca(Builder->GetInsertBlock()->getParent(), StructType, name);
        Builder->CreateStore(NewObjMalloc, NewObj);

        return Builder->CreateBitCast(NewObj, StructType->getPointerTo());
    }

    void Compiler::FreeObjects() const {
        const auto global = LoxModule->getNamedGlobal("objects");
        const auto object = CreateEntryBlockAlloca(MainFunction, Builder->getPtrTy(), "object");
        const auto next = CreateEntryBlockAlloca(MainFunction, Builder->getPtrTy(), "next");
        Builder->CreateStore(
            Builder->CreateLoad(Builder->getPtrTy(), global),
            object
        );

        const auto WhileCond = BasicBlock::Create(*Context, "while.cond", MainFunction);
        const auto WhileBody = BasicBlock::Create(*Context, "while.body", MainFunction);
        const auto WhileEnd = BasicBlock::Create(*Context, "while.end", MainFunction);

        Builder->CreateBr(WhileCond);
        Builder->SetInsertPoint(WhileCond);
        Builder->CreateCondBr(Builder->CreateIsNotNull(Builder->CreateLoad(Builder->getPtrTy(), object)), WhileBody, WhileEnd);
        Builder->SetInsertPoint(WhileBody);

        Builder->CreateStore(
            Builder->CreateLoad(
                Builder->getPtrTy(),
                Builder->CreateStructGEP(ObjStructType, Builder->CreateLoad(Builder->getPtrTy(), object), 2, "next")
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

    void Compiler::FreeObject(Value *value) const {
        const auto IsStringBlock = BasicBlock::Create(*Context, "string", MainFunction);
        const auto DefaultBlock = BasicBlock::Create(*Context, "default", MainFunction);

        const auto Switch = Builder->CreateSwitch(ObjType(value), DefaultBlock);
        Switch->addCase(Builder->getInt8(static_cast<uint8_t>(ObjType::STRING)), IsStringBlock);


        Builder->SetInsertPoint(IsStringBlock);
#ifdef DEBUG_LOG_GC
        static const auto fmt = Builder->CreateGlobalStringPtr("free '%s' @ %p\n");
        PrintF({fmt, AsCString(value), value});
        //Builder->CreateFree(); TODO: free string chars? but their not allocated by malloc.
        Builder->CreateFree(value);
#endif

        Builder->CreateBr(DefaultBlock);
        Builder->SetInsertPoint(DefaultBlock);
    }


}// namespace lox
