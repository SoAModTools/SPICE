#pragma once

#include "../Model/MldGroundModel.h"
#include "../../SpiceCore/Binary/Endian.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace spice::mld::parsing {

using GobjNode = model::GobjNode;

struct GobjDecodeResult {
    bool decoded = false;
    std::uint32_t sourceOffset = 0;
    model::GobjData data{};
    std::vector<GobjNode> nodes{};
    std::vector<std::size_t> rootNodeIndices{};
    std::vector<std::string> diagnostics{};
};

class GobjParser {
public:
    [[nodiscard]] GobjDecodeResult decode(std::span<const std::uint8_t> blockBytes,
        std::uint32_t sourceOffset = 0,
        spice::core::Endian endian = spice::core::Endian::Big) const;
};

} // namespace spice::mld::parsing
