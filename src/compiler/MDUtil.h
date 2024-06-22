#ifndef MDBUILDERUTIL_H
#define MDBUILDERUTIL_H
#include <llvm/IR/MDBuilder.h>

inline MDNode *createLikelyBranchWeights(MDBuilder &Builder) {
    // TODO: Use MDBuilder::createLikelyBranchWeights when updating LLVM.
    // Value chosen to match UR_NONTAKEN_WEIGHT, see BranchProbabilityInfo.cpp
    return Builder.createBranchWeights((1U << 20) - 1, 1);
}

inline bool hasMetadata(Value *value, const StringRef name) {
    if (auto *const i = dyn_cast<Instruction>(value); i && i->hasMetadata(name)) {
        return true;
    }

    if (auto *const g = dyn_cast<GlobalVariable>(value); g && g->hasMetadata(name)) {
        return true;
    }

    return false;
}

inline MDNode *getMetadata(Value *value, const StringRef name) {
    if (auto *const i = dyn_cast<Instruction>(value); i && i->hasMetadata()) {
        return i->getMetadata(name);
    }

    if (auto *const g = dyn_cast<GlobalVariable>(value); g && g->hasMetadata()) {
        return g->getMetadata(name);
    }

    return nullptr;
}

inline void setMetadata(Value *value, const StringRef name, MDNode *v) {
    if (auto *const i = dyn_cast<Instruction>(value)) {
        i->setMetadata(name, v);
    }

    if (auto *const g = dyn_cast<GlobalVariable>(value)) {
        g->setMetadata(name, v);
    }
}

inline void copyMetadata(Value *src, Value *dest) {
    if (auto *const i = dyn_cast<Instruction>(dest)) {
        if (isa<Instruction>(src)) {
            i->copyMetadata(*cast<Instruction>(src));
        } else if (auto *const d = dyn_cast<GlobalVariable>(src)) {
            SmallVector<std::pair<unsigned, MDNode *>> MDs;
            d->getAllMetadata(MDs);
            for (auto &[kindID, snd]: MDs) {
                MDNode *node = snd;
                i->setMetadata(kindID, node);
            }
        }
    }

    if (auto *const g = dyn_cast<GlobalVariable>(dest)) {
        if (isa<GlobalVariable>(src)) {
            g->copyMetadata(cast<GlobalVariable>(src), 0);
        } else if (const auto *const d = dyn_cast<Instruction>(src)) {
            SmallVector<std::pair<unsigned, MDNode *>> MDs;
            d->getAllMetadata(MDs);
            for (auto &[kindID, snd]: MDs) {
                MDNode *node = snd;
                g->setMetadata(kindID, node);
            }
        }
    }
}

inline void eraseMetadata(Value *value) {
    if (auto *const i = dyn_cast<Instruction>(value)) {
        i->eraseMetadataIf([](unsigned, auto *) { return true; });
    }

    if (auto *const g = dyn_cast<GlobalVariable>(value)) {
        g->eraseMetadataIf([](unsigned, auto *) { return true; });
    }
}


#endif//MDBUILDERUTIL_H
