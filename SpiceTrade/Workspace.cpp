#include "Workspace.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <utility>

namespace spice::trade::alx {
namespace {

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool isSafeCsvRelativePath(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    const auto normalized = path.lexically_normal();
    if (normalized.empty() || normalized == ".") {
        return false;
    }
    for (const auto& component : normalized) {
        if (component == "..") {
            return false;
        }
    }
    return lowerAscii(normalized.extension().string()) == ".csv";
}

std::string normalizedPathKey(const std::filesystem::path& path)
{
    return lowerAscii(path.lexically_normal().generic_string());
}

CsvDiagnostic pathDiagnostic(
    const DiagnosticSeverity severity,
    std::string message,
    const std::filesystem::path& relativePath)
{
    return CsvDiagnostic{
        .severity = severity,
        .message = std::move(message),
        .relativePath = relativePath,
    };
}

} // namespace

bool TrackedDocument::changed() const noexcept
{
    return current != baseline;
}

TrackedDocument* Workspace::find(const std::filesystem::path& relativePath) noexcept
{
    const auto key = normalizedPathKey(relativePath);
    const auto found = std::find_if(documents.begin(), documents.end(), [&](const auto& document) {
        return normalizedPathKey(document.relativePath) == key;
    });
    return found == documents.end() ? nullptr : &*found;
}

const TrackedDocument* Workspace::find(const std::filesystem::path& relativePath) const noexcept
{
    const auto key = normalizedPathKey(relativePath);
    const auto found = std::find_if(documents.begin(), documents.end(), [&](const auto& document) {
        return normalizedPathKey(document.relativePath) == key;
    });
    return found == documents.end() ? nullptr : &*found;
}

std::vector<std::filesystem::path> Workspace::changedPaths() const
{
    std::vector<std::filesystem::path> paths{};
    for (const auto& document : documents) {
        if (document.changed()) {
            paths.push_back(document.relativePath);
        }
    }
    return paths;
}

bool WorkspaceReadResult::ok() const noexcept
{
    return workspace.has_value() && !hasErrors(diagnostics);
}

WorkspaceReadResult WorkspaceReader::read(
    const std::filesystem::path& sourceRoot,
    const std::span<const std::filesystem::path> relativePaths) const
{
    WorkspaceReadResult result{};
    Workspace workspace{};
    std::set<std::string> requestedPaths{};

    for (const auto& requestedPath : relativePaths) {
        const auto relativePath = requestedPath.lexically_normal();
        if (!isSafeCsvRelativePath(relativePath)) {
            result.diagnostics.push_back(pathDiagnostic(
                DiagnosticSeverity::Error,
                "Requested ALX CSV path must be a safe relative .csv path",
                requestedPath));
            continue;
        }
        if (!requestedPaths.insert(normalizedPathKey(relativePath)).second) {
            result.diagnostics.push_back(pathDiagnostic(
                DiagnosticSeverity::Error,
                "Requested ALX CSV path is duplicated",
                relativePath));
            continue;
        }

        auto read = CsvReader{}.readFile(sourceRoot / relativePath);
        for (auto& diagnostic : read.diagnostics) {
            diagnostic.relativePath = relativePath;
            result.diagnostics.push_back(std::move(diagnostic));
        }
        if (!read.ok()) {
            continue;
        }

        workspace.documents.push_back(TrackedDocument{
            .relativePath = relativePath,
            .baseline = *read.document,
            .current = std::move(*read.document),
            .format = read.format,
        });
    }

    if (!hasErrors(result.diagnostics)) {
        result.workspace = std::move(workspace);
    }
    return result;
}

bool WorkspaceWriteResult::ok() const noexcept
{
    return !hasErrors(diagnostics);
}

WorkspaceWriteResult WorkspaceWriter::writeChanged(
    Workspace& workspace,
    const std::filesystem::path& outputRoot) const
{
    WorkspaceWriteResult result{};
    if (outputRoot.empty()) {
        result.diagnostics.push_back(pathDiagnostic(
            DiagnosticSeverity::Error,
            "ALX CSV output root must not be empty",
            {}));
        return result;
    }

    struct PendingWrite {
        TrackedDocument* document{ nullptr };
        CsvWriteResult serialized{};
    };
    std::vector<PendingWrite> pending{};
    for (auto& document : workspace.documents) {
        if (!document.changed()) {
            continue;
        }
        if (!isSafeCsvRelativePath(document.relativePath)) {
            result.diagnostics.push_back(pathDiagnostic(
                DiagnosticSeverity::Error,
                "Tracked ALX CSV path must be a safe relative .csv path",
                document.relativePath));
            continue;
        }

        auto serialized = CsvWriter{}.write(document.current, document.format);
        for (auto& diagnostic : serialized.diagnostics) {
            diagnostic.relativePath = document.relativePath;
            result.diagnostics.push_back(diagnostic);
        }
        pending.push_back(PendingWrite{
            .document = &document,
            .serialized = std::move(serialized),
        });
    }

    if (hasErrors(result.diagnostics)) {
        return result;
    }

    for (auto& item : pending) {
        auto write = CsvWriter{}.writeFile(
            item.document->current,
            outputRoot / item.document->relativePath,
            item.document->format);
        if (!write.ok()) {
            for (auto& diagnostic : write.diagnostics) {
                diagnostic.relativePath = item.document->relativePath;
                result.diagnostics.push_back(std::move(diagnostic));
            }
            continue;
        }
        item.document->baseline = item.document->current;
        result.writtenPaths.push_back(item.document->relativePath);
    }
    return result;
}

} // namespace spice::trade::alx
