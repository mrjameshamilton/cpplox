#ifndef LOXFUNCTIONCOMPILER_H
#define LOXFUNCTIONCOMPILER_H
#include "../Debug.h"
#include "../frontend/AST.h"
#include "GC.h"
#include "LoxBuilder.h"
#include "Memory.h"
#include "ModuleCompiler.h"
#include "Stack.h"
#include "Upvalue.h"

#include "llvm/IR/CFG.h"

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

        struct Local {
            FunctionCompiler &compiler;
            std::string_view name;
            Value *value;
            bool isCaptured = false;
            unsigned int index;
            Local(FunctionCompiler &compiler, const std::string_view name, Value *value, const bool isCaptured = false) : compiler{compiler}, name{name}, value{value}, isCaptured{isCaptured} {
                auto &B = compiler.Builder;
                B.CreateLifetimeStart(value, B.getInt64(64));
                index = compiler.localsCount++;
                if constexpr (DEBUG_STACK) {
                    auto *const stackOffset = B.CreateAdd(
                        B.CreateLoad(B.getInt32Ty(), compiler.sp),
                        B.getInt32(index),
                        "stackOffset",
                        true,
                        true
                    );
                    B.PrintF({B.CreateGlobalCachedString("create local %d at %d %p\n"), B.getInt32(index), stackOffset, value});
                }
            }
            ~Local() {
                auto &B = compiler.Builder;
                if (isCaptured) {
                    // When a captured local goes out of scope,
                    // the upvalues must be closed, since the local
                    // will not be available any longer.
                    if constexpr (DEBUG_UPVALUES) {
                        B.PrintF({B.CreateGlobalCachedString(("closing upvalues for " + name + " (%p)\n").str()), value});
                    }
                    closeUpvalues(B, value);
                }

                B.CreateLifetimeEnd(value, B.getInt64(64));

                const auto &locals = B.getModule().getLocalsStack();
                auto *const stackIndex = B.CreateAdd(
                    B.CreateLoad(B.getInt32Ty(), compiler.sp),
                    B.getInt32(index),
                    "stackIndex",
                    true,
                    true
                );
                if constexpr (DEBUG_STACK) {
                    B.PrintF({B.CreateGlobalCachedString("end local %d at %d %p sp: %d c: %d\n"), B.getInt32(index), stackIndex, value, B.CreateLoad(B.getInt32Ty(), compiler.sp), locals.CreateGetCount(B)});
                }

                // At the end of the scope, clear the entry in the locals stack,
                // so that it's no longer reachable by the GC.
                locals.CreateSet(B, stackIndex, B.getNullPtr());
            }
        };

        using ScopedHTType = ScopedHashTable<std::string_view, std::shared_ptr<Local>>;
        ScopedHTType variables;
        std::stack<ScopedHTType::ScopeTy> scopes;
        LoxBuilder Builder;
        FunctionCompiler *enclosing;
        std::vector<std::unique_ptr<Upvalue>> upvalues;
        LoxFunctionType type;
        BasicBlock *EntryBasicBlock = Builder.CreateBasicBlock("entry");
        BasicBlock *ExitBasicBlock = Builder.CreateBasicBlock("epilogue");
        AllocaInst *sp;
        unsigned int localsCount = 0;

    public:
        explicit FunctionCompiler(LLVMContext &Context, LoxModule &Module, Function &F, const LoxFunctionType type = LoxFunctionType::FUNCTION, FunctionCompiler *enclosing = nullptr)
            : Builder{Context, Module, F}, enclosing(enclosing), type{type} {
            Builder.SetInsertPoint(EntryBasicBlock);
            sp = CreateEntryBlockAlloca(Builder.getFunction(), Builder.getInt32Ty(), "$sp");
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
        void operator()(const ClassStmtPtr &classStmt);

        // Expression code generation.
        Value *evaluate(const Expr &expr);
        Value *operator()(const AssignExprPtr &assignExpr);
        Value *operator()(const BinaryExprPtr &binaryExpr);
        Value *operator()(const CallExprPtr &callExpr);
        Value *call(Value *receiver, Value *closure, std::vector<Value *> paramValues, unsigned int line);
        Value *operator()(const GetExprPtr &getExpr);
        Value *operator()(const SetExprPtr &setExpr);
        Value *operator()(const ThisExprPtr &thisExpr);
        Value *operator()(const SuperExprPtr &superExpr);
        Value *operator()(const VarExprPtr &varExpr);
        Value *operator()(const GroupingExprPtr &groupingExpr);
        Value *operator()(const LiteralExprPtr &literalExpr);
        Value *operator()(const LogicalExprPtr &logicalExpr);
        Value *operator()(const UnaryExprPtr &unaryExpr);


        void beginScope() {
            scopes.emplace(variables);
        }

        void endScope() {
            auto *const CurrentBlock = Builder.GetInsertBlock();
            const bool isEarlyReturn = predecessors(Builder.GetInsertBlock()).empty();
            if (isEarlyReturn) {
                // If the scope is closed when there is an early return,
                // then the code generated for closing upvalues would otherwise
                // end up in the "unreachable" block.
                // See Lox tests `test/while/return_closure.lox` and `test/for/return_closure.lox`.
                // For debugging: enable runtime asserts, to check that all upvalues are closed at
                //                at the end of the lox script.
                /*
                fun f() {
                    while (true) {
                      var i = "i";
                      fun g() { print i; }
                      return g;
                      // exit:
                      //    need to close upvalues here.
                      // unreachable:
                    } //    endscope
                  }

                  var h = f();
                  h(); // expect: i
                */

                Builder.SetInsertPoint(ExitBasicBlock);
            }

            scopes.pop();

            if (isEarlyReturn) {
                // Any further code can go in the unreachable block.
                Builder.SetInsertPoint(CurrentBlock);
            }
        }

        [[nodiscard]] FunctionCompiler *getEnclosing() const {
            return enclosing;
        }

        LoxBuilder &getBuilder() {
            return Builder;
        }

        [[nodiscard]] Value *lookupVariable(Assignable &assignable) {
            if constexpr (DEBUG_UPVALUES) {
                Builder.PrintF({Builder.CreateGlobalCachedString("lookupVariable(%s)\n"), Builder.CreateGlobalCachedString(assignable.name.getLexeme())});
            }
            if (const auto local = resolveLocal(this, assignable)) return local->value;

            if (auto *const upvalue = resolveUpvalue(this, assignable)) {
                // upvalue is a pointer to an upvalue object.
                // We need to load the value at the pointer location in the upvalue struct,
                // which points to the closed over value.

                return Builder.CreateLoad(
                    Builder.getPtrTy(),
                    Builder.CreateObjStructGEP(ObjType::UPVALUE, upvalue, 1, "upvalue.locationptr"),
                    "upvalue.valueptr"
                );
            }

            // Lookup global.
            auto *const global = lookupGlobal(assignable.name.getLexeme());

            if (auto const *c = dyn_cast<ConstantInt>(global->getInitializer()); c->getValue() == UNINITIALIZED_VAL) {
                // Globals are late bound, so we must check at runtime if
                // a global created as uninitialized is now initialized.
                auto *const UndefinedBlock = Builder.CreateBasicBlock("undefined");
                auto *const EndBlock = Builder.CreateBasicBlock("end");

                auto *const loadedValue = Builder.CreateLoad(Builder.getInt64Ty(), global);
                Builder.CreateCondBr(Builder.IsUninitialized(loadedValue), UndefinedBlock, EndBlock);
                Builder.SetInsertPoint(UndefinedBlock);
                Builder.RuntimeError(
                    assignable.name.getLine(),
                    "Undefined variable '%s'.\n",
                    {Builder.CreateGlobalCachedString(assignable.name.getLexeme())},
                    Builder.getFunction()
                );

                Builder.SetInsertPoint(EndBlock);
            }

            return global;
        }

        GlobalVariable *lookupGlobal(const std::string_view name) {
            auto *global = Builder.getModule().getNamedGlobal(("g" + name).str());
            if (global == nullptr) {
                // Global was not yet defined, so define it already but with an uninitialized value.
                global = cast<GlobalVariable>(Builder.getModule().getOrInsertGlobal(
                    ("g" + name).str(),
                    IntegerType::getInt64Ty(Builder.getContext())
                ));

                global->setLinkage(GlobalValue::PrivateLinkage);
                global->setAlignment(Align(8));
                global->setConstant(false);
                global->setInitializer(cast<ConstantInt>(Builder.getUninitializedVal()));

                AddGlobalGCRoot(Builder.getModule(), global);
            }

            assert(global->getValueType() == Builder.getInt64Ty());

            return global;
        }

        void insertVariable(const std::string_view key, Value *value) {
            assert(value->getType() == Builder.getInt64Ty());
            if (isGlobalScope()) {
                const auto &name = ("g" + key).str();// TODO: how to not call Twine.+?
                auto *const global = cast<GlobalVariable>(Builder.getModule().getOrInsertGlobal(
                    name,
                    Builder.getInt64Ty()
                ));

                global->setLinkage(GlobalValue::PrivateLinkage);
                global->setAlignment(Align(8));
                global->setConstant(false);

                if (auto *const i = dyn_cast<ConstantInt>(value); i && i->getBitWidth() == 64) {
                    global->setInitializer(i);
                } else {
                    global->setInitializer(cast<ConstantInt>(Builder.getNilVal()));
                    Builder.CreateStore(value, global);
                }

                AddGlobalGCRoot(Builder.getModule(), global);
            } else {
                auto *const alloca = CreateEntryBlockAlloca(Builder.getFunction(), Builder.getInt64Ty(), key);
                const auto local = std::make_shared<Local>(*this, key, alloca);
                variables.insert(key, local);
                Builder.CreateStore(value, alloca);

                const auto locals = Builder.getModule().getLocalsStack();
                auto *const stackIndex = Builder.CreateAdd(
                    Builder.CreateLoad(Builder.getInt32Ty(), sp),
                    Builder.getInt32(local->index),
                    "stackIndex",
                    true,
                    true
                );
                locals.CreateSet(Builder, stackIndex, alloca);
            }
        }

        Value *insertTemp(Value *value, const std::string_view what) {
            assert(value->getType() == Builder.getInt64Ty());

            const auto *const name = "$temp";
            auto *const alloca = CreateEntryBlockAlloca(Builder.getFunction(), Builder.getInt64Ty(), name);

            const auto local = std::make_shared<Local>(*this, name, alloca);
            variables.insert(name, local);
            Builder.CreateStore(value, alloca);

            const auto locals = Builder.getModule().getLocalsStack();
            auto *const stackIndex = Builder.CreateAdd(
                Builder.CreateLoad(Builder.getInt32Ty(), sp),
                Builder.getInt32(local->index),
                "stackIndex",
                true,
                true
            );
            locals.CreateSet(Builder, stackIndex, alloca);

            return value;
        }

        Value *captureLocal(Value *local);

    private:
        static Value *resolveUpvalue(FunctionCompiler *compiler, Assignable &assignable) {
            if (compiler->enclosing == nullptr) return nullptr;

            if (const auto local = resolveLocal(compiler->enclosing, assignable)) {
                local->isCaptured = true;
                return addUpvalue(compiler, local->value, true);
            }

            if (auto *const upvalue = resolveUpvalue(compiler->enclosing, assignable)) {
                return addUpvalue(compiler, upvalue, false);
            }

            return nullptr;
        }

        static Value *addUpvalue(FunctionCompiler *compiler, Value *value, const bool isLocal) {
            auto &Builder = compiler->Builder;

            const auto result = std::ranges::find_if(compiler->upvalues, [&value, &isLocal](auto &entry) {
                return entry->value == value && entry->isLocal == isLocal;
            });

            unsigned long upvalueArrayIndex;
            if (result != compiler->upvalues.end()) {
                upvalueArrayIndex = (*result)->index;
            } else {
                upvalueArrayIndex = compiler->upvalues.size();
                compiler->upvalues.emplace_back(std::make_unique<Upvalue>(upvalueArrayIndex, value, isLocal));
            }

            // Construct instruction sequence to load an upvalue from
            // the upvalue array which is the function's first argument,
            // in the *compiler*'s function.
            auto *const upvalues = Builder.getFunction()->arg_begin();
            auto *const upvalueIndex = Builder.CreateInBoundsGEP(Builder.getPtrTy(), upvalues, {Builder.getInt32(upvalueArrayIndex)}, "arrayindex");
            auto *const upvaluePtr = Builder.CreateLoad(Builder.getPtrTy(), upvalueIndex, "upvaluePtr");

            if constexpr (DEBUG_UPVALUES) {
                Builder.PrintF({Builder.CreateGlobalCachedString("addUpValue(%p, %d, %p, %p)\n"), upvalues, Builder.getInt32(upvalueArrayIndex), upvalueIndex, upvaluePtr});
            }

            // The pointer from the upvalues array for new upvalue index.
            return upvaluePtr;
        }

        static std::shared_ptr<Local> resolveLocal(FunctionCompiler *compiler, const Assignable &assignable) {
            const std::string_view &name = assignable.name.getLexeme();
            if (const auto local = compiler->variables.lookup(name)) {

                if constexpr (DEBUG_UPVALUES) {
                    auto &Builder = compiler->Builder;
                    Builder.PrintF({Builder.CreateGlobalCachedString("resolveLocal(%s) = %p = "), Builder.CreateGlobalCachedString(name), local->value});
                    Builder.Print(Builder.ObjVal(Builder.CreateLoad(Builder.getPtrTy(), local->value)));
                }

                return local;
            }
            return nullptr;
        }

        [[nodiscard]] bool isGlobalScope() const {
            return enclosing == nullptr &&
                   // 2 scopes are started in FunctionCompiler.cpp for a function:
                   // * outer scope for the returnVal, this value.
                   // * second scope for the implementation of the method.
                   scopes.size() <= 2;
        }

        Value *CreateFunction(LoxFunctionType type, const FunctionStmtPtr &functionStmt, std::string_view name);
    };

}// namespace lox

#endif//LOXFUNCTIONCOMPILER_H
