#include "LoxCompiler.h"
#include <llvm/IR/Value.h>

namespace lox {
    Value *LoxCompiler::IsTruthy(Value *value) const {
        static auto IsTruthyFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    Builder->getInt1Ty(),
                    Builder->getInt64Ty(),
                    false
                ),
                Function::InternalLinkage,
                "IsTruthy",
                *LoxModule
            );

            const auto EntryBasicBlock = BasicBlock::Create(*Context, "entry", F);
            const auto IsNullBlock = BasicBlock::Create(*Context, "if.null", F);
            const auto IsNotNullBlock = BasicBlock::Create(*Context, "if.not.bool", F);
            const auto IsBoolBlock = BasicBlock::Create(*Context, "if.bool", F);
            const auto EndBlock = BasicBlock::Create(*Context, "if.end", F);

            const auto InsertPoint = Builder->GetInsertBlock();
            Builder->SetInsertPoint(EntryBasicBlock);

            const auto p0 = F->args().begin();
            Builder->CreateCondBr(IsNil(p0), IsNullBlock, IsNotNullBlock);
            Builder->SetInsertPoint(IsNullBlock);
            Builder->CreateRet(Builder->getFalse());
            Builder->SetInsertPoint(IsNotNullBlock);
            Builder->CreateCondBr(IsBool(p0), IsBoolBlock, EndBlock);
            Builder->SetInsertPoint(IsBoolBlock);
            Builder->CreateRet(AsBool(p0));
            Builder->SetInsertPoint(EndBlock);
            Builder->CreateRet(Builder->getTrue());

            Builder->SetInsertPoint(InsertPoint);
            return F;
        }());

        return Builder->CreateCall(IsTruthyFunction, value);
    }

    Value *LoxCompiler::IsNotTruthy(Value *value) const {
        return Builder->CreateNot(IsTruthy(value));
    }
}// namespace lox
