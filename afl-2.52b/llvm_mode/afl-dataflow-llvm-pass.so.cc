#define AFL_LLVM_PASS

#include "../config.h"
#include "../debug.h"

#include <unistd.h>
#include <unordered_set>

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

namespace {

class AFLDataFlowCoverage : public ModulePass {
    private:
        GlobalVariable *AFLMapPtr;
        ConstantInt *MapSize;

        /**
         * Instrument the \c Use of definition \c Def in the module \c M.
         */
        void instrumentUse(Module &M, Value *Def, Instruction *Use);

    public:
        static char ID;
        AFLDataFlowCoverage() : ModulePass(ID) { }

        bool doInitialization(Module &M) override;
        bool runOnModule(Module &M) override;
};

} /* End anonymous namespace */

char AFLDataFlowCoverage::ID = 0;

/**
 * \brief Get all users of a given value.
 *
 * This essentially constructs the complete "def-use chain" (transitive
 * closure) for the given value.
 *
 * Note: This only performs an intraprocedural analysis!
 */
static std::unordered_set<User*> getUses(Value *Def) {
    std::unordered_set<User*> Uses;

    for (auto *U : Def->users()) {
        SmallVector<User*, 4> Worklist;
        Worklist.push_back(U);

        while (!Worklist.empty()) {
            auto *U = Worklist.pop_back_val();
            Uses.insert(U);

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

void AFLDataFlowCoverage::instrumentUse(Module &M, Value *Def, Instruction *Use) {
    LLVMContext &C = M.getContext();
    IntegerType *Int8Ty = IntegerType::getInt8Ty(C);

    IRBuilder<> IRB(Use);

    /*
     * Load SHM pointer based on the address of the definition modulo the size
     * of the SHM region.
     */

    LoadInst *MapPtr = IRB.CreateLoad(this->AFLMapPtr);
    MapPtr->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
    Value *MapPtrIdx =
        IRB.CreateGEP(MapPtr, IRB.CreateURem(Def, this->MapSize));

    /* Update bitmap */

    LoadInst *Counter = IRB.CreateLoad(MapPtrIdx);
    Counter->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
    Value *Incr = IRB.CreateAdd(Counter, ConstantInt::get(Int8Ty, 1));
    StoreInst *MapUpdate = IRB.CreateStore(Incr, MapPtrIdx);
    MapUpdate->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
}

bool AFLDataFlowCoverage::doInitialization(Module &M) {
    LLVMContext &C = M.getContext();

    IntegerType *Int8Ty = IntegerType::getInt8Ty(C);
    IntegerType *Int32Ty = IntegerType::getInt32Ty(C);

    /* Get globals for the SHM region */

    this->AFLMapPtr =
        new GlobalVariable(M, PointerType::get(Int8Ty, 0), false,
                           GlobalValue::ExternalLinkage, 0, "__afl_area_ptr");
    this->MapSize = ConstantInt::get(Int32Ty, MAP_SIZE);

    return true;
}

bool AFLDataFlowCoverage::runOnModule(Module &M) {
    /* Show a banner */
    char be_quiet = 0;

    if (isatty(2) && !getenv("AFL_QUIET")) {
        SAYF(cCYA "afl-dataflow-llvm-pass " cBRI VERSION cRST " by <adrian.herrera02@gmail.com>\n");
    } else {
        be_quiet = 1;
    }

    TargetLibraryInfoImpl TLII;
    TargetLibraryInfo TLI(TLII);

    /*
     * Instrument all the things!
     *
     * The data-flow instrumentation works as follows:
     *
     *   1. Collect the following definitions:
     *     a) Dynamically-allocated arrays (via malloc)
     *     b) Stack-based static arrays
     *     c) Global non-constant static arrays
     *   2. Find all the uses of the definitions calculated in 1.
     *   3. If the use is a load instruction (i.e., dereference), then
     *      instrument it
     *
     * The instrumentation is very similar to AFL's code coverage
     * instrumentation (i.e., it increments a counter associated with that
     * instrumentation point). However, instead of using a hash of the previous
     * instrumentation point to index into the SHM region, we instead take the
     * address of the definition (i.e., step 1. above) modulo the size of the
     * SHM region.
     */

    unsigned numDefs = 0;
    unsigned numUses = 0;

    for (auto &F : M.functions()) {
        for (auto I = inst_begin(F); I != inst_end(F); ++I) {
            /* Instrument uses of dynamically-allocated arrays */
            if (auto *Call = dyn_cast<CallInst>(&*I)) {
                if (isMallocOrCallocLikeFn(Call, &TLI)) {
                    numDefs++;

                    for (auto *U : getUses(Call)) {
                        if (auto *Load = dyn_cast<LoadInst>(U)) {
                            instrumentUse(M, Call, Load);
                            numUses++;
                        }
                    }
                }
            /* Instrument uses of stack-based statically-allocated arrays */
            } else if (auto *Alloca = dyn_cast<AllocaInst>(&*I)) {
                Type *ElemTy = Alloca->getType()->getElementType();
                if (isa<SequentialType>(ElemTy)) {
                    numDefs++;

                    for (auto *U : getUses(Alloca)) {
                        if (auto *Load = dyn_cast<LoadInst>(U)) {
                            instrumentUse(M, Alloca, Load);
                            numUses++;
                        }
                    }
                }
            }
        }
    }

    /* Instrument uses of global statically-allocated arrays */
    for (auto &G : M.globals()) {
        Type *ElemTy = G.getType()->getElementType();
        if (!G.isConstant() && isa<SequentialType>(ElemTy)) {
            numDefs++;

            for (auto *U : getUses(&G)) {
                if (auto *Load = dyn_cast<LoadInst>(U)) {
                    instrumentUse(M, &G, Load);
                    numUses++;
                }
            }
        }
    }

    /* Say something nice */

    if (!be_quiet) {
        if (!numDefs) {
            WARNF("No definitions to instrument found.");
        } else {
            OKF("Instrumented %u definition(s) and %u use(s).",
                numDefs, numUses);
        }
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
