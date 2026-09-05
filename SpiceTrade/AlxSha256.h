#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace spice::trade::alx::detail {

[[nodiscard]] std::array<std::uint8_t, 32U> sha256(std::span<const std::uint8_t> bytes) noexcept;

} // namespace spice::trade::alx::detail
