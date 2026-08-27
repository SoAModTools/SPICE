#pragma once

#include "AlxTypedCodec.h"

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace spice::trade::alx {

template <typename Table>
struct TrackedAlxTable {
    AlxLocale locale{};
    CsvFormat format{};
    Table baseline{};
    Table current{};

    [[nodiscard]] bool changed() const noexcept
    {
        return current != baseline;
    }
};

struct AlxWorkspace {
    std::optional<TrackedAlxTable<EnemyTable>> enemies{};
    std::optional<TrackedAlxTable<EnemyEncounterTable>> enemyEncounters{};
    std::optional<TrackedAlxTable<EnemyEventTable>> enemyEvents{};

    [[nodiscard]] std::vector<AlxTableKind> changedTables() const;
};

struct AlxWorkspaceReadResult {
    std::optional<AlxWorkspace> workspace{};
    std::vector<CsvDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept;
};

class AlxWorkspaceReader {
public:
    [[nodiscard]] AlxWorkspaceReadResult read(
        const std::filesystem::path& sourceRoot,
        std::span<const AlxTableKind> tables) const;
};

struct AlxWorkspaceWriteResult {
    std::vector<AlxTableKind> writtenTables{};
    std::vector<CsvDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept;
};

class AlxWorkspaceWriter {
public:
    [[nodiscard]] AlxWorkspaceWriteResult writeChanged(
        AlxWorkspace& workspace,
        const std::filesystem::path& outputRoot) const;
};

} // namespace spice::trade::alx
