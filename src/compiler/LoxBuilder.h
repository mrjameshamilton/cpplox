#ifndef LOXBUILDER_H
#define LOXBUILDER_H
#include "Value.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/NoFolder.h>

using namespace llvm;

namespace lox {
    class LoxBuilder : public IRBuilder<NoFolder> {
        StructType *ObjStructType = StructType::create(
            Context,
            {IntegerType::getInt8Ty(Context),// ObjType
             IntegerType::getInt1Ty(Context),// isMarked
             PointerType::get(Context, 0)},  // next
            "Obj"
        );
        StructType *StringStructType = StructType::create(
            Context,
            {ObjStructType,
             PointerType::getInt8PtrTy(Context),
             IntegerType::getInt32Ty(Context)},
            "String"
        );
        Module &LoxModule;
        llvm::Function &Function;

    public:
        explicit LoxBuilder(LLVMContext &Context, Module &Module, llvm::Function &Function) : IRBuilder(Context), LoxModule(Module), Function(Function) {
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
        Value *AsObj(Value *value);
        Value *AsString(Value *value);
        Value *AsCString(Value *value);
        Value *AsNumber(Value *value);
        Value *getNilVal();

        Value *ObjType(Value *value);
        ConstantInt *ObjTypeInt(enum ObjType);

        Value *AllocateObj(Value *objects, enum ObjType objType, std::string_view name = "");
        Value *AllocateString(Value *objects, Value *String, Value *Length, std::string_view name = "");

        Value *Concat(Value *a, Value *b);
        Value *StrEquals(Value *a, Value *b);

        void PrintF(const std::string &stringFormat, Value *value);
        void PrintF(const std::initializer_list<Value *> value);
        void PrintString(const std::string &string);
        void PrintNumber(Value *value);
        void PrintNil();
        void PrintObject(Value *value);
        void PrintString(Value *value);
        void PrintBool(Value *value);

        [[nodiscard]] StructType *getObjStructType() const {
            return ObjStructType;
        }

        [[nodiscard]] StructType *getStructType(const enum ObjType objType) const {
            switch (objType) {
                case ObjType::STRING:
                    return StringStructType;
                // TODO: other types.
                default:
                    throw std::runtime_error("Not implemented");
            }
        }

        [[nodiscard]] Module &getModule() const { return LoxModule; }
        [[nodiscard]] llvm::Function *getFunction() const { return &Function; }
    };
}// namespace lox

#endif//LOXBUILDER_H
