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

            const auto NewObjMalloc = B.CreateMalloc(
                IntPtrTy,
                B.getPtrTy(),
                allocsize,
                nullptr
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

#if DEBUG_LOG_GC
            static const auto fmt = B.CreateGlobalStringPtr("%p\n");
            B.PrintF({fmt, B.CreateLoad(B.getPtrTy(), objects)});
            static const auto fmt2 = B.CreateGlobalStringPtr("\t%p allocate %zu.\n");
            B.PrintF({fmt2, NewObjMalloc, allocsize});
            static const auto fmt3 = B.CreateGlobalStringPtr("\tobject.next = %p\n");
            B.PrintF({fmt3, B.CreateLoad(getPtrTy(), B.CreateStructGEP(B.getModule().getObjStructType(), NewObjMalloc, 2, "next"))});
#endif

            B.CreateRet(NewObjMalloc);

            return F;
        }());

#if DEBUG_LOG_GC
        switch (objType) {
            case ObjType::STRING:
                PrintString(Twine("Allocate string"));
                break;
            case ObjType::FUNCTION:
                PrintString(Twine("Allocate function"));
                break;
            case ObjType::CLOSURE:
                PrintString(Twine("Allocate closure"));
                break;
            case ObjType::UPVALUE:
                PrintString(Twine("Allocate upvalue"));
                break;
            default:
                PrintString(Twine("Allocate object"));
        }
        static const auto fmt0 = CreateGlobalStringPtr("\tobjects: %p => ");
        const auto objects = getModule().getObjects();
        PrintF({fmt0, CreateLoad(getPtrTy(), objects)});
#endif

        return CreateCall(AllocateObjectFunction, {ObjTypeInt(objType)}, name);
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
        const auto IsClassBlock = Builder->CreateBasicBlock("class");
        const auto IsInstanceBlock = Builder->CreateBasicBlock("instance");
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
        Switch->addCase(Builder->ObjTypeInt(ObjType::CLASS), IsClassBlock);
        Switch->addCase(Builder->ObjTypeInt(ObjType::INSTANCE), IsInstanceBlock);

        Builder->SetInsertPoint(IsStringBlock);
        Builder->CreateFree(value);
        //Builder->CreateFree(); TODO: free string chars? but they're not allocated by malloc.

        Builder->CreateBr(DefaultBlock);

        Builder->SetInsertPoint(IsFunctionBlock);
        Builder->CreateFree(Builder->AsObj(value));
        // Don't need to free the name, because it will be freed as a String obj anyway.
        Builder->CreateBr(DefaultBlock);

        Builder->SetInsertPoint(IsClosureBlock);
        const auto closure = Builder->AsObj(value);
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
        Builder->CreateFree(Builder->AsObj(value));
        Builder->CreateBr(DefaultBlock);

        Builder->SetInsertPoint(IsClassBlock);
        Builder->CreateFree(Builder->AsObj(value));

        Builder->CreateBr(DefaultBlock);

        Builder->SetInsertPoint(IsInstanceBlock);
        Builder->CreateFree(Builder->AsObj(value));

        Builder->CreateBr(DefaultBlock);
        Builder->SetInsertPoint(DefaultBlock);
    }
}// namespace lox
