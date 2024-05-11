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

        Value *getCount(LoxBuilder &B) const;
        void setCount(LoxBuilder &B, Value *count) const;

        void CreatePush(LoxBuilder &Builder, Value *Object, StringRef what) const;
        void CreatePop(LoxBuilder &Builder) const;
        void CreatePopAll(LoxBuilder &Builder, Function *FunctionPointer) const;
        void CreateIterateObjectValues(LoxBuilder &Builder, Function *FunctionPointer) const;
        void CreateFree(LoxBuilder &Builder) const;
    };

    void PushGlobal(LoxBuilder &Builder, GlobalVariable *global, std::string_view name);
    void IterateGlobals(LoxBuilder &Builder, Function *FunctionPointer);

    void PushLocal(LoxBuilder &Builder, Value *local, StringRef what);
    void IterateLocals(LoxBuilder &Builder, Function *FunctionPointer);
}// namespace lox
#endif//STACK_H
