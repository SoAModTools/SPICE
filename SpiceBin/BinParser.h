#pragma once

#include "BinModel.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace spice::bin {

struct BinParseOptions {
    std::optional<spice::root::Endian> forcedEndian{};
};

[[nodiscard]] BinIndexedTableProbe probeIndexedTable(std::span<const std::uint8_t> bytes,
    spice::root::Endian endian);
[[nodiscard]] BinIndexedTableProbe probeIndexedTable(std::span<const std::uint8_t> bytes);
[[nodiscard]] BinFile parseBytes(std::vector<std::uint8_t> bytes,
    std::string sourcePath = {},
    const BinParseOptions& options = {});
[[nodiscard]] BinFile parseFile(const std::filesystem::path& path,
    const BinParseOptions& options = {});

} // namespace spice::bin
