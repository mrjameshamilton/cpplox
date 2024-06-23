#ifndef LOXMODULE_H
#define LOXMODULE_H
#include "Value.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>

constexpr unsigned int MAX_CALL_STACK_SIZE = 512;
constexpr unsigned int FIRST_GC_AT = 512;

namespace lox {
    using namespace llvm;

    class GlobalStack;

    class LoxModule : public Module {
        StructType *const ObjStructType = StructType::create(
            getContext(),
            {IntegerType::getInt8Ty(getContext()),// ObjType
             IntegerType::getInt1Ty(getContext()),// isMarked
             PointerType::get(getContext(), 0)},  // next
            "Obj"
        );
        StructType *const StringStructType = StructType::create(
            getContext(),
            {
                ObjStructType,
                PointerType::getUnqual(getContext()), // char* ptr
                IntegerType::getInt32Ty(getContext()),// length
                IntegerType::getInt32Ty(getContext()),// hash
                IntegerType::getInt1Ty(getContext()), // dynamically allocated
            },
            "String"
        );
        StructType *const FunctionStructType = StructType::create(
            getContext(),
            {
                ObjStructType,
                IntegerType::getInt32Ty(getContext()),// arity
                PointerType::getUnqual(getContext()), // func ptr
                StringStructType->getPointerTo(),     // name
                IntegerType::getInt1Ty(getContext()), // isNative
            },
            "Function"
        );
        StructType *const ClosureStructType = StructType::create(
            getContext(),
            {
                ObjStructType,
                PointerType::getUnqual(getContext()), // func obj ptr
                PointerType::getUnqual(getContext()), // upvalues
                IntegerType::getInt32Ty(getContext()),// upvalue count
            },
            "Closure"
        );
        StructType *const UpvalueStruct = StructType::create(
            getContext(),
            {
                ObjStructType,
                PointerType::getUnqual(getContext()), // location ptr
                PointerType::getUnqual(getContext()), // next
                IntegerType::getInt64Ty(getContext()),// closed value
            },
            "Upvalue"
        );
        StructType *const ClassStruct = StructType::create(
            getContext(),
            {
                ObjStructType,
                StringStructType->getPointerTo(),    // name
                PointerType::getUnqual(getContext()),// methods
            },
            "Class"
        );
        StructType *const InstanceStruct = StructType::create(
            getContext(),
            {
                ObjStructType,
                ClassStruct->getPointerTo(),         // klass
                PointerType::getUnqual(getContext()),// fields
            },
            "Instance"
        );
        StructType *const BoundMethodStruct = StructType::create(
            getContext(),
            {
                ObjStructType,
                // TODO: store both as pointer or both as int64?
                IntegerType::getInt64Ty(getContext()),// receiver
                PointerType::getUnqual(getContext()), // closure
            },
            "BoundMethod"
        );
        StructType *const TableStruct = StructType::create(
            getContext(),
            {
                IntegerType::getInt32Ty(getContext()),// count
                IntegerType::getInt32Ty(getContext()),// capacity
                PointerType::getUnqual(getContext()), // entries
            },
            "Table"
        );
        StructType *const EntryStruct = StructType::create(
            getContext(),
            {
                StringStructType->getPointerTo(),     // key
                IntegerType::getInt64Ty(getContext()),// value
            },
            "Entry"
        );
        GlobalVariable *const objects =
            cast<GlobalVariable>(getOrInsertGlobal("objects", PointerType::get(getContext(), 0)));
        GlobalVariable *const runtimeStrings =
            cast<GlobalVariable>(getOrInsertGlobal("strings", PointerType::get(getContext(), 0)));
        GlobalVariable *const openUpvalues =
            cast<GlobalVariable>(getOrInsertGlobal("openUpvalues", PointerType::get(getContext(), 0)));
        StructType *const Call = StructType::create(
            getContext(),
            {
                IntegerType::getInt32Ty(getContext()),// line
                PointerType::getUnqual(getContext()), // name
            },
            "Call"
        );
        GlobalVariable *const callstack =
            cast<GlobalVariable>(getOrInsertGlobal("callstack", ArrayType::get(Call, MAX_CALL_STACK_SIZE)));
        GlobalVariable *const callstackpointer =
            cast<GlobalVariable>(getOrInsertGlobal("callsp", IntegerType::getInt32Ty(getContext())));
        GlobalVariable *const allocatedBytes =
            cast<GlobalVariable>(getOrInsertGlobal("$allocatedBytes", IntegerType::getInt32Ty(getContext())));
        GlobalVariable *const nextGC =
            cast<GlobalVariable>(getOrInsertGlobal("$nextGC", IntegerType::getInt32Ty(getContext())));
        GlobalVariable *const enableGC =
            cast<GlobalVariable>(getOrInsertGlobal("$enableGC", IntegerType::getInt1Ty(getContext())));
        std::shared_ptr<GlobalStack> grayStack;
        std::shared_ptr<GlobalStack> localsStack;
        llvm::StringMap<Constant *> strings;

    public:
        explicit LoxModule(LLVMContext &Context) : Module("lox", Context) {
            objects->setLinkage(GlobalValue::PrivateLinkage);
            objects->setAlignment(Align(8));
            objects->setConstant(false);
            objects->setInitializer(ConstantPointerNull::get(PointerType::get(Context, 0)));

            runtimeStrings->setLinkage(GlobalValue::PrivateLinkage);
            runtimeStrings->setAlignment(Align(8));
            runtimeStrings->setConstant(false);
            runtimeStrings->setInitializer(ConstantPointerNull::get(PointerType::get(Context, 0)));

            openUpvalues->setLinkage(GlobalValue::PrivateLinkage);
            openUpvalues->setAlignment(Align(8));
            openUpvalues->setConstant(false);
            openUpvalues->setInitializer(ConstantPointerNull::get(PointerType::get(Context, 0)));

            callstack->setLinkage(GlobalVariable::PrivateLinkage);
            callstack->setAlignment(Align(8));
            callstack->setConstant(false);
            callstack->setInitializer(Constant::getNullValue(ArrayType::get(Call, MAX_CALL_STACK_SIZE)));

            callstackpointer->setLinkage(GlobalVariable::PrivateLinkage);
            callstackpointer->setAlignment(Align(8));
            callstackpointer->setConstant(false);
            callstackpointer->setInitializer(ConstantInt::get(IntegerType::getInt32Ty(getContext()), 0));

            allocatedBytes->setLinkage(GlobalVariable::PrivateLinkage);
            allocatedBytes->setAlignment(Align(8));
            allocatedBytes->setConstant(false);
            allocatedBytes->setInitializer(ConstantInt::get(IntegerType::getInt32Ty(getContext()), 0));

            nextGC->setLinkage(GlobalVariable::PrivateLinkage);
            nextGC->setAlignment(Align(8));
            nextGC->setConstant(false);
            nextGC->setInitializer(ConstantInt::get(IntegerType::getInt32Ty(getContext()), FIRST_GC_AT));

            enableGC->setLinkage(GlobalVariable::PrivateLinkage);
            enableGC->setAlignment(Align(8));
            enableGC->setConstant(false);
            enableGC->setInitializer(ConstantInt::get(IntegerType::getInt1Ty(getContext()), 1));

            initialize();
        }

        void initialize();

        StructType *getObjStructType() const { return ObjStructType; }

        StructType *getTableStructType() const { return TableStruct; }

        StructType *getEntryStructType() const { return EntryStruct; }

        StructType *getStructType(const ObjType objType) const {
            switch (objType) {
                case ObjType::STRING:
                    return StringStructType;
                case ObjType::FUNCTION:
                    return FunctionStructType;
                case ObjType::CLOSURE:
                    return ClosureStructType;
                case ObjType::UPVALUE:
                    return UpvalueStruct;
                case ObjType::CLASS:
                    return ClassStruct;
                case ObjType::INSTANCE:
                    return InstanceStruct;
                case ObjType::BOUND_METHOD:
                    return BoundMethodStruct;
                default:
                    throw std::runtime_error("Not implemented");
            }
        }

        GlobalVariable *getObjects() const { return objects; }

        GlobalVariable *getOpenUpvalues() const { return openUpvalues; }

        GlobalVariable *getRuntimeStrings() const { return runtimeStrings; }

        GlobalVariable *getCallStack() const { return callstack; }

        Type *getCallStruct() const { return Call; }

        GlobalVariable *getCallStackPointer() const { return callstackpointer; }

        const GlobalStack &getGrayStack() const { return *grayStack; }

        const GlobalStack &getLocalsStack() const { return *localsStack; }

        GlobalVariable *getAllocatedBytes() const { return allocatedBytes; }

        GlobalVariable *getNextGC() const { return nextGC; }

        GlobalVariable *getEnableGC() const { return enableGC; }

        StringMap<Constant *> &getStringCache() { return strings; }

        const FunctionCallee PrintF = getOrInsertFunction(
            "printf",
            FunctionType::get(IntegerType::getInt8Ty(getContext()), {PointerType::getUnqual(getContext())}, true)
        );
    };
}// namespace lox

#endif//LOXMODULE_H
