#ifndef COMPILER_H
#define COMPILER_H

#include "../AST.h"
#include "LoxBuilder.h"
#include "Value.h"

#include <llvm/ADT/ScopedHashTable.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/NoFolder.h>
#include <llvm/IR/Value.h>
#include <llvm/Passes/PassBuilder.h>
#include <stack>

using namespace llvm;
using namespace llvm::sys;

template<>
struct llvm::DenseMapInfo<std::string_view> {
    static inline std::string_view getEmptyKey() {
        return "$EMPTY KEY$";
    }

    static inline std::string_view getTombstoneKey() {
        return "$TOMBSTONE KEY$";
    }

    static inline unsigned getHashValue(std::string_view Val);
    static inline bool isEqual(std::string_view LHS, std::string_view RHS);
};

inline unsigned DenseMapInfo<std::string_view>::getHashValue(const std::string_view Val) {
    constexpr std::hash<std::string_view> hasher;
    return hasher(Val);
}

inline bool DenseMapInfo<std::string_view>::isEqual(const std::string_view LHS, const std::string_view RHS) {
    return LHS == RHS;
}

namespace lox {

    AllocaInst *CreateEntryBlockAlloca(Function *TheFunction, Type *type, const std::string_view &VarName);

    struct LoxCompiler {
        std::shared_ptr<LLVMContext> Context = std::make_shared<LLVMContext>();
        std::shared_ptr<Module> LoxModule = std::make_shared<Module>("lox", *Context);
        Function *MainFunction =
            Function::Create(FunctionType::get(IntegerType::getInt32Ty(*Context), false), Function::ExternalLinkage, "main", *LoxModule);
        std::unique_ptr<LoxBuilder> Builder = std::make_unique<LoxBuilder>(*Context, *LoxModule, *MainFunction);
        using ScopedHTType = ScopedHashTable<std::string_view, Value *>;
        ScopedHTType variables;
        std::stack<ScopedHTType::ScopeTy> scopes;
        std::unordered_map<std::string_view, Value *> strings;


        LoxCompiler() = default;

        void FreeObjects() const;
        void FreeObject(Value *value) const;

        void evaluate(const Program &program);

        // Statement code generation.
        void evaluate(const Stmt &stmt);
        void operator()(const BlockStmtPtr &blockStmt);
        void operator()(const FunctionStmtPtr &functionStmt) const;
        void operator()(const ExpressionStmtPtr &expressionStmt);
        void operator()(const PrintStmtPtr &printStmt);
        void operator()(const ReturnStmtPtr &returnStmt) const;
        void operator()(const VarStmtPtr &varStmt);
        void operator()(const WhileStmtPtr &whileStmt);
        void operator()(const IfStmtPtr &ifStmt);
        void operator()(const ClassStmtPtr &classStmt) const;

        // Expression code generation.
        Value *evaluate(const Expr &expr);
        Value *operator()(const AssignExprPtr &assignExpr);
        Value *operator()(const BinaryExprPtr &binaryExpr);
        Value *operator()(const CallExprPtr &callExpr) const;
        Value *operator()(const GetExprPtr &getExpr) const;
        Value *operator()(const SetExprPtr &setExpr) const;
        Value *operator()(const ThisExprPtr &thisExpr) const;
        Value *operator()(const SuperExprPtr &superExpr) const;
        Value *operator()(const VarExprPtr &varExpr) const;
        Value *operator()(const GroupingExprPtr &groupingExpr);
        Value *operator()(const LiteralExprPtr &literalExpr);
        Value *operator()(const LogicalExprPtr &logicalExpr);
        Value *operator()(const UnaryExprPtr &unaryExpr);

        void beginScope() {
            scopes.emplace(variables);
        }

        void endScope() {
            scopes.pop();
        }

        bool writeIR(const std::string &Filename) const;
    };

}// namespace lox

#endif//COMPILER_H
