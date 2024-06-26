#ifndef LOXBUILDER_H
#define LOXBUILDER_H
#include "LoxModule.h"
#include "Value.h"
#include <llvm/IR/IRBuilder.h>

using namespace llvm;

using LLVMIRBuilder = IRBuilder<>;
//using LLVMIRBuilder = IRBuilder<NoFolder>;

namespace lox {
    class FunctionCompiler;
    class LoxBuilder : public LLVMIRBuilder {
        LoxModule &M;
        llvm::Function &Function;

    public:
        explicit LoxBuilder(LLVMContext &Context, LoxModule &Module, llvm::Function &Function)
            : IRBuilder(Context), M(Module), Function(Function) {}

        // Code generation for internal Lox functions.
        Value *IsTruthy(Value *value);

        // Code generation for checking types of values.
        Value *IsBool(Value *);
        Value *IsUninitialized(Value *value);
        Value *IsNil(Value *value);
        Value *IsNumber(Value *value);
        Value *IsObj(Value *value);
        Value *IsClosure(Value *value);
        Value *IsString(Value *value);
        Value *IsClass(Value *value);
        Value *IsBoundMethod(Value *value);
        Value *IsInstance(Value *value);
        Value *IsUpvalue(Value *value);

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

        Value *getSizeOf(Type *type, Value *arraySize = nullptr);
        ConstantInt *getSizeOf(enum ObjType type) const;
        ConstantInt *getSizeOf(Type *type, unsigned int arraySize) const;
        Value *AllocateObj(lox::ObjType objType, std::string_view name = "");
        Value *AllocateString(Value *String, Value *Length, std::string_view name = "");
        Value *AllocateString(StringRef String, std::string_view name = "");
        Value *AllocateClosure(llvm::Function *function, std::string_view name, bool isNative);
        Value *AllocateUpvalue(Value *value);
        Value *AllocateClass(Value *name);
        Value *AllocateInstance(Value *klass);
        Value *AllocateTable();
        Value *TableSet(Value *Table, Value *Key, Value *Value);
        Value *TableGet(Value *Table, Value *Key);
        Value *TableAddAll(Value *FromTable, Value *ToTable);

        Value *CreateReallocate(Value *ptr, Value *oldSize, Value *newSize);
        Value *CreateRealloc(Value *ptr, Value *newSize, StringRef what);
        void CreateFree(Value *ptr, enum ObjType type, Value *arraySize);
        void CollectGarbage(bool force, Value *extraRoot = nullptr);
        Value *Concat(Value *a, Value *b);

        void Print(Value *value);
        void PrintF(std::initializer_list<Value *> value);
        void PrintFErr(Value *message, const std::vector<Value *> &values = {});
        void PrintString(StringRef string);

        void PrintNumber(Value *value);
        void PrintNil();
        void PrintObject(Value *value);
        void PrintString(Value *value);
        void PrintBool(Value *value);

        void Exit(Value *code);

        Value *CreateObjStructGEP(const enum ObjType objType, Value *Ptr, const unsigned Idx, const Twine &Name = "") {
            return CreateStructGEP(getModule().getStructType(objType), Ptr, Idx, Name);
        }

        [[nodiscard]] Constant *getNullPtr() const {
            return Constant::getNullValue(PointerType::getUnqual(getContext()));
        }


        Constant *CreateGlobalCachedString(const std::string_view string) {
            auto &strings = getModule().getStringCache();
            if (strings.contains(string)) { return strings.at(string); }

            auto *const ptr = CreateGlobalStringPtr(string);
            strings[string] = ptr;
            return ptr;
        }

        void RuntimeError(
            Value *line, StringRef message, const std::vector<Value *> &values, Value *location, bool freeObjects = true
        );
        void RuntimeError(
            const unsigned line, const StringRef message, const std::vector<Value *> &values,
            const llvm::Function *function
        ) {
            RuntimeError(getInt32(line), message, values, CreateGlobalCachedString(function->getName()));
        }

        [[nodiscard]] LoxModule &getModule() const { return M; }
        [[nodiscard]] llvm::Function *getFunction() const { return &Function; }
        [[nodiscard]] BasicBlock *CreateBasicBlock(const std::string_view &name) const {
            return BasicBlock::Create(getContext(), name, getFunction());
        }
        Value *
        BindMethod(Value *klass, Value *receiver, Value *key, unsigned int line, const llvm::Function *pFunction);

        CallInst *CreateInvariantEnd(CallInst *start, Value *Ptr, ConstantInt *Size) {

            assert(isa<PointerType>(Ptr->getType()) && "invariant.start only applies to pointers.");
            if (!Size) Size = getInt64(-1);
            else
                assert(Size->getType() == getInt64Ty() && "invariant.start requires the size to be an i64");

            Value *Ops[] = {start, Size, Ptr};
            // Fill in the single overloaded type: memory object type.
            Type *ObjectPtr[1] = {Ptr->getType()};
            Module *M = BB->getParent()->getParent();
            llvm::Function *TheFn = Intrinsic::getDeclaration(M, Intrinsic::invariant_end, ObjectPtr);
            return CreateCall(TheFn, Ops);
        }
    };
}// namespace lox

#endif//LOXBUILDER_H
