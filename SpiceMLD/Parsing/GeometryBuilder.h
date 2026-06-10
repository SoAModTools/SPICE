#pragma once

#include "MldParser.h"
#include "../Model/GeometryModel.h"

namespace spice::mld::parsing {

class GeometryBuilder {
public:
    [[nodiscard]] model::GeometryBuildResult build(const ParseResult& parseResult) const;
};

} // namespace spice::mld::parsing
