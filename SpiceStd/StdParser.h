#pragma once

#include "StdModel.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace spice::stdfile {

struct StdParseOptions {
    std::optional<spice::root::Endian> forcedEndian{};
};

[[nodiscard]] StdFile parseBytes(std::vector<std::uint8_t> bytes,
    std::string sourcePath = {},
    const StdParseOptions& options = {});
[[nodiscard]] StdFile parseFile(const std::filesystem::path& path,
    const StdParseOptions& options = {});

[[nodiscard]] StdExportResult exportBytes(
    const StdFile& file,
    StdExportMode mode = StdExportMode::OriginalSourceBytes);

} // namespace spice::stdfile
