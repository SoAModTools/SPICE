#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace spice::sstsml {

struct SstCommandCatalogEntry {
    std::int16_t type{ 0 };
    std::string_view label{};
    std::string_view description{};
};

[[nodiscard]] std::optional<SstCommandCatalogEntry> commandCatalogEntry(std::int16_t type);

} // namespace spice::sstsml
