#pragma once

#include "ContentGraph.h"

#include <string>

namespace spice::contentgraph {

class ContentGraphJsonExporter {
public:
    [[nodiscard]] std::string toJson(const ContentGraph& graph,
        ContentGraphProjection projection = ContentGraphProjection::Full) const;
};

} // namespace spice::contentgraph
