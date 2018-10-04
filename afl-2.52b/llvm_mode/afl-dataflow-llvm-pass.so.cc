#define AFL_LLVM_PASS

#include "../config.h"
#include "../debug.h"

#include <unistd.h>

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

bool AFLDataFlowCoverage::runOnModule(Module &M) {
    /* Show a banner */
    char be_quiet = 0;

    if (isatty(2) && !getenv("AFL_QUIET")) {
        SAYF(cCYA "afl-dataflow-llvm-pass " cBRI VERSION cRST " by <adrian.herrera02@gmail.com>\n");
    } else {
        be_quiet = 1;
    }

    /* Instrument all the things! */

    for (auto &F : M) {
        for (auto I = inst_begin(F); I != inst_end(F); ++I) {
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
