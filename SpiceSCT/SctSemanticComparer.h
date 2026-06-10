#pragma once

#include "SctModel.h"

#include <string>
#include <vector>

namespace spice::sct {

struct SctSemanticCompareResult {
    bool equivalent = true;
    std::vector<std::string> differences;
};

class SctSemanticComparer {
public:
    [[nodiscard]] SctSemanticCompareResult compare(
        const SctParseResult& lhs,
        const SctParseResult& rhs) const;
};

} // namespace spice::sct
