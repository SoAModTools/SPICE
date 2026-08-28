#pragma once

#include "../Model/MldTextureArchiveModel.h"
#include "../../SpiceRoot/Binary/Endian.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace spice::mld::parsing {

[[nodiscard]] model::MldTextureArchive parseMldTextureArchive(std::span<const std::uint8_t> bytes,
    std::size_t textureTableOffset,
    spice::root::Endian endian = spice::root::Endian::Big);

} // namespace spice::mld::parsing
