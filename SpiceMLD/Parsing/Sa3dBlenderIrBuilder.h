#pragma once

#include "MldParser.h"
#include "../Model/BlenderIrModel.h"

namespace spice::mld::parsing {

class Sa3dBlenderIrBuilder {
public:
    [[nodiscard]] model::BlenderIrScene build(const ParseResult& parseResult) const;
};

} // namespace spice::mld::parsing
