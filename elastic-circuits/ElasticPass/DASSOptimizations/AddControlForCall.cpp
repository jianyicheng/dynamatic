#include "ElasticPass/CircuitGenerator.h"
#include "ElasticPass/DASS/DASSUtils.h"
#include "ElasticPass/Utils.h"

// #include <algorithm>

//--------------------------------------------------------//
// Function: addControlForCall
// If a call node does not have any input as triggers, connect it to the controlmerge in the basic
// block as a trigger
//--------------------------------------------------------//

static ENode* isTriggeredByCall(ENode* callNode, ENode_vec* enode_dag) {
    auto inst       = callNode->Instr;
    auto BB         = inst->getParent();
    bool beforeInst = false;

    auto I = --BB->end();
    while (true) {
        if (beforeInst)
            if (auto callInst = dyn_cast<CallInst>(I))
                for (auto i = 0; i < callInst->getNumOperands(); i++)
                    if (I->getOperand(i)->getType()->isPointerTy())
                        for (auto j = 0; j < inst->getNumOperands(); j++)
                            if (inst->getOperand(j)->getType()->isPointerTy())
                                if (inst->getOperand(j) == callInst->getOperand(i))
                                    return getCallNode(callInst, enode_dag);
        if (inst == &*I)
            beforeInst = true;
        if (I == BB->begin())
            break;
        I--;
    }
    return nullptr;
}

static void addControlEdge(ENode* callNode, ENode_vec* enode_dag) {
    llvm::errs() << "Add elastic trigger for call node: " << *(callNode->Instr) << ".\n";
    ENode* startNode;
    for (auto enode : *enode_dag)
        if (enode->type == Start_) {
            startNode = enode;
            break;
        }
    assert(startNode);

    ENode* cmergeNode =
        (startNode->BB == callNode->BB) ? startNode : getPhiC(callNode->BB, enode_dag);
    assert(cmergeNode);
    assert(cmergeNode->JustCntrlSuccs);

    auto lastCall = isTriggeredByCall(callNode, enode_dag);
    if (lastCall) {
        ENode* input = (lastCall->CntrlSuccs->size())
                           ? insertFork(lastCall->CntrlSuccs->front(), enode_dag, false)
                           : lastCall;

        input->JustCntrlSuccs->push_back(callNode);
        callNode->CntrlPreds->push_back(input);
        llvm::errs() << "Added call trigger: " << getNodeDotNameNew(lastCall) << " -> "
                     << getNodeDotNameNew(input) << " -> " << getNodeDotNameNew(callNode) << "\n";
    } else if (cmergeNode->JustCntrlSuccs->size() == 1) {
        auto newID = 0;
        for (auto enode : *enode_dag)
            if (enode->type == Cst_)
                newID = std::max(newID, enode->id);

        ENode* forkCNode = insertFork(cmergeNode, enode_dag, true);

        ENode* cstNode     = new ENode(Cst_, callNode->BB);
        cstNode->bbNode    = callNode->bbNode;
        cstNode->id        = ++newID;
        cstNode->isCntrlMg = false;
        cstNode->bbId      = callNode->bbId;
        cstNode->CI        = 0;

        forkCNode->JustCntrlSuccs->push_back(cstNode);
        cstNode->JustCntrlPreds->push_back(forkCNode);

        callNode->CntrlPreds->push_back(cstNode);
        cstNode->CntrlSuccs->push_back(callNode);
        enode_dag->push_back(cstNode);

        llvm::errs() << "Added call trigger: " << getNodeDotNameNew(forkCNode) << " -> "
                     << getNodeDotNameNew(cstNode) << " -> " << getNodeDotNameNew(callNode) << "\n";
    } else
        llvm_unreachable(std::string("CMerge has multiple outputs: " +
                                     std::to_string(cmergeNode->JustCntrlSuccs->size()) + "\n")
                             .c_str());
}

void CircuitGenerator::addControlForCall() {
    for (auto enode : *enode_dag) {
        if (enode->type != Inst_ || enode->Name.find("DASSFIFO_") != std::string::npos)
            continue;

        if (isa<CallInst>(enode->Instr) && enode->CntrlPreds->size() == 0)
            addControlEdge(enode, enode_dag);
    }
}
