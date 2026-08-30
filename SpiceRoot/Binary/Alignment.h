#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <limits>

namespace spice::root {

[[nodiscard]] inline constexpr std::size_t align_up(std::size_t value, std::size_t alignment) {
    return alignment == 0U ? value : ((value + alignment - 1U) / alignment) * alignment;
}

[[nodiscard]] inline constexpr bool is_aligned(std::size_t value, std::size_t alignment) {
    return alignment == 0U || (value % alignment) == 0U;
}

[[nodiscard]] inline constexpr bool bounds_contains(std::size_t size, std::size_t offset, std::size_t length) {
    return offset <= size && length <= size - offset;
}

[[nodiscard]] inline constexpr std::optional<std::size_t> checked_add(
    std::size_t left,
    std::size_t right) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return std::nullopt;
    }
    return left + right;
}

[[nodiscard]] inline constexpr std::optional<std::size_t> checked_multiply(
    std::size_t left,
    std::size_t right) {
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        return std::nullopt;
    }
    return left * right;
}

[[nodiscard]] inline constexpr std::optional<std::size_t> checked_table_end(
    std::size_t offset,
    std::size_t count,
    std::size_t stride) {
    const auto byteCount = checked_multiply(count, stride);
    return byteCount.has_value() ? checked_add(offset, *byteCount) : std::nullopt;
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

} // namespace spice::root
