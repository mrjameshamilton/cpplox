#include "Upvalue.h"
#include "FunctionCompiler.h"

namespace lox {
    Value *LoxBuilder::AllocateUpvalue(Value *value) {
        const auto obj = AllocateObj(ObjType::UPVALUE);
        CreateStore(value, CreateStructGEP(getModule().getStructType(ObjType::UPVALUE), CreateLoad(getPtrTy(), obj), 1));
        return ObjVal(
            CreatePtrToInt(
                CreateLoad(getPtrTy(), obj),
                getInt64Ty()
            )
        );
    }

    Value *FunctionCompiler::captureLocal(Value *value) {
        //std::cout << "\tcapture(" << value << ")" << std::endl;
        const auto upValue = Builder.AllocateUpvalue(value);
        return Builder.AsUpvalue(upValue);
    }
}// namespace lox
