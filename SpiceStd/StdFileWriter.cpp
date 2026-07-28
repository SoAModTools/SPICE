#include "StdFileWriter.h"

#include "../Compression/Aklz.h"

#include <algorithm>
#include <limits>
#include <span>
#include <sstream>
#include <utility>

namespace spice::stdfile {
namespace {

constexpr std::uint32_t kStdHeaderSize = 0x10U;
constexpr std::uint32_t kActionRowSize = 0x18U;
constexpr std::uint32_t kEntryRecordSize = 0x10U;

struct PayloadInterval {
    std::uint32_t recordIndex{ 0U };
    std::size_t offset{ 0U };
    std::size_t size{ 0U };
};

void addDiagnostic(StdWriteResult& result, const StdDiagnosticSeverity severity, std::string message, const std::uint32_t offset = 0U)
{
    result.diagnostics.push_back(StdDiagnostic{ severity, std::move(message), offset });
}

bool hasErrorDiagnostics(const StdWriteResult& result)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity == StdDiagnosticSeverity::Error;
    });
}

std::uint32_t sizeToU32Saturated(const std::size_t size)
{
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(size);
}

bool canReadRange(const std::size_t size, const std::size_t offset, const std::size_t length)
{
    return offset <= size && length <= size - offset;
}

void writeU16BeUnchecked(std::span<std::uint8_t> bytes, const std::uint32_t offset, const std::uint16_t value)
{
    bytes[offset + 0U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value & 0xffU);
}

void writeS16BeUnchecked(std::span<std::uint8_t> bytes, const std::uint32_t offset, const std::int16_t value)
{
    writeU16BeUnchecked(bytes, offset, static_cast<std::uint16_t>(value));
}

void writeU32BeUnchecked(std::span<std::uint8_t> bytes, const std::uint32_t offset, const std::uint32_t value)
{
    bytes[offset + 0U] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value & 0xffU);
}

void addLayout(
    StdWriteResult& result,
    std::string kind,
    const std::uint32_t sourceOffset,
    const std::uint32_t outputOffset,
    const std::size_t sourceSize,
    const std::size_t outputSize,
    const bool copiedVerbatim = false)
{
    result.layout.push_back(StdWriteLayoutRecord{
        .kind = std::move(kind),
        .sourceOffset = sourceOffset,
        .outputOffset = outputOffset,
        .sourceSize = sourceSize,
        .outputSize = outputSize,
        .copiedVerbatim = copiedVerbatim,
    });
}

std::vector<std::uint8_t> serializeActionRows(const StdFile& file, StdWriteResult& result)
{
    const auto rowCount = file.actionRows.rows.size();
    const auto decodedSize64 = static_cast<std::uint64_t>(kStdHeaderSize) +
        static_cast<std::uint64_t>(rowCount) * kActionRowSize;
    if (decodedSize64 > std::numeric_limits<std::uint32_t>::max()) {
        addDiagnostic(result, StdDiagnosticSeverity::Error, "Action-row STD output is too large to represent with a 32-bit size");
        return {};
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(decodedSize64), 0U);
    const std::span<std::uint8_t> out(bytes);
    const auto& header = file.actionRows.header;
    writeU16BeUnchecked(out, 0x00U, header.commandLow);
    writeU16BeUnchecked(out, 0x02U, header.commandHigh);
    writeU32BeUnchecked(out, 0x04U, header.loaderContextWord);
    writeU32BeUnchecked(out, 0x08U, static_cast<std::uint32_t>(rowCount));
    writeU32BeUnchecked(out, 0x0cU, header.rowTablePtrWord);

    for (std::size_t i = 0U; i < rowCount; ++i) {
        const auto& row = file.actionRows.rows[i];
        const auto offset = static_cast<std::uint32_t>(kStdHeaderSize + i * kActionRowSize);
        writeS16BeUnchecked(out, offset + 0x00U, row.actionId);
        writeS16BeUnchecked(out, offset + 0x02U, row.rowType);
        writeS16BeUnchecked(out, offset + 0x04U, row.callbackIndex);
        writeS16BeUnchecked(out, offset + 0x06U, row.motionSlotOrdinal);
        writeU32BeUnchecked(out, offset + 0x08U, row.flags);
        writeS16BeUnchecked(out, offset + 0x0cU, row.secondaryKey);
        writeS16BeUnchecked(out, offset + 0x0eU, row.callbackAuxParam);
        writeU32BeUnchecked(out, offset + 0x10U, row.selectionTransitionScalarBits);
        writeU32BeUnchecked(out, offset + 0x14U, row.motionProgressScalarBits);
    }

    addLayout(result, "action-rows-header", 0U, 0U, kStdHeaderSize, kStdHeaderSize);
    addLayout(result, "action-row-table", kStdHeaderSize, kStdHeaderSize, rowCount * kActionRowSize, rowCount * kActionRowSize);
    return bytes;
}

void validateEntryPayloadIntervals(StdWriteResult& result, std::vector<PayloadInterval> intervals, const std::size_t tableEnd)
{
    std::sort(intervals.begin(), intervals.end(), [](const auto& left, const auto& right) {
        if (left.offset != right.offset) {
            return left.offset < right.offset;
        }
        return left.size < right.size;
    });

    std::size_t cursor = tableEnd;
    for (const auto& interval : intervals) {
        if (interval.size == 0U) {
            continue;
        }
        if (interval.offset < tableEnd) {
            addDiagnostic(
                result,
                StdDiagnosticSeverity::Error,
                "Entry payload overlaps the entry record table; relocation is out of scope for this writer",
                sizeToU32Saturated(interval.offset));
            continue;
        }
        if (interval.offset < cursor) {
            addDiagnostic(
                result,
                StdDiagnosticSeverity::Error,
                "Entry payload spans overlap; overlap repair is out of scope for this writer",
                sizeToU32Saturated(interval.offset));
            continue;
        }
        cursor = interval.offset + interval.size;
    }
}

std::vector<std::uint8_t> serializeEntryTable(const StdFile& file, StdWriteResult& result)
{
    if (!file.decodedAvailable) {
        addDiagnostic(result, StdDiagnosticSeverity::Error, "STD decoded bytes are not available");
        return {};
    }

    if (file.decodedBytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        addDiagnostic(result, StdDiagnosticSeverity::Error, "Entry-table STD output is too large to represent with a 32-bit size");
        return {};
    }

    if (!canReadRange(file.decodedBytes.size(), 0U, kStdHeaderSize)) {
        addDiagnostic(result, StdDiagnosticSeverity::Error, "Entry-table STD output is too small for a 0x10-byte header");
        return {};
    }

    const auto& layout = file.entryTable;
    const auto recordCount = layout.records.size();
    const auto sourceRecordCount = layout.sourceRecordCountIncludingSentinel != 0U
        ? static_cast<std::size_t>(layout.sourceRecordCountIncludingSentinel)
        : static_cast<std::size_t>(layout.header.recordCountIncludingSentinel);
    if (recordCount != sourceRecordCount ||
        recordCount != static_cast<std::size_t>(layout.header.recordCountIncludingSentinel)) {
        addDiagnostic(
            result,
            StdDiagnosticSeverity::Error,
            "Entry-table record insertion/removal is out of scope for this writer",
            kStdHeaderSize);
        return {};
    }

    if (!layout.hasSentinel ||
        layout.sentinelIndex >= recordCount ||
        !layout.records[layout.sentinelIndex].isSentinel ||
        layout.sentinelIndex + 1U != recordCount) {
        addDiagnostic(
            result,
            StdDiagnosticSeverity::Error,
            "Entry-table writer requires the parsed sentinel to remain the final serialized record",
            kStdHeaderSize);
        return {};
    }

    const auto tableSize = recordCount * kEntryRecordSize;
    const auto tableEnd = static_cast<std::size_t>(kStdHeaderSize) + tableSize;
    if (!canReadRange(file.decodedBytes.size(), kStdHeaderSize, tableSize)) {
        addDiagnostic(result, StdDiagnosticSeverity::Error, "Entry record table extends beyond decoded file size", kStdHeaderSize);
        return {};
    }

    std::vector<PayloadInterval> intervals{};
    for (std::size_t i = 0U; i < recordCount; ++i) {
        const auto& record = layout.records[i];
        const auto expectedTableOffset = static_cast<std::uint32_t>(kStdHeaderSize + i * kEntryRecordSize);
        if (record.tableOffset != expectedTableOffset || record.sourceTableOffset != expectedTableOffset) {
            addDiagnostic(
                result,
                StdDiagnosticSeverity::Error,
                "Entry-table record offset changes are out of scope for this writer",
                expectedTableOffset);
            continue;
        }
        if (record.isSentinel) {
            continue;
        }
        if (!record.payloadInBounds) {
            addDiagnostic(result, StdDiagnosticSeverity::Error, "Entry payload is not safely writable because it was not in bounds", expectedTableOffset);
            continue;
        }
        if (record.payloadSize != record.sourcePayloadSize ||
            record.payloadOffsetRel != record.sourcePayloadOffsetRel ||
            record.payloadBytes.size() != record.sourcePayloadSize) {
            addDiagnostic(
                result,
                StdDiagnosticSeverity::Error,
                "Entry payload size or relocation change is out of scope for this writer",
                expectedTableOffset);
            continue;
        }
        const auto payloadOffsetAbs64 = static_cast<std::uint64_t>(kStdHeaderSize) + record.payloadOffsetRel;
        const auto payloadEndAbs64 = payloadOffsetAbs64 + record.payloadBytes.size();
        if (payloadEndAbs64 > file.decodedBytes.size() || payloadOffsetAbs64 > std::numeric_limits<std::uint32_t>::max()) {
            addDiagnostic(result, StdDiagnosticSeverity::Error, "Entry payload no longer fits in the decoded output", expectedTableOffset);
            continue;
        }
        intervals.push_back(PayloadInterval{
            .recordIndex = record.index,
            .offset = static_cast<std::size_t>(payloadOffsetAbs64),
            .size = record.payloadBytes.size(),
        });
    }

    validateEntryPayloadIntervals(result, std::move(intervals), tableEnd);
    if (hasErrorDiagnostics(result)) {
        return {};
    }

    std::vector<std::uint8_t> bytes = file.decodedBytes;
    const std::span<std::uint8_t> out(bytes);
    writeU16BeUnchecked(out, 0x00U, static_cast<std::uint16_t>(recordCount));
    writeU16BeUnchecked(out, 0x02U, layout.header.kind);
    writeU32BeUnchecked(out, 0x04U, layout.header.reserved0);
    writeU32BeUnchecked(out, 0x08U, layout.header.reserved1);
    writeU32BeUnchecked(out, 0x0cU, layout.header.decodedSpanMinusHeader);

    for (std::size_t i = 0U; i < recordCount; ++i) {
        const auto& record = layout.records[i];
        const auto offset = static_cast<std::uint32_t>(kStdHeaderSize + i * kEntryRecordSize);
        writeS16BeUnchecked(out, offset + 0x00U, record.locationCode);
        writeS16BeUnchecked(out, offset + 0x02U, record.opcode);
        writeU32BeUnchecked(out, offset + 0x04U, record.field2);
        writeU32BeUnchecked(out, offset + 0x08U, record.payloadSize);
        writeU32BeUnchecked(out, offset + 0x0cU, record.payloadOffsetRel);
        if (!record.isSentinel && !record.payloadBytes.empty()) {
            std::copy(
                record.payloadBytes.begin(),
                record.payloadBytes.end(),
                bytes.begin() + static_cast<std::ptrdiff_t>(kStdHeaderSize + record.payloadOffsetRel));
        }
    }

    addLayout(result, "entry-table-header", 0U, 0U, kStdHeaderSize, kStdHeaderSize);
    addLayout(result, "entry-record-table", kStdHeaderSize, kStdHeaderSize, tableSize, tableSize);
    for (const auto& record : layout.records) {
        if (record.isSentinel || record.payloadBytes.empty()) {
            continue;
        }
        addLayout(
            result,
            "entry-payload",
            record.payloadOffsetAbs,
            record.payloadOffsetAbs,
            record.payloadBytes.size(),
            record.payloadBytes.size());
    }
    return bytes;
}

std::vector<std::uint8_t> serializeDecoded(const StdFile& file, StdWriteResult& result)
{
    if (!file.decodedAvailable) {
        addDiagnostic(result, StdDiagnosticSeverity::Error, "STD decoded bytes are not available");
        return {};
    }
    if (file.parseStatus == StdParseStatus::Empty || file.parseStatus == StdParseStatus::Failed) {
        addDiagnostic(result, StdDiagnosticSeverity::Error, "STD file does not have a canonical parsed layout");
        return {};
    }

    switch (file.layoutKind) {
    case StdLayoutKind::ActionRows:
        return serializeActionRows(file, result);
    case StdLayoutKind::EntryTable:
        return serializeEntryTable(file, result);
    case StdLayoutKind::Unknown:
        break;
    }

    addDiagnostic(result, StdDiagnosticSeverity::Error, "STD layout kind is not supported by the model-aware writer");
    return {};
}

} // namespace

bool StdWriteResult::ok() const noexcept
{
    return !hasErrorDiagnostics(*this);
}

StdWriteResult StdFileWriter::write(const StdFile& file, const StdWriteOptions& options) const
{
    StdWriteResult result{};
    result.sourceSize = file.rawBytes.size();

    auto decoded = serializeDecoded(file, result);
    if (!result.ok()) {
        return result;
    }

    const auto outputEncoding = options.sourceEncoding.value_or(file.sourceEncoding);
    const auto preservesSourceEncoding = !options.sourceEncoding.has_value() || outputEncoding == file.sourceEncoding;
    if (options.preserveExactSourceWhenUnchanged &&
        preservesSourceEncoding &&
        decoded == file.decodedBytes) {
        result.bytes = file.rawBytes;
        result.outputSize = result.bytes.size();
        if (result.layout.empty()) {
            addLayout(result, "source", 0U, 0U, file.rawBytes.size(), file.rawBytes.size(), true);
        }
        return result;
    }

    if (outputEncoding == StdSourceEncoding::Plain) {
        result.bytes = std::move(decoded);
        result.outputSize = result.bytes.size();
        return result;
    }

    const auto encoded = spice::compression::aklz::compress(decoded);
    if (!encoded.ok()) {
        addDiagnostic(
            result,
            StdDiagnosticSeverity::Error,
            std::string("Unable to AKLZ-compress STD decoded bytes: ") +
                std::string(spice::compression::aklz::errorToString(encoded.error)));
        result.bytes.clear();
        result.outputSize = 0U;
        return result;
    }
    result.bytes = encoded.bytes;
    result.outputSize = result.bytes.size();
    return result;
}

} // namespace spice::stdfile
