#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace spice::sct::detail {

[[nodiscard]] std::array<std::uint8_t, 32> sha256(
    std::span<const std::uint8_t> bytes) noexcept;

} // namespace spice::sct::detail
