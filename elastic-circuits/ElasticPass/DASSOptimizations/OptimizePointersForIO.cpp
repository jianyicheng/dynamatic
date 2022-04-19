#include "ElasticPass/CircuitGenerator.h"
#include "ElasticPass/Utils.h"

//--------------------------------------------------------//
// Function: replacePointersWithRegisters
// Analyze the pointer whether it could be replaced with registers. If yes, then remove load and
// store nodes in the graph
//--------------------------------------------------------//

// Find the pointers that can be replaced with registers.

static bool operandsHaveValue(Instruction* inst, const Value* value) {
    for (unsigned int i = 0; i < inst->getNumOperands(); ++i)
        if (inst->getOperand(i) == value)
            return true;
    return false;
}

static bool checkCondition(Function& F, Value* value, bool isArg) {
    int callCount = 0, loadCount = 0, storeCount = 0;
    for (auto& BB : F) {
        for (auto& I : BB) {
            auto inst = &I;
            if (operandsHaveValue(inst, value)) {
                if (isa<LoadInst>(inst))
                    loadCount++;
                else if (isa<StoreInst>(inst))
                    storeCount++;
                else if (isa<CallInst>(inst))
                    callCount++;
            }
        }
    }
    // The transformation is only allowed for one of the following conditions:
    // 1. one store to argument pointer
    // 2. load from local pointer
    // TODO: make have bug as we cannot determine the order of these two inst
    return ((isArg && storeCount == 1 && loadCount == 0 && callCount == 0) ||
            (!isArg && loadCount > 0 && storeCount == 0 && callCount == 1));
}

static void findValidPointersToReplace(Function& F, const_val_vec* ptrs) {
    ptrs->clear();

    for (auto& arg : F.args()) {
        auto type = arg.getType();
        if (type->isPointerTy() && arg.hasOneUse()) {
            if (auto value = dyn_cast<Value>(&arg)) {
                auto useOp = value->use_begin()->getUser();
                if (isa<GetElementPtrInst>(useOp))
                    continue;
                if (checkCondition(F, value, 1))
                    ptrs->push_back(value);
            } else {
                llvm::errs() << &arg << "\n";
                llvm_unreachable("Cannot get pointer value from pointer argument, aborting...");
            }
        }
    }

    for (auto& BB : F) {
        for (auto& I : BB) {
            auto inst = &I;
            if (auto allocaInst = dyn_cast<AllocaInst>(inst)) {
                if (!allocaInst->isArrayAllocation()) {
                    if (auto value = dyn_cast<Value>(allocaInst)) {
                        if (checkCondition(F, value, 0))
                            ptrs->push_back(value);
                    } else {
                        llvm::errs() << inst << "\n";
                        llvm_unreachable("Cannot get pointer value from alloca inst, aborting...");
                    }
                }
            }
        }
    }
}

static ENode* getENode(Instruction* inst, ENode_vec* enode_dag) {
    for (size_t idx = 0; idx < enode_dag->size(); idx++) {
        auto& enode = (*enode_dag)[idx];
        if (enode->type == Inst_) {
            if ((enode->Instr == inst))
                return enode;
        }
    }
    llvm::errs() << inst << "\n";
    llvm_unreachable(", aborting...");
    return nullptr;
}

static void removePreds(ENode* enode) {
    if (enode->CntrlPreds->size()) {
        for (auto& pred : *enode->CntrlPreds)
            pred->CntrlSuccs->erase(
                std::remove(pred->CntrlSuccs->begin(), pred->CntrlSuccs->end(), enode),
                pred->CntrlSuccs->end());
        enode->CntrlPreds->clear();
    }
    if (enode->JustCntrlPreds->size()) {
        for (auto& pred : *enode->JustCntrlPreds)
            pred->JustCntrlSuccs->erase(
                std::remove(pred->JustCntrlSuccs->begin(), pred->JustCntrlSuccs->end(), enode),
                pred->JustCntrlSuccs->end());
        enode->JustCntrlPreds->clear();
    }
}

static void removeSuccs(ENode* enode) {
    if (enode->CntrlSuccs->size()) {
        for (auto& pred : *enode->CntrlSuccs)
            pred->CntrlPreds->erase(
                std::remove(pred->CntrlPreds->begin(), pred->CntrlPreds->end(), enode),
                pred->CntrlPreds->end());
        enode->CntrlSuccs->clear();
    }
    if (enode->JustCntrlSuccs->size()) {
        for (auto& pred : *enode->JustCntrlSuccs)
            pred->JustCntrlPreds->erase(
                std::remove(pred->JustCntrlPreds->begin(), pred->JustCntrlPreds->end(), enode),
                pred->JustCntrlPreds->end());
        enode->JustCntrlSuccs->clear();
    }
}

static void removeBBLivenessIn(ENode* enode, BBNode_vec* bbnode_dag) {
    for (auto bbnode : *bbnode_dag) {
        if (contains(bbnode->Live_in, enode)) {
            bbnode->Live_in->erase(
                std::remove(bbnode->Live_in->begin(), bbnode->Live_in->end(), enode),
                bbnode->Live_in->end());
        }
    }
}

static void removeBBLivenessOut(ENode* enode, BBNode_vec* bbnode_dag) {
    for (auto bbnode : *bbnode_dag) {
        if (contains(bbnode->Live_out, enode)) {
            bbnode->Live_out->erase(
                std::remove(bbnode->Live_out->begin(), bbnode->Live_out->end(), enode),
                bbnode->Live_out->end());
        }
    }
}

static void remove(ENode* enode, BBNode_vec* bbnode_dag, ENode_vec* enode_dag) {

    removePreds(enode);
    removeSuccs(enode);
    removeBBLivenessIn(enode, bbnode_dag);
    removeBBLivenessOut(enode, bbnode_dag);

    delete enode;
    enode_dag->erase(std::remove(enode_dag->begin(), enode_dag->end(), enode), enode_dag->end());
}

static void addLiveness(ENode* predNode, ENode* succNode, BBNode_vec* bbnode_dag) {
    if (predNode->BB != succNode->BB) {
        predNode->is_live_out = true;
        auto predBB           = getBBNode(predNode->BB, bbnode_dag);
        if (!contains(predBB->Live_out, predNode))
            predBB->Live_out->push_back(predNode);

        succNode->is_live_in = true;
        auto succBB          = getBBNode(succNode->BB, bbnode_dag);
        if (!contains(succBB->Live_in, succNode))
            succBB->Live_in->push_back(succNode);
    }
}

void CircuitGenerator::replacePointersWithRegisters(Function& F, const_val_vec* pList) {
    // Find pointers
    findValidPointersToReplace(F, pList);
    if (!pList->size())
        return;

    llvm::errs() << F.getName()
                 << " : Found the following pointers to be transformed to registers:\n";
    for (auto p : *pList) {
        llvm::errs() << *p << "\n";
    }

    ENode_vec tmpNodes;
    for (auto& ptr : *pList) {
        for (auto& BB : F) {
            for (auto& I : BB) {
                auto inst = &I;
                if (operandsHaveValue(inst, ptr)) {
                    if (isa<CallInst>(inst) || isa<StoreInst>(inst)) {
                        auto enode = getENode(inst, enode_dag);
                        if (!contains(&tmpNodes, enode))
                            tmpNodes.push_back(enode);
                    } else if (!isa<LoadInst>(inst)) {
                        llvm::errs() << *inst << "\n";
                        llvm_unreachable("Invalid inst for pointer analysis, aborting...");
                    }
                }
            }
        }
    }

    // Reformat DAG
    auto retCount      = 1;
    auto callTailCount = 0;
    for (auto& node : tmpNodes) {
        auto inst = node->Instr;
        if (isa<StoreInst>(*inst)) {
            // Change a function argument from a pointer to a direct output
            assert(node->CntrlPreds->size() == 2 && node->CntrlSuccs->size() == 0);
            auto argNode = (*(node->CntrlPreds))[1];
            remove(argNode, bbnode_dag, enode_dag);
            assert(node->CntrlPreds->size() == 1 && node->CntrlSuccs->size() == 0);

            auto resNode  = (*(node->CntrlPreds))[0];
            auto retNode  = new ENode(DASSComp_, node->BB);
            retNode->Name = "ret";
            retNode->id   = retCount++;
            retNode->CntrlPreds->push_back(resNode);
            resNode->CntrlSuccs->push_back(retNode);
            resNode->bbNode = node->bbNode;
            enode_dag->push_back(retNode);
            addLiveness(resNode, retNode, bbnode_dag);

            remove(node, bbnode_dag, enode_dag);
        } else if (isa<CallInst>(*inst)) {
            ENode_vec allocaList, removeList;
            for (auto& pred : *node->CntrlPreds) {
                if (pred->type == Inst_) {
                    if (auto allocaInst = dyn_cast<AllocaInst>(pred->Instr)) {
                        auto ptr = dyn_cast<Value>(allocaInst);
                        assert(std::find(pList->begin(), pList->end(), ptr) != pList->end());
                        if (!contains(&allocaList, pred)) {
                            allocaList.push_back(pred);
                            removeList.push_back(pred);
                        }
                    }
                }
            }

            // Dynamatic does not allow op with multiple results - so for each result we
            // implement
            // as a node tail following the call inst, connected by fork. Then these tails will
            // be
            // merged into the call inst later in the MyCFGPass pass
            ENode_vec callTailList;
            auto hasReturn = (node->CntrlSuccs->size() > 0);
            if (hasReturn) {
                auto resNode    = new ENode(DASSMutiUseTail_, node->BB);
                resNode->Name   = "calltail";
                resNode->id     = callTailCount++;
                resNode->bbNode = node->bbNode;
                for (auto& use : *node->CntrlSuccs) {
                    use->CntrlPreds->push_back(resNode);
                    resNode->CntrlSuccs->push_back(use);
                    addLiveness(resNode, use, bbnode_dag);
                }
                callTailList.push_back(resNode);
                enode_dag->push_back(resNode);
            }
            for (auto& allocaNode : allocaList) {
                auto resNode    = new ENode(DASSMutiUseTail_, node->BB);
                resNode->Name   = "calltail";
                resNode->id     = callTailCount++;
                resNode->bbNode = node->bbNode;
                for (auto& allocaSucc : *allocaNode->CntrlSuccs) {
                    if (allocaSucc != node) {
                        assert(isa<LoadInst>(allocaSucc->Instr));
                        for (auto& loadSucc : *allocaSucc->CntrlSuccs) {
                            loadSucc->CntrlPreds->push_back(resNode);
                            resNode->CntrlSuccs->push_back(loadSucc);
                            addLiveness(resNode, loadSucc, bbnode_dag);
                        }
                        removeList.push_back(allocaSucc);
                    }
                }
                enode_dag->push_back(resNode);
                callTailList.push_back(resNode);
            }

            removeBBLivenessOut(node, bbnode_dag);
            removeSuccs(node);

            for (auto& callTail : callTailList) {
                node->CntrlSuccs->push_back(callTail);
                callTail->CntrlPreds->push_back(node);
            }

            for (size_t idx = 0; idx < removeList.size(); idx++) {
                auto& removeNode = (removeList)[idx];
                remove(removeNode, bbnode_dag, enode_dag);
            }

            assert(node->CntrlSuccs->size() == hasReturn + allocaList.size());
        } else {
            llvm::errs() << inst << "\n";
            llvm_unreachable("Invalid ENode for pointer analysis, aborting...");
        }
    }
}

//--------------------------------------------------------//
// Function: mergeCallTail
// The dot graph needs to satisfy one result for each op except elatsic components, so a call
// with
// multiple outputs are constructed as a call followed by a folk. This pass merge the call and
// fork
// back into one call op for synthesis.
//--------------------------------------------------------//

// Merge call tail and fork into a single call
void CircuitGenerator::mergeCallTail(const_val_vec* pList) {
    if (!pList->size())
        return;

    ENode_vec callList;
    for (auto enode : *enode_dag) {
        if (enode->type == Inst_) {
            if (isa<CallInst>(*(enode->Instr)) && enode->CntrlSuccs->size())
                callList.push_back(enode);
        }
    }

    for (auto enode : callList) {
        if (enode->CntrlSuccs->front()->type == DASSMutiUseTail_) {
            auto tail = enode->CntrlSuccs->front();
            removeBBLivenessOut(enode, bbnode_dag);
            removeSuccs(enode);
            for (auto succ : *tail->CntrlSuccs) {
                succ->CntrlPreds->push_back(enode);
                enode->CntrlSuccs->push_back(succ);
                addLiveness(enode, succ, bbnode_dag);
            }
            remove(tail, bbnode_dag, enode_dag);
        } else if (enode->CntrlSuccs->front()->type == Fork_) {
            auto fork     = enode->CntrlSuccs->front();
            auto tailList = *enode->CntrlSuccs->front()->CntrlSuccs;
            removeBBLivenessOut(enode, bbnode_dag);
            removeSuccs(enode);
            for (auto tail : tailList) {
                for (auto succ : *tail->CntrlSuccs) {
                    succ->CntrlPreds->push_back(enode);
                    enode->CntrlSuccs->push_back(succ);
                    addLiveness(enode, succ, bbnode_dag);
                }
            }
            remove(fork, bbnode_dag, enode_dag);
            for (size_t idx = 0; idx < tailList.size(); idx++) {
                auto& tail = (tailList)[idx];
                remove(tail, bbnode_dag, enode_dag);
            }
        } else {
            auto illegalCall = 0;
            for (auto& p : *pList)
                illegalCall += (operandsHaveValue(enode->Instr, p));
            if (illegalCall) {
                llvm::errs() << *(enode->Instr) << "\n";
                llvm_unreachable("Invalid ENode for call merging, aborting...");
            }
        }
    }
}