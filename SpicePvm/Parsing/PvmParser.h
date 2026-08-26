#pragma once

#include "SpicePvm/Model/PvmTextureModel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace spice::pvm::parsing {

[[nodiscard]] model::PvrTexture parsePvrTexture(
    std::span<const std::uint8_t> bytes,
    std::size_t sourceOffset = 0);

[[nodiscard]] model::PvrScanResult scanPvrTextures(
    std::span<const std::uint8_t> bytes,
    std::size_t startOffset = 0,
    std::optional<std::size_t> expectedCount = std::nullopt);

[[nodiscard]] model::PvmArchive parsePvmArchive(
    std::span<const std::uint8_t> bytes,
    std::size_t sourceOffset = 0);

} // namespace spice::pvm::parsing
