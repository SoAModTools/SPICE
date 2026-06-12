#pragma once

#include "SstSmlModel.h"

#include <cstdint>
#include <span>
#include <string>

namespace spice::sstsml {

class BattleStageParser {
public:
    [[nodiscard]] static BattleStageParseResult parsePair(std::span<const std::uint8_t> smlBytes,
        std::span<const std::uint8_t> sstBytes,
        std::string stem = {});
};

} // namespace spice::sstsml
