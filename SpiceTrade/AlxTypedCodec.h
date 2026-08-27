#pragma once

#include "AlxTypedModel.h"
#include "CsvReader.h"
#include "CsvWriter.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace spice::trade::alx {

template <typename Table>
struct AlxTableReadResult {
    std::optional<Table> table{};
    std::optional<AlxLocale> locale{};
    CsvFormat format{};
    std::vector<CsvDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept
    {
        return table.has_value() && locale.has_value() && !hasErrors(diagnostics);
    }
};

using EnemyTableReadResult = AlxTableReadResult<EnemyTable>;
using EnemyEncounterTableReadResult = AlxTableReadResult<EnemyEncounterTable>;
using EnemyEventTableReadResult = AlxTableReadResult<EnemyEventTable>;

class EnemyCsvCodec {
public:
    [[nodiscard]] EnemyTableReadResult parse(
        std::span<const std::uint8_t> bytes,
        const std::filesystem::path& diagnosticPath = {}) const;
    [[nodiscard]] EnemyTableReadResult readFile(const std::filesystem::path& path) const;
    [[nodiscard]] CsvWriteResult write(
        const EnemyTable& table,
        AlxLocale locale,
        const CsvFormat& format = {}) const;
    [[nodiscard]] CsvWriteResult writeFile(
        const EnemyTable& table,
        AlxLocale locale,
        const std::filesystem::path& path,
        const CsvFormat& format = {}) const;
};

class EnemyEncounterCsvCodec {
public:
    [[nodiscard]] EnemyEncounterTableReadResult parse(
        std::span<const std::uint8_t> bytes,
        const std::filesystem::path& diagnosticPath = {}) const;
    [[nodiscard]] EnemyEncounterTableReadResult readFile(const std::filesystem::path& path) const;
    [[nodiscard]] CsvWriteResult write(
        const EnemyEncounterTable& table,
        AlxLocale locale,
        const CsvFormat& format = {}) const;
    [[nodiscard]] CsvWriteResult writeFile(
        const EnemyEncounterTable& table,
        AlxLocale locale,
        const std::filesystem::path& path,
        const CsvFormat& format = {}) const;
};

class EnemyEventCsvCodec {
public:
    [[nodiscard]] EnemyEventTableReadResult parse(
        std::span<const std::uint8_t> bytes,
        const std::filesystem::path& diagnosticPath = {}) const;
    [[nodiscard]] EnemyEventTableReadResult readFile(const std::filesystem::path& path) const;
    [[nodiscard]] CsvWriteResult write(
        const EnemyEventTable& table,
        AlxLocale locale,
        const CsvFormat& format = {}) const;
    [[nodiscard]] CsvWriteResult writeFile(
        const EnemyEventTable& table,
        AlxLocale locale,
        const std::filesystem::path& path,
        const CsvFormat& format = {}) const;
};

} // namespace spice::trade::alx
