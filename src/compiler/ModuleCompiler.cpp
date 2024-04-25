#include "ModuleCompiler.h"
#include "FunctionCompiler.h"
#include "GC.h"
#include "Stack.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Value.h>
#include <llvm/Passes/PassBuilder.h>

using namespace llvm;
using namespace llvm::sys;

namespace lox {

    void ModuleCompiler::evaluate(const Program &program) const {
        M->setGrayStack(std::make_shared<GlobalStack>(*M, "gray"));
        M->setGlobalsStack(std::make_shared<GlobalStack>(*M, "globals"));
        M->setLocalsStack(std::make_shared<GlobalStack>(*M, "locals"));

        // Native clock function.
        Function *Clock = Function::Create(
            FunctionType::get(IntegerType::getInt64Ty(*Context), {Builder->getPtrTy(), Builder->getInt64Ty()}, false),
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
            "script",
            *M
        );

        FunctionCompiler MainCompiler(*Context, *M, *F, LoxFunctionType::NONE);

        CreateGcFunction(*Builder);

        MainCompiler.compile(program, {}, [&MainCompiler, &Clock](LoxBuilder &B) {
            MainCompiler.insertVariable("$initString", B.ObjVal(B.AllocateString("init")));
            MainCompiler.insertVariable("clock", B.ObjVal(B.AllocateClosure(Clock, "clock", true)));
        });

        Builder->SetInsertPoint(Builder->CreateBasicBlock("entry"));
        Builder->CreateStore(Builder->AllocateTable(), M->getRuntimeStrings());
        Builder->CreateCall(F);

        FreeObjects(*Builder);

        Builder->CreateRet(Builder->getInt32(0));
    }

    void ModuleCompiler::optimize() const {
        LoopAnalysisManager LAM;
        FunctionAnalysisManager FAM;
        CGSCCAnalysisManager CGAM;
        ModuleAnalysisManager MAM;
        PassBuilder PB;

        // PB.printPassNames(outs());
        PB.registerModuleAnalyses(MAM);
        PB.registerCGSCCAnalyses(CGAM);
        PB.registerFunctionAnalyses(FAM);
        PB.registerLoopAnalyses(LAM);
        PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

        ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(OptimizationLevel::O3);

        MPM.run(*M, MAM);
    }

    bool ModuleCompiler::writeIR(const std::string &Filename) const {
        std::error_code ec;
        auto out = raw_fd_ostream(Filename, ec);
        M->print(out, nullptr);
        out.close();
        return ec.value() == 0;
    }
}// namespace lox
