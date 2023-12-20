#ifndef COMPILER_H
#define COMPILER_H

#include "../AST.h"
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
        std::unique_ptr<LLVMContext> Context = std::make_unique<LLVMContext>();
        std::unique_ptr<Module> LoxModule = std::make_unique<Module>("lox", *Context);
        std::unique_ptr<IRBuilder<NoFolder>> Builder = std::make_unique<IRBuilder<NoFolder>>(*Context);
        Function *MainFunction =
            Function::Create(FunctionType::get(Builder->getInt32Ty(), false), Function::ExternalLinkage, "main", *LoxModule);
        using ScopedHTType = ScopedHashTable<std::string_view, Value *>;
        ScopedHTType variables;
        std::stack<ScopedHTType::ScopeTy> scopes;
        std::unordered_map<std::string_view, Value *> strings;
        StructType *ObjStructType = StructType::create(
            *Context,
            {Builder->getInt8Ty(),// ObjType
             Builder->getInt1Ty(),// isMarked
             Builder->getPtrTy()},// next
            "Obj"
        );
        StructType *StringStructType = StructType::create(*Context, {ObjStructType, Builder->getInt8PtrTy(), Builder->getInt32Ty()}, "String");

        LoxCompiler() = default;

        // Code generation for checking types of values.
        Value *IsBool(Value *) const;
        Value *IsNil(Value *value) const;
        Value *IsNumber(Value *value) const;
        Value *IsObj(Value *value) const;
        Value *IsString(Value *value) const;

        // Code generation for converting an int64 to a Lox value.
        Value *BoolVal(Value *value) const;
        Value *ObjVal(Value *value) const;
        Value *NumberVal(Value *value) const;

        // Code generation for converting a Lox value to a native type.
        Value *AsBool(Value *value) const;
        Value *AsObj(Value *value) const;
        Value *AsString(Value *value) const;
        Value *AsCString(Value *value) const;
        Value *AsNumber(Value *value) const;

        // Code generation for getting the type of Lox object.
        Value *ObjType(Value *value) const;

        // Code generation for internal Lox functions.
        Value *IsTruthy(Value *value) const;
        Value *IsNotTruthy(Value *value) const;
        Value *StrEquals(Value *a, Value *b) const;
        Value *Concat(Value *a, Value *b) const;

        Value *AllocateObj(lox::ObjType objType, std::string_view name = "") const;
        Value *AllocateString(Value *String, Value *Length, std::string_view name = "") const;
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

        void PrintF(const std::string &stringFormat, Value *value) const;
        void PrintF(const std::initializer_list<Value *> value) const;
        void PrintString(const std::string &string) const;
        void PrintNumber(Value *value) const;
        void PrintNil() const;
        void PrintObject(Value *value) const;
        void PrintString(Value *value) const;
        void PrintBool(Value *value) const;

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
