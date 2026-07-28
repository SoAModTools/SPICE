#include "StdJsonExporter.h"

#include <iomanip>
#include <sstream>
#include <span>

namespace spice::stdfile {
namespace {

std::string jsonEscape(const std::string& value)
{
    std::string escaped{};
    escaped.reserve(value.size() + 8U);
    for (const char c : value) {
        switch (c) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(c);
            break;
        }
    }
    return escaped;
}

std::string bytesHex(std::span<const std::uint8_t> bytes)
{
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        out << std::setw(2) << static_cast<unsigned>(byte);
    }
    return out.str();
}

std::string u32Hex(const std::uint32_t value)
{
    std::ostringstream out;
    out << "0x" << std::hex << std::setfill('0') << std::setw(8) << value;
    return out.str();
}

void writeDiagnostics(std::ostringstream& out, const std::vector<StdDiagnostic>& diagnostics)
{
    out << "[";
    for (std::size_t i = 0U; i < diagnostics.size(); ++i) {
        if (i != 0U) {
            out << ',';
        }
        const auto& diagnostic = diagnostics[i];
        out << "{\"severity\":\"" << toString(diagnostic.severity)
            << "\",\"offset\":" << diagnostic.offset
            << ",\"message\":\"" << jsonEscape(diagnostic.message) << "\"}";
    }
    out << "]";
}

void writeSourceRanges(std::ostringstream& out, const std::vector<StdSourceRange>& ranges)
{
    out << "[";
    for (std::size_t i = 0U; i < ranges.size(); ++i) {
        if (i != 0U) {
            out << ',';
        }
        const auto& range = ranges[i];
        out << "{\"offset\":" << range.offset
            << ",\"size\":" << range.size
            << ",\"label\":\"" << jsonEscape(range.label)
            << "\",\"known\":" << (range.known ? "true" : "false")
            << ",\"pinned\":" << (range.pinned ? "true" : "false")
            << "}";
    }
    out << "]";
}

void writeUnknownRanges(std::ostringstream& out, const std::vector<StdUnknownRange>& ranges)
{
    out << "[";
    for (std::size_t i = 0U; i < ranges.size(); ++i) {
        if (i != 0U) {
            out << ',';
        }
        const auto& range = ranges[i];
        out << "{\"offset\":" << range.offset
            << ",\"size\":" << range.size
            << ",\"label\":\"" << jsonEscape(range.label)
            << "\",\"pinned\":" << (range.pinned ? "true" : "false")
            << ",\"bytesHex\":\"" << bytesHex(std::span<const std::uint8_t>(range.bytes.data(), range.bytes.size()))
            << "\"}";
    }
    out << "]";
}

void writeActionRows(std::ostringstream& out, const StdActionRowsLayout& layout)
{
    const auto& header = layout.header;
    out << "{\n";
    out << "    \"header\": {\n";
    out << "      \"commandLow\": " << header.commandLow << ",\n";
    out << "      \"commandHigh\": " << header.commandHigh << ",\n";
    out << "      \"combinedCommandKind\": " << header.combinedCommandKind << ",\n";
    out << "      \"combinedCommandKindHex\": \"" << u32Hex(header.combinedCommandKind) << "\",\n";
    out << "      \"loaderContextWord\": " << header.loaderContextWord << ",\n";
    out << "      \"loaderContextWordHex\": \"" << u32Hex(header.loaderContextWord) << "\",\n";
    out << "      \"rowCount\": " << header.rowCount << ",\n";
    out << "      \"rowTablePtrWord\": " << header.rowTablePtrWord << ",\n";
    out << "      \"rowTablePtrWordHex\": \"" << u32Hex(header.rowTablePtrWord) << "\"\n";
    out << "    },\n";
    out << "    \"rows\": [\n";
    for (std::size_t i = 0U; i < layout.rows.size(); ++i) {
        if (i != 0U) {
            out << ",\n";
        }
        const auto& row = layout.rows[i];
        out << "      {\n";
        out << "        \"index\": " << row.index << ",\n";
        out << "        \"decodedOffset\": " << row.decodedOffset << ",\n";
        out << "        \"actionId\": " << row.actionId << ",\n";
        out << "        \"rowType\": " << row.rowType << ",\n";
        out << "        \"callbackIndex\": " << row.callbackIndex << ",\n";
        out << "        \"callbackOrdinal\": " << row.motionSlotOrdinal << ",\n";
        out << "        \"flags\": " << row.flags << ",\n";
        out << "        \"flagsHex\": \"" << u32Hex(row.flags) << "\",\n";
        out << "        \"secondaryKey\": " << row.secondaryKey << ",\n";
        out << "        \"callbackAuxParam\": " << row.callbackAuxParam << ",\n";
        out << "        \"transitionGateDivisorBits\": " << row.selectionTransitionScalarBits << ",\n";
        out << "        \"transitionGateDivisorHex\": \"" << u32Hex(row.selectionTransitionScalarBits) << "\",\n";
        out << "        \"motionProgressStepBits\": " << row.motionProgressScalarBits << ",\n";
        out << "        \"motionProgressStepHex\": \"" << u32Hex(row.motionProgressScalarBits) << "\"\n";
        out << "      }";
    }
    out << "\n";
    out << "    ]\n";
    out << "  }";
}

void writeEntryPayloadBytes(std::ostringstream& out, const StdEntryRecord& record)
{
    if (record.isSentinel || !record.payloadInBounds) {
        out << "null";
        return;
    }

    out << '"' << bytesHex(std::span<const std::uint8_t>(record.payloadBytes.data(), record.payloadBytes.size())) << '"';
}

void writeActionViewPayload(std::ostringstream& out, const StdActionViewPayload& payload)
{
    out << "{";
    out << "\"primaryActionKey\":" << payload.primaryActionKey;
    out << ",\"routeSecondaryKey\":" << payload.routeSecondaryKey;
    out << ",\"directSecondaryKey\":" << payload.directSecondaryKey;
    out << ",\"lowFlags\":" << payload.lowFlags;
    out << ",\"lowFlagsHex\":\"0x" << std::hex << std::setfill('0') << std::setw(4) << payload.lowFlags << std::dec << "\"";
    out << ",\"reserved08\":" << payload.reserved08;
    out << ",\"reserved08Hex\":\"" << u32Hex(payload.reserved08) << "\"";
    out << ",\"reserved0c\":" << payload.reserved0c;
    out << ",\"reserved0cHex\":\"" << u32Hex(payload.reserved0c) << "\"";
    out << ",\"actionViewFlags\":" << payload.actionViewFlags;
    out << ",\"actionViewFlagsHex\":\"" << u32Hex(payload.actionViewFlags) << "\"";
    out << ",\"modeLocalAngleOrOffsetBits\":" << payload.modeLocalAngleOrOffsetBits;
    out << ",\"modeLocalAngleOrOffsetHex\":\"" << u32Hex(payload.modeLocalAngleOrOffsetBits) << "\"";
    out << ",\"startFrame\":" << payload.startFrame;
    out << ",\"reserved1a\":" << payload.reserved1a;
    out << ",\"endFrame\":" << payload.endFrame;
    out << ",\"holdFrameCount\":" << payload.holdFrameCount;
    out << ",\"stepFrameCount\":" << payload.stepFrameCount;
    out << ",\"requestedMode\":" << payload.requestedMode;
    out << "}";
}

void writeEntryTable(std::ostringstream& out, const StdFile& file)
{
    const auto& layout = file.entryTable;
    const auto& header = layout.header;
    out << "{\n";
    out << "    \"header\": {\n";
    out << "      \"recordCountIncludingSentinel\": " << header.recordCountIncludingSentinel << ",\n";
    out << "      \"kind\": " << header.kind << ",\n";
    out << "      \"reserved0\": " << header.reserved0 << ",\n";
    out << "      \"reserved0Hex\": \"" << u32Hex(header.reserved0) << "\",\n";
    out << "      \"reserved1\": " << header.reserved1 << ",\n";
    out << "      \"reserved1Hex\": \"" << u32Hex(header.reserved1) << "\",\n";
    out << "      \"decodedSpanMinusHeader\": " << header.decodedSpanMinusHeader << "\n";
    out << "    },\n";
    out << "    \"headerSpanDelta\": " << layout.headerSpanDelta << ",\n";
    out << "    \"hasSentinel\": " << (layout.hasSentinel ? "true" : "false") << ",\n";
    out << "    \"sentinelIndex\": " << layout.sentinelIndex << ",\n";
    out << "    \"entryCountWithoutSentinel\": " << layout.entryCountWithoutSentinel << ",\n";
    out << "    \"hasPayloads\": " << (layout.hasPayloads ? "true" : "false") << ",\n";
    out << "    \"firstPayloadOffsetRel\": " << layout.firstPayloadOffsetRel << ",\n";
    out << "    \"maxPayloadEndRel\": " << layout.maxPayloadEndRel << ",\n";
    out << "    \"trailingBytesAfterMaxPayload\": " << layout.trailingBytesAfterMaxPayload << ",\n";
    out << "    \"records\": [\n";
    for (std::size_t i = 0U; i < layout.records.size(); ++i) {
        if (i != 0U) {
            out << ",\n";
        }
        const auto& record = layout.records[i];
        out << "      {\n";
        out << "        \"index\": " << record.index << ",\n";
        out << "        \"tableOffset\": " << record.tableOffset << ",\n";
        out << "        \"sourceTableOffset\": " << record.sourceTableOffset << ",\n";
        out << "        \"isSentinel\": " << (record.isSentinel ? "true" : "false") << ",\n";
        out << "        \"locationCode\": " << record.locationCode << ",\n";
        out << "        \"opcode\": " << record.opcode << ",\n";
        out << "        \"combinedType\": " << record.combinedType << ",\n";
        out << "        \"combinedTypeHex\": \"" << u32Hex(record.combinedType) << "\",\n";
        out << "        \"field2\": " << record.field2 << ",\n";
        out << "        \"field2Hex\": \"" << u32Hex(record.field2) << "\",\n";
        out << "        \"payloadSize\": " << record.payloadSize << ",\n";
        out << "        \"sourcePayloadSize\": " << record.sourcePayloadSize << ",\n";
        out << "        \"payloadOffsetOrPtr\": " << record.payloadOffsetRel << ",\n";
        out << "        \"sourcePayloadOffsetOrPtr\": " << record.sourcePayloadOffsetRel << ",\n";
        out << "        \"payloadOffsetOrPtrHex\": \"" << u32Hex(record.payloadOffsetRel) << "\",\n";
        out << "        \"payloadOffsetAbs\": " << record.payloadOffsetAbs << ",\n";
        out << "        \"payloadEndRel\": " << record.payloadEndRel << ",\n";
        out << "        \"payloadInBounds\": " << (record.payloadInBounds ? "true" : "false") << ",\n";
        out << "        \"payloadBytesHex\": ";
        writeEntryPayloadBytes(out, record);
        out << ",\n";
        out << "        \"actionViewPayload\": ";
        if (const auto payload = readActionViewPayload(record)) {
            writeActionViewPayload(out, *payload);
        } else {
            out << "null";
        }
        out << "\n";
        out << "      }";
    }
    out << "\n";
    out << "    ]\n";
    out << "  }";
}

} // namespace

std::string StdJsonExporter::toJson(const StdFile& file) const
{
    std::ostringstream out;
    out << "{\n";
    out << "  \"schema\": \"spice_std_ir_v1\",\n";
    out << "  \"source\": \"" << jsonEscape(file.sourcePath) << "\",\n";
    out << "  \"sourceEncoding\": \"" << toString(file.sourceEncoding) << "\",\n";
    out << "  \"parseStatus\": \"" << toString(file.parseStatus) << "\",\n";
    out << "  \"layoutKind\": \"" << toString(file.layoutKind) << "\",\n";
    out << "  \"parseOk\": " << (file.ok() ? "true" : "false") << ",\n";
    out << "  \"rawSize\": " << file.rawSize << ",\n";
    out << "  \"decodedSize\": " << file.decodedSize << ",\n";
    out << "  \"decodedAvailable\": " << (file.decodedAvailable ? "true" : "false") << ",\n";
    out << "  \"rawBytesHex\": \""
        << bytesHex(std::span<const std::uint8_t>(file.rawBytes.data(), file.rawBytes.size())) << "\",\n";
    out << "  \"decodedBytesHex\": ";
    if (file.decodedAvailable) {
        out << '"' << bytesHex(std::span<const std::uint8_t>(file.decodedBytes.data(), file.decodedBytes.size())) << '"';
    } else {
        out << "null";
    }
    out << ",\n";
    out << "  \"diagnostics\": ";
    writeDiagnostics(out, file.diagnostics);
    out << ",\n";
    out << "  \"sourceRanges\": ";
    writeSourceRanges(out, file.sourceRanges);
    out << ",\n";
    out << "  \"unknownRanges\": ";
    writeUnknownRanges(out, file.unknownRanges);
    out << ",\n";

    out << "  \"actionRows\": ";
    if (file.layoutKind == StdLayoutKind::ActionRows) {
        writeActionRows(out, file.actionRows);
    } else {
        out << "null";
    }
    out << ",\n";

    out << "  \"entryTable\": ";
    if (file.layoutKind == StdLayoutKind::EntryTable) {
        writeEntryTable(out, file);
    } else {
        out << "null";
    }
    out << "\n";
    out << "}\n";
    return out.str();
}

} // namespace spice::stdfile
