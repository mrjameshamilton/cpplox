#include "FunctionCompiler.h"
#include "GC.h"
#include "MDUtil.h"

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

    Value *FunctionCompiler::CreateFunction(const LoxFunctionType type, const FunctionStmtPtr &functionStmt, const std::string_view name) {
        std::vector<Type *> paramTypes(functionStmt->parameters.size(), Builder.getInt64Ty());
        // The second parameter is for the receiver instance for methods
        // or the function object value itself for functions.
        paramTypes.insert(paramTypes.begin(), Builder.getInt64Ty());
        // The first parameter is the upvalues.
        paramTypes.insert(paramTypes.begin(), Builder.getPtrTy());
        auto *const FT = FunctionType::get(IntegerType::getInt64Ty(Builder.getContext()), paramTypes, false);

        auto *const F = Function::Create(
            FT,
            Function::InternalLinkage,
            name,
            Builder.getModule()
        );

        if (type == LoxFunctionType::INITIALIZER) {
            // Initializers always return their instance which is the second parameter.
            F->addParamAttr(1, Attribute::Returned);
        }

        auto *const closurePtr = Builder.AllocateClosure(*this, F, functionStmt->name.getLexeme(), false);

        if (type == LoxFunctionType::FUNCTION) {
            auto *const variable = insertVariable(functionStmt->name.getLexeme(), Builder.ObjVal(closurePtr), !isGlobalScope());
            auto *nameNode = MDString::get(Builder.getContext(), name);
            auto *arityNode = ValueAsMetadata::get(Builder.getInt32(functionStmt->parameters.size()));
            setMetadata(variable, "lox-function", MDTuple::get(Builder.getContext(), {nameNode, arityNode, nameNode}));
        } else if (type == LoxFunctionType::METHOD || type == LoxFunctionType::INITIALIZER) {
            // Methods aren't stored as variables.
            insertTemp(Builder.ObjVal(closurePtr), "method closure");
        }

        FunctionCompiler C(Builder.getContext(), Builder.getModule(), *F, type, this);
        C.compile(functionStmt->body, functionStmt->parameters, [&, &C](LoxBuilder &B) {
            if (C.type == LoxFunctionType::METHOD || C.type == LoxFunctionType::INITIALIZER) {
                C.insertVariable("this", B.getFunction()->arg_begin() + 1, true);
            } else if (C.type == LoxFunctionType::FUNCTION) {
                // For functions, use the 2nd parameter for the function itself.
                // This improves recursive calling performance since there is no
                // need for an upvalue any longer.
                auto *const variable = C.insertVariable(name, B.getFunction()->arg_begin() + 1);
                auto *nameNode = MDString::get(Builder.getContext(), name);
                auto *arityNode = ValueAsMetadata::get(Builder.getInt32(functionStmt->parameters.size()));
                setMetadata(variable, "lox-function", MDTuple::get(Builder.getContext(), {nameNode, arityNode, nameNode}));
            }
        });

        // Store captured variables in the closure's upvalue array.
        if (!C.upvalues.empty()) {
            F->addParamAttr(0, Attribute::NonNull);
            F->addParamAttr(0, Attribute::ReadOnly);
            F->addParamAttr(0, Attribute::NoUndef);

            if constexpr (DEBUG_UPVALUES) {
                Builder.PrintF({Builder.CreateGlobalCachedString("capture variables\n")});
            }

            auto *const upvaluesArraySize = Builder.getSizeOf(Builder.getModule().getStructType(ObjType::UPVALUE), C.upvalues.size());

            auto *const upvaluesArrayPtr = Builder.CreateReallocate(
                Builder.getNullPtr(),
                Builder.getInt32(0),
                Builder.CreateTrunc(upvaluesArraySize, Builder.getInt32Ty())
            );

            // Initialize upvalues to nullptr.
            for (const auto &upvalue: C.upvalues) {
                auto *const upvalueIndex = Builder.CreateInBoundsGEP(Builder.getPtrTy(), upvaluesArrayPtr, Builder.getInt32(upvalue->index), "upvalueIndex");
                Builder.CreateStore(Builder.getNullPtr(), upvalueIndex);
            }

            Builder.CreateStore(
                upvaluesArrayPtr,
                Builder.CreateStructGEP(Builder.getModule().getStructType(ObjType::CLOSURE), closurePtr, 2, "closure.upvalues")
            );
            Builder.CreateStore(
                Builder.getInt32(C.upvalues.size()),
                Builder.CreateStructGEP(Builder.getModule().getStructType(ObjType::CLOSURE), closurePtr, 3, "closure.upvaluesCount")
            );
            for (const auto &upvalue: C.upvalues) {
                auto *const upvalueIndex = Builder.CreateInBoundsGEP(Builder.getPtrTy(), upvaluesArrayPtr, Builder.getInt32(upvalue->index), "upvalueIndex");
                Builder.CreateStore(upvalue->isLocal ? captureLocal(upvalue->value) : upvalue->value, upvalueIndex);
            }

            Builder.CreateInvariantStart(upvaluesArrayPtr, upvaluesArraySize);

        } else {
            F->addParamAttr(0, Attribute::ReadNone);
        }

        Builder.CreateInvariantStart(closurePtr, Builder.getSizeOf(ObjType::CLOSURE));

        return closurePtr;
    }

    void FunctionCompiler::operator()(const FunctionStmtPtr &functionStmt) {
        CreateFunction(LoxFunctionType::FUNCTION, functionStmt, functionStmt->name.getLexeme());
    }

    void FunctionCompiler::operator()(const ExpressionStmtPtr &expressionStmt) {
        evaluate(expressionStmt->expression);
    }

    void FunctionCompiler::operator()(const PrintStmtPtr &printStmt) {
        Builder.Print(evaluate(printStmt->expression));
    }

    void FunctionCompiler::operator()(const ReturnStmtPtr &returnStmt) {
        if (returnStmt->expression.has_value()) {
            auto *const returnVal = variables.lookup("$returnVal")->value;
            Builder.CreateStore(evaluate(returnStmt->expression.value()), returnVal);
        }
        Builder.CreateBr(ExitBasicBlock);

        // Create somewhere to generate code that appears after a return e.g.
        // fun foo() {
        //    print "foo";
        //    return;
        //    print "bar";
        // }
        Builder.SetInsertPoint(Builder.CreateBasicBlock("unreachable"));
    }

    void FunctionCompiler::operator()(const VarStmtPtr &varStmt) {
        if (isGlobalScope()) {
            // Global variables can be re-declared.
            if (auto *const variable = lookupGlobal(varStmt->name.getLexeme())) {
                Builder.CreateStore(evaluate(varStmt->initializer), variable);
                return;
            }
        }

        insertVariable(varStmt->name.getLexeme(), evaluate(varStmt->initializer));
    }

    void FunctionCompiler::operator()(const WhileStmtPtr &whileStmt) {
        auto *const Cond = Builder.CreateBasicBlock("Cond");
        auto *const Body = Builder.CreateBasicBlock("Loop");
        auto *const Exit = Builder.CreateBasicBlock("Exit");

        Builder.CreateBr(Cond);
        Builder.SetInsertPoint(Cond);
        Builder.CreateCondBr(Builder.IsTruthy(evaluate(whileStmt->condition)), Body, Exit);
        Builder.SetInsertPoint(Body);
        evaluate(whileStmt->body);
        Builder.CreateBr(Cond);
        Builder.SetInsertPoint(Exit);
    }

    void FunctionCompiler::operator()(const IfStmtPtr &ifStmt) {
        auto *const TrueBlock = Builder.CreateBasicBlock("if.true");
        auto *const EndBlock = Builder.CreateBasicBlock("if.end");
        auto *const FalseBlock = ifStmt->elseBranch.has_value() ? Builder.CreateBasicBlock("else") : EndBlock;
        Builder.CreateCondBr(Builder.IsTruthy(evaluate(ifStmt->condition)), TrueBlock, FalseBlock);
        Builder.SetInsertPoint(TrueBlock);
        evaluate(ifStmt->thenBranch);
        Builder.CreateBr(EndBlock);
        if (ifStmt->elseBranch.has_value()) {
            Builder.SetInsertPoint(FalseBlock);
            evaluate(ifStmt->elseBranch.value());
            Builder.CreateBr(EndBlock);
        }
        Builder.SetInsertPoint(EndBlock);
    }

    void FunctionCompiler::operator()(const ClassStmtPtr &classStmt) {
        const auto className = classStmt->name.getLexeme();
        auto *const nameObj = Builder.AllocateString(className, ("class_" + className).str());
        insertTemp(Builder.ObjVal(nameObj), "class name");
        auto *const klass = Builder.AllocateClass(nameObj);
        auto *const methods = Builder.CreateLoad(Builder.getPtrTy(), Builder.CreateObjStructGEP(ObjType::CLASS, klass, 2));

        insertVariable(className, Builder.ObjVal(klass), !isGlobalScope());

        if (classStmt->super_class.has_value()) {
            // Copy all methods from the superclass methods table, to the subclass
            // to support inheritance. Do this before adding methods to the sub-class
            // to support overloaded methods.

            auto *const value = Builder.CreateLoad(Builder.getInt64Ty(), lookupVariable(*classStmt->super_class.value()));
            auto *const IsClassBlock = Builder.CreateBasicBlock("superclass.valid");
            auto *const IsNotClassBlock = Builder.CreateBasicBlock("superclass.invalid");
            auto *const EndBlock = Builder.CreateBasicBlock("superclass.end");

            Builder.CreateCondBr(Builder.IsClass(value), IsClassBlock, IsNotClassBlock);
            Builder.SetInsertPoint(IsClassBlock);

            beginScope();// Create a new scope since the "super" variable
                         // could be declared multiple times, for different classes.
            insertVariable("super", value, true);

            auto *const superklass = Builder.AsObj(value);
            auto *const supermethods = Builder.CreateLoad(Builder.getPtrTy(), Builder.CreateObjStructGEP(ObjType::CLASS, superklass, 2));
            Builder.TableAddAll(supermethods, methods);
            Builder.CreateBr(EndBlock);

            Builder.SetInsertPoint(IsNotClassBlock);
            Builder.RuntimeError(
                classStmt->super_class->get()->name.getLine(),
                "Superclass must be a class.\n",
                {},
                Builder.getFunction()
            );

            Builder.SetInsertPoint(EndBlock);
        }

        for (auto &method: classStmt->methods) {
            auto *const name = Builder.AllocateString(method->name.getLexeme());
            insertTemp(Builder.ObjVal(name), "method name");
            auto *const methodPtr =
                CreateFunction(
                    method->type,
                    method,
                    (className + "." + method->name.getLexeme()).str()
                );
            Builder.TableSet(methods, name, Builder.ObjVal(methodPtr));
        }

        if (classStmt->super_class.has_value()) {
            endScope();
        }
    }
}// namespace lox