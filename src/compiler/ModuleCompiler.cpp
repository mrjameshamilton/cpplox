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

    static void Native(
        const StringRef loxName, const unsigned long loxArgsSize, const FunctionCallee &native,
        FunctionCompiler &ScriptCompiler,
        const std::function<void(LoxBuilder &B, const FunctionCallee &native, Argument *args)> &block
    ) {
        auto &ScriptBuilder = ScriptCompiler.getBuilder();
        std::vector<Type *> paramTypes(loxArgsSize, ScriptBuilder.getInt64Ty());
        // The second parameter is for the receiver instance for methods
        // or the function object value itself for functions.
        paramTypes.insert(paramTypes.begin(), ScriptBuilder.getInt64Ty());
        // The first parameter is the upvalues.
        paramTypes.insert(paramTypes.begin(), ScriptBuilder.getPtrTy());

        Function *F = Function::Create(
            FunctionType::get(ScriptBuilder.getInt64Ty(), paramTypes, false), Function::InternalLinkage,
            loxName + "_native", ScriptBuilder.getModule()
        );
        F->addFnAttr(Attribute::NoRecurse);
        F->addFnAttr(Attribute::AlwaysInline);
        F->addParamAttr(0, Attribute::ReadNone);

        LoxBuilder B(ScriptBuilder.getContext(), ScriptBuilder.getModule(), *F);
        auto *const ExitEntry = B.CreateBasicBlock("entry");
        B.SetInsertPoint(ExitEntry);

        block(B, native, F->arg_begin() + 2);

        auto *const closure = ScriptBuilder.AllocateClosure(F, loxName, true);
        auto *const variable =
            cast<GlobalVariable>(ScriptCompiler.insertVariable(loxName, ScriptBuilder.ObjVal(closure)));
        auto *const nameNode = MDString::get(ScriptBuilder.getContext(), loxName);
        auto *const arityNode = ValueAsMetadata::get(ScriptBuilder.getInt32(loxArgsSize));
        auto *const llvmFunctionName = MDString::get(ScriptBuilder.getContext(), F->getName());
        setMetadata(
            variable, "lox-function", MDTuple::get(ScriptBuilder.getContext(), {nameNode, arityNode, llvmFunctionName})
        );
    }

    static void Native(
        const StringRef name, Type *result, const std::vector<Type *> &params, FunctionCompiler &ScriptCompiler,
        const std::function<void(LoxBuilder &B, const FunctionCallee &native, Argument *args)> &block
    ) {
        const FunctionCallee native =
            ScriptCompiler.getBuilder().getModule().getOrInsertFunction(name, FunctionType::get(result, params, false));
        Native(name, params.size(), native, ScriptCompiler, block);
    }

    void ModuleCompiler::evaluate(const Program &program) const {
        // ---- Main -----

        Function *F = Function::Create(
            FunctionType::get(Builder->getVoidTy(), {}, false), Function::InternalLinkage, "script", getModule()
        );

        F->addFnAttr(Attribute::NoRecurse);
        Builder->getFunction()->addFnAttr(Attribute::NoRecurse);

        FunctionCompiler ScriptCompiler(getContext(), getModule(), *F, LoxFunctionType::NONE);

        CreateGcFunction(*Builder);

        ScriptCompiler.compile(program, {}, [&ScriptCompiler](LoxBuilder &B) {
            ScriptCompiler.insertVariable("$initString", B.ObjVal(B.AllocateString("init")), true);
            Native(
                "clock", B.getInt64Ty(), {}, ScriptCompiler,
                [](LoxBuilder &B, const FunctionCallee &native, Argument *) {
                    B.CreateRet(B.NumberVal(B.CreateFDiv(
                        B.CreateSIToFP(B.CreateCall(native), B.getDoubleTy()),
                        ConstantFP::get(B.getDoubleTy(), 1000000.0)
                    )));
                }
            );

            Native(
                "exit", B.getVoidTy(), {B.getInt32Ty()}, ScriptCompiler,
                [](LoxBuilder &B, const FunctionCallee &native, Argument *args) {
                    B.CreateCall(native, {B.CreateFPToSI(B.AsNumber(args), B.getInt32Ty())});
                    B.CreateUnreachable();
                }
            );

            Native(
                "getchar", B.getInt8Ty(), {}, ScriptCompiler,
                [](LoxBuilder &B, const FunctionCallee &native, Argument *) {
                    auto *const result = B.CreateCall(native);
                    B.CreateRet(B.CreateSelect(
                        B.CreateICmpEQ(result, B.getInt8(-1)), B.getNilVal(),
                        B.NumberVal(B.CreateSIToFP(result, B.getDoubleTy()))
                    ));
                }
            );

            const FunctionCallee native = B.getModule().getOrInsertFunction(
                "fprintf", FunctionType::get(B.getInt8Ty(), {B.getPtrTy(), B.getPtrTy()}, true)
            );
            Native(
                "printerr", 1, native, ScriptCompiler,
                [](LoxBuilder &B, const FunctionCallee &native, Argument *args) {
                    static auto *const StdErr = B.getModule().getOrInsertGlobal("stderr", B.getPtrTy());
                    static auto *const fmt = B.CreateGlobalCachedString("%s\n");
                    B.CreateCall(native, {B.CreateLoad(B.getPtrTy(), StdErr), fmt, B.AsCString(args)});
                    B.CreateRet(B.getNilVal());
                }
            );

            Native("utf", 4, nullptr, ScriptCompiler, [](LoxBuilder &B, const FunctionCallee &, Argument *args) {
                // example: utf(224, 174, 131, nil); -> ஃ
                auto *const count = CreateEntryBlockAlloca(B.getFunction(), B.getInt32Ty(), "count");
                B.CreateStore(B.getInt32(0), count);

                auto *const type = ArrayType::get(B.getInt8Ty(), 4);
                auto *const bytes = CreateEntryBlockAlloca(B.getFunction(), type, "bytes");

                for (int i = 0; i < 4; i++) {
                    B.CreateStore(
                        B.CreateSelect(
                            B.IsNil(args + i), B.getInt8(0), B.CreateFPToSI(B.AsNumber(args + i), B.getInt8Ty())
                        ),
                        B.CreateInBoundsGEP(type, bytes, {B.getInt8(0), B.getInt8(i)})
                    );

                    auto *const c = B.CreateLoad(B.getInt32Ty(), count);
                    B.CreateStore(
                        B.CreateSelect(
                            B.IsNil(args + i), c,
                            B.CreateAdd(B.getInt32(1), c)
                        ),
                        count
                    );
                }

                auto *const length = B.CreateLoad(B.getInt32Ty(), count);
                auto *const allocsize =
                    B.CreateAdd(B.getInt32(1), length, "lengthwithnullterminator", true, true);
                auto *const chars = B.CreateRealloc(
                    B.getNullPtr(), B.CreateSExt(B.getSizeOf(B.getInt8Ty(), allocsize), B.getInt64Ty()), "string"
                );

                B.CreateMemCpy(chars, Align(8), bytes, Align(8), length);

                auto *const addr = B.CreateInBoundsGEP(B.getInt8Ty(), chars, {length});

                B.CreateStore(B.getInt8(0), addr);

                auto *const string = B.AllocateString(chars, length);

                B.CreateRet(B.ObjVal(string));
            });
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
            Builder->RuntimeError(
                Builder->getInt32(0), "locals not zero (%d)\n", {locals}, Builder->CreateGlobalCachedString("assert")
            );

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
        auto *const TheTargetMachine = Target->createTargetMachine(TargetTriple, CPU, Features, opt, Reloc::PIC_);

        M.setTargetTriple(TargetTriple);
        M.setDataLayout(TheTargetMachine->createDataLayout());

        this->TheTargetMachine = TheTargetMachine;

        return true;
    }

    bool ModuleCompiler::optimize() const {
        if (!this->TheTargetMachine) { return false; }
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
        if (!this->TheTargetMachine) { return false; }
        std::error_code ec;
        auto out = raw_fd_ostream(Filename, ec);
        getModule().print(out, nullptr);
        out.close();
        return ec.value() == 0;
    }

    bool ModuleCompiler::writeObject(const std::string_view Filename) const {
        if (!this->TheTargetMachine) { return false; }
        std::error_code EC;
        raw_fd_ostream dest(Filename, EC, sys::fs::OF_None);

        if (EC) {
            std::cerr << "Could not open file: " << EC.message();
            return false;
        }

        legacy::PassManager pass;

        if (constexpr auto FileType = CodeGenFileType::ObjectFile;
            TheTargetMachine->addPassesToEmitFile(pass, dest, nullptr, FileType)) {
            std::cerr << "TheTargetMachine can't emit a file of this type";
            return false;
        }

        pass.run(getModule());
        dest.flush();

        std::cout << "Wrote " << Filename << "\n";
        return true;
    }
}// namespace lox
