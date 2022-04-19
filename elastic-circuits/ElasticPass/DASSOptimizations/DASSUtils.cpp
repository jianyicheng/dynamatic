#include "DASS/DASSUtils.h"
#include "ElasticPass/Utils.h"
#include "Nodes.h"

#include <vector>

void printEnodeVector(ENode_vec* enode_dag, std::string str) {
    llvm::errs() << str << "\n";
    for (size_t idx = 0; idx < enode_dag->size(); idx++) {
        auto& enode = (*enode_dag)[idx];
        llvm::errs() << enode->Name << "_" << std::to_string(enode->id) << "\n";
    }
}

std::string demangleFuncName(const char* name) {
    auto status = -1;

    std::unique_ptr<char, void (*)(void*)> res{abi::__cxa_demangle(name, NULL, NULL, &status),
                                               std::free};
    auto func = (status == 0) ? res.get() : std::string(name);
    return func.substr(0, func.find("("));
}

BBNode* getBBNode(BasicBlock* BB, BBNode_vec* bbnode_dag) {
    for (auto bbnode : *bbnode_dag)
        if (bbnode->BB == BB)
            return bbnode;
    return nullptr;
}

// Append a fork node after enode
ENode* insertFork(ENode* enode, ENode_vec* enode_dag, bool isControl) {
    if (enode->JustCntrlSuccs->size() > 0 && isControl &&
        enode->JustCntrlSuccs->at(0)->type == Fork_c)
        return enode->JustCntrlSuccs->at(0);
    if (enode->CntrlSuccs->size() > 0 && !isControl && enode->CntrlSuccs->at(0)->type == Fork_)
        return enode->CntrlSuccs->at(0);
    if ((!isControl && enode->type == Fork_) || (isControl && enode->type == Fork_c))
        return enode;

    ENode* forkNode;
    if (isControl) {
        forkNode            = new ENode(Fork_c, "forkC", enode->BB);
        forkNode->bbNode    = enode->bbNode;
        forkNode->id        = enode_dag->size();
        forkNode->bbId      = enode->bbId;
        forkNode->isCntrlMg = false;

        if (enode->JustCntrlSuccs->size() > 0)
            forkNode->JustCntrlSuccs =
                new ENode_vec(enode->JustCntrlSuccs->begin(), enode->JustCntrlSuccs->end());
        forkNode->JustCntrlPreds->push_back(enode);

        for (auto succ : *enode->JustCntrlSuccs) {
            auto idx = std::find(succ->JustCntrlPreds->begin(), succ->JustCntrlPreds->end(), enode);
            assert(idx != succ->JustCntrlPreds->end());
            succ->JustCntrlPreds->at(std::distance(succ->JustCntrlPreds->begin(), idx)) = forkNode;
        }
        enode->JustCntrlSuccs->clear();
        enode->JustCntrlSuccs->push_back(forkNode);
    } else {
        forkNode            = new ENode(Fork_, "fork", enode->BB);
        forkNode->bbNode    = enode->bbNode;
        forkNode->id        = enode_dag->size();
        forkNode->bbId      = enode->bbId;
        forkNode->isCntrlMg = false;

        if (enode->CntrlSuccs->size() > 0)
            forkNode->CntrlSuccs =
                new ENode_vec(enode->CntrlSuccs->begin(), enode->CntrlSuccs->end());
        forkNode->CntrlPreds->push_back(enode);

        for (auto succ : *enode->CntrlSuccs) {
            auto idx = std::find(succ->CntrlPreds->begin(), succ->CntrlPreds->end(), enode);
            assert(idx != succ->CntrlPreds->end());
            succ->CntrlPreds->at(std::distance(succ->CntrlPreds->begin(), idx)) = forkNode;
        }
        enode->CntrlSuccs->clear();
        enode->CntrlSuccs->push_back(forkNode);
    }
    enode_dag->push_back(forkNode);
    return forkNode;
}

ENode* getPhiC(BasicBlock* BB, ENode_vec* enode_dag) {
    ENode* phiCNode;
    auto count = 0;
    for (auto enode : *enode_dag)
        if (enode->BB == BB && (enode->type == Phi_c || enode->type == Start_)) {
            phiCNode = enode;
            count++;
        }
    if (count != 1) {
        llvm::errs() << count << "\n" << *BB << "\n";
        llvm_unreachable("Cannot find the unique PhiC.");
    }

    return phiCNode;
}

ENode* getCallNode(CallInst* callInst, ENode_vec* enode_dag) {
    ENode* callNode;
    auto count = 0;
    for (auto enode : *enode_dag)
        if (enode->type == Inst_)
            if (enode->Instr == callInst) {
                callNode = enode;
                count++;
            }
    if (count != 1) {
        llvm::errs() << count << "\n" << *callInst << "\n";
        llvm_unreachable("Cannot find the unique call node.");
    }
    return callNode;
}

ENode* getBrCst(BasicBlock* BB, ENode_vec* enode_dag) {
    ENode* cstBr = nullptr;
    auto count   = 0;
    for (auto enode : *enode_dag)
        if (enode->BB == BB && enode->type == Branch_) {
            cstBr = enode;
            count++;
        }
    if (count > 1) {
        llvm::errs() << count << "\n" << *BB << "\n";
        llvm_unreachable("Cannot find the unique PhiC.");
    }

    return cstBr;
}

ENode* getBranchC(BasicBlock* BB, ENode_vec* enode_dag) {
    for (auto enode : *enode_dag)
        if (enode->BB == BB && enode->type == Branch_c)
            return enode;
    return nullptr;
}

static void removePred(ENode* enode, ENode* pred, ENode_vec* enode_dag) {
    if (std::find(pred->JustCntrlSuccs->begin(), pred->JustCntrlSuccs->end(), enode) !=
        pred->JustCntrlSuccs->end()) {
        pred->JustCntrlSuccs->erase(
            std::find(pred->JustCntrlSuccs->begin(), pred->JustCntrlSuccs->end(), enode));
        if (enode->type == Phi_c) {
            ENode* sinkNode = new ENode(Sink_, "sink");
            sinkNode->id    = enode_dag->size();
            sinkNode->bbId  = 0;
            enode_dag->push_back(sinkNode);
            pred->JustCntrlSuccs->push_back(sinkNode);
            sinkNode->JustCntrlPreds->push_back(pred);
        }
    } else if (std::find(pred->CntrlSuccs->begin(), pred->CntrlSuccs->end(), enode) !=
               pred->CntrlSuccs->end()) {
        pred->CntrlSuccs->erase(
            std::find(pred->CntrlSuccs->begin(), pred->CntrlSuccs->end(), enode));
        if (enode->type == Phi_c) {
            ENode* sinkNode = new ENode(Sink_, "sink");
            sinkNode->id    = enode_dag->size();
            sinkNode->bbId  = 0;
            enode_dag->push_back(sinkNode);
            pred->CntrlSuccs->push_back(sinkNode);
            sinkNode->CntrlPreds->push_back(pred);
        }
    } else {
        llvm::errs() << getNodeDotNameNew(pred) << "* -> " << getNodeDotNameNew(enode) << "\n";
        llvm_unreachable("Cannot find pred to remove");
    }
}

void removePreds(ENode* enode, ENode_vec* enode_dag) {
    if (enode->CntrlPreds->size()) {
        for (auto& pred : *enode->CntrlPreds)
            removePred(enode, pred, enode_dag);
        enode->CntrlPreds->clear();
    }
    if (enode->JustCntrlPreds->size()) {
        for (auto& pred : *enode->JustCntrlPreds)
            removePred(enode, pred, enode_dag);
        enode->JustCntrlPreds->clear();
    }
}

static void removeSucc(ENode* enode, ENode* pred, ENode_vec* enode_dag) {
    if (std::find(pred->CntrlPreds->begin(), pred->CntrlPreds->end(), enode) !=
        pred->CntrlPreds->end())
        pred->CntrlPreds->erase(
            std::find(pred->CntrlPreds->begin(), pred->CntrlPreds->end(), enode));
    else if (std::find(pred->JustCntrlPreds->begin(), pred->JustCntrlPreds->end(), enode) !=
             pred->JustCntrlPreds->end())
        pred->JustCntrlPreds->erase(
            std::find(pred->JustCntrlPreds->begin(), pred->JustCntrlPreds->end(), enode));
    else {
        llvm::errs() << getNodeDotNameNew(enode) << " -> " << getNodeDotNameNew(pred) << "*\n";
        llvm_unreachable("Cannot find succ to remove");
    }
}

void removeSuccs(ENode* enode, ENode_vec* enode_dag) {
    std::vector<ENode*> sinks;
    if (enode->CntrlSuccs->size()) {
        for (auto& pred : *enode->CntrlSuccs) {
            removeSucc(enode, pred, enode_dag);
            if (pred->type == Sink_)
                sinks.push_back(&*pred);
        }
        enode->CntrlSuccs->clear();
    }
    if (enode->JustCntrlSuccs->size()) {
        for (auto& pred : *enode->JustCntrlSuccs) {
            removeSucc(enode, pred, enode_dag);
            if (pred->type == Sink_)
                sinks.push_back(&*pred);
        }
        enode->JustCntrlSuccs->clear();
    }
    for (auto sink : sinks)
        eraseNode(sink, enode_dag);
}

void eraseNode(ENode* enode, ENode_vec* enode_dag) {
    assert(enode);
    removePreds(enode, enode_dag);
    removeSuccs(enode, enode_dag);

    // Verify
    // for (auto node : *enode_dag) {
    //     if (node == enode)
    //         continue;
    //     assert(std::find(node->CntrlPreds->begin(), node->CntrlPreds->end(), enode) ==
    //            node->CntrlPreds->end());
    //     assert(std::find(node->JustCntrlPreds->begin(), node->JustCntrlPreds->end(), enode) ==
    //            node->JustCntrlPreds->end());
    //     assert(std::find(node->CntrlSuccs->begin(), node->CntrlSuccs->end(), enode) ==
    //            node->CntrlSuccs->end());
    //     assert(std::find(node->JustCntrlSuccs->begin(), node->JustCntrlSuccs->end(), enode) ==
    //            node->JustCntrlSuccs->end());
    // }
    enode_dag->erase(std::find(enode_dag->begin(), enode_dag->end(), enode));
}

std::string getLoopName(Loop* loop) {
    auto nameOp = MDString::get(loop->getHeader()->getContext(), "llvm.loop.name");
    auto id     = loop->getLoopID();
    for (unsigned int i = 0; i < id->getNumOperands(); i++)
        if (auto node = dyn_cast<MDNode>(id->getOperand(i))) {
            Metadata* arg = node->getOperand(0);
            if (arg == nameOp) {
                Metadata* loopName    = node->getOperand(1);
                auto loopNameAsString = dyn_cast<MDString>(loopName);
                return loopNameAsString->getString();
            }
        }
    return loop->getName();
}

Loop* getLoopByName(Loop* loop, std::string name) {
    if (getLoopName(loop) == name)
        return loop;

    auto subloops = loop->getSubLoops();
    if (subloops.empty())
        return nullptr;

    for (auto subloop : subloops)
        if (auto targetLoop = getLoopByName(subloop, name))
            return targetLoop;
    return nullptr;
}

Loop* getLoop(std::string loopName, LoopInfo& LI) {
    if (!LI.empty())
        for (auto loop : LI) {
            auto targetLoop = getLoopByName(loop, loopName);
            if (targetLoop)
                return targetLoop;
        }
    return nullptr;
}

ENode* getNodeByName(std::string name, ENode_vec* enode_dag) {
    for (auto enode : *enode_dag) {
        auto n = getNodeDotNameNew(enode);
        if (n == name || n == "\"" + name + "\"")
            return enode;
    }
    return nullptr;
}

bool nodeNameCheck(ENode_vec* enode_dag) {
    auto offset = 0;
    int max     = 0;
    for (auto enode : *enode_dag)
        max = std::max(max, enode->id);
    max++;
    for (auto i = enode_dag->begin(); i != enode_dag->end(); i++)
        for (auto j = std::next(i, 1); j != enode_dag->end(); j++)
            if (getNodeDotNameNew(*i) == getNodeDotNameNew(*j)) {
                ENode* ii = *i;
                llvm::errs() << "Renamed " << getNodeDotNameNew(ii) << " to ";
                ii->id = max + offset;
                llvm::errs() << getNodeDotNameNew(ii) << "\n";
                offset++;
            }
    if (offset > 0)
        return nodeNameCheck(enode_dag);
    return 0;
}
