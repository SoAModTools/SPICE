#pragma once

#include "StdDocument.h"
#include "../SpiceRoot/Binary/Endian.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace spice::stdfile {

enum class StdCompression { None, Aklz };
enum class StdDiagnosticSeverity { Info, Warning, Error };
enum class StdByteOrderSelection { AutoDetected, CallerSpecified };
enum class StdDiagnosticCode {
    EmptyInput,
    FileReadFailed,
    AklzDecodeFailed,
    ByteOrderUndetectable,
    ByteOrderAmbiguous,
    LayoutAmbiguous,
    MalformedActionRows,
    MalformedEntryTable,
    KnownPayloadSizeMismatch,
    KnownPayloadMissing,
    UnrecognizedActionRowType,
    KnownPayloadMisaligned,
    InvalidTimelineRepeatRange,
    UnsupportedPointLightSlot,
    UnknownLayoutPreserved,
    InvalidDocument,
    DuplicateId,
    DanglingReference,
    DuplicateLayoutOwnership,
    MissingLayoutOwnership,
    ReceiptRequired,
    ReceiptMismatch,
    OpaqueByteOrderMismatch,
    OutputTooLarge,
    AklzEncodeFailed,
};

struct StdDocumentDiagnostic {
    StdDiagnosticCode code{ StdDiagnosticCode::InvalidDocument };
    StdDiagnosticSeverity severity{ StdDiagnosticSeverity::Error };
    std::string message{};
    std::optional<std::uint64_t> decodedOffset{};
};

struct StdOpaqueReceiptEvidence {
    std::vector<StdEntryPayloadId> payloadIds{};
    std::vector<StdOpaqueFragmentId> fragmentIds{};
    std::optional<StdOpaqueFragmentId> fileTrailerId{};
    std::optional<std::array<std::uint8_t, 32U>> fileTrailerSha256{};
    std::optional<std::array<std::uint8_t, 32U>> topLevelDecodedSha256{};
};

namespace detail { struct StdProjectionEvidence; }

struct StdImportReceipt {
    StdImportReceipt() = default;
    std::optional<std::filesystem::path> path{};
    std::array<std::uint8_t, 32U> sourceSha256{};
    std::uint64_t sourceSize{ 0U };
    std::uint64_t decodedSize{ 0U };
    StdCompression compression{ StdCompression::None };
    spice::root::Endian byteOrder{ spice::root::Endian::Big };
    StdByteOrderSelection byteOrderSelection{ StdByteOrderSelection::AutoDetected };
    StdOpaqueReceiptEvidence opaqueEvidence{};

private:
    std::shared_ptr<const detail::StdProjectionEvidence> projectionEvidence_{};
    friend class StdDocumentImporter;
    friend class StdType53Projector;
};

struct StdImportOptions {
    std::optional<spice::root::Endian> byteOrder{};
};

struct StdDocumentImportResult {
    std::optional<StdDocument> document{};
    StdImportReceipt receipt{};
    std::vector<StdDocumentDiagnostic> diagnostics{};
    [[nodiscard]] bool ok() const noexcept;
};

class StdDocumentImporter {
public:
    [[nodiscard]] static StdDocumentImportResult importBytes(
        std::span<const std::uint8_t> bytes,
        const StdImportOptions& options = {});
    [[nodiscard]] static StdDocumentImportResult importFile(
        const std::filesystem::path& path,
        const StdImportOptions& options = {});
};

[[nodiscard]] const char* toString(StdCompression value) noexcept;
[[nodiscard]] const char* toString(StdDiagnosticSeverity value) noexcept;
[[nodiscard]] const char* toString(StdByteOrderSelection value) noexcept;
[[nodiscard]] const char* toString(StdDiagnosticCode value) noexcept;

} // namespace spice::stdfile
