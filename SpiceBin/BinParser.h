#pragma once

#include "BinModel.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace spice::bin {

[[nodiscard]] BinIndexedTableProbe probeIndexedTable(std::span<const std::uint8_t> bytes);
[[nodiscard]] BinFile parseBytes(std::vector<std::uint8_t> bytes, std::string sourcePath = {});
[[nodiscard]] BinFile parseFile(const std::filesystem::path& path);

} // namespace spice::bin
