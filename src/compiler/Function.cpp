#include "LoxBuilder.h"

namespace lox {

    Value *LoxBuilder::AllocateFunction(llvm::Function *Function, const bool isNative) {
        static auto AllocateFunctionFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    getPtrTy(),
                    {getPtrTy(), getPtrTy(), getInt32Ty(), getInt32Ty(), getInt1Ty()},
                    false
                ),
                Function::InternalLinkage,
                "$allocateFunction",
                getModule()
            );

            LoxBuilder B(getContext(), getModule(), *F);

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

        return CreateCall(
            AllocateFunctionFunction,
            {Function,
             AllocateString(Function->getName()), getInt32(Function->arg_size() - 1),
             isNative ? getTrue() : getFalse()
            }
        );
    }

    Value *LoxBuilder::AllocateClosure(llvm::Function *Function, bool isNative) {
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
            B.CreateStore(B.getInt8(0), B.CreateObjStructGEP(ObjType::CLOSURE, ptr, 2));
            B.CreateStore(B.getInt32(0), B.CreateObjStructGEP(ObjType::CLOSURE, ptr, 3));
            B.CreateRet(ptr);

            return F;
        }());

        return CreateCall(AllocationClosureFunction, {AllocateFunction(Function, isNative)});
    }
}// namespace lox
