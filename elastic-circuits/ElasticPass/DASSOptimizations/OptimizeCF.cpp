#include "llvm/ADT/APInt.h"
#include "llvm/IR/Metadata.h"

#include "ElasticPass/CircuitGenerator.h"
#include "ElasticPass/DASS/DASSUtils.h"
#include "ElasticPass/Utils.h"

#include <algorithm>

//--------------------------------------------------------//
// Function: addBranchConstraints
// Initialise branch constraint for back end printing
//--------------------------------------------------------//

void CircuitGenerator::addBranchConstraints(Function* F) {
    for (auto BB = F->begin(); BB != F->end(); BB++) {
        auto block      = dyn_cast<BasicBlock>(BB);
        auto branchInst = dyn_cast<BranchInst>(block->getTerminator());
        if (!branchInst)
            continue;
        auto trueBB  = branchInst->getSuccessor(0);
        auto falseBB = (branchInst->isUnconditional()) ? nullptr : branchInst->getSuccessor(1);
        for (auto enode : *enode_dag) {
            if (enode->BB == block && (enode->type == Branch_c || enode->type == Branch_n)) {
                enode->trueBB  = trueBB;
                enode->falseBB = falseBB;
                if (!falseBB)
                    continue;
                ENode *n0, *n1;
                if (enode->type == Branch_c) {
                    n0 = enode->JustCntrlSuccs->at(0);
                    n1 = enode->JustCntrlSuccs->at(1);
                } else {
                    n0 = enode->CntrlSuccs->at(0);
                    n1 = enode->CntrlSuccs->at(1);
                }
                if (n0->type == Sink_) {
                    if (n1->BB == trueBB)
                        enode->falseBB = nullptr;
                    else
                        enode->trueBB = nullptr;
                } else if (n1->type == Sink_) {
                    if (n0->BB == trueBB)
                        enode->falseBB = nullptr;
                    else
                        enode->trueBB = nullptr;
                }
            }
        }
    }
    // for (auto enode : *enode_dag) {
    //     if (enode->BB && dyn_cast<BranchInst>(enode->BB->getTerminator()) &&
    //         dyn_cast<BranchInst>(enode->BB->getTerminator())->isConditional() &&
    //         (enode->type == Branch_c || enode->type == Branch_n)) {
    //         llvm::errs() << getNodeDotNameNew(enode) << ", " << enode->BB->getName() << " : ";
    //         if (enode->trueBB)
    //             llvm::errs() << enode->trueBB->getName();
    //         else
    //             llvm::errs() << "NULL";
    //         llvm::errs() << "; ";
    //         if (enode->falseBB)
    //             llvm::errs() << enode->falseBB->getName();
    //         else
    //             llvm::errs() << "NULL";
    //         llvm::errs() << "\n";
    //     }
    // }
}

//--------------------------------------------------------//
// Function: optimizeIfStmt
// Analyse if statement. If it does not touch memory, then directly forward its control branch
// signal from the condition block to the merged block.
//--------------------------------------------------------//

struct IfBlock {
    BasicBlock *condBB, *trueStart, *trueEnd, *falseStart, *falseEnd, *exitBB;

    IfBlock(BasicBlock* condB, BasicBlock* trueB, BasicBlock* falseB, BasicBlock* exitB) {
        condBB     = condB;
        trueStart  = trueB;
        trueEnd    = trueB;
        falseStart = falseB;
        falseEnd   = falseB;
        exitBB     = exitB;
    }
};

static bool touchMemory(BasicBlock* BB, ENode_vec* enode_dag) {
    if (!BB)
        return false;
    for (auto enode : *enode_dag)
        if (enode->Instr && enode->type == Inst_ && enode->BB == BB)
            if (isa<LoadInst>(enode->Instr) || isa<StoreInst>(enode->Instr))
                return true;

    return false;
}

static std::vector<IfBlock*> getInnerMostIfBlocks(Function* F, ENode_vec* enode_dag) {
    std::vector<IfBlock*> ifBlocks;
    for (auto BB = F->begin(); BB != F->end(); BB++) {
        auto branchInst = dyn_cast<BranchInst>(BB->getTerminator());
        if (!branchInst || !branchInst->isConditional())
            continue;

        auto bb0 = branchInst->getSuccessor(0);
        auto bb1 = branchInst->getSuccessor(1);
        auto br0 = dyn_cast<BranchInst>(bb0->getTerminator());
        auto br1 = dyn_cast<BranchInst>(bb1->getTerminator());

        // TODO: What if the exit block terminates with ret?
        if (!br0 || !br1)
            continue;

        // We are looking for inner most if statements, which has the structure of:
        // BB1 -> BB2 (+ BB3) -> BB4. At least one of the successor should have unconditional branch
        if (br0->isConditional() && br1->isConditional())
            continue;
        if (!br0->isConditional() && !br1->isConditional() &&
            br0->getSuccessor(0) != br1->getSuccessor(0))
            continue;

        BasicBlock *trueBB, *falseBB, *exitBB;
        if (!br0->isConditional()) {
            trueBB = bb0;
            exitBB = br0->getSuccessor(0);
            if (br1->isConditional() && exitBB != bb1)
                continue;
            if (!br1->isConditional() && br1->getSuccessor(0) != exitBB)
                continue;
            if (!br1->isConditional() && br1->getSuccessor(0) == exitBB)
                falseBB = bb1;
        } else {
            exitBB = bb0;
            if (br1->getSuccessor(0) != exitBB)
                continue;
            falseBB = bb1;
        }
        if (touchMemory(trueBB, enode_dag) || touchMemory(falseBB, enode_dag))
            continue;

        IfBlock* ifBlock = new IfBlock(&*BB, trueBB, falseBB, exitBB);
        ifBlocks.push_back(ifBlock);
    }
    return ifBlocks;
}

static bool isLegalToDestroy(ENode* enode, IfBlock* ifBlock) {
    for (auto succ : *enode->CntrlSuccs) {
        if (ifBlock->exitBB == succ->BB)
            continue;

        if (succ->type == Sink_)
            return false;

        if (succ->BB != ifBlock->trueStart && succ->BB != ifBlock->falseStart) {
            llvm::errs() << getNodeDotNameNew(succ) << "\n";
            llvm_unreachable(" does not match any of the block successors.");
        }

        if (succ->type == Phi_n && succ->CntrlSuccs->size() == 1) {
            auto nextBranch = succ->CntrlSuccs->at(0);
            if (nextBranch->type == Branch_n && (nextBranch->CntrlSuccs->at(0)->type == Sink_ ||
                                                 nextBranch->CntrlSuccs->at(1)->type == Sink_)) {
                auto exitPhi = (nextBranch->CntrlSuccs->at(0)->type == Sink_)
                                   ? nextBranch->CntrlSuccs->at(1)
                                   : nextBranch->CntrlSuccs->at(0);
                if (exitPhi->type == Phi_n && exitPhi->BB == ifBlock->exitBB)
                    continue;
            }
        }
        return false;
    }
    return true;
}

static void insertFIFOAfterPhi(ENode* enode, int depth, ENode_vec* enode_dag) {
    if (enode->type == Phi_ &&
        (enode->CntrlSuccs->size() != 1 || enode->JustCntrlSuccs->size() != 0))
        llvm_unreachable(
            std::string("Invalid node for inserting FIFOs: " + getNodeDotNameNew(enode)).c_str());
    if (enode->type == Phi_c &&
        (enode->CntrlSuccs->size() > 1 || enode->JustCntrlSuccs->size() > 1))
        llvm_unreachable(
            std::string("Invalid node for inserting FIFOs: " + getNodeDotNameNew(enode)).c_str());

    auto fifoNode       = new ENode(Inst_, enode->BB);
    fifoNode->Name      = "DASSFIFO_T" + std::to_string(depth);
    fifoNode->Instr     = enode->Instr;
    fifoNode->isMux     = false;
    fifoNode->isCntrlMg = false;
    fifoNode->depth     = depth;
    fifoNode->id        = enode_dag->size();
    fifoNode->bbNode    = enode->bbNode;
    fifoNode->bbId      = enode->bbId;
    enode_dag->push_back(fifoNode);

    if (enode->type == Phi_) {
        fifoNode->CntrlSuccs->push_back(enode->CntrlSuccs->front());
        fifoNode->CntrlPreds->push_back(enode);
        enode->CntrlSuccs->front()->CntrlPreds->erase(
            std::find(enode->CntrlSuccs->front()->CntrlPreds->begin(),
                      enode->CntrlSuccs->front()->CntrlPreds->end(), enode));
        enode->CntrlSuccs->front()->CntrlPreds->push_back(fifoNode);
        enode->CntrlSuccs->clear();
        enode->CntrlSuccs->push_back(fifoNode);
    } else {
        fifoNode->JustCntrlSuccs->push_back(enode->JustCntrlSuccs->front());
        fifoNode->JustCntrlPreds->push_back(enode);
        enode->JustCntrlSuccs->front()->JustCntrlPreds->erase(
            std::find(enode->JustCntrlSuccs->front()->JustCntrlPreds->begin(),
                      enode->JustCntrlSuccs->front()->JustCntrlPreds->end(), enode));
        enode->JustCntrlSuccs->front()->JustCntrlPreds->push_back(fifoNode);
        enode->JustCntrlSuccs->clear();
        enode->JustCntrlSuccs->push_back(fifoNode);

        // auto cntrlFifoNode       = new ENode(Inst_, enode->BB);
        // cntrlFifoNode->Name      = "DASSFIFO_D" + std::to_string(depth);
        // cntrlFifoNode->Instr     = enode->Instr;
        // cntrlFifoNode->isMux     = false;
        // cntrlFifoNode->isCntrlMg = true;
        // cntrlFifoNode->depth     = depth;
        // cntrlFifoNode->id        = enode_dag->size();
        // cntrlFifoNode->bbNode    = enode->bbNode;
        // cntrlFifoNode->bbId      = enode->bbId;
        // enode_dag->push_back(cntrlFifoNode);

        // cntrlFifoNode->CntrlSuccs->push_back(enode->CntrlSuccs->front());
        // cntrlFifoNode->CntrlPreds->push_back(enode);
        // enode->CntrlSuccs->front()->CntrlPreds->erase(
        //     std::find(enode->CntrlSuccs->front()->CntrlPreds->begin(),
        //               enode->CntrlSuccs->front()->CntrlPreds->end(), enode));
        // enode->CntrlSuccs->front()->CntrlPreds->push_back(cntrlFifoNode);
        // enode->CntrlSuccs->clear();
        // enode->CntrlSuccs->push_back(cntrlFifoNode);
    }
}

static ENode* createShortPathIf(IfBlock* ifBlock, ENode_vec* enode_dag, BBNode_vec* bbnode_dag) {

    auto BB = ifBlock->condBB;
    // Get control phi at the conditional block
    auto phiC = getPhiC(BB, enode_dag);
    assert(phiC->JustCntrlSuccs->size() > 0);
    phiC = insertFork(phiC, enode_dag, true);

    auto condBranch = getBranchC(BB, enode_dag);
    assert(condBranch->JustCntrlPreds->size() == 2);
    auto cond0 = condBranch->JustCntrlPreds->at(0);
    if (cond0->type == Fork_c || cond0->type == Fork_)
        cond0 = (cond0->CntrlPreds->size() > 0) ? cond0->CntrlPreds->at(0)
                                                : cond0->JustCntrlPreds->at(0);
    auto cond1 = condBranch->JustCntrlPreds->at(1);
    if (cond1->type == Fork_c || cond1->type == Fork_)
        cond1 = (cond1->CntrlPreds->size() > 0) ? cond1->CntrlPreds->at(0)
                                                : cond1->JustCntrlPreds->at(0);
    assert(cond0->type == Branch_ || cond1->type == Branch_);
    auto cond = (cond0->type == Branch_) ? condBranch->JustCntrlPreds->at(0)
                                         : condBranch->JustCntrlPreds->at(1);

    // Create a short path driver
    auto cstNode         = new ENode(Branch_, "brCst", phiC->BB);
    cstNode->bbNode      = phiC->bbNode;
    cstNode->id          = enode_dag->size();
    cstNode->bbId        = phiC->bbId;
    cstNode->isShortPath = true;
    enode_dag->push_back(cstNode);
    cstNode->JustCntrlPreds->push_back(phiC);
    phiC->JustCntrlSuccs->push_back(cstNode);
    cstNode = insertFork(cstNode, enode_dag, false);

    // Create a short path control branch
    auto branchC    = new ENode(Branch_c, "branchC", phiC->BB);
    branchC->bbNode = phiC->bbNode;
    branchC->id     = enode_dag->size();
    branchC->bbId   = phiC->bbId;
    enode_dag->push_back(branchC);
    branchC->condition = cstNode;
    branchC->CntrlPreds->push_back(cstNode);
    cstNode->CntrlSuccs->push_back(branchC);
    branchC->JustCntrlPreds->push_back(phiC);
    phiC->JustCntrlSuccs->push_back(branchC);
    // One constant sink dummy
    ENode* branchSinkNode = new ENode(Sink_, "sink");
    branchSinkNode->id    = enode_dag->size();
    branchSinkNode->bbId  = 0;
    enode_dag->push_back(branchSinkNode);
    branchSinkNode->JustCntrlPreds->push_back(branchC);
    branchC->JustCntrlSuccs->push_back(branchSinkNode);
    branchC->branchFalseOutputSucc = branchSinkNode;

    // Get the control phi at the exit block
    auto exitPhiC = getPhiC(ifBlock->exitBB, enode_dag);
    assert(exitPhiC->CntrlSuccs->size() <= 1);

    // Create a short path decision branch to synchronize the sequence from two branches
    if (exitPhiC->CntrlSuccs->size() == 1) {
        auto selectBranch       = new ENode(Branch_n, "branch", phiC->BB);
        selectBranch->bbNode    = phiC->bbNode;
        selectBranch->id        = enode_dag->size();
        selectBranch->bbId      = phiC->bbId;
        selectBranch->condition = cstNode;
        enode_dag->push_back(selectBranch);
        selectBranch->CntrlPreds->push_back(cstNode);
        cstNode->CntrlSuccs->push_back(selectBranch);
        selectBranch->CntrlPreds->push_back(cond);
        cond->CntrlSuccs->push_back(selectBranch);
        exitPhiC->CntrlSuccs->at(0)->CntrlPreds->push_back(selectBranch);
        selectBranch->CntrlSuccs->push_back(exitPhiC->CntrlSuccs->at(0));
        selectBranch->branchTrueOutputSucc = exitPhiC->CntrlSuccs->at(0);
        selectBranch->trueBB               = exitPhiC->BB;
        // One constant sink dummy
        ENode* selectBranchSinkNode = new ENode(Sink_, "sink");
        selectBranchSinkNode->id    = enode_dag->size();
        selectBranchSinkNode->bbId  = 0;
        enode_dag->push_back(selectBranchSinkNode);
        selectBranchSinkNode->CntrlPreds->push_back(selectBranch);
        selectBranch->CntrlSuccs->push_back(selectBranchSinkNode);
        selectBranch->branchFalseOutputSucc = selectBranchSinkNode;
    }

    // Create a new control phi at the exit block and take over the control output
    auto newPhiC       = new ENode(Phi_c, "phiC", exitPhiC->BB);
    newPhiC->isCntrlMg = false;
    newPhiC->bbNode    = exitPhiC->bbNode;
    newPhiC->id        = enode_dag->size();
    newPhiC->bbId      = exitPhiC->bbId;
    enode_dag->push_back(newPhiC);
    newPhiC->JustCntrlSuccs =
        new ENode_vec(exitPhiC->JustCntrlSuccs->begin(), exitPhiC->JustCntrlSuccs->end());
    exitPhiC->JustCntrlSuccs->clear();
    for (auto succ : *newPhiC->JustCntrlSuccs) {
        auto iter = std::find(succ->JustCntrlPreds->begin(), succ->JustCntrlPreds->end(), exitPhiC);
        if (iter != succ->JustCntrlPreds->end()) {
            succ->JustCntrlPreds->erase(iter);
            succ->JustCntrlPreds->push_back(newPhiC);
        }
        iter = std::find(succ->CntrlPreds->begin(), succ->CntrlPreds->end(), exitPhiC);
        if (iter != succ->CntrlPreds->end()) {
            succ->CntrlPreds->erase(iter);
            succ->CntrlPreds->push_back(newPhiC);
        }
    }
    newPhiC->JustCntrlPreds->push_back(branchC);
    branchC->JustCntrlSuccs->push_back(newPhiC);
    branchC->branchTrueOutputSucc = newPhiC;

    eraseNode(exitPhiC, enode_dag);
    // Remove the branchC in both branches - as we directly forward the control signal from the
    // conditional block to the exit block
    if (ifBlock->trueStart) {
        auto trueBranch = getBranchC(ifBlock->trueStart, enode_dag);
        eraseNode(trueBranch, enode_dag);
    }
    if (ifBlock->falseStart) {
        auto falseBranch = getBranchC(ifBlock->falseStart, enode_dag);
        eraseNode(falseBranch, enode_dag);
    }

    return cstNode;
}

static void replaceWithShortCut(ENode* branchNode, ENode* brCst, ENode_vec* enode_dag,
                                BasicBlock* exitBB) {
    ENode* select;
    for (auto pred : *branchNode->CntrlPreds) {
        auto in = (pred->type == Fork_) ? pred->CntrlPreds->at(0) : pred;
        if (in->type == Branch_ || (in->type == Inst_ && isa<CmpInst>(in->Instr))) {
            select = pred;
            break;
        }
    }
    assert(select);

    branchNode->CntrlPreds->erase(
        std::find(branchNode->CntrlPreds->begin(), branchNode->CntrlPreds->end(), select));
    select->CntrlSuccs->erase(
        std::find(select->CntrlSuccs->begin(), select->CntrlSuccs->end(), branchNode));
    branchNode->CntrlPreds->push_back(brCst);
    brCst->CntrlSuccs->push_back(branchNode);

    ENode* exitPhi;
    for (auto succ : *branchNode->CntrlSuccs) {
        if (exitBB == succ->BB)
            continue;

        auto phi    = succ;
        auto branch = phi->CntrlSuccs->at(0);
        if (branch->CntrlSuccs->at(0)->type == Sink_)
            exitPhi = branch->CntrlSuccs->at(1);
        else
            exitPhi = branch->CntrlSuccs->at(0);

        eraseNode(phi, enode_dag);
        eraseNode(branch, enode_dag);
    }

    ENode* branchSinkNode = new ENode(Sink_, "sink");
    branchSinkNode->id    = enode_dag->size();
    branchSinkNode->bbId  = 0;
    enode_dag->push_back(branchSinkNode);

    branchNode->CntrlSuccs->push_back(branchSinkNode);
    branchSinkNode->CntrlPreds->push_back(branchNode);

    if (branchNode->CntrlSuccs->size() == 2)
        return;

    branchNode->CntrlSuccs->push_back(exitPhi);
    exitPhi->CntrlPreds->push_back(branchNode);
    assert(exitPhi->CntrlPreds->size() == 1);
    return;
}

static void optimizeIfBlock(IfBlock* ifBlock, ENode_vec* enode_dag, BBNode_vec* bbnode_dag) {
    // Check if there is any values are directly forwarded through both branches
    ENode_vec branchNodes;
    auto condBB = ifBlock->condBB;
    for (auto enode : *enode_dag)
        if (condBB == enode->BB && enode->type == Branch_n && isLegalToDestroy(enode, ifBlock))
            branchNodes.push_back(enode);

    if (branchNodes.size() == 0)
        return;

    // Construct another branchC node in condBB and another phiC node in exitBB
    auto brCst  = createShortPathIf(ifBlock, enode_dag, bbnode_dag);
    auto exitBB = ifBlock->exitBB;
    for (auto branchNode : branchNodes)
        replaceWithShortCut(branchNode, brCst, enode_dag, exitBB);
}

// TODO: To support multi-depth if statements
void CircuitGenerator::optimizeIfStmt(Function* F) {
    auto ifBlocks = getInnerMostIfBlocks(F, enode_dag);
    for (auto ifBlock : ifBlocks)
        optimizeIfBlock(ifBlock, enode_dag, bbnode_dag);
}

//--------------------------------------------------------//
// Function: optimizeLoops
// Analyze and optimize the loops so loop invariants are replaced with mux to enable loop level
// pipelining
//--------------------------------------------------------//

static bool containsCallInst(Loop* loop) {
    for (auto BB : loop->getBlocks())
        for (auto I = BB->begin(); I != BB->end(); I++)
            if (isa<CallInst>(I))
                return true;
    return false;
}

static bool hasLoopInvar(Loop* loop, ENode_vec* enode_dag) {
    for (auto enode : *enode_dag)
        if (enode->BB == loop->getHeader() && enode->type == Phi_n)
            return true;
    return false;
}

static void createShortPathLoop(ENode* enode, ENode* cstNode, Loop* loop, ENode_vec* enode_dag) {
    // Loop pre-condition check: must be a for loop with a single exit
    assert(enode->CntrlPreds->size() == 2);
    ENode *entryBranch, *exitBranch, *exitPhi, *dataIn;
    if (enode->CntrlPreds->at(0)->BB == loop->getExitingBlock()) {
        entryBranch = enode->CntrlPreds->at(1);
        exitBranch  = enode->CntrlPreds->at(0);
    } else if (enode->CntrlPreds->at(1)->BB == loop->getExitingBlock()) {
        entryBranch = enode->CntrlPreds->at(0);
        exitBranch  = enode->CntrlPreds->at(1);
    } else {
        llvm::errs() << "Warning: Cannot find the back edge for the loop: "
                     << getNodeDotNameNew(enode) << "\n";
        return;
    }
    assert(exitBranch->CntrlSuccs->size() == 2);
    assert(exitBranch->BB == loop->getExitingBlock());
    if (exitBranch->CntrlSuccs->at(0) == enode)
        exitPhi = exitBranch->CntrlSuccs->at(1);
    else if (exitBranch->CntrlSuccs->at(1) == enode)
        exitPhi = exitBranch->CntrlSuccs->at(0);
    else {
        llvm::errs() << getNodeDotNameNew(enode) << "\n";
        llvm_unreachable("Cannot find the exit edge for the loop.");
    }
    // Exit if the value is not propagated to the exit of the loop
    if (exitPhi->type == Sink_)
        return;
    if (exitPhi->BB != loop->getExitBlock()) {
        llvm::errs() << "phi = " << getNodeDotNameNew(enode)
                     << "\nexit phi = " << getNodeDotNameNew(exitPhi) << "\n"
                     << *(enode->Instr) << "\nphi block: " << *(exitPhi->BB)
                     << "\nexit block:" << *(exitPhi->BB) << "\n";
        llvm_unreachable("Exit PhiC is not in the exit block");
    }
    if (exitPhi->CntrlPreds->size() != 1) {
        llvm::errs() << "phi = " << getNodeDotNameNew(enode)
                     << "\nexit phi = " << getNodeDotNameNew(exitPhi) << "\n"
                     << *(enode->Instr) << "\n";
        llvm_unreachable("Exit PhiC must has only one input");
    }

    auto branchIn0 = entryBranch->CntrlPreds->at(0);
    branchIn0      = (branchIn0->type == Fork_) ? branchIn0->CntrlPreds->at(0) : branchIn0;
    auto branchIn1 = entryBranch->CntrlPreds->at(1);
    branchIn1      = (branchIn1->type == Fork_) ? branchIn1->CntrlPreds->at(0) : branchIn1;

    if ((branchIn0->type == Branch_) == (branchIn1->type == Branch_)) {
        llvm::errs() << getNodeDotNameNew(entryBranch) << " -> " << getNodeDotNameNew(enode)
                     << "\n";
        llvm_unreachable("Cannot find the input data for the invariant.");
    }
    if (branchIn0->type == Branch_)
        dataIn = entryBranch->CntrlPreds->at(1);
    else if (branchIn1->type == Branch_)
        dataIn = entryBranch->CntrlPreds->at(0);
    dataIn = insertFork(dataIn, enode_dag, false);

    // One constant sink dummy for exit branch
    ENode* exitBranchSinkNode = new ENode(Sink_, "sink");
    exitBranchSinkNode->id    = enode_dag->size();
    exitBranchSinkNode->bbId  = 0;
    enode_dag->push_back(exitBranchSinkNode);
    exitBranchSinkNode->CntrlPreds->push_back(exitBranch);
    exitBranch->CntrlSuccs->erase(
        std::find(exitBranch->CntrlSuccs->begin(), exitBranch->CntrlSuccs->end(), exitPhi));
    exitPhi->CntrlPreds->clear();
    exitBranch->CntrlSuccs->push_back(exitBranchSinkNode);
    exitBranch->branchFalseOutputSucc = exitBranchSinkNode;

    auto selectBranch       = new ENode(Branch_n, "branch", cstNode->BB);
    selectBranch->bbNode    = cstNode->bbNode;
    selectBranch->id        = enode_dag->size();
    selectBranch->bbId      = cstNode->bbId;
    selectBranch->condition = cstNode;
    enode_dag->push_back(selectBranch);
    selectBranch->CntrlPreds->push_back(cstNode);
    cstNode->CntrlSuccs->push_back(selectBranch);
    selectBranch->CntrlPreds->push_back(dataIn);
    dataIn->CntrlSuccs->push_back(selectBranch);
    exitPhi->CntrlPreds->push_back(selectBranch);
    selectBranch->CntrlSuccs->push_back(exitPhi);
    selectBranch->branchTrueOutputSucc = exitPhi;
    selectBranch->trueBB               = exitPhi->BB;
    // One constant sink dummy for select branch
    ENode* selectBranchSinkNode = new ENode(Sink_, "sink");
    selectBranchSinkNode->id    = enode_dag->size();
    selectBranchSinkNode->bbId  = 0;
    enode_dag->push_back(selectBranchSinkNode);
    selectBranchSinkNode->CntrlPreds->push_back(selectBranch);
    selectBranch->CntrlSuccs->push_back(selectBranchSinkNode);
    selectBranch->branchFalseOutputSucc = selectBranchSinkNode;
}

static void replaceMergeWithMux(ENode* enode, ENode* cntrlMerge, Loop* loop, ENode_vec* enode_dag,
                                int& id) {

    enode->type  = Phi_;
    enode->isMux = true;
    enode->id    = ++id;
    enode->CntrlPreds->push_back(cntrlMerge);
    cntrlMerge->CntrlSuccs->push_back(enode);
}

static bool simplifyLoopInvariants(ENode* enode, Loop* loop, ENode_vec* enode_dag) {
    // if a phi is unused - it is directly connected to the branch
    assert(enode->CntrlSuccs->size() == 1);
    auto succ = enode->CntrlSuccs->front();
    if (succ->type != Branch_n)
        return false;
    if ((succ->CntrlSuccs->at(0) == enode && succ->CntrlSuccs->at(1)->type == Sink_) ||
        (succ->CntrlSuccs->at(1) == enode && succ->CntrlSuccs->at(0)->type == Sink_)) {
        eraseNode(succ, enode_dag);
        // Two-input phi with only one input and select bit left
        assert(enode->CntrlPreds->size() == 2);
        auto srcbranch = (enode->CntrlPreds->at(0)->type == Branch_n) ? enode->CntrlPreds->at(0)
                                                                      : enode->CntrlPreds->at(1);
        srcbranch->CntrlSuccs->erase(
            std::remove(srcbranch->CntrlSuccs->begin(), srcbranch->CntrlSuccs->end(), enode),
            srcbranch->CntrlSuccs->end());

        if (srcbranch->trueBB != enode->BB && srcbranch->falseBB != enode->BB) {
            llvm::errs() << getNodeDotNameNew(srcbranch) << ": "
                         << *(srcbranch->BB->getTerminator()) << "\n"
                         << "true branch:\n"
                         << *(srcbranch->trueBB) << "\n";
            if (!srcbranch->BB->getUniqueSuccessor())
                llvm::errs() << "false branch:\n" << *(srcbranch->falseBB) << "\n";
            llvm::errs() << "targetBB:\n" << *(enode->BB) << "\n";
            llvm_unreachable("Cannot update branch successors.");
        }
        if (srcbranch->trueBB == enode->BB) {
            llvm::errs() << "Update " << getNodeDotNameNew(srcbranch) << " true destination.\n";
            srcbranch->trueBB = succ->trueBB;
        } else if (srcbranch->falseBB == enode->BB) {
            llvm::errs() << "Update " << getNodeDotNameNew(srcbranch) << " false destination.\n";
            srcbranch->falseBB = succ->trueBB;
        }

        enode->CntrlPreds->erase(
            std::remove(enode->CntrlPreds->begin(), enode->CntrlPreds->end(), srcbranch),
            enode->CntrlPreds->end());
        ENode* sink = new ENode(Sink_, "sink");
        sink->id    = enode_dag->size();
        sink->bbId  = 0;
        enode_dag->push_back(sink);
        srcbranch->CntrlSuccs->push_back(sink);
        sink->CntrlPreds->push_back(srcbranch);
        eraseNode(enode, enode_dag);

        if (srcbranch->CntrlSuccs->at(0)->type == Sink_ &&
            srcbranch->CntrlSuccs->at(1)->type == Sink_) {
            eraseNode(srcbranch, enode_dag);
        }
        return true;
    }
    return false;
}

static void optimizeLoop(Loop* loop, ENode_vec* enode_dag, BBNode_vec* bbnode_dag) {

    if (!hasLoopInvar(loop, enode_dag))
        return;

    auto entryBlock = loop->getLoopPreheader();
    auto exitBlock  = loop->getExitBlock();
    auto header     = loop->getHeader();
    if (!entryBlock || !exitBlock)
        return;

    auto entryBBNode = getBBNode(entryBlock, bbnode_dag);
    auto exitBBNode  = getBBNode(exitBlock, bbnode_dag);
    assert(entryBBNode && exitBBNode);
    entryBBNode->CntrlSuccs->push_back(exitBBNode);
    exitBBNode->CntrlPreds->push_back(entryBBNode);

    auto brCst = getBrCst(entryBlock, enode_dag);
    if (!brCst) {
        auto phiC = getPhiC(entryBlock, enode_dag);
        assert(phiC->JustCntrlSuccs->size() > 0);
        phiC = insertFork(phiC, enode_dag, true);

        // Create a short path driver
        auto cstNode         = new ENode(Branch_, "brCstSP", phiC->BB);
        cstNode->bbNode      = phiC->bbNode;
        cstNode->id          = enode_dag->size();
        cstNode->bbId        = phiC->bbId;
        cstNode->isShortPath = true;
        enode_dag->push_back(cstNode);
        cstNode->JustCntrlPreds->push_back(phiC);
        phiC->JustCntrlSuccs->push_back(cstNode);
        brCst = cstNode;
    }
    brCst = insertFork(brCst, enode_dag, false);

    // Get the control merge op that controls the mux ops
    auto phiC = getPhiC(header, enode_dag);
    assert(phiC->JustCntrlSuccs->size() > 0);
    phiC = insertFork(phiC, enode_dag, false);

    // Get the maximum Phi ID
    int id = 0;
    for (auto enode : *enode_dag)
        if (enode->type == Phi_)
            id = std::max(id, enode->id);

    for (auto enode : *enode_dag)
        if (enode->BB == header && enode->type == Phi_n) {
            // 1. Create a short control path for all the loop invariants
            createShortPathLoop(enode, brCst, loop, enode_dag);
            // 2. Replace all the merge ops to mux ops to enable parallelism
            replaceMergeWithMux(enode, phiC, loop, enode_dag, id);
        }

    // dirty code... to be optimized
    bool done = false;
    while (!done) {
        done = true;
        for (auto enode : *enode_dag)
            if (enode->BB == header && enode->type == Phi_) {
                // 3. Remove unused loop invariants in the loop
                auto name = getNodeDotNameNew(enode);
                if (simplifyLoopInvariants(enode, loop, enode_dag)) {
                    llvm::errs() << "Strip " << name << " out of loop: " << getLoopName(loop)
                                 << "\n";
                    done = false;
                    break;
                }
            }
    }
}

static void extractInnermostLoop(Loop* loop, std::vector<Loop*>& innermostLoops) {
    auto subLoops = loop->getSubLoops();
    if (subLoops.empty())
        innermostLoops.push_back(loop);
    else
        for (auto subloop : subLoops)
            extractInnermostLoop(subloop, innermostLoops);
}

static std::vector<Loop*> extractInnermostLoops(LoopInfo& LI) {
    std::vector<Loop*> innermostLoops;
    for (auto& loop : LI)
        extractInnermostLoop(loop, innermostLoops);
    return innermostLoops;
}

// Dirty code - to be optimized...
static void removePathForUnliveVariables(ENode_vec* enode_dag, BBNode_vec* bbnode_dag) {
    bool clean = false;
    while (!clean) {
        clean = true;
        for (auto enode : *enode_dag) {
            if ((enode->type == Phi_n || enode->type == Phi_) && enode->CntrlPreds->size() == 1 &&
                enode->CntrlSuccs->at(0)->type == Branch_n) {
                auto branch = enode->CntrlSuccs->at(0);
                auto br0    = branch->CntrlPreds->at(0);
                br0         = (br0->type == Fork_) ? br0->CntrlPreds->at(0) : br0;
                auto br1    = branch->CntrlPreds->at(1);
                br1         = (br1->type == Fork_) ? br1->CntrlPreds->at(0) : br1;
                assert((br0->type == Branch_) + (br1->type == Branch_) == 1);
                auto select = (br0->type == Branch_) ? br0 : br1;
                // TODO: add skipping for non-constant branch block, such as icmp

                auto e0 = branch->CntrlSuccs->at(0);
                auto e1 = branch->CntrlSuccs->at(1);
                if (e0->type != Sink_ && e1->type != Sink_)
                    continue;

                assert((e0->type == Sink_) + (e1->type == Sink_) == 1);
                auto e      = (e0->type == Sink_) ? e1 : e0;
                auto brIter = std::find(e->CntrlPreds->begin(), e->CntrlPreds->end(), branch);
                assert(brIter != e->CntrlPreds->end());

                auto i0      = enode->CntrlPreds->at(0);
                auto phiIter = std::find(i0->CntrlSuccs->begin(), i0->CntrlSuccs->end(), enode);
                assert(phiIter != i0->CntrlSuccs->end());
                i0->CntrlSuccs->at(std::distance(i0->CntrlSuccs->begin(), phiIter)) = e;
                if (i0->trueBB != enode->BB && i0->falseBB != enode->BB) {
                    llvm::errs() << getNodeDotNameNew(i0) << ": " << *(i0->BB->getTerminator())
                                 << "\n"
                                 << "true branch:\n"
                                 << *(i0->trueBB) << "\n";
                    if (!i0->BB->getUniqueSuccessor())
                        llvm::errs() << "false branch:\n" << *(i0->falseBB) << "\n";
                    llvm::errs() << "targetBB:\n" << *(enode->BB) << "\n";
                    llvm_unreachable("Cannot update branch successors.");
                }
                if (i0->trueBB == enode->BB) {
                    llvm::errs() << "Update " << getNodeDotNameNew(i0)
                                 << " true destination: " << e->BB->getName() << ".\n";
                    i0->trueBB = e->BB;
                } else if (i0->falseBB == enode->BB) {
                    llvm::errs() << "Update " << getNodeDotNameNew(i0)
                                 << " false destination: " << e->BB->getName() << ".\n";
                    i0->falseBB = e->BB;
                }

                e->CntrlPreds->at(std::distance(e->CntrlPreds->begin(), brIter)) = i0;
                enode->CntrlPreds->clear();
                branch->CntrlSuccs->erase(
                    std::remove(branch->CntrlSuccs->begin(), branch->CntrlSuccs->end(), e),
                    branch->CntrlSuccs->end());

                auto entryBBNode = getBBNode(i0->BB, bbnode_dag);
                auto exitBBNode  = getBBNode(e->BB, bbnode_dag);
                assert(entryBBNode && exitBBNode);
                if (std::find(entryBBNode->CntrlSuccs->begin(), entryBBNode->CntrlSuccs->end(),
                              exitBBNode) == entryBBNode->CntrlSuccs->end())
                    entryBBNode->CntrlSuccs->push_back(exitBBNode);
                if (std::find(exitBBNode->CntrlPreds->begin(), exitBBNode->CntrlPreds->end(),
                              entryBBNode) == exitBBNode->CntrlPreds->end())
                    exitBBNode->CntrlPreds->push_back(entryBBNode);

                llvm::errs() << "Forward unused variables: -> " << getNodeDotNameNew(enode)
                             << " -> " << getNodeDotNameNew(branch) << " -> \n";
                eraseNode(branch, enode_dag);
                eraseNode(enode, enode_dag);
                clean = false;
                break;
            }
        }
    }
}

// Dirty code - to be optimized...
// Disabled as it will cause buffer tool to crash
static void removeDeterministicBranch(ENode_vec* enode_dag, BBNode_vec* bbnode_dag) {
    bool clean = false;
    while (!clean) {
        clean = true;
        for (auto enode : *enode_dag) {
            if (enode->type == Branch_n) {
                assert(enode->CntrlSuccs->size() == 2);
                if (enode->CntrlSuccs->at(0) == enode->CntrlSuccs->at(1)) {
                    auto br0 = enode->CntrlPreds->at(0);
                    br0      = (br0->type == Fork_) ? br0->CntrlPreds->at(0) : br0;
                    auto br1 = enode->CntrlPreds->at(1);
                    br1      = (br1->type == Fork_) ? br1->CntrlPreds->at(0) : br1;
                    assert((br0->type == Branch_) + (br1->type == Branch_) == 1);
                    auto data = (br0->type == Branch_) ? enode->CntrlPreds->at(1)
                                                       : enode->CntrlPreds->at(0);
                    auto phi = enode->CntrlSuccs->at(0);
                    auto out = phi->CntrlSuccs->at(0);

                    auto phiIter = std::find(out->CntrlPreds->begin(), out->CntrlPreds->end(), phi);
                    assert(phiIter != out->CntrlPreds->end());
                    auto brIter =
                        std::find(data->CntrlSuccs->begin(), data->CntrlSuccs->end(), enode);
                    assert(brIter != data->CntrlSuccs->end());
                    data->CntrlSuccs->at(std::distance(data->CntrlSuccs->begin(), brIter)) = out;
                    out->CntrlPreds->at(std::distance(out->CntrlPreds->begin(), phiIter))  = data;

                    enode->CntrlPreds->erase(
                        std::find(enode->CntrlPreds->begin(), enode->CntrlPreds->end(), data));
                    phi->CntrlSuccs->clear();

                    auto entryBBNode = getBBNode(data->BB, bbnode_dag);
                    auto exitBBNode  = getBBNode(out->BB, bbnode_dag);
                    assert(entryBBNode && exitBBNode);
                    if (std::find(entryBBNode->CntrlSuccs->begin(), entryBBNode->CntrlSuccs->end(),
                                  exitBBNode) == entryBBNode->CntrlSuccs->end())
                        entryBBNode->CntrlSuccs->push_back(exitBBNode);
                    if (std::find(exitBBNode->CntrlPreds->begin(), exitBBNode->CntrlPreds->end(),
                                  entryBBNode) == exitBBNode->CntrlPreds->end())
                        exitBBNode->CntrlPreds->push_back(entryBBNode);

                    eraseNode(phi, enode_dag);
                    eraseNode(enode, enode_dag);
                    clean = false;
                    break;
                }
            }
        }
    }
}

void CircuitGenerator::optimizeLoops(Function* F) {
    auto DT = llvm::DominatorTree(*F);
    LoopInfo LI(DT);

    auto innermostLoops = extractInnermostLoops(LI);
    for (auto& loop : innermostLoops) {
        // Skip loops that have function calls or multiple back edges - cannot analyze function
        if (containsCallInst(loop) || loop->getNumBackEdges() != 1)
            continue;
        optimizeLoop(loop, enode_dag, bbnode_dag);
    }

    removeRedundantAfterElastic(enode_dag);
    // Create a short path for unused variables in each basic block
    removePathForUnliveVariables(enode_dag, bbnode_dag);
    // If both outputs of a branch points to the same node, erase this branch - phi pair
    // removeDeterministicBranch(enode_dag, bbnode_dag);
}

//--------------------------------------------------------//
// Function: insertLoopInterchangers
// Analyze specified loops and insert loop interchangers
//--------------------------------------------------------//

int getLoopInterchangeDepth(Loop* loop) {
    auto id              = loop->getLoopID();
    LLVMContext& Context = loop->getHeader()->getContext();
    for (unsigned int i = 0; i < id->getNumOperands(); i++)
        if (auto node = dyn_cast<MDNode>(id->getOperand(i))) {
            Metadata* arg = node->getOperand(0);
            if (arg == MDString::get(Context, "dass.loop.interchange")) {
                Metadata* depth                  = node->getOperand(1);
                ConstantAsMetadata* depthAsValue = dyn_cast<ConstantAsMetadata>(depth);
                if (depthAsValue)
                    return depthAsValue->getValue()->getUniqueInteger().getSExtValue();
            }
        }
    return -1;
}

static std::string insertLoopInterchanger(Loop* loop, ENode_vec* enode_dag) {
    auto depth = getLoopInterchangeDepth(loop);
    if (depth == -1)
        return "";

    for (auto enode : *enode_dag)
        if (enode->BB == loop->getHeader() && (enode->type == Phi_ || enode->type == Phi_c))
            // Inserting FIFO slots into all the cycles in the loop
            insertFIFOAfterPhi(enode, depth, enode_dag);

    auto entryBanchC = getBranchC(loop->getLoopPreheader(), enode_dag);
    auto entryPhiC   = getPhiC(loop->getHeader(), enode_dag);
    auto exitBranchC = getBranchC(loop->getExitingBlock(), enode_dag);
    auto exitPhiC    = getPhiC(loop->getExitBlock(), enode_dag);
    return getNodeDotNameNew(entryBanchC) + ", " + getNodeDotNameNew(exitBranchC) + ", " +
           getNodeDotNameNew(entryPhiC) + ", " + getNodeDotNameNew(exitPhiC) + ", " +
           std::to_string(depth) + "\n";
}

static void insertLoopInterchangerRecursive(Loop* loop, ENode_vec* enode_dag,
                                            llvm::raw_fd_ostream& outfile) {
    outfile << insertLoopInterchanger(loop, enode_dag);
    auto subLoops = loop->getSubLoops();
    if (!subLoops.empty())
        for (auto subloop : subLoops)
            insertLoopInterchangerRecursive(subloop, enode_dag, outfile);
}

void CircuitGenerator::insertLoopInterchangers(Function* F) {
    std::error_code ec;
    llvm::raw_fd_ostream outfile("./loop_interchange.tcl", ec);

    auto DT = llvm::DominatorTree(*F);
    LoopInfo LI(DT);
    for (auto loop : LI)
        insertLoopInterchangerRecursive(loop, enode_dag, outfile);
    outfile.close();
}

//--------------------------------------------------------//
// Function: paralleliseLoops
// Analyze specified loops and insert loop controllers for parallelism
//--------------------------------------------------------//

struct ParallelLoops {
    Loop* earliestLoop;
    Loop* latestLoop;
    std::vector<Loop*> loops;
    std::string str;
};

typedef std::vector<Loop*> SequentialLoops;

static void parseSequentialLoops(Loop* loop, std::vector<SequentialLoops*>& sls) {
    SequentialLoops* sl = new SequentialLoops;
    for (auto subloop : loop->getSubLoops()) {
        sl->push_back(subloop);
        parseSequentialLoops(subloop, sls);
    }
    if (!sl->empty())
        sls.push_back(sl);
}

static std::vector<SequentialLoops*> parseSequentialLoops(LoopInfo& LI) {
    std::vector<SequentialLoops*> sls;
    if (LI.empty())
        return sls;

    SequentialLoops* sl = new SequentialLoops;
    for (auto loop : LI) {
        sl->push_back(loop);
        parseSequentialLoops(loop, sls);
    }
    if (!sl->empty())
        sls.push_back(sl);

    llvm::errs() << "Sequential Loops:\n";
    for (auto sl : sls) {
        for (auto loop : *sl)
            llvm::errs() << getLoopName(loop) << ", ";
        llvm::errs() << "\n";
    }
    return sls;
}

static std::vector<ParallelLoops*> parseParallelLoops(Function* F, LoopInfo& LI) {
    std::string str = F->getFnAttribute("dass.parallel.loops").getValueAsString();
    auto n          = std::count(str.begin(), str.end(), ';');
    auto ncursor    = 0;
    std::vector<ParallelLoops*> pls;
    for (auto i = 0; i < n; i++) {
        ParallelLoops* pl = new ParallelLoops;
        auto plstr        = str.substr(ncursor, str.find(';', ncursor) - ncursor);
        pl->str           = plstr;
        auto m            = std::count(plstr.begin(), plstr.end(), ',') + 1;
        auto mcursor      = 0;
        for (auto j = 0; j < m; j++) {
            auto idx      = plstr.find(',', mcursor);
            auto loopName = (idx == std::string::npos) ? plstr.substr(mcursor)
                                                       : plstr.substr(mcursor, idx - mcursor);
            auto loop = getLoop(loopName, LI);
            if (!loop)
                llvm_unreachable(
                    std::string("Cannot find loop " + loopName + " in function " +
                                demangleFuncName(F->getName().str().c_str()) +
                                ". Please check if your pragma is specified correctly.\n")
                        .c_str());
            pl->loops.push_back(loop);
            mcursor = idx + 1;
        }
        ncursor = str.find(';', ncursor) + 1;
        if (!pl->loops.empty()) {
            assert(pl->loops.size() > 1);
            pls.push_back(pl);
        }
    }
    llvm::errs() << "Loops to parallelise:\n";
    for (auto pl : pls) {
        for (auto loop : pl->loops)
            llvm::errs() << getLoopName(loop) << ", ";
        llvm::errs() << "\n";
    }
    return pls;
}

static bool parallelLoopsInCurrentSequentialLoops(SequentialLoops* sl, ParallelLoops* pl) {
    std::vector<int> indices;
    for (auto loop : pl->loops) {
        auto iter = std::find(sl->begin(), sl->end(), loop);
        if (iter == sl->end()) {
            return false;
        }
        indices.push_back(std::distance(sl->begin(), iter));
    }

    // Verify these parallel loops are continuously sequential
    auto min    = std::min_element(indices.begin(), indices.end());
    auto minIdx = std::distance(indices.begin(), min);
    auto max    = std::max_element(indices.begin(), indices.end());
    auto maxIdx = std::distance(indices.begin(), max);
    std::sort(indices.begin(), indices.end());
    auto index = indices[0];
    for (unsigned int i = 1; i < indices.size(); i++)
        if (indices[i] - index != 1) {
            return false;
        } else
            index = indices[i];
    // Index in reversed order
    pl->earliestLoop = pl->loops[maxIdx];
    pl->latestLoop   = pl->loops[minIdx];
    return true;
}

static void parallelLoopsSanityCheck(ArrayRef<SequentialLoops*> sls, ArrayRef<ParallelLoops*> pls) {
    // Each set of parallel loops must be contineously sequential
    for (auto pl : pls) {
        bool verify = false;
        for (auto sl : sls)
            if (parallelLoopsInCurrentSequentialLoops(sl, pl))
                verify = true;
        if (!verify) {
            llvm::errs() << pl->str << "\n";
            llvm_unreachable("Cannot verify whether the above loops are sequential.");
        }
    }
}

static void insertLoopParallelisers(ParallelLoops* pl, ENode_vec* enode_dag,
                                    llvm::raw_fd_ostream& outfile, LoopInfo& LI) {
    ENode_vec startPhiC, exitBranchC;
    ENode *fork, *join;
    for (auto loop : pl->loops) {
        auto header  = loop->getLoopPreheader();
        auto exit    = loop->getExitingBlock();
        auto phic    = getPhiC(header, enode_dag);
        auto branchc = getBranchC(exit, enode_dag);
        // assert(phic->JustCntrlPreds->size() == 1);
        phic = (phic->type == Start_) ? phic->JustCntrlSuccs->at(0) : phic;
        if (loop == pl->earliestLoop)
            fork = phic;
        if (loop == pl->latestLoop)
            join = (branchc->JustCntrlSuccs->at(0)->BB == loop->getHeader())
                       ? branchc->JustCntrlSuccs->at(1)
                       : branchc->JustCntrlSuccs->at(0);
        startPhiC.push_back(phic);
        exitBranchC.push_back(branchc);
    }
    assert(join && fork);
    auto buffer = std::to_string(startPhiC.size()) + "; " + getNodeDotNameNew(fork) + ": ";
    for (auto phic : startPhiC)
        buffer += getNodeDotNameNew(phic) + ", ";
    buffer += "; " + getNodeDotNameNew(join) + ": ";
    for (auto branchc : exitBranchC)
        buffer += getNodeDotNameNew(branchc) + ", ";
    buffer += ";\n";
    outfile << buffer;
}

void CircuitGenerator::paralleliseLoops(Function* F) {
    std::error_code ec;
    llvm::raw_fd_ostream outfile("./parallel_loops.tcl", ec);

    if (!F->hasFnAttribute("dass.parallel.loops")) {
        outfile.close();
        return;
    }

    auto DT = llvm::DominatorTree(*F);
    LoopInfo LI(DT);
    auto sequentialLoops = parseSequentialLoops(LI);
    auto parallelLoops   = parseParallelLoops(F, LI);
    parallelLoopsSanityCheck(sequentialLoops, parallelLoops);

    for (auto pl : parallelLoops)
        insertLoopParallelisers(pl, enode_dag, outfile, LI);
    outfile.close();
}
