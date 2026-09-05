#include "StdDocumentImporter.h"

#include "StdSha256.h"
#include "../Compression/Aklz.h"
#include "../SpiceRoot/Binary/EndianReader.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <utility>

namespace spice::stdfile {
namespace {

using spice::root::Endian;
using spice::root::EndianReader;

constexpr std::size_t kHeaderSize = 0x10U;
constexpr std::size_t kActionRowSize = 0x18U;
constexpr std::size_t kEntryRecordSize = 0x10U;
constexpr std::uint32_t kMaxRows = 100000U;

enum class CandidateState { NoMatch, Malformed, Valid };

struct Candidate {
    CandidateState state{ CandidateState::NoMatch };
    Endian endian{ Endian::Big };
    StdDocumentContent content{ StdOpaqueContent{} };
    StdDiagnosticCode errorCode{ StdDiagnosticCode::InvalidDocument };
    std::string message{};
    std::optional<std::uint64_t> offset{};
    std::vector<StdDocumentDiagnostic> warnings{};
};

void addDiagnostic(StdDocumentImportResult& result, const StdDiagnosticCode code,
    const StdDiagnosticSeverity severity, std::string message,
    const std::optional<std::uint64_t> offset = std::nullopt) {
    result.diagnostics.push_back(StdDocumentDiagnostic{ code, severity, std::move(message), offset });
}

Candidate malformed(const Endian endian, const StdDiagnosticCode code,
    std::string message, const std::optional<std::uint64_t> offset = std::nullopt) {
    Candidate result{};
    result.state = CandidateState::Malformed;
    result.endian = endian;
    result.errorCode = code;
    result.message = std::move(message);
    result.offset = offset;
    return result;
}

StdActionViewPayload parseActionView(const EndianReader& reader, const std::size_t offset) {
    return StdActionViewPayload{
        .primaryActionKey = reader.read_i16(offset + 0x00U),
        .routeSecondaryKey = reader.read_i16(offset + 0x02U),
        .directSecondaryKey = reader.read_i16(offset + 0x04U),
        .rawLowFlags = reader.read_u16(offset + 0x06U),
        .raw08 = reader.read_u32(offset + 0x08U),
        .raw0c = reader.read_u32(offset + 0x0cU),
        .actionViewFlags = reader.read_u32(offset + 0x10U),
        .modeLocalValueBits = reader.read_u32(offset + 0x14U),
        .startFrame = reader.read_i16(offset + 0x18U),
        .raw1a = reader.read_u16(offset + 0x1aU),
        .endFrame = reader.read_i16(offset + 0x1cU),
        .holdFrameCount = reader.read_i16(offset + 0x1eU),
        .stepFrameCount = reader.read_i16(offset + 0x20U),
        .requestedMode = reader.read_i16(offset + 0x22U),
    };
}

Candidate tryActionRows(const std::span<const std::uint8_t> bytes, const Endian endian) {
    Candidate result{};
    result.endian = endian;
    if (bytes.size() < kHeaderSize) return result;
    const EndianReader reader(bytes, endian);
    const auto rowCount = reader.read_u32(0x08U);
    if (rowCount > kMaxRows) return result;
    const auto expected = static_cast<std::uint64_t>(kHeaderSize) +
        static_cast<std::uint64_t>(rowCount) * kActionRowSize;
    if (expected != bytes.size()) return result;
    if (rowCount == 0U) {
        return malformed(endian, StdDiagnosticCode::MalformedActionRows,
            "A 0x10-byte zero-row action table is malformed and has no useful action-row content.", 0x08U);
    }

    StdActionRowsContent content{};
    content.rawCommandLow = reader.read_u16(0x00U);
    content.rawCommandHigh = reader.read_u16(0x02U);
    content.rawLoaderContextWord = reader.read_u32(0x04U);
    content.rawRowTablePointerWord = reader.read_u32(0x0cU);
    content.rows.reserve(rowCount);
    for (std::uint32_t index = 0U; index < rowCount; ++index) {
        const auto offset = kHeaderSize + static_cast<std::size_t>(index) * kActionRowSize;
        StdActionRow row{
            .id = StdActionRowId{ index + 1U },
            .actionId = reader.read_i16(offset + 0x00U),
            .rowType = reader.read_i16(offset + 0x02U),
            .callbackIndex = reader.read_i16(offset + 0x04U),
            .raw06 = reader.read_i16(offset + 0x06U),
            .flags = reader.read_u32(offset + 0x08U),
            .secondaryKey = reader.read_i16(offset + 0x0cU),
            .raw0e = reader.read_i16(offset + 0x0eU),
            .raw10Bits = reader.read_u32(offset + 0x10U),
            .raw14Bits = reader.read_u32(offset + 0x14U),
        };
        if (row.rowType == 3) {
            result.warnings.push_back(StdDocumentDiagnostic{
                StdDiagnosticCode::MalformedActionRows, StdDiagnosticSeverity::Warning,
                "Action row contains the runtime rowType 3 terminator value; source files normally omit it.", offset + 0x02U });
        }
        content.rows.push_back(row);
    }
    result.state = CandidateState::Valid;
    result.content = std::move(content);
    return result;
}

struct PayloadSpan {
    std::size_t recordIndex{ 0U };
    std::size_t begin{ 0U };
    std::size_t end{ 0U };
};

Candidate tryEntryTable(const std::span<const std::uint8_t> bytes, const Endian endian) {
    Candidate result{};
    result.endian = endian;
    if (bytes.size() < kHeaderSize) return result;
    const EndianReader reader(bytes, endian);
    if (reader.read_u16(0x02U) != 4U) return result;

    const auto count = reader.read_u16(0x00U);
    if (count == 0U) {
        return malformed(endian, StdDiagnosticCode::MalformedEntryTable,
            "Entry table declares no terminal record.", 0x00U);
    }
    const auto decodedSpan = reader.read_u32(0x0cU);
    if (decodedSpan != bytes.size() - kHeaderSize) {
        return malformed(endian, StdDiagnosticCode::MalformedEntryTable,
            "Entry-table decoded span does not equal the decoded file size minus 0x10.", 0x0cU);
    }
    const auto tableEnd64 = static_cast<std::uint64_t>(kHeaderSize) +
        static_cast<std::uint64_t>(count) * kEntryRecordSize;
    if (tableEnd64 > bytes.size()) {
        return malformed(endian, StdDiagnosticCode::MalformedEntryTable,
            "Entry-table record count extends beyond the decoded file.", 0x00U);
    }
    const auto tableEnd = static_cast<std::size_t>(tableEnd64);
    const auto terminatorOffset = kHeaderSize + static_cast<std::size_t>(count - 1U) * kEntryRecordSize;
    for (std::uint16_t index = 0U; index + 1U < count; ++index) {
        const auto offset = kHeaderSize + static_cast<std::size_t>(index) * kEntryRecordSize;
        if (reader.read_i16(offset) < 0) {
            return malformed(endian, StdDiagnosticCode::MalformedEntryTable,
                "Entry-table terminator appears before the final declared record.", offset);
        }
    }
    if (reader.read_i16(terminatorOffset) >= 0) {
        return malformed(endian, StdDiagnosticCode::MalformedEntryTable,
            "Entry table is missing its final negative-location terminator.", terminatorOffset);
    }

    std::vector<PayloadSpan> spans{};
    spans.reserve(count - 1U);
    for (std::uint16_t index = 0U; index + 1U < count; ++index) {
        const auto offset = kHeaderSize + static_cast<std::size_t>(index) * kEntryRecordSize;
        const auto size = reader.read_u32(offset + 0x08U);
        if (size == 0U) continue;
        const auto relative = reader.read_u32(offset + 0x0cU);
        const auto begin64 = static_cast<std::uint64_t>(kHeaderSize) + relative;
        const auto end64 = begin64 + size;
        if (begin64 < tableEnd || end64 > bytes.size()) {
            return malformed(endian, StdDiagnosticCode::MalformedEntryTable,
                "Entry payload is out of bounds or overlaps the record table.", offset + 0x08U);
        }
        spans.push_back(PayloadSpan{ index, static_cast<std::size_t>(begin64), static_cast<std::size_t>(end64) });
    }
    std::sort(spans.begin(), spans.end(), [](const auto& left, const auto& right) {
        if (left.begin != right.begin) return left.begin < right.begin;
        return left.end < right.end;
    });
    for (std::size_t index = 1U; index < spans.size(); ++index) {
        if (spans[index].begin < spans[index - 1U].end) {
            return malformed(endian, StdDiagnosticCode::MalformedEntryTable,
                "Entry payload spans overlap.", spans[index].begin);
        }
    }

    StdEntryTableContent content{};
    content.kind = 4U;
    content.rawHeader04 = reader.read_u32(0x04U);
    content.rawHeader08 = reader.read_u32(0x08U);
    content.records.reserve(count - 1U);
    for (std::uint16_t index = 0U; index + 1U < count; ++index) {
        const auto offset = kHeaderSize + static_cast<std::size_t>(index) * kEntryRecordSize;
        StdEntryRecord record{
            .id = StdEntryRecordId{ index + 1U },
            .locationCode = reader.read_i16(offset + 0x00U),
            .opcode = reader.read_i16(offset + 0x02U),
            .raw04 = reader.read_u32(offset + 0x04U),
        };
        const auto size = reader.read_u32(offset + 0x08U);
        if (size != 0U) record.payload = StdEntryPayloadId{ index + 1U };
        content.records.push_back(record);
    }
    content.terminator = StdEntryTerminator{
        .id = StdEntryTerminatorId{ 1U },
        .negativeLocation = reader.read_i16(terminatorOffset + 0x00U),
        .raw02 = reader.read_i16(terminatorOffset + 0x02U),
        .raw04 = reader.read_u32(terminatorOffset + 0x04U),
        .raw08 = reader.read_u32(terminatorOffset + 0x08U),
        .raw0c = reader.read_u32(terminatorOffset + 0x0cU),
    };

    std::size_t cursor = tableEnd;
    std::uint64_t nextFragmentId = 1U;
    for (const auto& span : spans) {
        if (span.begin > cursor) {
            StdOpaqueFragment fragment{ StdOpaqueFragmentId{ nextFragmentId++ },
                std::vector<std::uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                    bytes.begin() + static_cast<std::ptrdiff_t>(span.begin)) };
            content.payloadLayout.push_back(fragment.id);
            content.opaqueFragments.push_back(std::move(fragment));
        }
        const auto& record = content.records[span.recordIndex];
        const auto id = *record.payload;
        const auto payloadBytes = bytes.subspan(span.begin, span.end - span.begin);
        StdEntryPayload payload{};
        payload.id = id;
        if (record.combinedType() == kStdActionViewCombinedType && payloadBytes.size() == kStdActionViewPayloadSize) {
            payload.content = parseActionView(reader, span.begin);
        } else {
            payload.content = StdOpaquePayload{ std::vector<std::uint8_t>(payloadBytes.begin(), payloadBytes.end()) };
        }
        content.payloadLayout.push_back(id);
        content.payloads.push_back(std::move(payload));
        cursor = span.end;
    }
    if (cursor < bytes.size()) {
        StdOpaqueFragment fragment{ StdOpaqueFragmentId{ nextFragmentId },
            std::vector<std::uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(cursor), bytes.end()) };
        content.payloadLayout.push_back(fragment.id);
        content.opaqueFragments.push_back(std::move(fragment));
    }

    result.state = CandidateState::Valid;
    result.content = std::move(content);
    return result;
}

void collectOpaqueEvidence(const StdDocument& document, StdImportReceipt& receipt) {
    if (const auto* opaque = std::get_if<StdOpaqueContent>(&document.content)) {
        receipt.opaqueEvidence.topLevelDecodedSha256 = detail::sha256(opaque->decodedBytes);
        return;
    }
    const auto* table = std::get_if<StdEntryTableContent>(&document.content);
    if (table == nullptr) return;
    for (const auto& payload : table->payloads) {
        if (std::holds_alternative<StdOpaquePayload>(payload.content)) {
            receipt.opaqueEvidence.payloadIds.push_back(payload.id);
        }
    }
    for (const auto& fragment : table->opaqueFragments) receipt.opaqueEvidence.fragmentIds.push_back(fragment.id);
}

std::vector<std::uint8_t> readAll(const std::filesystem::path& path, bool& ok) {
    ok = false;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    std::vector<std::uint8_t> bytes{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    ok = stream.good() || stream.eof();
    return bytes;
}

} // namespace

bool StdDocumentImportResult::ok() const noexcept {
    return document.has_value() && std::none_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.severity == StdDiagnosticSeverity::Error;
    });
}

StdDocumentImportResult StdDocumentImporter::importBytes(
    const std::span<const std::uint8_t> sourceBytes, const StdImportOptions& options) {
    StdDocumentImportResult result{};
    result.receipt.sourceSize = sourceBytes.size();
    result.receipt.sourceSha256 = detail::sha256(sourceBytes);
    if (sourceBytes.empty()) {
        addDiagnostic(result, StdDiagnosticCode::EmptyInput, StdDiagnosticSeverity::Error,
            "STD input is empty.");
        return result;
    }

    std::vector<std::uint8_t> decodedStorage{};
    std::span<const std::uint8_t> decoded = sourceBytes;
    if (spice::compression::aklz::isAklz(sourceBytes)) {
        result.receipt.compression = StdCompression::Aklz;
        auto decompressed = spice::compression::aklz::decompress(sourceBytes);
        if (!decompressed.ok()) {
            addDiagnostic(result, StdDiagnosticCode::AklzDecodeFailed, StdDiagnosticSeverity::Error,
                "AKLZ decompression failed: " + std::string(spice::compression::aklz::errorToString(decompressed.error)));
            return result;
        }
        decodedStorage = std::move(decompressed.bytes);
        decoded = decodedStorage;
    }
    result.receipt.decodedSize = decoded.size();
    if (decoded.empty()) {
        addDiagnostic(result, StdDiagnosticCode::EmptyInput, StdDiagnosticSeverity::Error,
            "Decoded STD input is empty.");
        return result;
    }

    std::vector<Candidate> candidates{};
    const auto appendCandidates = [&](const Endian endian) {
        candidates.push_back(tryActionRows(decoded, endian));
        candidates.push_back(tryEntryTable(decoded, endian));
    };
    if (options.byteOrder.has_value()) appendCandidates(*options.byteOrder);
    else {
        appendCandidates(Endian::Little);
        appendCandidates(Endian::Big);
    }

    std::vector<const Candidate*> valid{};
    std::vector<const Candidate*> malformedCandidates{};
    for (const auto& candidate : candidates) {
        if (candidate.state == CandidateState::Valid) valid.push_back(&candidate);
        if (candidate.state == CandidateState::Malformed) malformedCandidates.push_back(&candidate);
    }

    if (valid.size() > 1U) {
        const bool sameEndian = std::all_of(valid.begin(), valid.end(), [&](const auto* item) {
            return item->endian == valid.front()->endian;
        });
        addDiagnostic(result, sameEndian ? StdDiagnosticCode::LayoutAmbiguous : StdDiagnosticCode::ByteOrderAmbiguous,
            StdDiagnosticSeverity::Error, sameEndian
                ? "STD input validates as more than one recognized layout."
                : "STD input validates under more than one byte order.");
        return result;
    }

    if (valid.empty()) {
        if (!malformedCandidates.empty()) {
            const auto* issue = malformedCandidates.front();
            addDiagnostic(result, issue->errorCode, StdDiagnosticSeverity::Error, issue->message, issue->offset);
            return result;
        }
        if (!options.byteOrder.has_value()) {
            addDiagnostic(result, StdDiagnosticCode::ByteOrderUndetectable, StdDiagnosticSeverity::Error,
                "STD byte order cannot be inferred from an unrecognized layout; supply an explicit byte order to preserve it opaquely.");
            return result;
        }
        result.receipt.byteOrder = *options.byteOrder;
        result.receipt.byteOrderSelection = StdByteOrderSelection::CallerSpecified;
        result.document = StdDocument{ StdOpaqueContent{ std::vector<std::uint8_t>(decoded.begin(), decoded.end()) } };
        collectOpaqueEvidence(*result.document, result.receipt);
        addDiagnostic(result, StdDiagnosticCode::UnknownLayoutPreserved, StdDiagnosticSeverity::Warning,
            "STD layout is unrecognized; decoded bytes were retained as top-level opaque content.");
        return result;
    }

    const auto& selected = *valid.front();
    result.receipt.byteOrder = selected.endian;
    result.receipt.byteOrderSelection = options.byteOrder.has_value()
        ? StdByteOrderSelection::CallerSpecified : StdByteOrderSelection::AutoDetected;
    result.document = StdDocument{ selected.content };
    result.diagnostics.insert(result.diagnostics.end(), selected.warnings.begin(), selected.warnings.end());
    collectOpaqueEvidence(*result.document, result.receipt);
    return result;
}

StdDocumentImportResult StdDocumentImporter::importFile(
    const std::filesystem::path& path, const StdImportOptions& options) {
    bool read = false;
    const auto bytes = readAll(path, read);
    if (!read) {
        StdDocumentImportResult result{};
        result.receipt.path = path;
        addDiagnostic(result, StdDiagnosticCode::FileReadFailed, StdDiagnosticSeverity::Error,
            "Unable to read STD file.");
        return result;
    }
    auto result = importBytes(bytes, options);
    result.receipt.path = path;
    return result;
}

const char* toString(const StdCompression value) noexcept {
    switch (value) { case StdCompression::None: return "none"; case StdCompression::Aklz: return "aklz"; }
    return "unknown";
}

const char* toString(const StdDiagnosticSeverity value) noexcept {
    switch (value) { case StdDiagnosticSeverity::Info: return "info"; case StdDiagnosticSeverity::Warning: return "warning"; case StdDiagnosticSeverity::Error: return "error"; }
    return "unknown";
}

const char* toString(const StdByteOrderSelection value) noexcept {
    switch (value) { case StdByteOrderSelection::AutoDetected: return "auto_detected"; case StdByteOrderSelection::CallerSpecified: return "caller_specified"; }
    return "unknown";
}

const char* toString(const StdDiagnosticCode value) noexcept {
    switch (value) {
    case StdDiagnosticCode::EmptyInput: return "empty_input";
    case StdDiagnosticCode::FileReadFailed: return "file_read_failed";
    case StdDiagnosticCode::AklzDecodeFailed: return "aklz_decode_failed";
    case StdDiagnosticCode::ByteOrderUndetectable: return "byte_order_undetectable";
    case StdDiagnosticCode::ByteOrderAmbiguous: return "byte_order_ambiguous";
    case StdDiagnosticCode::LayoutAmbiguous: return "layout_ambiguous";
    case StdDiagnosticCode::MalformedActionRows: return "malformed_action_rows";
    case StdDiagnosticCode::MalformedEntryTable: return "malformed_entry_table";
    case StdDiagnosticCode::UnknownLayoutPreserved: return "unknown_layout_preserved";
    case StdDiagnosticCode::InvalidDocument: return "invalid_document";
    case StdDiagnosticCode::DuplicateId: return "duplicate_id";
    case StdDiagnosticCode::DanglingReference: return "dangling_reference";
    case StdDiagnosticCode::DuplicateLayoutOwnership: return "duplicate_layout_ownership";
    case StdDiagnosticCode::MissingLayoutOwnership: return "missing_layout_ownership";
    case StdDiagnosticCode::ReceiptRequired: return "receipt_required";
    case StdDiagnosticCode::ReceiptMismatch: return "receipt_mismatch";
    case StdDiagnosticCode::OpaqueByteOrderMismatch: return "opaque_byte_order_mismatch";
    case StdDiagnosticCode::OutputTooLarge: return "output_too_large";
    case StdDiagnosticCode::AklzEncodeFailed: return "aklz_encode_failed";
    }
    return "unknown";
}

} // namespace spice::stdfile
