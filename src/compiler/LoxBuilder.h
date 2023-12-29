#ifndef LOXBUILDER_H
#define LOXBUILDER_H
#include "LoxModule.h"
#include "Value.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/NoFolder.h>

using namespace llvm;

namespace lox {
    class LoxBuilder : public IRBuilder<NoFolder> {
        LoxModule &M;
        llvm::Function &Function;

    public:
        explicit LoxBuilder(LLVMContext &Context, LoxModule &Module, llvm::Function &Function) : IRBuilder(Context), M(Module), Function(Function) {
        }

        // Code generation for internal Lox functions.
        Value *IsTruthy(Value *value);
        Value *IsNotTruthy(Value *value);

        // Code generation for checking types of values.
        Value *IsBool(Value *);
        Value *IsNil(Value *value);
        Value *IsNumber(Value *value);
        Value *IsObj(Value *value);
        Value *IsString(Value *value);

        // Code generation for converting an int64 to a Lox value.
        Value *BoolVal(Value *value);
        Value *ObjVal(Value *value);
        Value *NumberVal(Value *value);

        // Code generation for converting a Lox value to a native type.
        Value *AsBool(Value *value);
        Value *AsObj(Value *value, std::optional<ObjType> type = std::nullopt);
        Value *AsFunction(Value *value);
        Value *AsString(Value *value);
        Value *AsCString(Value *value);
        Value *AsNumber(Value *value);
        Value *getNilVal();

        Value *ObjType(Value *value);
        ConstantInt *ObjTypeInt(enum ObjType);

        Value *AllocateObj(Value *objects, enum ObjType objType, std::string_view name = "");
        Value *AllocateString(Value *objects, Value *String, Value *Length, std::string_view name = "");
        Value *AllocateFunction(Value *objects, llvm::Function *Function);

        Value *Concat(Value *a, Value *b);
        Value *StrEquals(Value *a, Value *b);

        void Print(Value *value);
        void PrintF(const std::string &stringFormat, Value *value);
        void PrintF(const std::initializer_list<Value *> value);
        void PrintString(const std::string &string);
        void PrintNumber(Value *value);
        void PrintNil();
        void PrintObject(Value *value);
        void PrintString(Value *value);
        void PrintBool(Value *value);

        [[nodiscard]] LoxModule &getModule() const { return M; }
        [[nodiscard]] llvm::Function *getFunction() const { return &Function; }
        [[nodiscard]] BasicBlock *CreateBasicBlock(const std::string_view &name) const {
            return BasicBlock::Create(getContext(), name, getFunction());
        }
    };
}// namespace lox

#endif//LOXBUILDER_H
