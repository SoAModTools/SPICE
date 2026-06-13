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
        std::string sourcePath = {});

    [[nodiscard]] static MlkScanResult scanFile(const std::filesystem::path& path);
};

} // namespace spice::mlk

