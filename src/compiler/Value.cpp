#include "Value.h"
#include "LoxBuilder.h"
#include "ModuleCompiler.h"

#include <llvm/ADT/StringExtras.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Value.h>
#include <llvm/Passes/PassBuilder.h>

using namespace llvm;
using namespace llvm::sys;

namespace lox {

    Value *LoxBuilder::getNilVal() {
        return getInt64(NIL_VAL);
    }

    Value *LoxBuilder::getTrueVal() {
        return getInt64(TRUE_VAL);
    }

    Value *LoxBuilder::getFalseVal() {
        return getInt64(FALSE_VAL);
    }

    Value *LoxBuilder::IsBool(Value *value) {
        return CreateICmpEQ(CreateOr(value, 1), getTrueVal());
    }

    Value *LoxBuilder::IsNil(Value *value) {
        return CreateICmpEQ(value, getNilVal());
    }

    Value *LoxBuilder::IsNumber(Value *value) {
        return CreateICmpNE(CreateAnd(value, QNAN), getInt64(QNAN));
    }

    Value *LoxBuilder::IsObj(Value *value) {
        return CreateICmpEQ(CreateAnd(value, QNAN | SIGN_BIT), getInt64(QNAN | SIGN_BIT));
    }

    Value *LoxBuilder::IsString(Value *value) {
        return CreateAnd(
            IsObj(value),
            CreateICmpEQ(ObjType(value), ObjTypeInt(ObjType::STRING))
        );
    }

    Value *LoxBuilder::ObjType(Value *value) {
        return CreateLoad(
            getInt8Ty(),
            CreateStructGEP(getModule().getObjStructType(), AsObj(value), 0)
        );
    }

    ConstantInt *LoxBuilder::ObjTypeInt(enum ObjType objType) {
        return getInt8(static_cast<uint8_t>(objType));
    }


    Value *LoxBuilder::BoolVal(Value *value) {
        assert(value->getType() == getInt1Ty());
        return CreateSelect(value, getTrueVal(), getFalseVal());
    }

    Value *LoxBuilder::AsBool(Value *value) {
        assert(value->getType() == getInt64Ty());
        return CreateICmpEQ(value, getTrueVal());
    }

    Value *LoxBuilder::AsNumber(Value *value) {
        return CreateBitCast(value, getDoubleTy());
    }

    Value *LoxBuilder::ObjVal(Value *value) {
        return CreateOr(value, SIGN_BIT | QNAN);
    }

    Value *LoxBuilder::AsObj(Value *value, const std::optional<enum ObjType> type) {
        return CreateBitCast(
            CreateIntToPtr(CreateAnd(value, ~(SIGN_BIT | QNAN)), getInt8PtrTy()),
            (type.has_value() ? getModule().getStructType(type.value()) : getModule().getObjStructType())->getPointerTo()
        );
    }

    Value *LoxBuilder::AsFunction(Value *value) {
        return AsObj(value, ObjType::FUNCTION);
    }

    Value *LoxBuilder::AsString(Value *value) {
        return AsObj(value, ObjType::STRING);
    }

    Value *LoxBuilder::AsCString(Value *value) {
        const auto string = CreateIntToPtr(CreateAnd(value, ~(SIGN_BIT | QNAN)), getInt8PtrTy());
        return CreateLoad(getInt8PtrTy(), CreateStructGEP(getModule().getStructType(ObjType::STRING), string, 1));
    }

    Value *LoxBuilder::NumberVal(Value *value) {
        return CreateBitCast(value, getInt64Ty());
    }

    void LoxBuilder::Print(Value *value) {
        static auto PrintFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    getVoidTy(),
                    getInt64Ty(),
                    false
                ),
                Function::InternalLinkage,
                "Print",
                this->getModule()
            );

            LoxBuilder B(this->getContext(), this->getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto value = F->args().begin();

            const auto BoolBlock = B.CreateBasicBlock("if.print.bool");
            const auto EndBoolBlock = B.CreateBasicBlock("if.print.bool.end");
            const auto NilBlock = B.CreateBasicBlock("if.print.nil");
            const auto EndNilBlock = B.CreateBasicBlock("if.print.nil.end");
            const auto NumBlock = B.CreateBasicBlock("if.print.num");
            const auto ObjBlock = B.CreateBasicBlock("if.print.obj");
            const auto EndBlock = B.CreateBasicBlock("if.print.end");

            B.CreateCondBr(B.IsBool(value), BoolBlock, EndBoolBlock);
            B.SetInsertPoint(BoolBlock);
            B.PrintBool(value);
            B.CreateBr(EndBlock);
            B.SetInsertPoint(EndBoolBlock);

            B.CreateCondBr(B.IsNil(value), NilBlock, EndNilBlock);
            B.SetInsertPoint(NilBlock);
            B.PrintNil();
            B.CreateBr(EndBlock);
            B.SetInsertPoint(EndNilBlock);

            B.CreateCondBr(B.IsNumber(value), NumBlock, ObjBlock);
            B.SetInsertPoint(NumBlock);
            B.PrintNumber(value);
            B.CreateBr(EndBlock);

            B.SetInsertPoint(ObjBlock);
            B.PrintObject(value);
            B.CreateBr(EndBlock);

            B.SetInsertPoint(EndBlock);
            B.CreateRetVoid();

            return F;
        }());

        CreateCall(PrintFunction, value);
    }

    void LoxBuilder::PrintF(const std::string &stringFormat, Value *value) {
        PrintF({CreateGlobalStringPtr(stringFormat), value});
    }

    void LoxBuilder::PrintF(const std::initializer_list<Value *> value) {
        static const auto PrintF = getModule().getOrInsertFunction(
            "printf",
            FunctionType::get(getInt8Ty(), {Type::getInt8PtrTy(getContext())}, true)
        );
        CreateCall(PrintF, value);
    }

    void LoxBuilder::PrintString(const std::string &string) {
        static const auto fmt = CreateGlobalStringPtr("%s\n", "printf_fmt_str");
        PrintF({fmt, CreateGlobalStringPtr(string)});
    }

    void LoxBuilder::PrintNumber(Value *value) {
        static const auto gfmt = CreateGlobalStringPtr("%g\n", "printf_fmt_num");
        PrintF({gfmt, AsNumber(value)});
    }

    void LoxBuilder::PrintNil() {
        static const auto fmt = CreateGlobalStringPtr("nil\n", "printf_fmt_nil");
        PrintF({fmt});
    }

    void LoxBuilder::PrintObject(Value *value) {
        const auto IsStringBlock = CreateBasicBlock("print.string");
        const auto IsFunctionBlock = CreateBasicBlock("print.function");
        const auto IsNativeFunctionBlock = CreateBasicBlock("print.native.function");
        const auto IsNotNativeFunctionBlock = CreateBasicBlock("print.not.native.function");
        const auto DefaultBlock = CreateBasicBlock("print.default");
        const auto EndBlock = CreateBasicBlock("print.end");

        const auto Switch = CreateSwitch(ObjType(value), DefaultBlock);
        Switch->addCase(ObjTypeInt(ObjType::STRING), IsStringBlock);
        Switch->addCase(ObjTypeInt(ObjType::FUNCTION), IsFunctionBlock);

        SetInsertPoint(IsStringBlock);
        PrintString(value);
        CreateBr(EndBlock);

        SetInsertPoint(IsFunctionBlock);
        static auto fmt = CreateGlobalStringPtr("<fn %s>\n", "printf_fmt_fun");
        static auto nfmt = CreateGlobalStringPtr("<native fn>\n", "printf_nfmt_fun");

        const auto function = AsFunction(value);
        const auto isNative = CreateLoad(getInt1Ty(), CreateStructGEP(getModule().getStructType(ObjType::FUNCTION), function, 4));

        CreateCondBr(isNative, IsNativeFunctionBlock, IsNotNativeFunctionBlock);
        SetInsertPoint(IsNativeFunctionBlock);
        PrintF({nfmt});
        CreateBr(EndBlock);
        SetInsertPoint(IsNotNativeFunctionBlock);
        PrintF({fmt, AsCString(CreateLoad(getInt64Ty(), CreateStructGEP(getModule().getStructType(ObjType::FUNCTION), function, 3)))});
        CreateBr(EndBlock);

        CreateBr(EndBlock);
        SetInsertPoint(DefaultBlock);

        CreateBr(EndBlock);
        SetInsertPoint(EndBlock);
    }

    void LoxBuilder::PrintString(Value *value) {
        static const auto fmt = CreateGlobalStringPtr("%s\n", "printf_fmt_str");
        PrintF({fmt, AsCString(value)});
    }

    void LoxBuilder::PrintBool(Value *value) {
        static const auto fmt = CreateGlobalStringPtr("%s\n", "printf_fmt_bool");
        static const auto true_ = CreateGlobalStringPtr("true", "true_str");
        static const auto false_ = CreateGlobalStringPtr("false", "false_str");
        PrintF({fmt, CreateSelect(AsBool(value), true_, false_)});
    }

    void LoxBuilder::RuntimeError(const unsigned line, Value *message, const std::string_view &value, const llvm::Function *function) {
        static const auto StdErr = getModule().getOrInsertGlobal("stderr", getPtrTy());
        static const auto FPrintF = getModule().getOrInsertFunction(
            "fprintf",
            FunctionType::get(getInt8Ty(), {PointerType::get(Context, 0), PointerType::get(Context, 0)}, true)
        );
        static const auto Exit = getModule().getOrInsertFunction(
            "exit",
            FunctionType::get(getVoidTy(), {getInt32Ty()}, true)
        );

        CreateCall(
            FPrintF,
            {CreateLoad(getPtrTy(), StdErr),
             message,
             CreateGlobalStringPtr(value)
            }
        );
        static const auto fmtScript = CreateGlobalStringPtr("[line %d] in script\n");
        static const auto fmtFunc = CreateGlobalStringPtr("[line %d] in %s()\n");
        if (function == nullptr) {
            CreateCall(
                FPrintF,
                {
                    CreateLoad(getPtrTy(), StdErr),
                    fmtScript,
                    getInt32(line),
                }
            );
        } else {
            CreateCall(
                FPrintF,
                {CreateLoad(getPtrTy(), StdErr),
                 fmtFunc,
                 getInt32(line),
                 CreateGlobalStringPtr(function->getName())
                }
            );
        }
        CreateCall(Exit, getInt32(70));
    }
}// namespace lox
