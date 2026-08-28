#pragma once

#include "../../SpiceRoot/Binary/EndianReader.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace spice::mld::common {

[[nodiscard]] inline std::optional<std::uint32_t> readU32AtLE(std::span<const std::uint8_t> bytes, const std::size_t offset) {
    return spice::root::EndianReader(bytes, spice::root::Endian::Little).try_read_u32(offset);
}

[[nodiscard]] inline std::optional<std::uint32_t> readU32AtBE(std::span<const std::uint8_t> bytes, const std::size_t offset) {
    return spice::root::EndianReader(bytes, spice::root::Endian::Big).try_read_u32(offset);
}

[[nodiscard]] inline std::optional<float> readF32AtLE(std::span<const std::uint8_t> bytes, const std::size_t offset) {
    return spice::root::EndianReader(bytes, spice::root::Endian::Little).try_read_f32(offset);
}

[[nodiscard]] inline std::optional<float> readF32AtBE(std::span<const std::uint8_t> bytes, const std::size_t offset) {
    return spice::root::EndianReader(bytes, spice::root::Endian::Big).try_read_f32(offset);
}

} // namespace spice::mld::common
