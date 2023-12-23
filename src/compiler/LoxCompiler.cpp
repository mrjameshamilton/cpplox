#include "LoxCompiler.h"
#include "../AST.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Value.h>
#include <llvm/Passes/PassBuilder.h>

using namespace llvm;
using namespace llvm::sys;

namespace lox {

    void LoxCompiler::evaluate(const Program &program) {
        LoxModule->getOrInsertGlobal("objects", Builder->getPtrTy());
        const auto global = LoxModule->getNamedGlobal("objects");
        global->setLinkage(GlobalValue::PrivateLinkage);
        global->setAlignment(Align(8));
        global->setConstant(false);
        global->setInitializer(ConstantPointerNull::get(Builder->getObjStructType()->getPointerTo()));

        beginScope();
        const auto EntryBasicBlock = BasicBlock::Create(Builder->getContext(), "entry", Builder->getFunction());
        Builder->SetInsertPoint(EntryBasicBlock);
        for (auto &stmt: program) {
            evaluate(stmt);
        }

        FreeObjects();

        Builder->CreateRet(Builder->getInt32(0));
        endScope();
    }

    bool LoxCompiler::writeIR(const std::string &Filename) const {
        std::error_code ec;
        auto out = raw_fd_ostream(Filename, ec);
        LoxModule->print(out, nullptr);
        out.close();
        return ec.value() == 0;
    }
}// namespace lox
