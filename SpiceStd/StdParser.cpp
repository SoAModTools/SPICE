#include "StdParser.h"

#include "../Compression/Aklz.h"
#include "StdFileWriter.h"
#include "../SpiceRoot/Binary/Alignment.h"
#include "../SpiceRoot/Binary/EndianReader.h"
#include "../SpiceRoot/Binary/EndianWriter.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <utility>

namespace spice::stdfile {
namespace {

using spice::root::Endian;
using spice::root::EndianReader;
using spice::root::EndianSpanWriter;

constexpr std::uint32_t kStdHeaderSize = 0x10U;
constexpr std::uint32_t kActionRowSize = 0x18U;
constexpr std::uint32_t kEntryRecordSize = 0x10U;
constexpr std::uint32_t kMaxConservativeRowCount = 100000U;

struct KnownRangeCandidate {
    std::size_t offset{ 0U };
    std::size_t size{ 0U };
    std::string label{};
    bool pinned{ false };
};

void addDiagnostic(StdFile& file, StdDiagnosticSeverity severity, std::string message, std::uint32_t offset = 0U)
{
    file.diagnostics.push_back(StdDiagnostic{ severity, std::move(message), offset });
}

bool hasErrorDiagnostics(const StdFile& file)
{
    return std::any_of(file.diagnostics.begin(), file.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity == StdDiagnosticSeverity::Error;
    });
}

void finalizeKnownLayoutStatus(StdFile& file)
{
    file.parseStatus = hasErrorDiagnostics(file)
        ? StdParseStatus::Partial
        : StdParseStatus::Complete;
}

std::uint32_t sizeToU32Saturated(const std::size_t size)
{
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(size);
}

void addUnknownRange(StdFile& file, const std::size_t offset, const std::size_t size, std::string label)
{
    if (size == 0U) {
        return;
    }

    StdUnknownRange unknown{};
    unknown.offset = offset;
    unknown.size = size;
    unknown.label = std::move(label);
    unknown.bytes.assign(
        file.decodedBytes.begin() + static_cast<std::ptrdiff_t>(offset),
        file.decodedBytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
    file.unknownRanges.push_back(std::move(unknown));
}

void addSourceRange(StdFile& file, const std::size_t offset, const std::size_t size, std::string label, const bool known, const bool pinned)
{
    if (size == 0U) {
        return;
    }

    file.sourceRanges.push_back(StdSourceRange{
        .offset = offset,
        .size = size,
        .label = std::move(label),
        .known = known,
        .pinned = pinned,
    });
}

void buildDecodedSourceRanges(StdFile& file, std::vector<KnownRangeCandidate> knownRanges)
{
    file.sourceRanges.clear();
    file.unknownRanges.clear();

    std::sort(knownRanges.begin(), knownRanges.end(), [](const auto& left, const auto& right) {
        if (left.offset != right.offset) {
            return left.offset < right.offset;
        }
        return left.size < right.size;
    });

    std::size_t cursor = 0U;
    for (const auto& range : knownRanges) {
        if (range.size == 0U) {
            continue;
        }
        if (range.offset > file.decodedBytes.size() || range.size > file.decodedBytes.size() - range.offset) {
            addDiagnostic(
                file,
                StdDiagnosticSeverity::Error,
                "Known STD source range extends beyond decoded file size",
                sizeToU32Saturated(range.offset));
            continue;
        }
        if (range.offset < cursor) {
            addDiagnostic(
                file,
                StdDiagnosticSeverity::Error,
                "Known STD source ranges overlap",
                sizeToU32Saturated(range.offset));
            continue;
        }
        if (range.offset > cursor) {
            const auto gapSize = range.offset - cursor;
            addUnknownRange(file, cursor, gapSize, "padding-or-unknown");
            addSourceRange(file, cursor, gapSize, "padding-or-unknown", false, true);
        }
        addSourceRange(file, range.offset, range.size, range.label, true, range.pinned);
        cursor = range.offset + range.size;
    }

    if (cursor < file.decodedBytes.size()) {
        const auto gapSize = file.decodedBytes.size() - cursor;
        addUnknownRange(file, cursor, gapSize, "trailing-padding-or-unknown");
        addSourceRange(file, cursor, gapSize, "trailing-padding-or-unknown", false, true);
    }
}

bool actionRowEnvelopeMatches(std::span<const std::uint8_t> bytes, Endian endian)
{
    if (!spice::root::bounds_contains(bytes.size(), 0U, kStdHeaderSize)) {
        return false;
    }

    const auto rowCount = EndianReader(bytes, endian).read_u32(0x08U);
    if (rowCount > kMaxConservativeRowCount) {
        return false;
    }

    const auto expectedSize = static_cast<std::uint64_t>(kStdHeaderSize) +
        static_cast<std::uint64_t>(rowCount) * kActionRowSize;
    return expectedSize == bytes.size();
}

bool entryTableEnvelopeMatches(std::span<const std::uint8_t> bytes, Endian endian)
{
    if (!spice::root::bounds_contains(bytes.size(), 0U, kStdHeaderSize)) {
        return false;
    }

    const EndianReader reader(bytes, endian);
    return reader.read_u16(0x02U) == 4U &&
        reader.read_u32(0x04U) == 0U &&
        reader.read_u32(0x08U) == 0U;
}

std::optional<Endian> detectEndian(std::span<const std::uint8_t> bytes, [[maybe_unused]] bool compressed)
{
    const auto score = [&](Endian endian) {
        int value = 0;
        if (actionRowEnvelopeMatches(bytes, endian)) value += 4;
        if (entryTableEnvelopeMatches(bytes, endian)) {
            value += 3;
            const EndianReader reader(bytes, endian);
            const auto count = reader.read_u16(0U);
            const auto tableEnd = spice::root::checked_table_end(kStdHeaderSize, count, kEntryRecordSize);
            if (tableEnd.has_value() && *tableEnd <= bytes.size()) value += 2;
            if (count > 0U && tableEnd.has_value() && *tableEnd <= bytes.size() &&
                reader.read_i16(kStdHeaderSize + (count - 1U) * kEntryRecordSize) < 0) value += 2;
        }
        return value;
    };
    const auto big = score(Endian::Big);
    const auto little = score(Endian::Little);
    if (big == little) return std::nullopt;
    return big > little ? Endian::Big : Endian::Little;
}

void parseActionRows(StdFile& file)
{
    const std::span<const std::uint8_t> bytes(file.decodedBytes);
    const EndianReader reader(bytes, file.sourceEndian);
    auto& layout = file.actionRows;
    auto& header = layout.header;

    header.commandLow = reader.read_u16(0x00U);
    header.commandHigh = reader.read_u16(0x02U);
    header.combinedCommandKind =
        (static_cast<std::uint32_t>(header.commandHigh) << 16U) |
        static_cast<std::uint32_t>(header.commandLow);
    header.loaderContextWord = reader.read_u32(0x04U);
    header.rowCount = reader.read_u32(0x08U);
    header.rowTablePtrWord = reader.read_u32(0x0cU);

    layout.rows.reserve(header.rowCount);
    for (std::uint32_t index = 0U; index < header.rowCount; ++index) {
        const auto offset = kStdHeaderSize + index * kActionRowSize;
        StdActionRow row{};
        row.index = index;
        row.decodedOffset = offset;
        row.actionId = reader.read_i16(offset);
        row.rowType = reader.read_i16(offset + 0x02U);
        row.callbackIndex = reader.read_i16(offset + 0x04U);
        row.motionSlotOrdinal = reader.read_i16(offset + 0x06U);
        row.flags = reader.read_u32(offset + 0x08U);
        row.secondaryKey = reader.read_i16(offset + 0x0cU);
        row.callbackAuxParam = reader.read_i16(offset + 0x0eU);
        row.selectionTransitionScalarBits = reader.read_u32(offset + 0x10U);
        row.motionProgressScalarBits = reader.read_u32(offset + 0x14U);
        if (row.rowType == 3) {
            addDiagnostic(
                file,
                StdDiagnosticSeverity::Warning,
                "Action-row file contains a rowType 3 sentinel; source files normally omit the runtime sentinel",
                offset + 0x02U);
        }
        layout.rows.push_back(row);
    }

    buildDecodedSourceRanges(file, {
        KnownRangeCandidate{ .offset = 0U, .size = kStdHeaderSize, .label = "action-rows-header", .pinned = true },
        KnownRangeCandidate{ .offset = kStdHeaderSize, .size = layout.rows.size() * kActionRowSize, .label = "action-row-table", .pinned = true },
    });
}

void parseEntryTable(StdFile& file)
{
    const std::span<const std::uint8_t> bytes(file.decodedBytes);
    const EndianReader reader(bytes, file.sourceEndian);
    auto& layout = file.entryTable;
    auto& header = layout.header;

    header.recordCountIncludingSentinel = reader.read_u16(0x00U);
    header.kind = reader.read_u16(0x02U);
    header.reserved0 = reader.read_u32(0x04U);
    header.reserved1 = reader.read_u32(0x08U);
    header.decodedSpanMinusHeader = reader.read_u32(0x0cU);
    layout.sourceRecordCountIncludingSentinel = header.recordCountIncludingSentinel;

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
        record.sourceTableOffset = offset;
        record.sourceEndian = file.sourceEndian;
        record.locationCode = reader.read_i16(offset);
        record.opcode = reader.read_i16(offset + 0x02U);
        record.combinedType =
            (static_cast<std::uint32_t>(static_cast<std::uint16_t>(record.opcode)) << 16U) |
            static_cast<std::uint32_t>(static_cast<std::uint16_t>(record.locationCode));
        record.field2 = reader.read_u32(offset + 0x04U);
        record.payloadSize = reader.read_u32(offset + 0x08U);
        record.sourcePayloadSize = record.payloadSize;
        record.payloadOffsetRel = reader.read_u32(offset + 0x0cU);
        record.sourcePayloadOffsetRel = record.payloadOffsetRel;
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
        } else {
            record.payloadBytes.assign(
                bytes.begin() + static_cast<std::ptrdiff_t>(record.payloadOffsetAbs),
                bytes.begin() + static_cast<std::ptrdiff_t>(record.payloadOffsetAbs + record.payloadSize));
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

    if (layout.hasSentinel &&
        layout.sentinelIndex + 1U != static_cast<std::uint32_t>(header.recordCountIncludingSentinel)) {
        addDiagnostic(
            file,
            StdDiagnosticSeverity::Error,
            "Entry table sentinel position does not match declared record count",
            kStdHeaderSize + layout.sentinelIndex * kEntryRecordSize);
    }

    layout.hasPayloads = hasPayloads;
    layout.firstPayloadOffsetRel = firstPayloadOffsetRel;
    layout.maxPayloadEndRel = maxPayloadEndRel;
    const auto decodedSpanU32 = sizeToU32Saturated(decodedSpan);
    layout.trailingBytesAfterMaxPayload = maxPayloadEndRel <= decodedSpanU32
        ? decodedSpanU32 - maxPayloadEndRel
        : 0U;

    std::vector<KnownRangeCandidate> knownRanges{};
    knownRanges.push_back(KnownRangeCandidate{ .offset = 0U, .size = kStdHeaderSize, .label = "entry-table-header", .pinned = true });
    knownRanges.push_back(KnownRangeCandidate{
        .offset = kStdHeaderSize,
        .size = layout.records.size() * kEntryRecordSize,
        .label = "entry-record-table",
        .pinned = true,
    });
    for (const auto& record : layout.records) {
        if (record.isSentinel || !record.payloadInBounds || record.payloadSize == 0U) {
            continue;
        }
        std::ostringstream label;
        label << "entry-payload[" << record.index << "]";
        knownRanges.push_back(KnownRangeCandidate{
            .offset = record.payloadOffsetAbs,
            .size = record.payloadSize,
            .label = label.str(),
            .pinned = true,
        });
    }
    buildDecodedSourceRanges(file, std::move(knownRanges));
}

void parseDecodedLayout(StdFile& file)
{
    const std::span<const std::uint8_t> bytes(file.decodedBytes);
    if (bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        addDiagnostic(file, StdDiagnosticSeverity::Error, "STD decoded payload is too large to represent with a 32-bit size");
        file.parseStatus = StdParseStatus::Failed;
        return;
    }

    file.decodedSize = static_cast<std::uint32_t>(bytes.size());
    if (!spice::root::bounds_contains(bytes.size(), 0U, kStdHeaderSize)) {
        addDiagnostic(file, StdDiagnosticSeverity::Error, "STD decoded payload is too small for a 0x10-byte header");
        file.parseStatus = StdParseStatus::Failed;
        return;
    }

    if (actionRowEnvelopeMatches(bytes, file.sourceEndian)) {
        file.layoutKind = StdLayoutKind::ActionRows;
        parseActionRows(file);
        finalizeKnownLayoutStatus(file);
        return;
    }

    if (entryTableEnvelopeMatches(bytes, file.sourceEndian)) {
        file.layoutKind = StdLayoutKind::EntryTable;
        parseEntryTable(file);
        finalizeKnownLayoutStatus(file);
        return;
    }

    addDiagnostic(file, StdDiagnosticSeverity::Error, "STD decoded payload does not match a known conservative layout");
    file.parseStatus = StdParseStatus::Failed;
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
    return parseStatus == StdParseStatus::Complete && !hasErrorDiagnostics(*this);
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

const char* toString(StdParseStatus status)
{
    switch (status) {
    case StdParseStatus::Empty:
        return "empty";
    case StdParseStatus::Partial:
        return "partial";
    case StdParseStatus::Complete:
        return "complete";
    case StdParseStatus::Failed:
        return "failed";
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

const std::vector<std::uint8_t>* findEntryPayload(const StdFile& file, const std::uint32_t recordIndex)
{
    if (file.layoutKind != StdLayoutKind::EntryTable) {
        return nullptr;
    }

    const auto found = std::find_if(file.entryTable.records.begin(), file.entryTable.records.end(), [recordIndex](const auto& record) {
        return record.index == recordIndex;
    });
    if (found == file.entryTable.records.end() || found->isSentinel || !found->payloadInBounds) {
        return nullptr;
    }
    return &found->payloadBytes;
}

std::vector<std::uint8_t>* findMutableEntryPayload(StdFile& file, const std::uint32_t recordIndex)
{
    if (file.layoutKind != StdLayoutKind::EntryTable) {
        return nullptr;
    }

    const auto found = std::find_if(file.entryTable.records.begin(), file.entryTable.records.end(), [recordIndex](const auto& record) {
        return record.index == recordIndex;
    });
    if (found == file.entryTable.records.end() || found->isSentinel || !found->payloadInBounds) {
        return nullptr;
    }
    return &found->payloadBytes;
}

std::optional<StdActionViewPayload> readActionViewPayload(const StdEntryRecord& record)
{
    if (record.isSentinel ||
        !record.payloadInBounds ||
        record.combinedType != kStdActionViewCombinedType ||
        record.payloadBytes.size() != kStdActionViewPayloadSize) {
        return std::nullopt;
    }

    const std::span<const std::uint8_t> bytes(record.payloadBytes);
    StdActionViewPayload payload{};
    const EndianReader reader(bytes, record.sourceEndian);
    payload.primaryActionKey = reader.read_i16(0x00U);
    payload.routeSecondaryKey = reader.read_i16(0x02U);
    payload.directSecondaryKey = reader.read_i16(0x04U);
    payload.lowFlags = reader.read_u16(0x06U);
    payload.reserved08 = reader.read_u32(0x08U);
    payload.reserved0c = reader.read_u32(0x0cU);
    payload.actionViewFlags = reader.read_u32(0x10U);
    payload.modeLocalAngleOrOffsetBits = reader.read_u32(0x14U);
    payload.startFrame = reader.read_i16(0x18U);
    payload.reserved1a = reader.read_u16(0x1aU);
    payload.endFrame = reader.read_i16(0x1cU);
    payload.holdFrameCount = reader.read_i16(0x1eU);
    payload.stepFrameCount = reader.read_i16(0x20U);
    payload.requestedMode = reader.read_i16(0x22U);
    return payload;
}

bool writeActionViewPayload(StdEntryRecord& record, const StdActionViewPayload& payload)
{
    if (record.isSentinel ||
        !record.payloadInBounds ||
        record.combinedType != kStdActionViewCombinedType ||
        record.payloadBytes.size() != kStdActionViewPayloadSize) {
        return false;
    }

    const std::span<std::uint8_t> bytes(record.payloadBytes);
    EndianSpanWriter writer(bytes, record.sourceEndian);
    writer.write_i16_at(0x00U, payload.primaryActionKey);
    writer.write_i16_at(0x02U, payload.routeSecondaryKey);
    writer.write_i16_at(0x04U, payload.directSecondaryKey);
    writer.write_u16_at(0x06U, payload.lowFlags);
    writer.write_u32_at(0x08U, payload.reserved08);
    writer.write_u32_at(0x0cU, payload.reserved0c);
    writer.write_u32_at(0x10U, payload.actionViewFlags);
    writer.write_u32_at(0x14U, payload.modeLocalAngleOrOffsetBits);
    writer.write_i16_at(0x18U, payload.startFrame);
    writer.write_u16_at(0x1aU, payload.reserved1a);
    writer.write_i16_at(0x1cU, payload.endFrame);
    writer.write_i16_at(0x1eU, payload.holdFrameCount);
    writer.write_i16_at(0x20U, payload.stepFrameCount);
    writer.write_i16_at(0x22U, payload.requestedMode);
    return true;
}

StdFile parseBytes(std::vector<std::uint8_t> bytes,
    std::string sourcePath,
    const StdParseOptions& options)
{
    StdFile file{};
    file.sourcePath = std::move(sourcePath);
    file.rawSize = sizeToU32Saturated(bytes.size());
    file.rawBytes = std::move(bytes);

    if (file.rawBytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        addDiagnostic(file, StdDiagnosticSeverity::Error, "STD source payload is too large to represent with a 32-bit size");
        file.parseStatus = StdParseStatus::Failed;
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
            file.parseStatus = StdParseStatus::Failed;
            return file;
        }
        file.decodedBytes = decoded.bytes;
        file.decodedAvailable = true;
    } else {
        file.sourceEncoding = StdSourceEncoding::Plain;
        file.decodedBytes = file.rawBytes;
        file.decodedAvailable = true;
    }

    const auto endian = options.forcedEndian.has_value()
        ? options.forcedEndian
        : detectEndian(file.decodedBytes, file.sourceEncoding == StdSourceEncoding::Aklz);
    if (!endian.has_value()) {
        addDiagnostic(file, StdDiagnosticSeverity::Error,
            "STD byte order is ambiguous or the decoded payload matches neither known layout");
        file.parseStatus = StdParseStatus::Failed;
        return file;
    }
    file.sourceEndian = *endian;
    file.endianWasForced = options.forcedEndian.has_value();
    parseDecodedLayout(file);
    return file;
}

StdFile parseFile(const std::filesystem::path& path, const StdParseOptions& options)
{
    bool readOk = false;
    auto bytes = readAllBytes(path, readOk);
    if (readOk) {
        return parseBytes(std::move(bytes), path.string(), options);
    }

    StdFile file{};
    file.sourcePath = path.string();
    addDiagnostic(file, StdDiagnosticSeverity::Error, "Unable to open or read STD file");
    file.parseStatus = StdParseStatus::Failed;
    return file;
}

StdExportResult exportBytes(const StdFile& file, StdExportMode mode)
{
    StdExportResult result{};
    if ((mode == StdExportMode::ReencodeAklz ||
            (mode == StdExportMode::ReencodeSourceKind && file.sourceEncoding == StdSourceEncoding::Aklz)) &&
        file.sourceEndian == spice::root::Endian::Little) {
        result.error = "AKLZ output is not supported for little-endian Dreamcast STD files";
        return result;
    }
    switch (mode) {
    case StdExportMode::OriginalSourceBytes:
        result.bytes = file.rawBytes;
        return result;

    case StdExportMode::DecodedBytes:
        if (!file.decodedAvailable) {
            result.error = "STD decoded bytes are not available";
            return result;
        }
        if (file.layoutKind != StdLayoutKind::Unknown && file.parseStatus != StdParseStatus::Failed) {
            const auto written = StdFileWriter{}.write(file, StdWriteOptions{
                .sourceEncoding = StdSourceEncoding::Plain,
                .preserveExactSourceWhenUnchanged = false,
            });
            if (!written.ok()) {
                result.error = written.diagnostics.empty()
                    ? "Unable to write STD decoded bytes"
                    : written.diagnostics.front().message;
                return result;
            }
            result.bytes = written.bytes;
            return result;
        }
        result.bytes = file.decodedBytes;
        return result;

    case StdExportMode::ReencodeSourceKind:
        if (!file.decodedAvailable) {
            result.error = "STD decoded bytes are not available";
            return result;
        }
        if (file.layoutKind != StdLayoutKind::Unknown && file.parseStatus != StdParseStatus::Failed) {
            const auto written = StdFileWriter{}.write(file, StdWriteOptions{
                .sourceEncoding = file.sourceEncoding,
                .preserveExactSourceWhenUnchanged = false,
            });
            if (!written.ok()) {
                result.error = written.diagnostics.empty()
                    ? "Unable to re-encode STD source bytes"
                    : written.diagnostics.front().message;
                return result;
            }
            result.bytes = written.bytes;
            return result;
        }
        if (file.sourceEncoding == StdSourceEncoding::Plain) {
            result.bytes = file.decodedBytes;
            return result;
        }
        break;

    case StdExportMode::ReencodeAklz:
        if (!file.decodedAvailable) {
            result.error = "STD decoded bytes are not available";
            return result;
        }
        if (file.layoutKind != StdLayoutKind::Unknown && file.parseStatus != StdParseStatus::Failed) {
            const auto written = StdFileWriter{}.write(file, StdWriteOptions{
                .sourceEncoding = StdSourceEncoding::Aklz,
                .preserveExactSourceWhenUnchanged = false,
            });
            if (!written.ok()) {
                result.error = written.diagnostics.empty()
                    ? "Unable to AKLZ-compress STD decoded bytes"
                    : written.diagnostics.front().message;
                return result;
            }
            result.bytes = written.bytes;
            return result;
        }
        break;
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
