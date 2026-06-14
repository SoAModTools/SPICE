#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace spice::ect {

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error,
};

enum class EctLayoutHint {
    Auto,
    FlatTables,
    IndexedContainer,
};

enum class EctLayout {
    Unknown,
    FlatTables,
    IndexedContainer,
};

struct EctDiagnostic {
    DiagnosticSeverity severity{ DiagnosticSeverity::Info };
    std::string message{};
    std::uint32_t offset{ 0U };
};

struct EctEncounterEntry {
    std::size_t entryIndex{ 0U };
    std::uint16_t encounterId{ 0U };
    std::uint16_t encounterRate{ 0U };
};

struct EctEncounterTable {
    std::size_t tableIndex{ 0U };
    std::uint32_t decodedOffset{ 0U };
    std::uint16_t stage{ 0U };
    std::uint16_t overallEncounterRate{ 0U };
    std::vector<EctEncounterEntry> encounters{};
};

struct EctIndexEntry {
    std::size_t rowIndex{ 0U };
    std::string title{};
    std::uint32_t dataOffset{ 0U };
    bool skippedDam{ false };
    std::optional<std::size_t> firstParsedTableIndex{};
    std::size_t tableCount{ 0U };
};

struct EctFile {
    std::string sourcePath{};
    bool sourceWasCompressedAklz{ false };
    std::uint32_t rawSize{ 0U };
    std::uint32_t decodedSize{ 0U };
    EctLayout layout{ EctLayout::Unknown };
    std::vector<EctEncounterTable> tables{};
    std::vector<EctIndexEntry> indexEntries{};
    std::vector<EctDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const;
};

[[nodiscard]] const char* toString(DiagnosticSeverity severity);
[[nodiscard]] const char* toString(EctLayoutHint hint);
[[nodiscard]] const char* toString(EctLayout layout);

} // namespace spice::ect
