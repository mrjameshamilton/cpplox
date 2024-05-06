#include "FunctionCompiler.h"
#include "LoxBuilder.h"
#include "ModuleCompiler.h"
#include "Stack.h"

namespace lox {

    static Value *AllocateFunction(LoxBuilder &Builder, llvm::Function *Function, Value *name, const bool isNative) {
        static auto AllocateFunctionFunction([&Builder] {
            const auto F = Function::Create(
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

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();

            const auto functionPtr = arguments;
            const auto name = arguments + 1;
            const auto argSize = arguments + 2;
            const auto isNative = arguments + 3;

            const auto ptr = B.AllocateObj(ObjType::FUNCTION, "function");

            B.CreateStore(argSize, B.CreateObjStructGEP(ObjType::FUNCTION, ptr, 1), "argSize");
            B.CreateStore(functionPtr, B.CreateObjStructGEP(ObjType::FUNCTION, ptr, 2, "funcPtr"));
            B.CreateStore(name, B.CreateObjStructGEP(ObjType::FUNCTION, ptr, 3), "name");
            B.CreateStore(isNative, B.CreateObjStructGEP(ObjType::FUNCTION, ptr, 4), "isNative");

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
        static auto AllocationClosureFunction([this] {
            const auto F = Function::Create(
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

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();

            const auto function = arguments;

            const auto ptr = B.AllocateObj(ObjType::CLOSURE, "closure");

            B.CreateStore(function, B.CreateObjStructGEP(ObjType::CLOSURE, ptr, 1));
            // An array will be allocated for the upvalues in lox::FunctionCompiler::CreateFunction.
            B.CreateStore(B.getNullPtr(), B.CreateObjStructGEP(ObjType::CLOSURE, ptr, 2));
            B.CreateStore(B.getInt32(0), B.CreateObjStructGEP(ObjType::CLOSURE, ptr, 3));

            B.CreateRet(ptr);

            return F;
        }());

        const auto nameValue = compiler.insertTemp(AllocateString(name), "function name");
        return CreateCall(
            AllocationClosureFunction,
            {compiler.insertTemp(AllocateFunction(*this, function, nameValue, isNative), "function")}
        );
    }
}// namespace lox
