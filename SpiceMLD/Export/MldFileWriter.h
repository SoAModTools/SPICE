#pragma once

#include "../Model/MldFile.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace spice::mld::exporting {

struct MldWriteOptions {
    std::optional<model::TargetPlatform> platform{};
    std::optional<bool> compressAklz{};
    bool rejectUnknownPointerRelocations = true;
};

struct MldWriteLayoutRecord {
    std::string kind{};
    std::uint32_t sourceOffset = 0;
    std::uint32_t outputOffset = 0;
    std::size_t sourceSize = 0;
    std::size_t outputSize = 0;
    bool relocated = false;
    bool copiedVerbatim = false;
};

struct MldWriteResult {
    std::vector<std::uint8_t> bytes{};
    std::vector<model::MldDiagnostic> diagnostics{};
    std::vector<MldWriteLayoutRecord> layout{};
    std::size_t sourceSize = 0;
    std::size_t outputSize = 0;

    [[nodiscard]] bool ok() const noexcept;
};

class MldFileWriter {
public:
    [[nodiscard]] MldWriteResult write(
        const model::MldFile& file,
        const MldWriteOptions& options = {}) const;
};

} // namespace spice::mld::exporting
