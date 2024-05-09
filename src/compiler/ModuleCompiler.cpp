#include "ModuleCompiler.h"
#include "FunctionCompiler.h"
#include "GC.h"
#include "Stack.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"

#include <iostream>
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
        Clock->addFnAttr(Attribute::NoRecurse);

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

        F->addFnAttr(Attribute::NoRecurse);
        Builder->getFunction()->addFnAttr(Attribute::NoRecurse);

        FunctionCompiler MainCompiler(*Context, *M, *F, LoxFunctionType::NONE);

        CreateGcFunction(*Builder);

        MainCompiler.compile(program, {}, [&MainCompiler, &Clock](LoxBuilder &B) {
            MainCompiler.insertVariable("$initString", B.ObjVal(B.AllocateString("init")));
            MainCompiler.insertVariable("clock", B.ObjVal(B.AllocateClosure(MainCompiler, Clock, "clock", true)));
        });

        Builder->SetInsertPoint(Builder->CreateBasicBlock("entry"));
        const auto runtimeStringsTable = Builder->AllocateTable();
        Builder->CreateStore(runtimeStringsTable, M->getRuntimeStrings());
        Builder->CreateCall(F);

        FreeObjects(*Builder);

        M->getGrayStack()->CreateFree(*Builder);
        M->getLocalsStack()->CreateFree(*Builder);
        M->getGlobalsStack()->CreateFree(*Builder);
        Builder->IRBuilder::CreateFree(Builder->CreateLoad(Builder->getPtrTy(), Builder->CreateStructGEP(Builder->getModule().getTableStructType(), runtimeStringsTable, 2)));
        Builder->IRBuilder::CreateFree(runtimeStringsTable);

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

        ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(OptimizationLevel::O2);

        //MPM.run(*M, MAM);
    }

    bool ModuleCompiler::writeIR(const std::string_view Filename) const {
        std::error_code ec;
        auto out = raw_fd_ostream(Filename, ec);
        M->print(out, nullptr);
        out.close();
        return ec.value() == 0;
    }

    bool ModuleCompiler::writeObject(const std::string_view Filename) const {
        const auto TargetTriple = getDefaultTargetTriple();
        InitializeAllTargetInfos();
        InitializeAllTargets();
        InitializeAllTargetMCs();
        InitializeAllAsmParsers();
        InitializeAllAsmPrinters();

        std::string Error;

        // Print an error and exit if we couldn't find the requested target.
        // This generally occurs if we've forgotten to initialise the
        // TargetRegistry or we have a bogus target triple.
        const auto Target = TargetRegistry::lookupTarget(TargetTriple, Error);
        if (!Target) {
            std::cerr << Error;
            return false;
        }

        const auto CPU = "generic";
        const auto Features = "";

        const TargetOptions opt;
        const auto TheTargetMachine = Target->createTargetMachine(
            TargetTriple, CPU, Features, opt, Reloc::PIC_
        );

        M->setDataLayout(TheTargetMachine->createDataLayout());

        std::error_code EC;
        raw_fd_ostream dest(Filename, EC, sys::fs::OF_None);

        if (EC) {
            std::cerr << "Could not open file: " << EC.message();
            return false;
        }

        legacy::PassManager pass;

        if (constexpr auto FileType = CodeGenFileType::ObjectFile; TheTargetMachine->addPassesToEmitFile(pass, dest, nullptr, FileType)) {
            std::cerr << "TheTargetMachine can't emit a file of this type";
            return false;
        }

        pass.run(*M);
        dest.flush();

        std::cout << "Wrote " << Filename << "\n";
        return true;
    }
}// namespace lox
