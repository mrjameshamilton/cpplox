#ifndef LOXMODULE_H
#define LOXMODULE_H
#include <llvm/IR/Module.h>

namespace lox {
    class LoxModule : public Module {

    public:
        explicit LoxModule(LLVMContext &Context) : Module("lox", Context) {}
    };
}// namespace lox

#endif//LOXMODULE_H
