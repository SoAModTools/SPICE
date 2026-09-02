#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace spice::mld::model {

enum class MldResourceStatus {
    Empty,
    Complete,
    Partial,
    Failed,
};

enum class MldResourceKind {
    Unknown,
    Object,
    Motion,
    Ground,
    TextureList,
    TextureArchive,
    TextureArchiveEntry,
};

enum class MldDiagnosticScope {
    Structure,
    Resource,
};

struct MldDiagnostic {
    enum class Severity {
        Info,
        Warning,
        Error,
    };

    Severity severity = Severity::Info;
    std::string message{};
    std::optional<std::uint32_t> sourceOffset{};
    MldDiagnosticScope scope = MldDiagnosticScope::Structure;
    MldResourceKind resourceKind = MldResourceKind::Unknown;
};

struct MldByteRange {
    std::size_t offset = 0;
    std::size_t size = 0;

    [[nodiscard]] std::size_t end() const noexcept { return offset + size; }
};

} // namespace spice::mld::model
