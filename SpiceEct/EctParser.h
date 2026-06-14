#pragma once

#include "EctModel.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace spice::ect {

struct EctParseOptions {
    EctLayoutHint layoutHint{ EctLayoutHint::Auto };
};

class EctParser {
public:
    [[nodiscard]] static EctFile parse(std::span<const std::uint8_t> bytes,
        std::string sourcePath = {},
        EctParseOptions options = {});

    [[nodiscard]] static EctFile parseFile(const std::filesystem::path& path,
        EctParseOptions options = {});
};

} // namespace spice::ect
