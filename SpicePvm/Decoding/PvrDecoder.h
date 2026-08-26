#pragma once

#include "SpicePvm/Model/PvmTextureModel.h"

namespace spice::pvm::decoding {

[[nodiscard]] model::DecodeResult decodePvrTexture(const model::PvrTexture& texture);

} // namespace spice::pvm::decoding
