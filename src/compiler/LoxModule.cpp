#include "LoxModule.h"

#include "Stack.h"

namespace lox {
    void LoxModule::initialize() {
        grayStack = std::make_shared<GlobalStack>(*this, "gray");
        localsStack = std::make_shared<GlobalStack>(*this, "locals");
    }
}// namespace lox