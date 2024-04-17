#ifndef LOXMODULE_H
#define LOXMODULE_H
#include "Value.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>

constexpr unsigned int MAX_CALL_STACK_SIZE = 512;
constexpr unsigned int MAX_LOCALS = 65'536;
constexpr unsigned int MAX_GLOBALS = 65'536;
constexpr unsigned int FIRST_GC_AT = 512;

namespace lox {
    using namespace llvm;

    struct Greystack {
        GlobalVariable *count;
        GlobalVariable *capacity;
        GlobalVariable *stack;
    };

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
        GlobalVariable *const objects = cast<GlobalVariable>(getOrInsertGlobal(
            "objects",
            PointerType::get(getContext(), 0)
        ));
        GlobalVariable *const runtimeStrings = cast<GlobalVariable>(getOrInsertGlobal(
            "strings",
            PointerType::get(getContext(), 0)
        ));
        GlobalVariable *const openUpvalues = cast<GlobalVariable>(getOrInsertGlobal(
            "openUpvalues",
            PointerType::get(getContext(), 0)
        ));
        StructType *const Call = StructType::create(
            getContext(),
            {
                IntegerType::getInt32Ty(getContext()),// line
                PointerType::getUnqual(getContext()), // name
            },
            "Call"
        );
        GlobalVariable *const callstack = cast<GlobalVariable>(getOrInsertGlobal(
            "callstack",
            ArrayType::get(Call, MAX_CALL_STACK_SIZE)
        ));
        GlobalVariable *const callstackpointer = cast<GlobalVariable>(getOrInsertGlobal(
            "callsp",
            IntegerType::getInt32Ty(getContext())
        ));
        GlobalVariable *const localsstack = cast<GlobalVariable>(getOrInsertGlobal(
            "localsstack",
            ArrayType::get(PointerType::getUnqual(getContext()), MAX_LOCALS)
        ));
        GlobalVariable *const localsstackpointer = cast<GlobalVariable>(getOrInsertGlobal(
            "localsp",
            IntegerType::getInt32Ty(getContext())
        ));
        GlobalVariable *const globalsstack = cast<GlobalVariable>(getOrInsertGlobal(
            "globalsstack",
            ArrayType::get(PointerType::getUnqual(getContext()), MAX_GLOBALS)
        ));
        GlobalVariable *const globalsstackpointer = cast<GlobalVariable>(getOrInsertGlobal(
            "globalsp",
            IntegerType::getInt32Ty(getContext())
        ));
        GlobalVariable *const globals = cast<GlobalVariable>(getOrInsertGlobal(
            "globals",
            TableStruct->getPointerTo()
        ));
        GlobalVariable *const greyCapacity = cast<GlobalVariable>(getOrInsertGlobal(
            "greyCapacity",
            IntegerType::getInt32Ty(getContext())
        ));
        GlobalVariable *const greyCount = cast<GlobalVariable>(getOrInsertGlobal(
            "greyCount",
            IntegerType::getInt32Ty(getContext())
        ));

        GlobalVariable *const greyStack = cast<GlobalVariable>(getOrInsertGlobal(
            "greystack",
            PointerType::getUnqual(getContext())
        ));
        GlobalVariable *const allocatedBytes = cast<GlobalVariable>(getOrInsertGlobal(
            "$allocatedBytes",
            IntegerType::getInt32Ty(getContext())
        ));
        GlobalVariable *const nextGC = cast<GlobalVariable>(getOrInsertGlobal(
            "$nextGC",
            IntegerType::getInt32Ty(getContext())
        ));
        llvm::StringMap<Constant *> strings;

    public:
        explicit LoxModule(LLVMContext &Context) : Module("lox", Context) {
            objects->setLinkage(GlobalValue::PrivateLinkage);
            objects->setAlignment(Align(8));
            objects->setConstant(false);
            objects->setInitializer(ConstantPointerNull::get(PointerType::get(Context, 0)));

            globals->setLinkage(GlobalValue::PrivateLinkage);
            globals->setAlignment(Align(8));
            globals->setConstant(false);
            globals->setInitializer(ConstantPointerNull::get(PointerType::get(Context, 0)));

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

            localsstack->setLinkage(GlobalVariable::PrivateLinkage);
            localsstack->setAlignment(Align(8));
            localsstack->setConstant(false);
            localsstack->setInitializer(Constant::getNullValue(ArrayType::get(PointerType::getUnqual(getContext()), MAX_LOCALS)));

            localsstackpointer->setLinkage(GlobalVariable::PrivateLinkage);
            localsstackpointer->setAlignment(Align(8));
            localsstackpointer->setConstant(false);
            localsstackpointer->setInitializer(ConstantInt::get(IntegerType::getInt32Ty(getContext()), 0));

            globalsstack->setLinkage(GlobalVariable::PrivateLinkage);
            globalsstack->setAlignment(Align(8));
            globalsstack->setConstant(false);
            globalsstack->setInitializer(Constant::getNullValue(ArrayType::get(PointerType::getUnqual(getContext()), MAX_GLOBALS)));

            globalsstackpointer->setLinkage(GlobalVariable::PrivateLinkage);
            globalsstackpointer->setAlignment(Align(8));
            globalsstackpointer->setConstant(false);
            globalsstackpointer->setInitializer(ConstantInt::get(IntegerType::getInt32Ty(getContext()), 0));

            greyCapacity->setLinkage(GlobalVariable::PrivateLinkage);
            greyCapacity->setAlignment(Align(8));
            greyCapacity->setConstant(false);
            greyCapacity->setInitializer(ConstantInt::get(IntegerType::getInt32Ty(getContext()), 0));

            greyCount->setLinkage(GlobalVariable::PrivateLinkage);
            greyCount->setAlignment(Align(8));
            greyCount->setConstant(false);
            greyCount->setInitializer(ConstantInt::get(IntegerType::getInt32Ty(getContext()), 0));

            greyStack->setLinkage(GlobalVariable::PrivateLinkage);
            greyStack->setAlignment(Align(8));
            greyStack->setConstant(false);
            greyStack->setInitializer(ConstantPointerNull::get(PointerType::get(Context, 0)));

            allocatedBytes->setLinkage(GlobalVariable::PrivateLinkage);
            allocatedBytes->setAlignment(Align(8));
            allocatedBytes->setConstant(false);
            allocatedBytes->setInitializer(ConstantInt::get(IntegerType::getInt32Ty(getContext()), 0));

            nextGC->setLinkage(GlobalVariable::PrivateLinkage);
            nextGC->setAlignment(Align(8));
            nextGC->setConstant(false);
            nextGC->setInitializer(ConstantInt::get(IntegerType::getInt32Ty(getContext()), FIRST_GC_AT));
        }

        StructType *getObjStructType() const {
            return ObjStructType;
        }

        StructType *getTableStructType() const {
            return TableStruct;
        }

        StructType *getEntryStructType() const {
            return EntryStruct;
        }

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

        GlobalVariable *getObjects() const {
            return objects;
        }

        GlobalVariable *getOpenUpvalues() const {
            return openUpvalues;
        }

        GlobalVariable *getRuntimeStrings() const {
            return runtimeStrings;
        }

        GlobalVariable *getCallStack() const {
            return callstack;
        }

        Type *getCallStruct() const {
            return Call;
        }

        GlobalVariable *getCallStackPointer() const {
            return callstackpointer;
        }

        GlobalVariable *getLocalsStack() const {
            return localsstack;
        }

        GlobalVariable *getLocalsStackPointer() const {
            return localsstackpointer;
        }

        GlobalVariable *getGlobals() const {
            return globalsstack;
        }

        GlobalVariable *getGlobalsCount() const {
            return globalsstackpointer;
        }

        GlobalVariable *getGreyCount() const {
            return greyCount;
        }

        GlobalVariable *getGreyCapacity() const {
            return greyCapacity;
        }

        GlobalVariable *getGrayStack() const {
            return greyStack;
        }

        GlobalVariable *getAllocatedBytes() const {
            return allocatedBytes;
        }

        GlobalVariable *getNextGC() const {
            return nextGC;
        }

        StringMap<Constant *> &getStringCache() {
            return strings;
        }
    };
}// namespace lox

#endif//LOXMODULE_H
