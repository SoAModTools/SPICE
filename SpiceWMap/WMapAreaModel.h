#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace spice::wmap {

inline constexpr std::size_t kWMapAreaLayerCount = 3U;
inline constexpr std::size_t kWMapAreaRowCount = 24U;
inline constexpr std::size_t kWMapAreaColumnCount = 28U;
inline constexpr std::size_t kWMapAreaLayerSize =
    kWMapAreaRowCount * kWMapAreaColumnCount;
inline constexpr std::size_t kWMapAreaSerializedSize =
    kWMapAreaLayerCount * kWMapAreaLayerSize;
inline constexpr std::uint8_t kWMapAreaMaximumCellId = 15U;

using WMapAreaRow = std::array<std::uint8_t, kWMapAreaColumnCount>;
using WMapAreaLayer = std::array<WMapAreaRow, kWMapAreaRowCount>;

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error,
};

enum class WMapAreaStorage {
    Raw,
    Aklz,
};

struct WMapAreaDiagnostic {
    DiagnosticSeverity severity{ DiagnosticSeverity::Info };
    std::string message{};
    std::optional<std::size_t> offset{};
};

struct WMapAreaDocument {
    std::array<WMapAreaLayer, kWMapAreaLayerCount> layers{};

    [[nodiscard]] std::uint8_t& cell(
        std::size_t layer,
        std::size_t row,
        std::size_t column);
    [[nodiscard]] const std::uint8_t& cell(
        std::size_t layer,
        std::size_t row,
        std::size_t column) const;

    bool operator==(const WMapAreaDocument&) const = default;
};

[[nodiscard]] constexpr std::size_t serializedOffset(
    const std::size_t layer,
    const std::size_t row,
    const std::size_t column) noexcept {
    return layer * kWMapAreaLayerSize + row * kWMapAreaColumnCount + column;
}

[[nodiscard]] const char* toString(DiagnosticSeverity severity) noexcept;
[[nodiscard]] const char* toString(WMapAreaStorage storage) noexcept;
[[nodiscard]] bool hasErrors(const std::vector<WMapAreaDiagnostic>& diagnostics) noexcept;

} // namespace spice::wmap
