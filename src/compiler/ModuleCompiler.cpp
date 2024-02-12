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
        // Native clock function.
        Function *Clock = Function::Create(
            FunctionType::get(IntegerType::getInt64Ty(*Context), {Builder->getPtrTy()}, false),
            Function::InternalLinkage,
            "clock_native",
            *M
        );

        LoxBuilder ClockBuilder(*Context, *M, *Clock);
        const auto Entry = ClockBuilder.CreateBasicBlock("entry");
        ClockBuilder.SetInsertPoint(Entry);

        const auto libcClock = M->getOrInsertFunction(
            "clock",
            FunctionType::get(ClockBuilder.getInt64Ty(), false)
        );

        ClockBuilder.CreateRet(
            ClockBuilder.NumberVal(
                ClockBuilder.CreateFDiv(
                    ClockBuilder.CreateSIToFP(ClockBuilder.CreateCall(libcClock), ClockBuilder.getDoubleTy()),
                    ConstantFP::get(ClockBuilder.getDoubleTy(), 1000000.0)
                )
            )
        );

        // ---- Main -----

        Function *F = Function::Create(
            FunctionType::get(IntegerType::getInt64Ty(*Context), {}, false),
            Function::InternalLinkage,
            "main",
            *M
        );

        FunctionCompiler MainCompiler(*Context, *M, *F);

        MainCompiler.compile(program, {}, [&MainCompiler, &Clock](LoxBuilder &B) {
            MainCompiler.insertVariable("clock", B.ObjVal(B.AllocateClosure(Clock, true)));
        });

        Builder->SetInsertPoint(Builder->CreateBasicBlock("entry"));
        Builder->CreateCall(F);

        FreeObjects();

        Builder->CreateRet(Builder->getInt32(0));
    }

    bool ModuleCompiler::writeIR(const std::string &Filename) const {
        std::error_code ec;
        auto out = raw_fd_ostream(Filename, ec);
        M->print(out, nullptr);
        out.close();
        return ec.value() == 0;
    }
}// namespace lox
