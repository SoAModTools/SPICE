#pragma once

#include "../Model/GvmTextureModel.h"

#include <cstdint>
#include <vector>

namespace spice::gvm::encoding {

struct EncodeOptions {
    model::TextureFormat textureFormat = model::TextureFormat::RGBA8;
    model::PaletteFormat paletteFormat = model::PaletteFormat::None;
    bool generateMipmaps = false;
    bool hasGlobalIndex = false;
    std::uint32_t globalIndex = 0;
};

[[nodiscard]] std::vector<std::uint8_t> encodeGvr(const model::RgbaImage& image,
    const EncodeOptions& options = {});

[[nodiscard]] std::vector<std::uint8_t> encodeRgba8Gvr(const model::RgbaImage& image,
    const EncodeOptions& options = {});

} // namespace spice::gvm::encoding
