#pragma once

#include <cstdint>
#include <string>

namespace spice::sct::detail {

[[nodiscard]] std::string toHexWord(std::uint32_t value);
[[nodiscard]] float floatFromWordBits(std::uint32_t value);

} // namespace spice::sct::detail
