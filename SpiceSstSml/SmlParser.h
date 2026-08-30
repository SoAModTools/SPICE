#pragma once

#include "SstSmlModel.h"

#include <cstdint>
#include <span>
#include <string>

namespace spice::sstsml {

class SmlParser {
public:
    [[nodiscard]] static SmlParseResult parse(std::span<const std::uint8_t> bytes,
        std::string sourcePath = {},
        const ParseOptions& options = {});
};

} // namespace spice::sstsml
