#ifndef LOXMODULE_H
#define LOXMODULE_H
#include "Value.h"


#include <llvm/IR/Module.h>

namespace lox {
    using namespace llvm;

    class LoxModule : public Module {
        StructType *ObjStructType = StructType::create(
            getContext(),
            {IntegerType::getInt8Ty(getContext()),// ObjType
             IntegerType::getInt1Ty(getContext()),// isMarked
             PointerType::get(getContext(), 0)},  // next
            "Obj"
        );

        StructType *StringStructType = StructType::create(
            getContext(),
            {ObjStructType,
             PointerType::getInt8PtrTy(getContext()),
             IntegerType::getInt32Ty(getContext())},
            "String"
        );
        StructType *FunctionStructType = StructType::create(
            getContext(),
            {
                ObjStructType,
                IntegerType::getInt8Ty(getContext()),   // arity
                PointerType::getInt8PtrTy(getContext()),// func ptr
                StringStructType                        // name
            },
            "Function"
        );

    public:
        explicit LoxModule(LLVMContext &Context) : Module("lox", Context) {}

        StructType *getObjStructType() const {
            return ObjStructType;
        }

        StructType *getStructType(const ObjType objType) const {
            switch (objType) {
                case ObjType::STRING:
                    return StringStructType;
                case ObjType::FUNCTION:
                    return FunctionStructType;
                // TODO: other types.
                default:
                    throw std::runtime_error("Not implemented");
            }
        }
    };
}// namespace lox

#endif//LOXMODULE_H
