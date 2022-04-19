//--------------------------------------------------------//
// Added for DASS compiler, by Jianyi

#pragma once

#include "Nodes.h"

#include <cxxabi.h>
#include <vector>

typedef std::vector<Value*> val_vec;
typedef std::vector<const Value*> const_val_vec;
typedef std::vector<int> int_vec;
typedef std::vector<bool> bool_vec;
typedef std::vector<ENode*> ENode_vec;
typedef std::vector<BBNode*> BBNode_vec;

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Support/ErrorHandling.h"

void printEnodeVector(ENode_vec* enode_dag, std::string str = "");
std::string demangleFuncName(const char* name);
BBNode* getBBNode(BasicBlock* BB, BBNode_vec* bbnode_dag);
ENode* getPhiC(BasicBlock* BB, ENode_vec* enode_dag);
ENode* insertFork(ENode* enode, ENode_vec* enode_dag, bool isControl);
ENode* getBranchC(BasicBlock* BB, ENode_vec* enode_dag);
ENode* getBrCst(BasicBlock* BB, ENode_vec* enode_dag);
void eraseNode(ENode* enode, ENode_vec* enode_dag);
void removeSuccs(ENode* enode, ENode_vec* enode_dag);
void removePreds(ENode* enode, ENode_vec* enode_dag);
ENode* getCallNode(CallInst* callInst, ENode_vec* enode_dag);
Loop* getLoop(std::string loopName, LoopInfo& LI);
std::string getLoopName(Loop* loop);
Loop* getLoopByName(Loop* loop, std::string name);
ENode* getNodeByName(std::string name, ENode_vec* enode_dag);
bool nodeNameCheck(ENode_vec* enode_dag);
