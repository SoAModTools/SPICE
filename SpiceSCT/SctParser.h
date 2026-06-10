#pragma once

#include "SctModel.h"

#include <cstdint>
#include <span>
#include <string>

namespace spice::sct {

class SctParser {
public:
    [[nodiscard]] SctParseResult parse(std::span<const std::uint8_t> bytes, std::string sourcePath = {}) const;
    [[nodiscard]] SctParseResult parseFile(const std::string& sourcePath) const;
};

} // namespace spice::sct
