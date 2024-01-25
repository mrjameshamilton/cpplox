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
            {ObjStructType,
             PointerType::getUnqual(getContext()),
             IntegerType::getInt32Ty(getContext())},
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
                PointerType::getUnqual(getContext()),// func obj ptr
            },
            "Closure"
        );
        GlobalVariable *const objects = cast<GlobalVariable>(getOrInsertGlobal(
            "objects",
            PointerType::get(getContext(), 0)
        ));

    public:
        explicit LoxModule(LLVMContext &Context) : Module("lox", Context) {
            objects->setLinkage(GlobalValue::PrivateLinkage);
            objects->setAlignment(Align(8));
            objects->setConstant(false);
            objects->setInitializer(ConstantPointerNull::get(PointerType::get(Context, 0)));
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
                // TODO: other types.
                default:
                    throw std::runtime_error("Not implemented");
            }
        }

        GlobalVariable *getObjects() const {
            return objects;
        }
    };
}// namespace lox

#endif//LOXMODULE_H
