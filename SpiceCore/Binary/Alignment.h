#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace spice::core {

[[nodiscard]] inline constexpr std::size_t align_up(std::size_t value, std::size_t alignment) {
    return alignment == 0U ? value : ((value + alignment - 1U) / alignment) * alignment;
}

[[nodiscard]] inline constexpr bool is_aligned(std::size_t value, std::size_t alignment) {
    return alignment == 0U || (value % alignment) == 0U;
}

[[nodiscard]] inline constexpr bool bounds_contains(std::size_t size, std::size_t offset, std::size_t length) {
    return offset <= size && length <= size - offset;
}

[[nodiscard]] inline constexpr std::optional<std::size_t> add_relative_offset(
    std::size_t base,
    std::int32_t relative,
    std::size_t size) {
    const auto target = static_cast<std::int64_t>(base) + static_cast<std::int64_t>(relative);
    if (target < 0 || static_cast<std::uint64_t>(target) > static_cast<std::uint64_t>(size)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(target);
}

} // namespace spice::core

