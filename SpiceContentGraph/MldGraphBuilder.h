#pragma once

#include "ContentGraph.h"

#include "../SpiceMLD/Model/MldFile.h"

#include <string>

namespace spice::contentgraph {

class MldGraphBuilder {
public:
    void addToGraph(ContentGraph& graph, const std::string& sourcePath,
        const spice::mld::model::MldFile& file) const;
};

} // namespace spice::contentgraph
