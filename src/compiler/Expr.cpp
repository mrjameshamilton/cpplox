#include "LoxCompiler.h"

using namespace llvm;
using namespace llvm::sys;

namespace lox {

    Value *LoxCompiler::evaluate(const Expr &expr) {
        return std::visit(*this, expr);
    }

    Value *LoxCompiler::operator()(const AssignExprPtr &assignExpr) {
        const auto value = evaluate(assignExpr->value);
        /*const auto global = LoxModule->getNamedGlobal(assignExpr->name.getLexeme());
            Builder->CreateStore(value, global);*/
        const auto current = *variables.begin(assignExpr->name.getLexeme());
        Builder->CreateStore(value, current);
        return value;
    }

    Value *LoxCompiler::operator()(const BinaryExprPtr &binaryExpr) {
        const auto left = evaluate(binaryExpr->left);
        const auto right = evaluate(binaryExpr->right);

        switch (binaryExpr->op) {
            case BinaryOp::PLUS: {
                const auto IsMaybeStringBlock = Builder->CreateBasicBlock("if.string");
                const auto IsStringBlock = Builder->CreateBasicBlock("is.string");
                const auto IsNumBlock = Builder->CreateBasicBlock("if.num");
                const auto InvalidBlock = Builder->CreateBasicBlock("invalid");
                const auto EndBlock = Builder->CreateBasicBlock("if.end");
                Builder->CreateCondBr(Builder->CreateAnd(Builder->IsNumber(left), Builder->IsNumber(right)), IsNumBlock, IsMaybeStringBlock);
                Builder->SetInsertPoint(IsNumBlock);
                const auto &X = Builder->NumberVal(Builder->CreateFAdd(Builder->AsNumber(left), Builder->AsNumber(right)));
                Builder->CreateBr(EndBlock);

                Builder->SetInsertPoint(IsMaybeStringBlock);
                Builder->CreateCondBr(Builder->CreateAnd(Builder->IsString(left), Builder->IsString(right)), IsStringBlock, InvalidBlock);
                Builder->SetInsertPoint(IsStringBlock);
                const auto &Y = Builder->Concat(left, right);
                Builder->CreateBr(EndBlock);

                Builder->SetInsertPoint(InvalidBlock);
                // TODO: Throw exception here.
                const auto &Z = Builder->getNilVal();
                Builder->CreateBr(EndBlock);

                Builder->SetInsertPoint(EndBlock);

                const auto &Result = Builder->CreatePHI(Builder->getInt64Ty(), 3);
                Result->addIncoming(X, IsNumBlock);
                Result->addIncoming(Y, IsStringBlock);
                Result->addIncoming(Z, InvalidBlock);

                return Result;
            }
            case BinaryOp::MINUS:
                return Builder->NumberVal(Builder->CreateFSub(Builder->AsNumber(left), Builder->AsNumber(right)));
            case BinaryOp::SLASH:
                return Builder->NumberVal(Builder->CreateFDiv(Builder->AsNumber(left), Builder->AsNumber(right)));
            case BinaryOp::STAR:
                return Builder->NumberVal(Builder->CreateFMul(Builder->AsNumber(left), Builder->AsNumber(right)));
            case BinaryOp::GREATER:
                return Builder->BoolVal(Builder->CreateFCmpOGT(Builder->AsNumber(left), Builder->AsNumber(right)));
            case BinaryOp::GREATER_EQUAL:
                return Builder->BoolVal(Builder->CreateFCmpOGE(Builder->AsNumber(left), Builder->AsNumber(right)));
            case BinaryOp::LESS:
                return Builder->BoolVal(Builder->CreateFCmpOLT(Builder->AsNumber(left), Builder->AsNumber(right)));
            case BinaryOp::LESS_EQUAL:
                return Builder->BoolVal(Builder->CreateFCmpOLE(Builder->AsNumber(left), Builder->AsNumber(right)));
            case BinaryOp::BANG_EQUAL:
            case BinaryOp::EQUAL_EQUAL:
                // A == B if both are numbers and they're equal as fp numbers or they're both equal int64 values.
                const auto IsNumBlock = Builder->CreateBasicBlock("if.num");
                const auto NotNumBlock = Builder->CreateBasicBlock("not.num");
                const auto IsStringBlock = Builder->CreateBasicBlock("is.string");
                const auto NotStringBlock = Builder->CreateBasicBlock("not.string");
                const auto EndBlock = Builder->CreateBasicBlock("end");

                Builder->CreateCondBr(Builder->CreateAnd(Builder->IsNumber(left), Builder->IsNumber(right)), IsNumBlock, NotNumBlock);
                Builder->SetInsertPoint(IsNumBlock);
                const auto X = Builder->CreateFCmpOEQ(Builder->AsNumber(left), Builder->AsNumber(right));
                Builder->CreateBr(EndBlock);
                Builder->SetInsertPoint(NotNumBlock);
                Builder->CreateCondBr(Builder->CreateAnd(Builder->IsString(left), Builder->IsString(right)), IsStringBlock, NotStringBlock);
                Builder->SetInsertPoint(IsStringBlock);
                const auto Y = Builder->StrEquals(left, right);
                Builder->CreateBr(EndBlock);
                Builder->SetInsertPoint(NotStringBlock);
                const auto Z = Builder->CreateICmpEQ(left, right);
                Builder->CreateBr(EndBlock);
                Builder->SetInsertPoint(EndBlock);

                const auto Result = Builder->CreatePHI(Builder->getInt1Ty(), 3);
                Result->addIncoming(X, IsNumBlock);
                Result->addIncoming(Y, IsStringBlock);
                Result->addIncoming(Z, NotStringBlock);

                return Builder->BoolVal(binaryExpr->op == BinaryOp::EQUAL_EQUAL ? Result : Builder->CreateNot(Result));
        }

        std::unreachable();
    }

    Value *LoxCompiler::operator()(const CallExprPtr &callExpr) const {
        throw std::runtime_error("not implemented");
    }

    Value *LoxCompiler::operator()(const GetExprPtr &getExpr) const {
        throw std::runtime_error("not implemented");
    }

    Value *LoxCompiler::operator()(const SetExprPtr &setExpr) const {
        throw std::runtime_error("not implemented");
    }

    Value *LoxCompiler::operator()(const ThisExprPtr &thisExpr) const {
        throw std::runtime_error("not implemented");
    }

    Value *LoxCompiler::operator()(const SuperExprPtr &superExpr) const {
        throw std::runtime_error("not implemented");
    }

    Value *LoxCompiler::operator()(const VarExprPtr &varExpr) const {
        const auto value = variables.lookup(varExpr->name.getLexeme());
        return Builder->CreateLoad(Builder->getInt64Ty(), value);
        //if (varExpr->distance == -1) {
        const auto global = LoxModule->getNamedGlobal(varExpr->name.getLexeme());
        return Builder->CreateLoad(Builder->getInt64Ty(), global);
        //}
    }

    Value *LoxCompiler::operator()(const GroupingExprPtr &groupingExpr) {
        return evaluate(groupingExpr->expression);
    }

    Value *LoxCompiler::operator()(const LiteralExprPtr &literalExpr) {
        return std::visit(
            overloaded{
                [this](const bool value) -> Value * { return Builder->BoolVal(value ? Builder->getTrue() : Builder->getFalse()); },
                [this](const double double_value) -> Value * {
                    // bitcast double -> i64.
                    uint64_t value;
                    memcpy(&value, &double_value, sizeof(double));
                    return Builder->getInt64(value);
                },
                [this](const std::string_view string_value) -> Value * {
                    if (strings.contains(string_value))
                        return strings.at(string_value);

                    const auto value = Builder->AllocateString(
                        LoxModule->getNamedGlobal("objects"),
                        Builder->CreateGlobalStringPtr(string_value),
                        Builder->getInt32(string_value.length())
                    );

                    strings[string_value] = value;

                    return value;
                },
                [this](const std::nullptr_t) -> Value * { return Builder->getNilVal(); },
            },
            literalExpr->literal
        );
    }

    Value *LoxCompiler::operator()(const LogicalExprPtr &logicalExpr) {
        const auto left = evaluate(logicalExpr->left);

        switch (logicalExpr->op) {
            case LogicalOp::AND:
            case LogicalOp::OR: {
                const auto LeftIsTruthyBlock = Builder->CreateBasicBlock("if.left.truthy");
                const auto LeftNotTruthyBlock = Builder->CreateBasicBlock("if.left.nottruthy");
                const auto EndBlock = Builder->CreateBasicBlock("end");
                Builder->CreateCondBr(
                    Builder->IsTruthy(left),
                    logicalExpr->op == LogicalOp::OR ? LeftIsTruthyBlock : LeftNotTruthyBlock,
                    logicalExpr->op == LogicalOp::OR ? LeftNotTruthyBlock : LeftIsTruthyBlock
                );
                Builder->SetInsertPoint(LeftNotTruthyBlock);
                const auto X = Builder->IsTruthy(evaluate(logicalExpr->right));
                const auto EndLeftNotTruthyBlock = Builder->GetInsertBlock();
                Builder->CreateBr(EndBlock);
                Builder->SetInsertPoint(LeftIsTruthyBlock);
                const auto Y = logicalExpr->op == LogicalOp::OR ? Builder->getTrue() : Builder->getFalse();
                Builder->CreateBr(EndBlock);
                Builder->SetInsertPoint(EndBlock);

                const auto Result = Builder->CreatePHI(Builder->getInt1Ty(), 2);
                Result->addIncoming(X, EndLeftNotTruthyBlock);
                Result->addIncoming(Y, LeftIsTruthyBlock);

                return Builder->BoolVal(Result);
            }
        }
        std::unreachable();
    }

    Value *LoxCompiler::operator()(const UnaryExprPtr &unaryExpr) {
        const auto left = evaluate(unaryExpr->expression);

        switch (unaryExpr->op) {
            case UnaryOp::BANG:
                return Builder->BoolVal(Builder->CreateSelect(Builder->IsTruthy(left), Builder->getFalse(), Builder->getTrue()));
            case UnaryOp::MINUS:
                return Builder->NumberVal(Builder->CreateFNeg(Builder->AsNumber(left)));
        }

        std::unreachable();
    }

}// namespace lox
