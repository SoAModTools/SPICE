#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace spice::ect {

inline constexpr std::size_t kEncounterEntriesPerTable = 32U;
inline constexpr std::size_t kOverworldTablesPerEntry = 8U;

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error,
};

enum class EctLayout {
    Flat,
    OverworldIndexed,
};

enum class EctTargetPlatform {
    Dreamcast,
    GameCube,
};

struct EctDiagnostic {
    DiagnosticSeverity severity{ DiagnosticSeverity::Info };
    std::string message{};
    std::optional<std::size_t> offset{};
};

struct EctEncounterEntry {
    std::uint16_t encounterId{ 0U };
    std::uint16_t encounterRate{ 0U };

    bool operator==(const EctEncounterEntry&) const = default;
};

struct EctEncounterTable {
    std::uint16_t stage{ 0U };
    std::uint16_t overallEncounterRate{ 0U };
    std::array<EctEncounterEntry, kEncounterEntriesPerTable> encounters{};

    bool operator==(const EctEncounterTable&) const = default;
};

struct EctFlatContent {
    std::vector<EctEncounterTable> tables{};

    bool operator==(const EctFlatContent&) const = default;
};

struct EctOverworldEntry {
    std::string title{};
    std::array<EctEncounterTable, kOverworldTablesPerEntry> tables{};

    bool operator==(const EctOverworldEntry&) const = default;
};

struct EctOverworldContent {
    std::vector<EctOverworldEntry> entries{};

    bool operator==(const EctOverworldContent&) const = default;
};

using EctContent = std::variant<EctFlatContent, EctOverworldContent>;

struct EctFile {
    EctContent content{ EctFlatContent{} };

    [[nodiscard]] EctLayout layout() const noexcept;

    bool operator==(const EctFile&) const = default;
};

[[nodiscard]] const char* toString(DiagnosticSeverity severity);
[[nodiscard]] const char* toString(EctLayout layout);
[[nodiscard]] const char* toString(EctTargetPlatform platform);

} // namespace spice::ect
