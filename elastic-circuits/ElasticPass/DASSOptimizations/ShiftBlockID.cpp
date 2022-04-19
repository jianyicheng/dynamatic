#include "ElasticPass/CircuitGenerator.h"
#include "ElasticPass/Utils.h"

#include <algorithm>

//--------------------------------------------------------//
// Function: shiftBlockID
// Shift all the block IDs by an offset so blocks start from 1
//--------------------------------------------------------//

static std::string shiftedBlock(std::string block, int offset) {
    return "block" + std::to_string(std::stoi(block.substr(block.find("block") + 5)) - offset);
}

void CircuitGenerator::shiftBlockID() {
    int offset = 1000;
    for (auto enode : *enode_dag)
        if (enode->bbId != 0)
            offset = std::min(offset, enode->bbId);
    offset--;

    if (offset > 0)
        for (auto enode : *enode_dag)
            if (enode->bbId != 0)
                enode->bbId = enode->bbId - offset;
}
