#pragma once

#include "../Model/GvmTextureModel.h"

namespace spice::gvm::encoding {

struct EncodeOptions {
    model::TextureFormat textureFormat = model::TextureFormat::RGB5A3;
    model::PaletteFormat paletteFormat = model::PaletteFormat::None;
    bool generateMipmaps = false;
};

// Placeholder for the eventual RGBA8 -> GameCube texture path. The decode model
// already preserves the metadata needed to add this without changing callers.

} // namespace spice::gvm::encoding
