#include "Class.h"
#include "FunctionCompiler.h"
#include "Upvalue.h"

#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <ranges>
#include <vector>

namespace lox {

    void FunctionCompiler::evaluate(const Stmt &stmt) {
        std::visit(*this, stmt);
    }

    void FunctionCompiler::operator()(const BlockStmtPtr &blockStmt) {
        beginScope();
        for (auto &statement: blockStmt->statements) {
            evaluate(statement);
        }
        endScope();
    }

    Value *FunctionCompiler::CreateFunction(const FunctionStmtPtr &functionStmt, const std::string_view name) {
        std::vector<Type *> paramTypes(functionStmt->parameters.size(), Builder.getInt64Ty());
        // The second parameter is for the receiver instance.
        paramTypes.insert(paramTypes.begin(), Builder.getInt64Ty());
        // The first parameter is the upvalues.
        paramTypes.insert(paramTypes.begin(), Builder.getPtrTy());
        FunctionType *FT = FunctionType::get(IntegerType::getInt64Ty(Builder.getContext()), paramTypes, false);

        Function *F = Function::Create(
            FT,
            Function::InternalLinkage,
            name,
            Builder.getModule()
        );

        const auto closurePtr = Builder.AllocateClosure(F, false);

        if (!functionStmt->isMethod) {
            // Methods aren't stored as variables.
            insertVariable(functionStmt->name.getLexeme(), Builder.ObjVal(closurePtr));
        }

        FunctionCompiler C(Builder.getContext(), Builder.getModule(), *F, this);
        C.compile(functionStmt->body, functionStmt->parameters, [&C, &functionStmt](LoxBuilder &B) {
            if (functionStmt->isMethod) {
                C.insertVariable("this", B.getFunction()->arg_begin() + 1);
            }
        });

        // Store captured variables in the closure's upvalue array.
        if (!C.upvalues.empty()) {
            const auto upvaluesArrayPtr = Builder.AllocateArray(Builder.getModule().getStructType(ObjType::UPVALUE), C.upvalues.size(), "upvaluesArrayPtr");

            Builder.CreateStore(
                upvaluesArrayPtr,
                Builder.CreateStructGEP(Builder.getModule().getStructType(ObjType::CLOSURE), closurePtr, 2, "closure.upvalues")
            );
            Builder.CreateStore(
                Builder.getInt32(C.upvalues.size()),
                Builder.CreateStructGEP(Builder.getModule().getStructType(ObjType::CLOSURE), closurePtr, 3, "closure.upvaluesCount")
            );

            for (auto &upvalue: C.upvalues) {
                const auto upvalueIndex = Builder.CreateGEP(Builder.getPtrTy(), upvaluesArrayPtr, Builder.getInt32(upvalue->index), "upvalueIndex");
                Builder.CreateStore(upvalue->isLocal ? captureLocal(upvalue->value) : upvalue->value, upvalueIndex);
            }
        }

        return closurePtr;
    }

    void FunctionCompiler::operator()(const FunctionStmtPtr &functionStmt) {
        CreateFunction(
            functionStmt,
            functionStmt->name.getLexeme()
        );
    }

    void FunctionCompiler::operator()(const ExpressionStmtPtr &expressionStmt) {
        evaluate(expressionStmt->expression);
    }

    void FunctionCompiler::operator()(const PrintStmtPtr &printStmt) {
        Builder.Print(evaluate(printStmt->expression));
    }

    void FunctionCompiler::operator()(const ReturnStmtPtr &returnStmt) {
        BasicBlock *ExitBasicBlock = Builder.CreateBasicBlock("return");
        BasicBlock *NewBasicBlock = Builder.CreateBasicBlock("return.unreachable");
        Builder.CreateBr(ExitBasicBlock);
        Builder.SetInsertPoint(ExitBasicBlock);
        Builder.CreateRet(returnStmt->expression.has_value() ? evaluate(returnStmt->expression.value()) : Builder.getNilVal());
        Builder.SetInsertPoint(NewBasicBlock);
    }

    void FunctionCompiler::operator()(const VarStmtPtr &varStmt) {
        if (!enclosing && scopes.size() == 1) {
            // Global variables can be re-declared.
            if (const auto variable = lookupGlobal(varStmt->name.getLexeme())) {
                Builder.CreateStore(evaluate(varStmt->initializer), variable);
                return;
            }
        }

        insertVariable(varStmt->name.getLexeme(), evaluate(varStmt->initializer));
    }

    void FunctionCompiler::operator()(const WhileStmtPtr &whileStmt) {
        const auto Cond = Builder.CreateBasicBlock("Cond");
        const auto Body = Builder.CreateBasicBlock("Loop");
        const auto Exit = Builder.CreateBasicBlock("Exit");

        Builder.CreateBr(Cond);
        Builder.SetInsertPoint(Cond);
        Builder.CreateCondBr(Builder.IsTruthy(evaluate(whileStmt->condition)), Body, Exit);
        Builder.SetInsertPoint(Body);
        evaluate(whileStmt->body);
        Builder.CreateBr(Cond);
        Builder.SetInsertPoint(Exit);
    }

    void FunctionCompiler::operator()(const IfStmtPtr &ifStmt) {
        const auto TrueBlock = Builder.CreateBasicBlock("if.true");
        const auto FalseBlock = Builder.CreateBasicBlock("else");
        const auto EndBlock = Builder.CreateBasicBlock("if.end");
        Builder.CreateCondBr(Builder.IsTruthy(evaluate(ifStmt->condition)), TrueBlock, FalseBlock);
        Builder.SetInsertPoint(TrueBlock);
        evaluate(ifStmt->thenBranch);
        Builder.CreateBr(EndBlock);
        Builder.SetInsertPoint(FalseBlock);
        if (ifStmt->elseBranch.has_value()) {
            evaluate(ifStmt->elseBranch.value());
        }
        Builder.CreateBr(EndBlock);
        Builder.SetInsertPoint(EndBlock);
    }

    void FunctionCompiler::operator()(const ClassStmtPtr &classStmt) {
        const auto klass = Builder.AllocateClass(classStmt->name.getLexeme());
        const auto methods = Builder.CreateLoad(Builder.getPtrTy(), Builder.CreateObjStructGEP(ObjType::CLASS, klass, 2));

        insertVariable(classStmt->name.getLexeme(), Builder.ObjVal(klass));

        for (auto &method: classStmt->methods) {
            const auto methodPtr = CreateFunction(method, (classStmt->name.getLexeme() + "_" + method->name.getLexeme()).str());
            Builder.TableSet(methods, Builder.AllocateString(method->name.getLexeme()), Builder.ObjVal(methodPtr));
        }
    }
}// namespace lox