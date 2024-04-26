#ifndef LOXFUNCTIONCOMPILER_H
#define LOXFUNCTIONCOMPILER_H
#include "../frontend/AST.h"
#include "LoxBuilder.h"
#include "Memory.h"
#include "ModuleCompiler.h"
#include "Stack.h"
#include "Upvalue.h"

#include "llvm/IR/CFG.h"
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
            Local(FunctionCompiler &compiler, const std::string_view name, Value *value, const bool isCaptured = false) : compiler{compiler}, name{name}, value{value}, isCaptured{isCaptured} {
                compiler.Builder.CreateLifetimeStart(value);
            }
            ~Local() {
                if (isCaptured) {
                    // When a captured local goes out of scope,
                    // the upvalues must be closed, since the local
                    // will not be available any longer.
                    if constexpr (DEBUG_UPVALUES) {
                        compiler.Builder.PrintF({compiler.Builder.CreateGlobalCachedString(("closing upvalues for " + name + " (%p)\n").str()), value});
                    }
                    closeUpvalues(compiler.Builder, value);
                }
                compiler.Builder.CreateLifetimeEnd(value);
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
        AllocaInst *returnVal;
        AllocaInst *sp;

    public:
        explicit FunctionCompiler(LLVMContext &Context, LoxModule &Module, Function &F, const LoxFunctionType type = LoxFunctionType::FUNCTION, FunctionCompiler *enclosing = nullptr)
            : Builder{Context, Module, F}, enclosing(enclosing), type{type} {
            Builder.SetInsertPoint(EntryBasicBlock);
            returnVal = CreateEntryBlockAlloca(Builder.getFunction(), Builder.getInt64Ty(), "$returnVal");
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
            // At the beginning of the scope, remember the current local variable stack pointer.
            //Builder.CreateStore(Builder.CreateLoad(Builder.getInt32Ty(), Builder.getModule().getLocalsStackPointer()), sp);
            Builder.CreateStore(Builder.getModule().getLocalsStack()->getCount(Builder), sp);
            scopes.emplace(variables);
        }

        void endScope() {
            const auto CurrentBlock = Builder.GetInsertBlock();
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
            // At the end of the scope, reset the stack pointer then any variables allocated
            // in the scope are no longer accessible as GC roots and can be freed.
            Builder.getModule().getLocalsStack()->setCount(Builder, Builder.CreateLoad(Builder.getInt32Ty(), sp));
            //Builder.CreateStore(Builder.CreateLoad(Builder.getInt32Ty(), sp), Builder.getModule().getLocalsStackPointer());

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

            if (const auto upvalue = resolveUpvalue(this, assignable)) {
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
            const auto global = lookupGlobal(assignable.name.getLexeme());
            // Globals are late bound, so we must check at runtime if
            // the global is defined and initialized.
            const auto UndefinedBlock = Builder.CreateBasicBlock("undefined");
            const auto EndBlock = Builder.CreateBasicBlock("end");

            const auto loadedValue = Builder.CreateLoad(Builder.getInt64Ty(), global);
            Builder.CreateCondBr(Builder.IsUninitialized(loadedValue), UndefinedBlock, EndBlock);
            Builder.SetInsertPoint(UndefinedBlock);
            Builder.RuntimeError(
                assignable.name.getLine(),
                "Undefined variable '%s'.\n",
                {Builder.CreateGlobalCachedString(assignable.name.getLexeme())},
                Builder.getFunction()
            );
            Builder.CreateBr(EndBlock);
            Builder.SetInsertPoint(EndBlock);

            return global;
        }

        GlobalVariable *lookupGlobal(const std::string_view &name) {
            auto global = Builder.getModule().getNamedGlobal(("g" + name).str());
            if (!global) {
                // Global was not yet defined, so define it already but with an uninitialized value.
                global = cast<GlobalVariable>(Builder.getModule().getOrInsertGlobal(
                    ("g" + name).str(),
                    IntegerType::getInt64Ty(Builder.getContext())
                ));

                global->setLinkage(GlobalValue::PrivateLinkage);
                global->setAlignment(Align(8));
                global->setConstant(false);
                global->setInitializer(cast<ConstantInt>(Builder.getUninitializedVal()));

                PushGlobal(Builder, global);
            }

            return global;
        }

        void insertVariable(const std::string_view &key, Value *value) {
            if (isGlobalScope()) {
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
                PushGlobal(Builder, global);
            } else {
                const auto alloca = CreateEntryBlockAlloca(Builder.getFunction(), Builder.getInt64Ty(), key);
                variables.insert(key, std::make_shared<Local>(*this, key, alloca));
                Builder.CreateStore(value, alloca);
                PushLocal(Builder, alloca, ("normal local {" + key + "}").str());
            }
        }

        Value *captureLocal(Value *local);

    private:
        static Value *resolveUpvalue(FunctionCompiler *compiler, Assignable &assignable) {
            if (compiler->enclosing == nullptr) return nullptr;

            if (const auto local = resolveLocal(compiler->enclosing, assignable)) {
                local->isCaptured = true;
                return addUpvalue(compiler, local->value, true);
            }

            if (const auto upvalue = resolveUpvalue(compiler->enclosing, assignable)) {
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
            const auto upvalues = Builder.getFunction()->arg_begin();
            const auto upvalueIndex = Builder.CreateGEP(Builder.getPtrTy(), upvalues, {Builder.getInt32(upvalueArrayIndex)}, "arrayindex");
            const auto upvaluePtr = Builder.CreateLoad(Builder.getPtrTy(), upvalueIndex, "upvaluePtr");

            if constexpr (DEBUG_UPVALUES) {
                Builder.PrintF({Builder.CreateGlobalCachedString("addUpValue(%d, %p, %p)\n"), Builder.getInt32(upvalueArrayIndex), upvalueIndex, upvaluePtr});
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
