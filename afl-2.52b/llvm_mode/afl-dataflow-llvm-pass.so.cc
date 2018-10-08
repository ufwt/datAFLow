#define AFL_LLVM_PASS

#include "../config.h"
#include "../debug.h"

#include <unistd.h>
#include <vector>

#include "llvm/Pass.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

using namespace llvm;

namespace {

class AFLDataFlowCoverage : public ModulePass {
    public:
        static char ID;
        AFLDataFlowCoverage() : ModulePass(ID) { }

        bool runOnModule(Module &M) override;
};

} // annonymous namespace

char AFLDataFlowCoverage::ID = 0;

/**
 * \brief Get all users of a given value.
 *
 * This essentially constructs the "def-use chain" for the given value.
 */
static std::vector<User*> getUses(Value *Def) {
    std::vector<User*> Uses;

    for (auto *U : Def->users()) {
        SmallVector<User*, 4> Worklist;
        Worklist.push_back(U);

        while (!Worklist.empty()) {
            auto *U = Worklist.pop_back_val();
            Uses.push_back(U);

            /*
             * Store instructions are a special case.
             *
             * Look at the users of the memory address that is written to
             * (i.e., the pointer operand).
             */
            if (auto *StoreInst = dyn_cast<llvm::StoreInst>(U)) {
                auto *StorePtr = StoreInst->getPointerOperand();
                for (auto *StoreUser : StorePtr->users()) {
                    if (StoreUser != StoreInst) {
                        Worklist.push_back(StoreUser);
                    }
                }
            } else {
                Worklist.append(U->user_begin(), U->user_end());
            }
        }
    }

    return Uses;
}

bool AFLDataFlowCoverage::runOnModule(Module &M) {
    /* Show a banner */
    char be_quiet = 0;

    if (isatty(2) && !getenv("AFL_QUIET")) {
        SAYF(cCYA "afl-dataflow-llvm-pass " cBRI VERSION cRST " by <adrian.herrera02@gmail.com>\n");
    } else {
        be_quiet = 1;
    }

    LLVMContext &C = M.getContext();
    const DataLayout &DL = M.getDataLayout();

    /* malloc reference */
    Type *BPTy = Type::getInt8PtrTy(C);
    IntegerType *IntPtrTy = DL.getIntPtrType(C);
    const Value *MallocFunc = M.getOrInsertFunction("malloc", BPTy, IntPtrTy);

    /* Instrument all the things! */

    for (auto &F : M.functions()) {
        for (auto I = inst_begin(F); I != inst_end(F); ++I) {
            /* Get the def-use chain for dynamically-allocated arrays */
            if (auto *CallInst = dyn_cast<llvm::CallInst>(&*I)) {
                if (CallInst->getCalledFunction() == MallocFunc) {
                    llvm::outs() << "def-use chain for dynamic array - " << *CallInst << "\n";
                    for (auto *I : getUses(CallInst)) {
                        llvm::outs() << "    - " << *I << "\n";
                    }
                }
            /* Get the def-use chain for local statically-allocated arrays */
            } else if (auto *AllocaInst = dyn_cast<llvm::AllocaInst>(&*I)) {
                Type *ElemTy = AllocaInst->getType()->getElementType();
                if (isa<SequentialType>(ElemTy)) {
                    llvm::outs() << "def-use chain for static array - " << *AllocaInst << "\n";
                    for (auto *I : getUses(AllocaInst)) {
                        llvm::outs() << "    - " << *I << "\n";
                    }
                }
            }
        }
    }

    /* Get the def-use chain for global statically-allocated arrays */
    for (auto &G : M.globals()) {
        Type *ElemTy = G.getType()->getElementType();
        if (isa<SequentialType>(ElemTy)) {
            llvm::outs() << "def-use chain for global static array - " << G << "\n";
            for (auto *I : getUses(&G)) {
                llvm::outs() << "    - " << *I << "\n";
            }
        }
    }

    /* Say something nice */

    if (!be_quiet) {
    }

    return true;
}

static RegisterPass<AFLDataFlowCoverage> AFLDataFlowPass(
        "afl-dataflow-coverage", "AFL data-flow coverage pass");

static void registerAFLDataFlowPass(const PassManagerBuilder &,
                                    legacy::PassManagerBase &PM) {
    PM.add(new AFLDataFlowCoverage());
}

static RegisterStandardPasses RegisterAFLDFPass(
        PassManagerBuilder::EP_OptimizerLast, registerAFLDataFlowPass);

static RegisterStandardPasses RegisterAFLDFPass0(
        PassManagerBuilder::EP_EnabledOnOptLevel0, registerAFLDataFlowPass);
