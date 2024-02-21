#ifndef LOXBUILDER_H
#define LOXBUILDER_H
#include "LoxModule.h"
#include "Value.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/NoFolder.h>

using namespace llvm;

using LLVMIRBuilder = IRBuilder<NoFolder>;

namespace lox {
    class FunctionCompiler;
    class LoxBuilder : public LLVMIRBuilder {
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
        Value *IsUninitialized(Value *value);
        Value *IsNil(Value *value);
        Value *IsNumber(Value *value);
        Value *IsObj(Value *value);
        Value *IsClosure(Value *value);
        Value *IsString(Value *value);
        Value *IsClass(Value *value);
        Value *IsInstance(Value *value);

        // Code generation for converting an int64 to a Lox value.
        Value *BoolVal(Value *value);
        Value *ObjVal(Value *ptrValue);
        Value *NumberVal(Value *value);

        // Code generation for converting a Lox value to a native type.
        Value *AsBool(Value *value);
        Value *AsObj(Value *value);
        Value *AsCString(Value *value);
        Value *AsNumber(Value *value);
        Value *getUninitializedVal();
        Value *getNilVal();
        Value *getTrueVal();
        Value *getFalseVal();

        Value *ObjType(Value *value);
        ConstantInt *ObjTypeInt(enum ObjType);

        Value *AllocateObj(lox::ObjType objType, const std::string_view name = "");
        Value *AllocateString(Value *String, Value *Length, const std::string_view name = "");
        Value *AllocateString(StringRef String, const std::string_view name = "");
        Value *AllocateFunction(llvm::Function *Function, bool isNative = false);
        Value *AllocateClosure(llvm::Function *function, bool isNative = false);
        Value *AllocateUpvalue(Value *value);
        Value *AllocateArray(Type *type, int size, const std::string_view &name);
        Value *AllocateArray(Type *type, Value *size, const std::string_view &name);
        Value *AllocateClass(const std::string_view name);
        Value *AllocateInstance(Value *klass);
        Value *AllocateTable();
        Value *TableSet(Value *Table, Value *Key, Value *V);
        Value *TableGet(Value *Table, Value *Key);

        Value *Concat(Value *a, Value *b);

        void Print(Value *value);
        void PrintF(const std::string &stringFormat, Value *value);
        void PrintF(const std::initializer_list<Value *> value);
        void PrintString(const std::string &string);
        void PrintString(const Twine &string) {
            PrintString(string.str());
        }
        void PrintNumber(Value *value);
        void PrintNil();
        void PrintObject(Value *value);
        void PrintString(Value *value);
        void PrintBool(Value *value);

        Value *CreateObjStructGEP(const enum ObjType objType, Value *Ptr, unsigned Idx, const Twine &Name = "") {
            return IRBuilder::CreateStructGEP(getModule().getStructType(objType), Ptr, Idx, Name);
        }

        Constant *getNullPtr() {
            return Constant::getNullValue(PointerType::getUnqual(getContext()));
        }

        Constant *CreateGlobalCachedString(std::string_view string) {
            auto &strings = getModule().getStringCache();
            if (strings.contains(string)) {
                return strings.at(string);
            }

            auto ptr = CreateGlobalStringPtr(string);
            strings[string] = ptr;
            return ptr;
        }

        void RuntimeError(const unsigned line, StringRef message, const std::vector<Value *> &values, const llvm::Function *function);

        [[nodiscard]] LoxModule &getModule() const { return M; }
        [[nodiscard]] llvm::Function *getFunction() const { return &Function; }
        [[nodiscard]] BasicBlock *CreateBasicBlock(const std::string_view &name) const {
            return BasicBlock::Create(getContext(), name, getFunction());
        }
    };
}// namespace lox

#endif//LOXBUILDER_H
