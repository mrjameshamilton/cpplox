#include "ModuleCompiler.h"

#include "../AST.h"
#include "FunctionCompiler.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Value.h>
#include <llvm/Passes/PassBuilder.h>

using namespace llvm;
using namespace llvm::sys;

namespace lox {

    void ModuleCompiler::evaluate(const Program &program) const {
        LoxModule->getOrInsertGlobal("objects", Builder->getPtrTy());
        const auto global = LoxModule->getNamedGlobal("objects");
        global->setLinkage(GlobalValue::PrivateLinkage);
        global->setAlignment(Align(8));
        global->setConstant(false);
        global->setInitializer(ConstantPointerNull::get(Builder->getObjStructType()->getPointerTo()));

        const auto selfType = IntegerType::getInt64Ty(*Context);
        Function *M = Function::Create(
            FunctionType::get(IntegerType::getInt64Ty(*Context), {selfType}, false),
            Function::InternalLinkage,
            "main",
            *LoxModule
        );
        LoxBuilder FBuilder(*Context, *LoxModule, *M);
        FunctionCompiler C(FBuilder);

        C.compile({}, program);

        Builder->SetInsertPoint(Builder->CreateBasicBlock("entry"));
        Builder->CreateCall(M, /* self = */ Builder->getNilVal());

        FreeObjects();
        Builder->CreateRet(Builder->getInt32(0));
    }

    bool ModuleCompiler::writeIR(const std::string &Filename) const {
        std::error_code ec;
        auto out = raw_fd_ostream(Filename, ec);
        LoxModule->print(out, nullptr);
        out.close();
        return ec.value() == 0;
    }
}// namespace lox
