#ifndef LOXFUNCTIONCOMPILER_H
#define LOXFUNCTIONCOMPILER_H
#include "../AST.h"
#include "LoxBuilder.h"
#include "ModuleCompiler.h"

#include <iostream>
#include <llvm/ADT/ScopedHashTable.h>
#include <llvm/IR/Value.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <stack>

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
    using namespace llvm;

    class FunctionCompiler {

        using ScopedHTType = ScopedHashTable<std::string_view, AllocaInst *>;
        ScopedHTType variables;
        std::stack<ScopedHTType::ScopeTy> scopes;
        LoxBuilder Builder;
        FunctionCompiler const *enclosing;

    public:
        explicit FunctionCompiler(LLVMContext &Context, LoxModule &Module, Function &F, FunctionCompiler const *enclosing = nullptr)
            : Builder{Context, Module, F}, enclosing(enclosing) {
        }

        // Statement code generation.
        void compile(const std::vector<Stmt> &statements, const std::vector<Token> &parameters = {}, const std::function<void(LoxBuilder &)> &entryBlockBuilder = nullptr);
        void evaluate(const Stmt &stmt);
        void operator()(const BlockStmtPtr &blockStmt);
        void operator()(const FunctionStmtPtr &functionStmt);
        void operator()(const ExpressionStmtPtr &expressionStmt);
        void operator()(const PrintStmtPtr &printStmt);
        void operator()(const ReturnStmtPtr &returnStmt);
        void operator()(const VarStmtPtr &varStmt);
        void operator()(const WhileStmtPtr &whileStmt);
        void operator()(const IfStmtPtr &ifStmt);
        void operator()(const ClassStmtPtr &classStmt) const;

        // Expression code generation.
        Value *evaluate(const Expr &expr);
        Value *operator()(const AssignExprPtr &assignExpr);
        Value *operator()(const BinaryExprPtr &binaryExpr);
        Value *operator()(const CallExprPtr &callExpr);
        Value *operator()(const GetExprPtr &getExpr) const;
        Value *operator()(const SetExprPtr &setExpr) const;
        Value *operator()(const ThisExprPtr &thisExpr) const;
        Value *operator()(const SuperExprPtr &superExpr) const;
        Value *operator()(const VarExprPtr &varExpr);
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

        [[nodiscard]] Value *lookupVariable(const std::string_view &name) {
            if (const auto local = variables.lookup(name)) return local;

            auto global = Builder.getModule().getNamedGlobal(("g" + name).str());
            if (!global) {
                // Global was not yet defined, so define it already but with an unitialized value.
                global = cast<GlobalVariable>(Builder.getModule().getOrInsertGlobal(
                    ("g" + name).str(),
                    IntegerType::getInt64Ty(Builder.getContext())
                ));

                global->setLinkage(GlobalValue::PrivateLinkage);
                global->setAlignment(Align(8));
                global->setConstant(false);
                global->setInitializer(cast<ConstantInt>(Builder.getUninitializedVal()));
            }

            return global;
        }

        [[nodiscard]] Value *lookupVariable(const Assignable &assignable) {
            return lookupVariable(assignable.name.getLexeme());
        }

        void insertVariable(const std::string_view &key, Value *value) {
            if (enclosing == nullptr && scopes.size() == 1) {
                const auto name = ("g" + key).str();// TODO: how to not call Twine.+?
                const auto global = cast<GlobalVariable>(Builder.getModule().getOrInsertGlobal(
                    name,
                    Builder.getInt64Ty()
                ));

                global->setLinkage(GlobalValue::PrivateLinkage);
                global->setAlignment(Align(8));
                global->setConstant(false);

                if (const auto i = dyn_cast<ConstantInt>(value); i && i->getBitWidth() == 64) {
                    global->setInitializer(i);
                } else {
                    global->setInitializer(cast<ConstantInt>(Builder.getNilVal()));
                    Builder.CreateStore(value, global);
                }
            } else {
                const auto alloca = CreateEntryBlockAlloca(Builder.getFunction(), Builder.getInt64Ty(), key);
                Builder.CreateStore(value, alloca);
                variables.insert(key, alloca);
            }
        }
    };

}// namespace lox

#endif//LOXFUNCTIONCOMPILER_H
