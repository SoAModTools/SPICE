#include "StdParser.h"

#include "../Compression/Aklz.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <utility>

namespace spice::stdfile {
namespace {

constexpr std::uint32_t kStdHeaderSize = 0x10U;
constexpr std::uint32_t kActionRowSize = 0x18U;
constexpr std::uint32_t kEntryRecordSize = 0x10U;
constexpr std::uint32_t kMaxConservativeRowCount = 100000U;

void addDiagnostic(StdFile& file, StdDiagnosticSeverity severity, std::string message, std::uint32_t offset = 0U)
{
    file.diagnostics.push_back(StdDiagnostic{ severity, std::move(message), offset });
}

std::uint32_t sizeToU32Saturated(const std::size_t size)
{
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(size);
}

bool canReadRange(const std::size_t size, const std::uint32_t offset, const std::uint32_t length)
{
    return offset <= size && length <= size - offset;
}

std::uint16_t readU16BeUnchecked(std::span<const std::uint8_t> bytes, const std::uint32_t offset)
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
        static_cast<std::uint16_t>(bytes[offset + 1U]));
}

std::int16_t readS16BeUnchecked(std::span<const std::uint8_t> bytes, const std::uint32_t offset)
{
    return static_cast<std::int16_t>(readU16BeUnchecked(bytes, offset));
}

std::uint32_t readU32BeUnchecked(std::span<const std::uint8_t> bytes, const std::uint32_t offset)
{
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
        static_cast<std::uint32_t>(bytes[offset + 3U]);
}

bool actionRowEnvelopeMatches(std::span<const std::uint8_t> bytes)
{
    if (!canReadRange(bytes.size(), 0U, kStdHeaderSize)) {
        return false;
    }

    const auto rowCount = readU32BeUnchecked(bytes, 0x08U);
    if (rowCount > kMaxConservativeRowCount) {
        return false;
    }

    const auto expectedSize = static_cast<std::uint64_t>(kStdHeaderSize) +
        static_cast<std::uint64_t>(rowCount) * kActionRowSize;
    return expectedSize == bytes.size();
}

bool entryTableEnvelopeMatches(std::span<const std::uint8_t> bytes)
{
    if (!canReadRange(bytes.size(), 0U, kStdHeaderSize)) {
        return false;
    }

    return readU16BeUnchecked(bytes, 0x02U) == 4U &&
        readU32BeUnchecked(bytes, 0x04U) == 0U &&
        readU32BeUnchecked(bytes, 0x08U) == 0U;
}

void parseActionRows(StdFile& file)
{
    const std::span<const std::uint8_t> bytes(file.decodedBytes);
    auto& layout = file.actionRows;
    auto& header = layout.header;

    header.commandLow = readU16BeUnchecked(bytes, 0x00U);
    header.commandHigh = readU16BeUnchecked(bytes, 0x02U);
    header.combinedCommandKind =
        (static_cast<std::uint32_t>(header.commandHigh) << 16U) |
        static_cast<std::uint32_t>(header.commandLow);
    header.loaderContextWord = readU32BeUnchecked(bytes, 0x04U);
    header.rowCount = readU32BeUnchecked(bytes, 0x08U);
    header.rowTablePtrWord = readU32BeUnchecked(bytes, 0x0cU);

    layout.rows.reserve(header.rowCount);
    for (std::uint32_t index = 0U; index < header.rowCount; ++index) {
        const auto offset = kStdHeaderSize + index * kActionRowSize;
        StdActionRow row{};
        row.index = index;
        row.decodedOffset = offset;
        row.actionId = readS16BeUnchecked(bytes, offset);
        row.rowType = readS16BeUnchecked(bytes, offset + 0x02U);
        row.callbackIndex = readS16BeUnchecked(bytes, offset + 0x04U);
        row.motionSlotOrdinal = readS16BeUnchecked(bytes, offset + 0x06U);
        row.flags = readU32BeUnchecked(bytes, offset + 0x08U);
        row.secondaryKey = readS16BeUnchecked(bytes, offset + 0x0cU);
        row.callbackAuxParam = readS16BeUnchecked(bytes, offset + 0x0eU);
        row.selectionTransitionScalarBits = readU32BeUnchecked(bytes, offset + 0x10U);
        row.motionProgressScalarBits = readU32BeUnchecked(bytes, offset + 0x14U);
        if (row.rowType == 3) {
            addDiagnostic(
                file,
                StdDiagnosticSeverity::Warning,
                "Action-row file contains a rowType 3 sentinel; source files normally omit the runtime sentinel",
                offset + 0x02U);
        }
        layout.rows.push_back(row);
    }
}

void parseEntryTable(StdFile& file)
{
    const std::span<const std::uint8_t> bytes(file.decodedBytes);
    auto& layout = file.entryTable;
    auto& header = layout.header;

    header.recordCountIncludingSentinel = readU16BeUnchecked(bytes, 0x00U);
    header.kind = readU16BeUnchecked(bytes, 0x02U);
    header.reserved0 = readU32BeUnchecked(bytes, 0x04U);
    header.reserved1 = readU32BeUnchecked(bytes, 0x08U);
    header.decodedSpanMinusHeader = readU32BeUnchecked(bytes, 0x0cU);

    const auto decodedSpan = bytes.size() >= kStdHeaderSize ? bytes.size() - kStdHeaderSize : 0U;
    layout.headerSpanDelta = static_cast<std::int64_t>(decodedSpan) -
        static_cast<std::int64_t>(header.decodedSpanMinusHeader);

    const auto declaredTableBytes = static_cast<std::uint64_t>(header.recordCountIncludingSentinel) *
        kEntryRecordSize;
    const auto declaredTableEnd = static_cast<std::uint64_t>(kStdHeaderSize) + declaredTableBytes;
    if (declaredTableEnd > bytes.size()) {
        addDiagnostic(
            file,
            StdDiagnosticSeverity::Error,
            "Entry-table declared record count extends beyond decoded file size",
            kStdHeaderSize);
    }

    const auto maxRecordsInBounds = bytes.size() < kStdHeaderSize
        ? 0U
        : static_cast<std::uint32_t>((bytes.size() - kStdHeaderSize) / kEntryRecordSize);
    const auto recordLimit = std::min<std::uint32_t>(
        header.recordCountIncludingSentinel,
        maxRecordsInBounds);
    layout.records.reserve(recordLimit);

    std::uint32_t maxPayloadEndRel = 0U;
    std::uint32_t firstPayloadOffsetRel = 0U;
    bool hasPayloads = false;

    for (std::uint32_t index = 0U; index < recordLimit; ++index) {
        const auto offset = kStdHeaderSize + index * kEntryRecordSize;
        StdEntryRecord record{};
        record.index = index;
        record.tableOffset = offset;
        record.locationCode = readS16BeUnchecked(bytes, offset);
        record.opcode = readS16BeUnchecked(bytes, offset + 0x02U);
        record.combinedType =
            (static_cast<std::uint32_t>(static_cast<std::uint16_t>(record.opcode)) << 16U) |
            static_cast<std::uint32_t>(static_cast<std::uint16_t>(record.locationCode));
        record.field2 = readU32BeUnchecked(bytes, offset + 0x04U);
        record.payloadSize = readU32BeUnchecked(bytes, offset + 0x08U);
        record.payloadOffsetRel = readU32BeUnchecked(bytes, offset + 0x0cU);
        record.isSentinel = record.locationCode < 0;

        if (record.isSentinel) {
            layout.hasSentinel = true;
            layout.sentinelIndex = index;
            layout.entryCountWithoutSentinel = index;
            layout.records.push_back(record);
            break;
        }

        const auto payloadAbs64 = static_cast<std::uint64_t>(kStdHeaderSize) + record.payloadOffsetRel;
        const auto payloadEndRel64 = static_cast<std::uint64_t>(record.payloadOffsetRel) + record.payloadSize;
        const auto payloadEndAbs64 = static_cast<std::uint64_t>(kStdHeaderSize) + payloadEndRel64;
        if (payloadAbs64 <= std::numeric_limits<std::uint32_t>::max()) {
            record.payloadOffsetAbs = static_cast<std::uint32_t>(payloadAbs64);
        } else {
            record.payloadOffsetAbs = std::numeric_limits<std::uint32_t>::max();
        }
        if (payloadEndRel64 <= std::numeric_limits<std::uint32_t>::max()) {
            record.payloadEndRel = static_cast<std::uint32_t>(payloadEndRel64);
        } else {
            record.payloadEndRel = std::numeric_limits<std::uint32_t>::max();
        }

        record.payloadInBounds = payloadAbs64 <= bytes.size() && payloadEndAbs64 <= bytes.size();
        if (!record.payloadInBounds) {
            addDiagnostic(file, StdDiagnosticSeverity::Error, "Entry payload span is outside decoded file", offset);
        }

        if (!hasPayloads || record.payloadOffsetRel < firstPayloadOffsetRel) {
            firstPayloadOffsetRel = record.payloadOffsetRel;
        }
        hasPayloads = true;
        if (record.payloadEndRel > maxPayloadEndRel) {
            maxPayloadEndRel = record.payloadEndRel;
        }

        layout.records.push_back(record);
    }

    if (!layout.hasSentinel) {
        layout.entryCountWithoutSentinel = static_cast<std::uint32_t>(layout.records.size());
        addDiagnostic(
            file,
            StdDiagnosticSeverity::Error,
            "Entry table did not contain a negative locationCode sentinel within the declared records",
            kStdHeaderSize);
    }

    layout.hasPayloads = hasPayloads;
    layout.firstPayloadOffsetRel = firstPayloadOffsetRel;
    layout.maxPayloadEndRel = maxPayloadEndRel;
    const auto decodedSpanU32 = sizeToU32Saturated(decodedSpan);
    layout.trailingBytesAfterMaxPayload = maxPayloadEndRel <= decodedSpanU32
        ? decodedSpanU32 - maxPayloadEndRel
        : 0U;
}

void parseDecodedLayout(StdFile& file)
{
    const std::span<const std::uint8_t> bytes(file.decodedBytes);
    if (bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        addDiagnostic(file, StdDiagnosticSeverity::Error, "STD decoded payload is too large to represent with a 32-bit size");
        return;
    }

    file.decodedSize = static_cast<std::uint32_t>(bytes.size());
    if (!canReadRange(bytes.size(), 0U, kStdHeaderSize)) {
        addDiagnostic(file, StdDiagnosticSeverity::Error, "STD decoded payload is too small for a 0x10-byte header");
        return;
    }

    if (actionRowEnvelopeMatches(bytes)) {
        file.layoutKind = StdLayoutKind::ActionRows;
        parseActionRows(file);
        return;
    }

    if (entryTableEnvelopeMatches(bytes)) {
        file.layoutKind = StdLayoutKind::EntryTable;
        parseEntryTable(file);
        return;
    }

    addDiagnostic(file, StdDiagnosticSeverity::Error, "STD decoded payload does not match a known conservative layout");
}

std::vector<std::uint8_t> readAllBytes(const std::filesystem::path& path, bool& ok)
{
    ok = false;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }

    std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>() };
    ok = in.good() || in.eof();
    return bytes;
}

} // namespace

bool StdFile::ok() const
{
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == StdDiagnosticSeverity::Error) {
            return false;
        }
    }
    return true;
}

const char* toString(StdDiagnosticSeverity severity)
{
    switch (severity) {
    case StdDiagnosticSeverity::Info:
        return "info";
    case StdDiagnosticSeverity::Warning:
        return "warning";
    case StdDiagnosticSeverity::Error:
        return "error";
    }
    return "unknown";
}

const char* toString(StdSourceEncoding encoding)
{
    switch (encoding) {
    case StdSourceEncoding::Plain:
        return "plain";
    case StdSourceEncoding::Aklz:
        return "aklz";
    }
    return "unknown";
}

const char* toString(StdLayoutKind kind)
{
    switch (kind) {
    case StdLayoutKind::Unknown:
        return "unknown";
    case StdLayoutKind::ActionRows:
        return "action_rows";
    case StdLayoutKind::EntryTable:
        return "entry_table";
    }
    return "unknown";
}

const char* toString(StdExportMode mode)
{
    switch (mode) {
    case StdExportMode::OriginalSourceBytes:
        return "original_source_bytes";
    case StdExportMode::DecodedBytes:
        return "decoded_bytes";
    case StdExportMode::ReencodeSourceKind:
        return "reencode_source_kind";
    case StdExportMode::ReencodeAklz:
        return "reencode_aklz";
    }
    return "unknown";
}

StdFile parseBytes(std::vector<std::uint8_t> bytes, std::string sourcePath)
{
    StdFile file{};
    file.sourcePath = std::move(sourcePath);
    file.rawSize = sizeToU32Saturated(bytes.size());
    file.rawBytes = std::move(bytes);

    if (file.rawBytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        addDiagnostic(file, StdDiagnosticSeverity::Error, "STD source payload is too large to represent with a 32-bit size");
        return file;
    }

    if (spice::compression::aklz::isAklz(file.rawBytes)) {
        file.sourceEncoding = StdSourceEncoding::Aklz;
        const auto decoded = spice::compression::aklz::decompress(file.rawBytes);
        if (!decoded.ok()) {
            std::ostringstream message;
            message << "Unable to decompress STD AKLZ source: "
                    << spice::compression::aklz::errorToString(decoded.error);
            addDiagnostic(file, StdDiagnosticSeverity::Error, message.str());
            return file;
        }
        file.decodedBytes = decoded.bytes;
        file.decodedAvailable = true;
    } else {
        file.sourceEncoding = StdSourceEncoding::Plain;
        file.decodedBytes = file.rawBytes;
        file.decodedAvailable = true;
    }

    parseDecodedLayout(file);
    return file;
}

StdFile parseFile(const std::filesystem::path& path)
{
    bool readOk = false;
    auto bytes = readAllBytes(path, readOk);
    if (readOk) {
        return parseBytes(std::move(bytes), path.string());
    }

    StdFile file{};
    file.sourcePath = path.string();
    addDiagnostic(file, StdDiagnosticSeverity::Error, "Unable to open or read STD file");
    return file;
}

StdExportResult exportBytes(const StdFile& file, StdExportMode mode)
{
    StdExportResult result{};
    switch (mode) {
    case StdExportMode::OriginalSourceBytes:
        result.bytes = file.rawBytes;
        return result;

    case StdExportMode::DecodedBytes:
        if (!file.decodedAvailable) {
            result.error = "STD decoded bytes are not available";
            return result;
        }
        result.bytes = file.decodedBytes;
        return result;

    case StdExportMode::ReencodeSourceKind:
        if (file.sourceEncoding == StdSourceEncoding::Aklz) {
            break;
        }
        if (!file.decodedAvailable) {
            result.error = "STD decoded bytes are not available";
            return result;
        }
        result.bytes = file.decodedBytes;
        return result;

    case StdExportMode::ReencodeAklz:
        break;
    }

    if (!file.decodedAvailable) {
        result.error = "STD decoded bytes are not available";
        return result;
    }

    const auto encoded = spice::compression::aklz::compress(file.decodedBytes);
    if (!encoded.ok()) {
        result.error = std::string("Unable to AKLZ-compress STD decoded bytes: ") +
            std::string(spice::compression::aklz::errorToString(encoded.error));
        return result;
    }
    result.bytes = encoded.bytes;
    return result;
}

} // namespace spice::stdfile
