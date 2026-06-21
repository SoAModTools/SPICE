#pragma once

#include "StdModel.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace spice::stdfile {

[[nodiscard]] StdFile parseBytes(std::vector<std::uint8_t> bytes, std::string sourcePath = {});
[[nodiscard]] StdFile parseFile(const std::filesystem::path& path);

[[nodiscard]] StdExportResult exportBytes(
    const StdFile& file,
    StdExportMode mode = StdExportMode::OriginalSourceBytes);

} // namespace spice::stdfile
