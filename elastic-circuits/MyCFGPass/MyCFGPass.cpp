/**
 * Elastic Pass
 *
 * Forming a netlist of dataflow components out of the LLVM IR
 *
 */

#include <cassert>

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "ElasticPass/CircuitGenerator.h"
#include "ElasticPass/Head.h"
#include "ElasticPass/Nodes.h"
#include "ElasticPass/Pragmas.h"
#include "ElasticPass/Utils.h"
#include "MyCFGPass/MyCFGPass.h"
#include "OptimizeBitwidth/OptimizeBitwidth.h"

#include <sys/time.h>
#include <time.h>

static cl::opt<bool> opt_useLSQ("use-lsq", cl::desc("Emit LSQs where applicable"), cl::Hidden,
                                cl::init(true), cl::Optional);

static cl::opt<std::string> opt_cfgOutdir("cfg-outdir", cl::desc("Output directory of MyCFGPass"),
                                          cl::Hidden, cl::init("."), cl::Optional);

static cl::opt<bool> opt_buffers("simple-buffers", cl::desc("Naive buffer placement"), cl::Hidden,
                                 cl::init(false), cl::Optional);

//--------------------------------------------------------//
// Added for DASS compiler, by Jianyi
static cl::opt<bool> opt_ptrToReg("ptr2reg", cl::desc("Replace pointers to registers"), cl::Hidden,
                                  cl::init(true), cl::Optional);
static cl::opt<bool> opt_exportDot("export-dot", cl::desc("Export dot graph"), cl::Hidden,
                                   cl::init(false), cl::Optional);
static cl::opt<bool> opt_optIf("if-conversion", cl::desc("Optimize if statements"), cl::Hidden,
                               cl::init(false), cl::Optional);
//--------------------------------------------------------//

struct timeval start, end;

std::string fname;
static int indx_fname = 0;

void set_clock() { gettimeofday(&start, NULL); }

double elapsed_time() {
    gettimeofday(&end, NULL);

    double elapsed = (end.tv_sec - start.tv_sec);
    elapsed += (double)(end.tv_usec - start.tv_usec) / 1000000.0;
    return elapsed;
}

void MyCFGPass::getAnalysisUsage(AnalysisUsage& AU) const {
    // 			  AU.setPreservesCFG();
    AU.addRequired<OptimizeBitwidth>();
    AU.addRequired<MemElemInfoPass>();
}

void MyCFGPass::compileAndProduceDOTFile(Function& F) {
    fname = demangleFuncName(F.getName().str().c_str());

    // main is used for frequency extraction, we do not use its dataflow graph
    if (fname == "main")
        return;

    bool done = false;

    auto M = F.getParent();

    pragmas(0, M->getModuleIdentifier());

    bbnode_dag           = new std::vector<BBNode*>;
    enode_dag            = new std::vector<ENode*>;
    OptimizeBitwidth* OB = &getAnalysis<OptimizeBitwidth>();
    MemElemInfo& MEI     = getAnalysis<MemElemInfoPass>().getInfo();

    //----- Internally constructs elastic circuit and adds elastic components -----//

    // Naively building circuit
    CircuitGenerator* circuitGen = new CircuitGenerator(enode_dag, bbnode_dag, OB, MEI);

    circuitGen->buildDagEnodes(F);

    circuitGen->fixBuildDagEnodes();

    circuitGen->formLiveBBnodes();

    // Jianyi 04.01.2021: Here to add optimization for pointers: replace pointer with IO
    // ports
    const_val_vec pointers;
    if (opt_ptrToReg)
        circuitGen->replacePointersWithRegisters(F, &pointers);

    circuitGen->removeRedundantBeforeElastic(bbnode_dag, enode_dag);

    circuitGen->setGetelementPtrConsts(enode_dag);

    circuitGen->addPhi();

    // printDotDFG(enode_dag, bbnode_dag, opt_cfgOutdir + "/_build/" + fname + "/" + fname + ".dot",
    //             done);

    circuitGen->phiSanityCheck(enode_dag);

    circuitGen->addFork();

    // Jianyi 04.01.2021: Here to add optimization for pointers: merge call net into a call
    // op
    if (opt_ptrToReg)
        circuitGen->mergeCallTail(&pointers);

    circuitGen->forkSanityCheck(enode_dag);

    circuitGen->addBranch();

    circuitGen->branchSanityCheck(enode_dag);

    if (get_pragma_generic("USE_CONTROL")) {
        circuitGen->addControl();
    }

    circuitGen->addMemoryInterfaces(opt_useLSQ);
    circuitGen->addControl();

    circuitGen->addSink();
    circuitGen->addSource();

    // circuitGen->setSizes();

    set_clock();
    if (opt_buffers)
        circuitGen->addBuffersSimple();

    printf("Time elapsed: %.4gs.\n", elapsed_time());
    fflush(stdout);

    std::string dbgInfo("a");

    circuitGen->setMuxes();

    circuitGen->setBBIds();

    circuitGen->setFreqs(F.getName());

    circuitGen->removeRedundantAfterElastic(enode_dag);

    done = true;

    fflush(stdout);

    circuitGen->sanityCheckVanilla(enode_dag);

    // Jianyi 03.03.2022: Here to add branch successor BBs for backend printing
    circuitGen->addBranchConstraints(&F);

    // Jianyi 15.06.2021: Here to add elastic triggers for calls without input edges
    circuitGen->addControlForCall();

    // Jianyi 08.07.2021: Here to optimize the if statement so independent variables are not stalled
    // by the if condition
    if (opt_optIf) {
        circuitGen->optimizeIfStmt(&F);
        circuitGen->removeRedundantAfterElastic(enode_dag);
    }

    // Jianyi 10.10.2021: Here to optimize the loops so loop invariants are directly forwarded to
    // the next basic block instead of going through basic blocks with the control path
    circuitGen->optimizeLoops(&F);
    circuitGen->removeRedundantAfterElastic(enode_dag);

    // Jianyi 15.10.2021: Here to insert the loop interchangers
    circuitGen->insertLoopInterchangers(&F);

    // Jianyi 1.1.2022: Here to parallelise loops
    circuitGen->paralleliseLoops(&F);

    // Jianyi 07.01.2021: Here to shift blocks to ones starting from 1
    circuitGen->shiftBlockID();

    // Jianyi 03.01.2022: Add name check which may cause error in output dataflow graph
    assert(!nodeNameCheck(enode_dag));

    if (opt_exportDot) {
        printDotDFG(enode_dag, bbnode_dag, opt_cfgOutdir + "/" + fname + ".dot", done);
        printDotCFG(bbnode_dag, (opt_cfgOutdir + "/" + fname + "_bbgraph.dot").c_str());
    }
}

char MyCFGPass::ID = 1;

static RegisterPass<MyCFGPass> Z("mycfgpass", "Creates new CFG pass",
                                 false, // modifies the CFG!
                                 false);

/* for clang pass registration
 */
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

static void registerClangPass(const PassManagerBuilder&, legacy::PassManagerBase& PM) {
    PM.add(new MyCFGPass());
}

static RegisterStandardPasses RegisterClangPass(PassManagerBuilder::EP_EarlyAsPossible,
                                                registerClangPass);

bool fileExists(const char* fileName) {
    FILE* file = NULL;
    if ((file = fopen(fileName, "r")) != NULL) {
        fclose(file);
        return true;
    } else
        return false;
}
