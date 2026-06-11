#pragma once

#include "SctModel.h"

#include <cstdint>
#include <span>
#include <string>

namespace spice::sct {

struct SctParseOptions {
    bool decodeUnreachedCode = false;
};

class SctParser {
public:
    [[nodiscard]] SctParseResult parse(
        std::span<const std::uint8_t> bytes,
        std::string sourcePath = {},
        SctParseOptions options = {}) const;
    [[nodiscard]] SctParseResult parseFile(
        const std::string& sourcePath,
        SctParseOptions options = {}) const;
};

} // namespace spice::sct
