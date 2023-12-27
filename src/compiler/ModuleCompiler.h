#ifndef COMPILER_H
#define COMPILER_H

#include "../AST.h"
#include "LoxBuilder.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/NoFolder.h>
#include <llvm/IR/Value.h>
#include <llvm/Passes/PassBuilder.h>

using namespace llvm;
using namespace llvm::sys;


namespace lox {

    AllocaInst *CreateEntryBlockAlloca(Function *TheFunction, Type *type, const std::string_view &VarName);

    struct ModuleCompiler {
        std::shared_ptr<LLVMContext> Context = std::make_shared<LLVMContext>();
        std::shared_ptr<Module> LoxModule = std::make_shared<Module>("lox", *Context);
        Function *MainFunction =
            Function::Create(FunctionType::get(IntegerType::getInt32Ty(*Context), false), Function::ExternalLinkage, "main", *LoxModule);
        std::unique_ptr<LoxBuilder> Builder = std::make_unique<LoxBuilder>(*Context, *LoxModule, *MainFunction);

        // TODO: use this.
        std::unordered_map<std::string_view, Value *> strings;


        ModuleCompiler() = default;

        void FreeObjects() const;
        void FreeObject(Value *value) const;

        void evaluate(const Program &program) const;

        bool writeIR(const std::string &Filename) const;
    };

}// namespace lox

#endif//COMPILER_H
