#include "ModuleCompiler.h"
#include <llvm/IR/Value.h>

namespace lox {
    Value *LoxBuilder::IsTruthy(Value *value) {
        static auto IsTruthyFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    getInt1Ty(),
                    getInt64Ty(),
                    false
                ),
                Function::InternalLinkage,
                "IsTruthy",
                this->getModule()
            );

            LoxBuilder B(getContext(), getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            const auto IsNullBlock = B.CreateBasicBlock("if.null");
            const auto IsNotNullBlock = B.CreateBasicBlock("if.not.bool");
            const auto IsBoolBlock = B.CreateBasicBlock("if.bool");
            const auto EndBlock = B.CreateBasicBlock("if.end");

            B.SetInsertPoint(EntryBasicBlock);

            const auto p0 = F->args().begin();
            B.CreateCondBr(B.IsNil(p0), IsNullBlock, IsNotNullBlock);
            B.SetInsertPoint(IsNullBlock);
            B.CreateRet(B.getFalse());
            B.SetInsertPoint(IsNotNullBlock);
            B.CreateCondBr(B.IsBool(p0), IsBoolBlock, EndBlock);
            B.SetInsertPoint(IsBoolBlock);
            B.CreateRet(B.AsBool(p0));
            B.SetInsertPoint(EndBlock);
            B.CreateRet(B.getTrue());

            return F;
        }());

        return CreateCall(IsTruthyFunction, value);
    }

    Value *LoxBuilder::IsNotTruthy(Value *value) {
        return CreateNot(IsTruthy(value));
    }
}// namespace lox
