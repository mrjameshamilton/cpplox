#ifndef STACK_H
#define STACK_H
#include "LoxBuilder.h"
#include "LoxModule.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>

using namespace llvm;

constexpr unsigned int GROWTH_FACTOR = 2;

namespace lox {

    // Creates a stack global variable that can store
    // pointers.
    class GlobalStack {
        LoxModule &M;
        StructType *const StackStruct = StructType::create(
            M.getContext(),
            {
                PointerType::getUnqual(M.getContext()),
                IntegerType::getInt32Ty(M.getContext()),
                IntegerType::getInt32Ty(M.getContext()),
            },
            "Stack"
        );
        std::string_view name;
        GlobalVariable *const stack = cast<GlobalVariable>(M.getOrInsertGlobal(
            ("stack_" + name).str(),
            StackStruct
        ));

    public:
        explicit GlobalStack(LoxModule &M, const std::string_view name) : M{M}, name(name) {
            stack->setLinkage(GlobalVariable::PrivateLinkage);
            stack->setAlignment(Align(8));
            stack->setConstant(false);
            stack->setInitializer(ConstantAggregateZero::get(StackStruct));
        }

        Value *CreateGetCount(IRBuilder<> &B) const;

        void CreateSet(LoxBuilder &B, Value *index, Value *value) const;

        void CreatePush(LoxModule &M, IRBuilder<> &Builder, Value *Object) const;
        void CreatePushN(LoxModule &M, IRBuilder<> &Builder, Value *N) const;
        void CreatePopN(LoxBuilder &Builder, Value *N) const;
        void CreatePopAll(LoxBuilder &Builder, Function *FunctionPointer) const;
        void CreateIterateObjectValues(LoxBuilder &Builder, Function *FunctionPointer) const;
        void CreateFree(LoxBuilder &Builder) const;
    };
}// namespace lox
#endif//STACK_H
