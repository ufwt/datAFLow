#define AFL_LLVM_PASS

#include "../config.h"
#include "../debug.h"

#include <unistd.h>
#include <vector>

#include "llvm/Analysis/EscapeAnalysis.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

using namespace llvm;

#define PTR_BITS 16ULL

namespace {

class AFLowCoverage : public ModulePass {
    private:
        GlobalVariable *AFLMapPtr;
        ConstantInt *MapSize;

    public:
        static char ID;
        AFLowCoverage() : ModulePass(ID) { }

        bool doInitialization(Module &M) override;
        bool runOnModule(Module &M) override;

        void getAnalysisUsage(AnalysisUsage &AU) const override;
};

} /* End anonymous namespace */

/**
 * Create a fat pointer by storing `Data` in the upper `PTR_BITS` bits of the
 * given `Ptr`.
 */
static Value *createFatPointer(IRBuilder<> &IRB, Value *Ptr, Value *Data) {
    IntegerType *Int64Ty = IRB.getInt64Ty();

    Value *Upper = IRB.CreateShl(Data, PTR_BITS);
    Value *Lower = IRB.CreatePtrToInt(Ptr, Int64Ty);
    Value *Combined = IRB.CreateOr(Lower, Upper);

    return IRB.CreateIntToPtr(Combined, Ptr->getType());
}

char AFLowCoverage::ID = 0;

bool AFLowCoverage::doInitialization(Module &M) {
    LLVMContext &C = M.getContext();

    IntegerType *Int8Ty = IntegerType::getInt8Ty(C);
    IntegerType *Int32Ty = IntegerType::getInt32Ty(C);

    /* Get globals for the SHM region */

    this->AFLMapPtr =
        new GlobalVariable(M, PointerType::get(Int8Ty, 0), false,
                           GlobalValue::ExternalLinkage, 0, "__afl_area_ptr");
    this->MapSize = ConstantInt::get(Int32Ty, MAP_SIZE);

    return false;
}

bool AFLowCoverage::runOnModule(Module &M) {
    /* Show a banner */
    char be_quiet = 0;

    if (isatty(2) && !getenv("AFL_QUIET")) {
        SAYF(cCYA "aflow-llvm-pass " cBRI VERSION cRST " by <adrian.herrera02@gmail.com>\n");
    } else {
        be_quiet = 1;
    }

    TargetLibraryInfoImpl TLII;
    TargetLibraryInfo TLI(TLII);

    LLVMContext &C = M.getContext();

    IntegerType *Int64Ty = IntegerType::getInt64Ty(C);

    /* Instrument all the things! */

    unsigned numDefs = 0;

    for (auto &F : M.functions()) {
        /* Skip if declared outside of this module */
        if (F.isDeclaration()) {
            continue;
        }

        // TODO Escape analysis does not seem to work correctly :(
        auto const &EI = getAnalysis<EscapeAnalysisPass>(F).getEscapeInfo();

        for (auto I = inst_begin(F); I != inst_end(F); ++I) {
            /* Instrument uses of dynamically-allocated arrays */
            if (auto *Call = dyn_cast<CallInst>(&*I)) {
                if (isMallocOrCallocLikeFn(Call, &TLI)) {
                    llvm::outs() << "Found malloc -> " << *Call << "\n";

                    // TODO ideally perform escape analysis on malloc'd data
                    // and only transform to fat pointer if it escapes

                    /* Cache uses before creating more */
                    std::vector<User*> Users(I->user_begin(), I->user_end());

                    uint16_t def_id = AFL_R(MAP_SIZE);
                    ConstantInt *DefId = ConstantInt::get(Int64Ty, def_id);

                    // TODO check NextNode != NULL
                    IRBuilder<> IRB(Call->getNextNode());

                    Value *FatPtr = createFatPointer(IRB, Call, DefId);

                    /* Replace uses with the fat pointer */
                    for (User *U : Users) {
                        U->replaceUsesOfWith(Call, FatPtr);
                    }

                    numDefs++;
                }
            }
        }
    }

    /* Say something nice */

    if (!be_quiet) {
        if (!numDefs) {
            WARNF("No definitions to instrument found.");
        } else {
            OKF("Instrumented %u definition(s).", numDefs);
        }
    }

    return true;
}

void AFLowCoverage::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<EscapeAnalysisPass>();
}

static RegisterPass<AFLowCoverage> AFLowPass(
        "aflow-coverage", "AFL data-flow coverage pass", false, false);

static void registerAFLowPass(const PassManagerBuilder &,
                              legacy::PassManagerBase &PM) {
    PM.add(new AFLowCoverage());
}

static RegisterStandardPasses RegisterAFLDFPass(
        PassManagerBuilder::EP_OptimizerLast, registerAFLowPass);

static RegisterStandardPasses RegisterAFLDFPass0(
        PassManagerBuilder::EP_EnabledOnOptLevel0, registerAFLowPass);
