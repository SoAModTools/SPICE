#pragma once

#include "MlkModel.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace spice::mlk {

class MlkScanner {
public:
    [[nodiscard]] static MlkScanResult scan(std::span<const std::uint8_t> bytes,
        std::string sourcePath = {},
        const MlkParseOptions& options = {});

    [[nodiscard]] static MlkScanResult scanFile(const std::filesystem::path& path,
        const MlkParseOptions& options = {});
};

} // namespace spice::mlk
