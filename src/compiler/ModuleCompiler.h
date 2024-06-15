#ifndef COMPILER_H
#define COMPILER_H

#include "../frontend/AST.h"
#include "LoxBuilder.h"
#include "LoxModule.h"

#include <llvm/IR/Function.h>
#include <llvm/Passes/PassBuilder.h>

using namespace llvm;
using namespace llvm::sys;

namespace lox {

    class ModuleCompiler {
        std::shared_ptr<LLVMContext> Context = std::make_shared<LLVMContext>();
        std::unique_ptr<LoxModule> M = std::make_unique<LoxModule>(*Context);
        std::unique_ptr<LoxBuilder> Builder;
        mutable TargetMachine * TheTargetMachine{};

    public:
        ModuleCompiler() {
            Function *MainFunction = Function::Create(FunctionType::get(IntegerType::getInt32Ty(*Context), false), Function::ExternalLinkage, "main", *M);
            Builder = std::make_unique<LoxBuilder>(*Context, *M, *MainFunction);
        }

        [[nodiscard]] LoxModule &getModule() const {
            return *M;
        }

        [[nodiscard]] LLVMContext &getContext() const {
            return *Context;
        }

        bool initializeTarget() const;
        void evaluate(const Program &program) const;
        bool optimize() const;
        [[nodiscard]] bool writeIR(std::string_view Filename) const;
        [[nodiscard]] bool writeObject(std::string_view Filename) const;
    };

}// namespace lox

#endif//COMPILER_H
