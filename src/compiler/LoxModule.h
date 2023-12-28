#ifndef LOXMODULE_H
#define LOXMODULE_H
#include <llvm/IR/Module.h>

namespace lox {
    class LoxModule : public llvm::Module {

    public:
        explicit LoxModule(llvm::LLVMContext &Context) : llvm::Module("lox", Context) {}
    };
}// namespace lox

#endif//LOXMODULE_H
