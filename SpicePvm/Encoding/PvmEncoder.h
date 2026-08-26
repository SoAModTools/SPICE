#pragma once

#include "PvrEncoder.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace spice::pvm::encoding {

struct PvmEncodeEntry {
    std::uint16_t archiveIndex = 0;
    std::string name;
    std::vector<std::uint8_t> pvrBytes;
    std::optional<std::uint32_t> globalIndex;
    std::uint8_t dimensionUnknownByte = 0;
};

struct PvmEncodeOptions {
    // Known record fields: global index, dimensions, formats, and filename.
    std::uint16_t flags = 0x000F;
    std::vector<std::uint8_t> headerPadding;
    std::vector<std::uint8_t> interstitialMetadata;
};

struct PvmEncodeResult {
    model::ParseStatus status = model::ParseStatus::Failed;
    std::vector<std::uint8_t> bytes;
    model::ByteRange pvmhRange;
    std::vector<model::ByteRange> textureRanges;
    std::vector<model::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] PvmEncodeResult encodePvmArchive(
    std::span<const PvmEncodeEntry> entries,
    const PvmEncodeOptions& options = {});

} // namespace spice::pvm::encoding
