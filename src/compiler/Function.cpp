#include "FunctionCompiler.h"
#include "LoxBuilder.h"
#include "ModuleCompiler.h"
#include "Stack.h"

namespace lox {

    static Value *AllocateFunction(LoxBuilder &Builder, llvm::Function *Function, Value *name, const bool isNative) {
        static auto *AllocateFunctionFunction([&Builder] {
            auto *const F = Function::Create(
                FunctionType::get(
                    Builder.getPtrTy(),
                    {Builder.getPtrTy(), Builder.getPtrTy(), Builder.getInt32Ty(), Builder.getInt1Ty()},
                    false
                ),
                Function::InternalLinkage,
                "$allocateFunction",
                Builder.getModule()
            );

            LoxBuilder B(Builder.getContext(), Builder.getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->args().begin();

            auto *const functionPtr = arguments;
            auto *const name = arguments + 1;
            auto *const argSize = arguments + 2;
            auto *const isNative = arguments + 3;

            auto *const ptr = B.AllocateObj(ObjType::FUNCTION, "function");

            B.CreateStore(argSize, B.CreateObjStructGEP(ObjType::FUNCTION, ptr, 1, "argSize"));
            B.CreateStore(functionPtr, B.CreateObjStructGEP(ObjType::FUNCTION, ptr, 2, "funcPtr"));
            B.CreateStore(name, B.CreateObjStructGEP(ObjType::FUNCTION, ptr, 3, "name"));
            B.CreateStore(isNative, B.CreateObjStructGEP(ObjType::FUNCTION, ptr, 4, "isNative"));

            B.CreateInvariantStart(ptr);

            B.CreateRet(ptr);

            return F;
        }());

        return Builder.CreateCall(
            AllocateFunctionFunction,
            {Function,
             name,
             Builder.getInt32(Function->arg_size() - 2),// receiver + upvalues
             isNative ? Builder.getTrue() : Builder.getFalse()
            }
        );
    }

    Value *LoxBuilder::AllocateClosure(FunctionCompiler &compiler, llvm::Function *function, const std::string_view name, const bool isNative) {
        static auto *AllocationClosureFunction([this] {
            auto *const F = Function::Create(
                FunctionType::get(
                    getPtrTy(),
                    {getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$allocateClosure",
                getModule()
            );

            LoxBuilder B(getContext(), getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const function = F->args().begin();

            auto *const ptr = B.AllocateObj(ObjType::CLOSURE, "closure");

            B.CreateStore(function, B.CreateObjStructGEP(ObjType::CLOSURE, ptr, 1));
            // An array will be allocated for the upvalues in lox::FunctionCompiler::CreateFunction.
            B.CreateStore(B.getNullPtr(), B.CreateObjStructGEP(ObjType::CLOSURE, ptr, 2));
            B.CreateStore(B.getInt32(0), B.CreateObjStructGEP(ObjType::CLOSURE, ptr, 3));

            B.CreateRet(ptr);

            return F;
        }());

        auto *const nameObj = AllocateString(name);
        compiler.insertTemp(ObjVal(nameObj), "function name");
        auto *const functionObj = AllocateFunction(*this, function, nameObj, isNative);
        compiler.insertTemp(ObjVal(functionObj), "function");

        auto *const ptr = CreateCall(AllocationClosureFunction, {functionObj});

        if (isNative) {
            // Native closures won't change.
            CreateInvariantStart(ptr);
        }

        return ptr;
    }
}// namespace lox
