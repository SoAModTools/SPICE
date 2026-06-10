#pragma once

#include "ContentGraph.h"

#include "../SpiceSCT/SctModel.h"

namespace spice::contentgraph {

struct SctGraphBuildOptions {
    ContentGraphDetailLevel detailLevel = ContentGraphDetailLevel::Instructions;
    bool includeResourceEdges = true;
    bool includeFlagEdges = true;
};

class SctGraphBuilder {
public:
    void addToGraph(ContentGraph& graph, const spice::sct::SctParseResult& parseResult,
        const SctGraphBuildOptions& options = {}) const;
};

} // namespace spice::contentgraph
