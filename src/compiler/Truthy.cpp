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

            const auto EntryBasicBlock = CreateBasicBlock("entry", F);
            const auto IsNullBlock = CreateBasicBlock("if.null", F);
            const auto IsNotNullBlock = CreateBasicBlock("if.not.bool", F);
            const auto IsBoolBlock = CreateBasicBlock("if.bool", F);
            const auto EndBlock = CreateBasicBlock("if.end", F);

            const auto InsertPoint = GetInsertBlock();
            SetInsertPoint(EntryBasicBlock);

            const auto p0 = F->args().begin();
            CreateCondBr(IsNil(p0), IsNullBlock, IsNotNullBlock);
            SetInsertPoint(IsNullBlock);
            CreateRet(getFalse());
            SetInsertPoint(IsNotNullBlock);
            CreateCondBr(IsBool(p0), IsBoolBlock, EndBlock);
            SetInsertPoint(IsBoolBlock);
            CreateRet(AsBool(p0));
            SetInsertPoint(EndBlock);
            CreateRet(getTrue());

            SetInsertPoint(InsertPoint);
            return F;
        }());

        return CreateCall(IsTruthyFunction, value);
    }

    Value *LoxBuilder::IsNotTruthy(Value *value) {
        return CreateNot(IsTruthy(value));
    }
}// namespace lox
