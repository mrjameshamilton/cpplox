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

    struct ModuleCompiler {
        std::shared_ptr<LLVMContext> Context = std::make_shared<LLVMContext>();
        std::shared_ptr<LoxModule> M = std::make_shared<LoxModule>(*Context);
        Function *MainFunction =
            Function::Create(FunctionType::get(IntegerType::getInt32Ty(*Context), false), Function::ExternalLinkage, "main", *M);
        std::unique_ptr<LoxBuilder> Builder = std::make_unique<LoxBuilder>(*Context, *M, *MainFunction);

        ModuleCompiler() = default;

        void evaluate(const Program &program) const;
        void optimize() const;
        bool writeIR(std::string_view Filename) const;
        bool writeObject(std::string_view Filename) const;
    };

}// namespace lox

#endif//COMPILER_H
