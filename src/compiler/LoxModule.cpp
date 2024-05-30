#include "LoxModule.h"

#include "Stack.h"

namespace lox {
    void LoxModule::initialize() {
        grayStack = std::make_shared<GlobalStack>(*this, "gray");
        globalsStack = std::make_shared<GlobalStack>(*this, "globals");
        localsStack = std::make_shared<GlobalStack>(*this, "locals");
    }
}// namespace lox