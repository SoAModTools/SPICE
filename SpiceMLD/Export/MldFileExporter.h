#pragma once

#include "../Model/MldFile.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace spice::mld::exporting {

struct MldTextureReplacement {
    std::size_t textureIndex = 0;
    std::vector<std::uint8_t> gvrData{};
    bool allowPostArchiveShift = false;
};

struct MldExportOptions {
    model::TargetPlatform platform = model::TargetPlatform::GameCube;
    bool compressAklz = false;
    std::optional<MldTextureReplacement> textureReplacement{};
};

// Compatibility wrapper. New callers should use MldFileWriter.
class MldFileExporter {
public:
    [[nodiscard]] std::vector<std::uint8_t> exportFile(
        const model::MldFile& file,
        const MldExportOptions& options = {}) const;
};

} // namespace spice::mld::exporting
