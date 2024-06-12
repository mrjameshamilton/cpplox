#ifndef MDBUILDERUTIL_H
#define MDBUILDERUTIL_H
#include <llvm/IR/MDBuilder.h>

inline MDNode *createLikelyBranchWeights(MDBuilder &Builder) {
    // TODO: Use MDBuilder::createLikelyBranchWeights when updating LLVM.
    // Value chosen to match UR_NONTAKEN_WEIGHT, see BranchProbabilityInfo.cpp
    return Builder.createBranchWeights((1U << 20) - 1, 1);
}
#endif //MDBUILDERUTIL_H
