#pragma once

#include "MllModel.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace spice::mll {

class MllParser {
public:
    [[nodiscard]] static MllFile parse(std::span<const std::uint8_t> bytes,
        std::string sourcePath = {});

    [[nodiscard]] static MllFile parseFile(const std::filesystem::path& path);
};

} // namespace spice::mll
