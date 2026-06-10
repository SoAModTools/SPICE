#pragma once

#include "../Model/MldFile.h"

#include <cstdint>
#include <vector>

namespace spice::mld::exporting {

struct MldExportOptions {
    model::TargetPlatform platform = model::TargetPlatform::GameCube;
    bool compressAklz = false;
};

class MldFileExporter {
public:
    [[nodiscard]] std::vector<std::uint8_t> exportFile(
        const model::MldFile& file,
        const MldExportOptions& options = {}) const;
};

} // namespace spice::mld::exporting

