#ifndef COMPILER_H
#define COMPILER_H
#include "../AST.h"
#include <llvm/ADT/DenseMapInfo.h>
#include <llvm/ADT/ScopedHashTable.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/NoFolder.h>
#include <llvm/Passes/PassBuilder.h>
#include <ranges>
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
    static AllocaInst *CreateEntryBlockAlloca(Function *TheFunction, Type *type, const std::string_view &VarName) {
        IRBuilder TmpB(&TheFunction->getEntryBlock(), TheFunction->getEntryBlock().begin());
        return TmpB.CreateAlloca(type, nullptr, VarName);
    }

    struct Compiler {
        std::unique_ptr<LLVMContext> Context = std::make_unique<LLVMContext>();
        std::unique_ptr<Module> LoxModule = std::make_unique<Module>("lox", *Context);
        std::unique_ptr<IRBuilder<NoFolder>> Builder = std::make_unique<IRBuilder<NoFolder>>(*Context);
        Function *MainFunction =
            Function::Create(FunctionType::get(Builder->getInt32Ty(), false), Function::ExternalLinkage, "main", *LoxModule);
        using ScopedHTType = ScopedHashTable<std::string_view, Value *>;
        ScopedHTType variables;
        std::stack<ScopedHTType::ScopeTy> scopes;
        std::unordered_map<std::string_view, Value *> strings;
        StructType *ObjStructType = StructType::create(*Context, {Builder->getInt8Ty()}, "Obj");
        StructType *StringStructType = StructType::create(*Context, {ObjStructType, Builder->getInt8PtrTy(), Builder->getInt32Ty()}, "String");

        Compiler() = default;

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


        // Statement code generation.
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

        void evaluate(const Program &program);
        Value *evaluate(const Expr &expr) {
            return std::visit(*this, expr);
        }
        void evaluate(const Stmt &stmt) {
            std::visit(*this, stmt);
        }
        bool writeIR(const std::string &Filename) const {
            std::error_code ec;
            auto out = raw_fd_ostream(Filename, ec);
            LoxModule->print(out, nullptr);
            out.close();
            return ec.value() == 0;
        }

        void DebugPrint(const std::string &stringFormat, Value *value) const {
            const auto fmt = Builder->CreateGlobalStringPtr(stringFormat);
            static const auto PrintF = LoxModule->getOrInsertFunction(
                "printf",
                FunctionType::get(Builder->getInt8Ty(), {Type::getInt8PtrTy(*Context)}, true)
            );
            Builder->CreateCall(PrintF, {fmt, value});
        }

        void DebugPrint(const std::string &string) const {
            DebugPrint("%s\n", Builder->CreateGlobalStringPtr(string));
        }
    };
}// namespace lox

#endif//COMPILER_H
