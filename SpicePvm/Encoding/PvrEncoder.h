#pragma once

#include "../Model/PvmTextureModel.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace spice::pvm::encoding {

struct PvrEncodeOptions {
    model::PixelFormat pixelFormat = model::PixelFormat::Argb4444;
    model::DataLayout dataLayout = model::DataLayout::Twiddled;
    bool generateMipmaps = false;
    bool includeGlobalIndex = false;
    std::uint32_t globalIndex = 0;
    std::array<std::uint8_t, 4> gbixTrailingBytes{};
    std::array<std::uint8_t, 2> pvrtUnknownHeader{};
    bool alignVqMipmapsTo32Bytes = true;
};

struct PvrEncodeResult {
    model::ParseStatus status = model::ParseStatus::Failed;
    std::vector<std::uint8_t> bytes;
    model::ByteRange sourceRange;
    std::optional<model::ByteRange> gbixRange;
    model::ByteRange pvrtRange;
    model::ByteRange textureDataRange;
    model::ByteRange codebookRange;
    std::vector<model::ByteRange> mipSourceRanges;
    std::vector<model::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

// Mip images are supplied in logical largest-to-smallest order. When a single
// image is supplied for a mipmapped layout, generateMipmaps must be enabled.
[[nodiscard]] PvrEncodeResult encodePvrTexture(
    const model::RgbaImage& image,
    const PvrEncodeOptions& options = {});

[[nodiscard]] PvrEncodeResult encodePvrTexture(
    std::span<const model::RgbaImage> mipImages,
    const PvrEncodeOptions& options = {});

} // namespace spice::pvm::encoding
