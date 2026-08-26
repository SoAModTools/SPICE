#pragma once

#include "CsvReader.h"
#include "CsvWriter.h"

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace spice::trade::alx {

struct TrackedDocument {
    std::filesystem::path relativePath{};
    CsvDocument baseline{};
    CsvDocument current{};
    CsvFormat format{};

    [[nodiscard]] bool changed() const noexcept;
};

struct Workspace {
    std::vector<TrackedDocument> documents{};

    [[nodiscard]] TrackedDocument* find(const std::filesystem::path& relativePath) noexcept;
    [[nodiscard]] const TrackedDocument* find(const std::filesystem::path& relativePath) const noexcept;
    [[nodiscard]] std::vector<std::filesystem::path> changedPaths() const;
};

struct WorkspaceReadResult {
    std::optional<Workspace> workspace{};
    std::vector<CsvDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept;
};

class WorkspaceReader {
public:
    [[nodiscard]] WorkspaceReadResult read(
        const std::filesystem::path& sourceRoot,
        std::span<const std::filesystem::path> relativePaths) const;
};

struct WorkspaceWriteResult {
    std::vector<std::filesystem::path> writtenPaths{};
    std::vector<CsvDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept;
};

class WorkspaceWriter {
public:
    [[nodiscard]] WorkspaceWriteResult writeChanged(
        Workspace& workspace,
        const std::filesystem::path& outputRoot) const;
};

} // namespace spice::trade::alx
