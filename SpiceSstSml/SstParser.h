#pragma once

#include "SstSmlModel.h"

#include <cstdint>
#include <span>
#include <string>

namespace spice::sstsml {

class SstParser {
public:
    [[nodiscard]] static SstParseResult parse(std::span<const std::uint8_t> bytes,
        std::string sourcePath = {});

    [[nodiscard]] static std::uint32_t commandPayloadSize(std::int16_t type);
    [[nodiscard]] static bool isKnownCommandType(std::int16_t type);
    [[nodiscard]] static bool isModelIndexCommandType(std::int16_t type);
    [[nodiscard]] static std::vector<CommandFieldSummary> fieldSummariesForType(std::int16_t type);
};

} // namespace spice::sstsml
