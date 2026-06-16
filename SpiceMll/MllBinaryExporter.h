#pragma once

#include "MllModel.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace spice::mll {

struct MllPayloadReplacement {
    std::size_t memberIndex{ 0U };
    std::vector<std::uint8_t> payload{};
};

struct MllExportOptions {
    bool compressAklz{ false };
    bool allowPayloadResize{ false };
    bool requireRawWord1cSentinel{ true };
    std::vector<MllPayloadReplacement> payloadReplacements{};
};

class MllBinaryExporter {
public:
    [[nodiscard]] std::vector<std::uint8_t> exportFile(
        const MllFile& file,
        const MllExportOptions& options = {}) const;

    [[nodiscard]] std::vector<std::uint8_t> exportDecoded(
        const MllFile& file,
        std::span<const std::uint8_t> originalDecodedBytes,
        const MllExportOptions& options = {}) const;
};

} // namespace spice::mll
