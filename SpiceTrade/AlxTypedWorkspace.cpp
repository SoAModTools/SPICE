#include "AlxTypedWorkspace.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <utility>

namespace spice::trade::alx {
namespace {

CsvDiagnostic workspaceDiagnostic(
    std::string message,
    const std::filesystem::path& relativePath = {})
{
    return CsvDiagnostic{
        .severity = DiagnosticSeverity::Error,
        .message = std::move(message),
        .relativePath = relativePath,
    };
}

template <typename ReadResult, typename Table>
void adoptReadResult(
    ReadResult&& read,
    std::optional<TrackedAlxTable<Table>>& destination,
    std::vector<CsvDiagnostic>& diagnostics,
    const std::filesystem::path& relativePath)
{
    for (auto& diagnostic : read.diagnostics) {
        diagnostic.relativePath = relativePath;
        diagnostics.push_back(std::move(diagnostic));
    }
    if (!read.ok()) {
        return;
    }
    destination = TrackedAlxTable<Table>{
        .locale = *read.locale,
        .format = read.format,
        .baseline = *read.table,
        .current = std::move(*read.table),
    };
}

bool writeBytes(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes,
    std::vector<CsvDiagnostic>& diagnostics,
    const std::filesystem::path& relativePath)
{
    std::error_code error{};
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        diagnostics.push_back(workspaceDiagnostic(
            "Could not create ALX workspace output directory",
            relativePath));
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        diagnostics.push_back(workspaceDiagnostic(
            "Could not open ALX workspace output file",
            relativePath));
        return false;
    }
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!output.good()) {
        diagnostics.push_back(workspaceDiagnostic(
            "Could not write ALX workspace output file",
            relativePath));
        return false;
    }
    return true;
}

} // namespace

std::vector<AlxTableKind> AlxWorkspace::changedTables() const
{
    std::vector<AlxTableKind> changed{};
    if (enemies.has_value() && enemies->changed()) {
        changed.push_back(AlxTableKind::Enemy);
    }
    if (enemyEncounters.has_value() && enemyEncounters->changed()) {
        changed.push_back(AlxTableKind::EnemyEncounter);
    }
    if (enemyEvents.has_value() && enemyEvents->changed()) {
        changed.push_back(AlxTableKind::EnemyEvent);
    }
    return changed;
}

bool AlxWorkspaceReadResult::ok() const noexcept
{
    return workspace.has_value() && !hasErrors(diagnostics);
}

AlxWorkspaceReadResult AlxWorkspaceReader::read(
    const std::filesystem::path& sourceRoot,
    const std::span<const AlxTableKind> tables) const
{
    AlxWorkspaceReadResult result{};
    if (sourceRoot.empty()) {
        result.diagnostics.push_back(workspaceDiagnostic("ALX workspace source root must not be empty"));
        return result;
    }
    if (tables.empty()) {
        result.diagnostics.push_back(workspaceDiagnostic("At least one whitelisted ALX table is required"));
        return result;
    }

    std::set<AlxTableKind> requested{};
    AlxWorkspace workspace{};
    for (const auto kind : tables) {
        const auto relativePath = std::filesystem::path(canonicalFilename(kind));
        if (!requested.insert(kind).second) {
            result.diagnostics.push_back(workspaceDiagnostic(
                "Whitelisted ALX table was requested more than once",
                relativePath));
            continue;
        }

        const auto inputPath = sourceRoot / relativePath;
        switch (kind) {
        case AlxTableKind::Enemy:
            adoptReadResult(
                EnemyCsvCodec{}.readFile(inputPath),
                workspace.enemies,
                result.diagnostics,
                relativePath);
            break;
        case AlxTableKind::EnemyEncounter:
            adoptReadResult(
                EnemyEncounterCsvCodec{}.readFile(inputPath),
                workspace.enemyEncounters,
                result.diagnostics,
                relativePath);
            break;
        case AlxTableKind::EnemyEvent:
            adoptReadResult(
                EnemyEventCsvCodec{}.readFile(inputPath),
                workspace.enemyEvents,
                result.diagnostics,
                relativePath);
            break;
        }
    }

    if (!hasErrors(result.diagnostics)) {
        result.workspace = std::move(workspace);
    }
    return result;
}

bool AlxWorkspaceWriteResult::ok() const noexcept
{
    return !hasErrors(diagnostics);
}

AlxWorkspaceWriteResult AlxWorkspaceWriter::writeChanged(
    AlxWorkspace& workspace,
    const std::filesystem::path& outputRoot) const
{
    AlxWorkspaceWriteResult result{};
    if (outputRoot.empty()) {
        result.diagnostics.push_back(workspaceDiagnostic("ALX workspace output root must not be empty"));
        return result;
    }

    struct PendingWrite {
        AlxTableKind kind{};
        std::vector<std::uint8_t> bytes{};
    };
    std::vector<PendingWrite> pending{};
    const auto stage = [&](const AlxTableKind kind, CsvWriteResult write) {
        const auto relativePath = std::filesystem::path(canonicalFilename(kind));
        for (auto& diagnostic : write.diagnostics) {
            diagnostic.relativePath = relativePath;
            result.diagnostics.push_back(std::move(diagnostic));
        }
        if (write.ok()) {
            pending.push_back(PendingWrite{ .kind = kind, .bytes = std::move(write.bytes) });
        }
    };

    if (workspace.enemies.has_value() && workspace.enemies->changed()) {
        stage(AlxTableKind::Enemy, EnemyCsvCodec{}.write(
            workspace.enemies->current,
            workspace.enemies->locale,
            workspace.enemies->format));
    }
    if (workspace.enemyEncounters.has_value() && workspace.enemyEncounters->changed()) {
        stage(AlxTableKind::EnemyEncounter, EnemyEncounterCsvCodec{}.write(
            workspace.enemyEncounters->current,
            workspace.enemyEncounters->locale,
            workspace.enemyEncounters->format));
    }
    if (workspace.enemyEvents.has_value() && workspace.enemyEvents->changed()) {
        stage(AlxTableKind::EnemyEvent, EnemyEventCsvCodec{}.write(
            workspace.enemyEvents->current,
            workspace.enemyEvents->locale,
            workspace.enemyEvents->format));
    }
    if (hasErrors(result.diagnostics)) {
        return result;
    }

    for (const auto& item : pending) {
        const auto relativePath = std::filesystem::path(canonicalFilename(item.kind));
        if (!writeBytes(outputRoot / relativePath, item.bytes, result.diagnostics, relativePath)) {
            continue;
        }
        switch (item.kind) {
        case AlxTableKind::Enemy:
            workspace.enemies->baseline = workspace.enemies->current;
            break;
        case AlxTableKind::EnemyEncounter:
            workspace.enemyEncounters->baseline = workspace.enemyEncounters->current;
            break;
        case AlxTableKind::EnemyEvent:
            workspace.enemyEvents->baseline = workspace.enemyEvents->current;
            break;
        }
        result.writtenTables.push_back(item.kind);
    }
    return result;
}

} // namespace spice::trade::alx
