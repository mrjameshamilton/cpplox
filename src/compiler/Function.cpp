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
            const auto namePtr = arguments + 1;
            const auto nameLength = arguments + 2;
            const auto argSize = arguments + 3;
            const auto isNative = arguments + 4;

            const auto ptr = B.AllocateObj(ObjType::FUNCTION, "function");
            const auto name = B.AllocateString(namePtr, nameLength, "name");

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
             CreateGlobalStringPtr(Function->getName()), getInt32(Function->getName().size()), getInt32(Function->arg_size() - 1),
             isNative ? getTrue() : getFalse()
            }
        );
    }

    Value *LoxBuilder::AllocateClosure(llvm::Function *F, bool isNative) {
        const auto function = AllocateFunction(F, isNative);
        const auto ptr = AllocateObj(ObjType::CLOSURE, "closure");

        CreateStore(function, CreateObjStructGEP(ObjType::CLOSURE, ptr, 1));
        CreateStore(getInt8(0), CreateObjStructGEP(ObjType::CLOSURE, ptr, 2));
        CreateStore(getInt32(0), CreateObjStructGEP(ObjType::CLOSURE, ptr, 3));

        return ptr;
    }
}// namespace lox
