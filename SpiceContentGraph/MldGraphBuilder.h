#pragma once

#include "ContentGraph.h"

#include "../SpiceMLD/Parsing/MldParser.h"

#include <string>

namespace spice::contentgraph {

class MldGraphBuilder {
public:
    void addToGraph(ContentGraph& graph, const std::string& sourcePath,
        const spice::mld::parsing::ParseResult& parseResult) const;
};

} // namespace spice::contentgraph
