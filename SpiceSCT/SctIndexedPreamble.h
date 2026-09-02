#pragma once

#include <algorithm>
#include <cstdint>
#include <span>

namespace spice::sct::detail {

// Shared structural rule for opcode-9 preambles owned by indexed strings and
// indexed-string group markers. The first exact stop word must end the record.
[[nodiscard]] constexpr bool isValidIndexedStringPreamble(
    std::span<const std::uint32_t> words) noexcept {
    if (words.size() < 2u || words.front() != 9u) return false;
    return std::find(words.begin(), words.end(), 0x0000001du) == words.end() - 1u;
}

} // namespace spice::sct::detail
