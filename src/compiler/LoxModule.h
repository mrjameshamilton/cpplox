#ifndef LOXMODULE_H
#define LOXMODULE_H
#include "Value.h"


#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>

namespace lox {
    using namespace llvm;

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
                StringStructType,                     // name
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
                StringStructType,// name
            },
            "ClassStruct"
        );
        StructType *const InstanceStruct = StructType::create(
            getContext(),
            {
                ObjStructType,
                ClassStruct,// klass
                // TODO: fields
            },
            "InstanceStruct"
        );
        GlobalVariable *const objects = cast<GlobalVariable>(getOrInsertGlobal(
            "objects",
            PointerType::get(getContext(), 0)
        ));
        GlobalVariable *const openUpvalues = cast<GlobalVariable>(getOrInsertGlobal(
            "openUpvalues",
            PointerType::get(getContext(), 0)
        ));

        llvm::StringMap<Constant *> strings;

    public:
        explicit LoxModule(LLVMContext &Context) : Module("lox", Context) {
            objects->setLinkage(GlobalValue::PrivateLinkage);
            objects->setAlignment(Align(8));
            objects->setConstant(false);
            objects->setInitializer(ConstantPointerNull::get(PointerType::get(Context, 0)));

            openUpvalues->setLinkage(GlobalValue::PrivateLinkage);
            openUpvalues->setAlignment(Align(8));
            openUpvalues->setConstant(false);
            openUpvalues->setInitializer(ConstantPointerNull::get(PointerType::get(Context, 0)));
        }

        StructType *getObjStructType() const {
            return ObjStructType;
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
                // TODO: other types.
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

        llvm::StringMap<Constant *> &getStringCache() {
            return strings;
        }
    };
}// namespace lox

#endif//LOXMODULE_H
