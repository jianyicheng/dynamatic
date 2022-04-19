#include "Nodes.h"

using namespace llvm;

class MyCFGPass : public llvm::FunctionPass {

public:
    static char ID;
    std::vector<BBNode*>* bbnode_dag;
    std::vector<ENode*>* enode_dag;

    MyCFGPass() : llvm::FunctionPass(ID) {}

    virtual void getAnalysisUsage(AnalysisUsage& AU) const;

    void compileAndProduceDOTFile(Function& F);

    bool runOnFunction(Function& F) override { this->compileAndProduceDOTFile(F); return true; }

    void print(llvm::raw_ostream& OS, const Module* M) const override {}
};
