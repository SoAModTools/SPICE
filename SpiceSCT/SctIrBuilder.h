#pragma once

#include "SctModel.h"

namespace spice::sct {

class SctIrBuilder {
public:
    [[nodiscard]] SctParseResult build(const SctParseResult& parseResult) const;
};

} // namespace spice::sct
