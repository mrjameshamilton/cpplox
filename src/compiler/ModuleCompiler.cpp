#include "ModuleCompiler.h"
#include "../Debug.h"
#include "FunctionCompiler.h"
#include "GC.h"
#include "MDUtil.h"
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

    static Function *createClockFunction(LoxBuilder &Builder) {
        Function *Clock = Function::Create(
            FunctionType::get(Builder.getInt64Ty(), {Builder.getPtrTy(), Builder.getInt64Ty()}, false),
            Function::InternalLinkage,
            "clock_native",
            Builder.getModule()
        );
        Clock->addFnAttr(Attribute::NoRecurse);
        Clock->addFnAttr(Attribute::AlwaysInline);

        LoxBuilder ClockBuilder(Builder.getContext(), Builder.getModule(), *Clock);
        auto *const Entry = ClockBuilder.CreateBasicBlock("entry");
        ClockBuilder.SetInsertPoint(Entry);

        const auto &libcClock = Builder.getModule().getOrInsertFunction(
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

        return Clock;
    }

    static Function *createExitFunction(LoxBuilder &Builder) {
        Function *Exit = Function::Create(
            FunctionType::get(
                Builder.getInt64Ty(),
                {Builder.getPtrTy(), Builder.getInt64Ty(), Builder.getInt64Ty()},
                false
            ),
            Function::InternalLinkage,
            "exit_native",
            Builder.getModule()
        );
        Exit->addFnAttr(Attribute::NoRecurse);
        Exit->addFnAttr(Attribute::AlwaysInline);

        LoxBuilder ExitBuilder(Builder.getContext(), Builder.getModule(), *Exit);
        auto *const ExitEntry = ExitBuilder.CreateBasicBlock("entry");
        ExitBuilder.SetInsertPoint(ExitEntry);

        const auto libcExit = Builder.getModule().getOrInsertFunction(
            "exit",
            FunctionType::get(ExitBuilder.getVoidTy(), {ExitBuilder.getInt32Ty()}, false)
        );

        ExitBuilder.CreateCall(libcExit, {ExitBuilder.CreateFPToSI(ExitBuilder.AsNumber(Exit->arg_begin() + 2), ExitBuilder.getInt32Ty())});
        ExitBuilder.CreateUnreachable();

        return Exit;
    }

    void ModuleCompiler::evaluate(const Program &program) const {
        auto *const Clock = createClockFunction(*Builder);
        auto *const Exit = createExitFunction(*Builder);

        // ---- Main -----

        Function *F = Function::Create(
            FunctionType::get(Builder->getVoidTy(), {}, false),
            Function::InternalLinkage,
            "script",
            getModule()
        );

        F->addFnAttr(Attribute::NoRecurse);
        Builder->getFunction()->addFnAttr(Attribute::NoRecurse);

        FunctionCompiler ScriptCompiler(getContext(), getModule(), *F, LoxFunctionType::NONE);

        CreateGcFunction(*Builder);

        ScriptCompiler.compile(program, {}, [&ScriptCompiler, &Clock, &Exit](LoxBuilder &B) {
            ScriptCompiler.insertVariable("$initString", B.ObjVal(B.AllocateString("init")), true);

            {
                auto *const clockClosure = B.AllocateClosure(ScriptCompiler, Clock, "clock", true);
                auto *const clock = cast<GlobalVariable>(ScriptCompiler.insertVariable("clock", B.ObjVal(clockClosure)));
                auto *const nameNode = MDString::get(B.getContext(), "clock");
                auto *const arityNode = ValueAsMetadata::get(B.getInt32(Clock->arg_size() - 2));
                auto *const llvmFunctionName = MDString::get(B.getContext(), "clock_native");
                setMetadata(clock, "lox-function", MDTuple::get(B.getContext(), {nameNode, arityNode, llvmFunctionName}));
            }

            {
                auto *const exitClosure = B.AllocateClosure(ScriptCompiler, Exit, "exit", true);
                auto *const exit = ScriptCompiler.insertVariable("exit", B.ObjVal(exitClosure));
                auto *const nameNode = MDString::get(B.getContext(), "exit");
                auto *const arityNode = ValueAsMetadata::get(B.getInt32(Exit->arg_size() - 2));
                auto *const llvmFunctionName = MDString::get(B.getContext(), "exit_native");
                setMetadata(exit, "lox-function", MDTuple::get(B.getContext(), {nameNode, arityNode, llvmFunctionName}));
            }
        });

        Builder->SetInsertPoint(Builder->CreateBasicBlock("entry"));
        auto *const runtimeStringsTable = Builder->AllocateTable();
        Builder->CreateStore(runtimeStringsTable, getModule().getRuntimeStrings());
        Builder->CreateCall(F);

        if constexpr (ENABLE_RUNTIME_ASSERTS) {
            auto *const locals = Builder->getModule().getLocalsStack().CreateGetCount(*Builder);
            auto *const IsZeroBlock = Builder->CreateBasicBlock("is.empty");
            auto *const IsNotZeroBlock = Builder->CreateBasicBlock("is.notempty");

            Builder->CreateCondBr(Builder->CreateICmpEQ(Builder->getInt32(0), locals), IsZeroBlock, IsNotZeroBlock);
            Builder->SetInsertPoint(IsNotZeroBlock);
            Builder->RuntimeError(Builder->getInt32(0), "locals not zero (%d)\n", {locals}, Builder->CreateGlobalCachedString("assert"));

            Builder->SetInsertPoint(IsZeroBlock);
        }

        FreeObjects(*Builder);

        Builder->CreateRet(Builder->getInt32(0));
    }

    bool ModuleCompiler::initializeTarget() const {
        const auto TargetTriple = getDefaultTargetTriple();
        InitializeAllTargetInfos();
        InitializeAllTargets();
        InitializeAllTargetMCs();
        InitializeAllAsmParsers();
        InitializeAllAsmPrinters();
        auto &M = getModule();

        std::string Error;
        const auto *const Target = TargetRegistry::lookupTarget(TargetTriple, Error);
        if (!Target) {
            std::cerr << Error;
            return false;
        }
        const auto *const CPU = "generic";
        const auto *const Features = "";

        const TargetOptions opt;
        auto *const TheTargetMachine = Target->createTargetMachine(
            TargetTriple, CPU, Features, opt, Reloc::PIC_
        );

        M.setTargetTriple(TargetTriple);
        M.setDataLayout(TheTargetMachine->createDataLayout());

        this->TheTargetMachine = TheTargetMachine;

        return true;
    }

    bool ModuleCompiler::optimize() const {
        if (!this->TheTargetMachine) {
            return false;
        }
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

        MPM.run(getModule(), MAM);

        return true;
    }

    bool ModuleCompiler::writeIR(const std::string_view Filename) const {
        if (!this->TheTargetMachine) {
            return false;
        }
        std::error_code ec;
        auto out = raw_fd_ostream(Filename, ec);
        getModule().print(out, nullptr);
        out.close();
        return ec.value() == 0;
    }

    bool ModuleCompiler::writeObject(const std::string_view Filename) const {
        if (!this->TheTargetMachine) {
            return false;
        }
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

        pass.run(getModule());
        dest.flush();

        std::cout << "Wrote " << Filename << "\n";
        return true;
    }
}// namespace lox
