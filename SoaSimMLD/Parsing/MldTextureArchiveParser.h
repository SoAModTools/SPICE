#pragma once

#include "../Model/MldTextureArchiveModel.h"
#include "../../SpiceCore/Binary/Endian.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace soasim::mld::parsing {

[[nodiscard]] model::MldTextureArchive parseMldTextureArchive(std::span<const std::uint8_t> bytes,
    std::size_t textureTableOffset,
    spice::core::Endian endian = spice::core::Endian::Big);

} // namespace soasim::mld::parsing
