#include "SstSmlExport.h"
#include "SstSmlDocumentMaterialization.h"
#include "SstParser.h"
#include "../SpiceRoot/Binary/EndianReader.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <span>
#include <string_view>
#include <system_error>

namespace spice::sstsml {
using namespace detail;
namespace {

std::string jsonEscape(std::string_view value) {
    std::ostringstream out;
    for (const char c : value) {
        switch (c) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20U) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<int>(static_cast<unsigned char>(c));
            } else {
                out << c;
            }
            break;
        }
    }
    return out.str();
}

void writeJsonString(std::ostream& out, const std::string& value) {
    out << '"' << jsonEscape(value) << '"';
}

std::string pathString(const std::filesystem::path& path) {
    return path.generic_string();
}

void writeJsonPathOrNull(std::ostream& out, const std::filesystem::path& path) {
    if (path.empty()) {
        out << "null";
        return;
    }
    writeJsonString(out, pathString(path));
}

std::string severityName(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::Info: return "info";
    case DiagnosticSeverity::Warning: return "warning";
    case DiagnosticSeverity::Error: return "error";
    default: return "unknown";
    }
}

std::string fieldKindName(CommandFieldKind kind) {
    switch (kind) {
    case CommandFieldKind::ModelIndex: return "modelIndex";
    case CommandFieldKind::RuntimeSlot: return "runtimeSlot";
    case CommandFieldKind::LookupKey: return "lookupKey";
    case CommandFieldKind::RawWord: return "rawWord";
    case CommandFieldKind::HalfwordParameter: return "halfwordParameter";
    case CommandFieldKind::FloatParameter: return "floatParameter";
    case CommandFieldKind::RuntimePointer: return "runtimePointer";
    case CommandFieldKind::VectorComponent: return "vectorComponent";
    case CommandFieldKind::RotationComponent: return "rotationComponent";
    case CommandFieldKind::VectorDelta: return "vectorDelta";
    case CommandFieldKind::Duration: return "duration";
    case CommandFieldKind::Counter: return "counter";
    case CommandFieldKind::AxisSelector: return "axisSelector";
    case CommandFieldKind::BufferPointer: return "bufferPointer";
    case CommandFieldKind::ReservedRaw: return "reservedRaw";
    default: return "unknown";
    }
}

std::string fieldWidthName(CommandFieldWidth width) {
    switch (width) {
    case CommandFieldWidth::I8: return "i8";
    case CommandFieldWidth::U8: return "u8";
    case CommandFieldWidth::I16: return "i16";
    case CommandFieldWidth::U16: return "u16";
    case CommandFieldWidth::U32: return "u32";
    case CommandFieldWidth::F32: return "f32";
    default: return "unknown";
    }
}

std::string fieldEvidenceName(CommandFieldEvidence evidence) {
    switch (evidence) {
    case CommandFieldEvidence::Gekko: return "gekko";
    case CommandFieldEvidence::GekkoAndCorpus: return "gekkoAndCorpus";
    case CommandFieldEvidence::CorpusStable: return "corpusStable";
    case CommandFieldEvidence::CodeSupportedCorpusAbsent: return "codeSupportedCorpusAbsent";
    case CommandFieldEvidence::Provisional: return "provisional";
    default: return "unknown";
    }
}

std::string fieldScopeName(CommandFieldScope scope) {
    switch (scope) {
    case CommandFieldScope::StructuralPayload: return "structuralPayload";
    case CommandFieldScope::ConsumerTrailing: return "consumerTrailing";
    case CommandFieldScope::RuntimeLocal: return "runtimeLocal";
    default: return "unknown";
    }
}

bool canRead(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t size) {
    return offset <= bytes.size() && size <= bytes.size() - offset;
}

std::optional<std::uint8_t> readU8(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (!canRead(bytes, offset, 1U)) {
        return std::nullopt;
    }
    return bytes[offset];
}

std::optional<std::int16_t> readI16(std::span<const std::uint8_t> bytes, std::size_t offset,
    spice::root::Endian endian) {
    return spice::root::EndianReader(bytes, endian).try_read_i16(offset);
}

std::optional<std::uint32_t> readU32(std::span<const std::uint8_t> bytes, std::size_t offset,
    spice::root::Endian endian) {
    return spice::root::EndianReader(bytes, endian).try_read_u32(offset);
}

std::optional<float> readF32(std::span<const std::uint8_t> bytes, std::size_t offset,
    spice::root::Endian endian) {
    return spice::root::EndianReader(bytes, endian).try_read_f32(offset);
}

void writeOptionalI16(std::ostream& out, std::optional<std::int16_t> value) {
    if (value.has_value()) {
        out << *value;
    } else {
        out << "null";
    }
}

void writeOptionalU8(std::ostream& out, std::optional<std::uint8_t> value) {
    if (value.has_value()) {
        out << static_cast<unsigned>(*value);
    } else {
        out << "null";
    }
}

void writeOptionalU32(std::ostream& out, std::optional<std::uint32_t> value) {
    if (value.has_value()) {
        out << *value;
    } else {
        out << "null";
    }
}

void writeOptionalF32(std::ostream& out, std::optional<float> value) {
    if (value.has_value()) {
        out << std::setprecision(9) << *value;
    } else {
        out << "null";
    }
}

std::string hex32(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

std::string bytesHex(std::span<const std::uint8_t> bytes) {
    std::ostringstream out;
    out << std::hex << std::uppercase << std::setfill('0');
    for (std::size_t i = 0U; i < bytes.size(); ++i) {
        if (i != 0U) {
            out << ' ';
        }
        out << std::setw(2) << static_cast<unsigned>(bytes[i]);
    }
    return out.str();
}

void writeDiagnosticsArray(std::ostream& out, const std::vector<ParseDiagnostic>& diagnostics, const std::string& indent) {
    out << "[";
    for (std::size_t i = 0; i < diagnostics.size(); ++i) {
        const auto& diagnostic = diagnostics[i];
        out << (i == 0 ? "\n" : ",\n") << indent << "  {\"severity\":";
        writeJsonString(out, severityName(diagnostic.severity));
        out << ",\"offset\":" << diagnostic.offset << ",\"message\":";
        writeJsonString(out, diagnostic.message);
        out << "}";
    }
    if (!diagnostics.empty()) {
        out << "\n" << indent;
    }
    out << "]";
}

void writePostCommandTail(std::ostream& out, const SstCommandBlock& block, const std::string& indent) {
    out << "{\n"
        << indent << "  \"offset\":" << block.postCommandTailOffset
        << ",\n" << indent << "  \"size\":" << block.postCommandTailSize
        << ",\n" << indent << "  \"inBounds\":" << (block.postCommandTailInBounds ? "true" : "false")
        << ",\n" << indent << "  \"bytesHex\":";
    writeJsonString(out, bytesHex(block.postCommandTailBytes));
    out << "\n" << indent << "}";
}

void writeBattleGridTerrainSource9x9(std::ostream& out,
    const std::optional<SstBattleGridTerrainSource>& source,
    const std::string& indent) {
    if (!source.has_value()) {
        out << "null";
        return;
    }

    out << "{\n"
        << indent << "  \"sourceOffset\":" << source->sourceOffset
        << ",\n" << indent << "  \"sourceSize\":" << source->sourceSize
        << ",\n" << indent << "  \"inBounds\":" << (source->inBounds ? "true" : "false")
        << ",\n" << indent << "  \"source9x9Rows\":[";
    for (std::size_t row = 0U; row < 9U; ++row) {
        out << (row == 0U ? "\n" : ",\n") << indent << "    [";
        for (std::size_t col = 0U; col < 9U; ++col) {
            out << (col == 0U ? "" : ",")
                << static_cast<unsigned>(source->source9x9[(row * 9U) + col]);
        }
        out << "]";
    }
    out << "\n" << indent << "  ],\n"
        << indent << "  \"paddingAfterSourceHex\":";
    writeJsonString(out, bytesHex(source->paddingAfterSource));
    out << "\n" << indent << "}";
}

void writeActiveRowRuntimeContext(std::ostream& out, const std::string& indent) {
    out << "{\n"
        << indent << "  \"evidenceScope\":\"runtimeContextOnly\",\n"
        << indent << "  \"provedRowStride\":20,\n"
        << indent << "  \"allocationWidthPerRecord\":44,\n"
        << indent << "  \"allocationWidthNote\":";
    writeJsonString(out,
        "JoinSmlSstRecords allocates recordCount * 0x2c, but current direct Gekko evidence addresses active rows with recordIndex * 0x14.");
    out << ",\n" << indent << "  \"fields\":["
        << "\n" << indent << "    {\"offset\":0,\"size\":4,\"name\":\"localModelObjectSlotTable\"},"
        << "\n" << indent << "    {\"offset\":4,\"size\":4,\"name\":\"loadedMldResourceRecord\"},"
        << "\n" << indent << "    {\"offset\":8,\"size\":4,\"name\":\"localRuntimePointerTable\"},"
        << "\n" << indent << "    {\"offset\":12,\"size\":1,\"name\":\"localSlotCount\"},"
        << "\n" << indent << "    {\"offset\":16,\"size\":4,\"name\":\"secondaryModelEffectRuntimeBuffer\"}"
        << "\n" << indent << "  ]\n"
        << indent << "}";
}

void writeStringArray(std::ostream& out, const std::vector<std::string>& values, const std::string& indent) {
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        out << (i == 0 ? "\n" : ",\n") << indent << "  ";
        writeJsonString(out, values[i]);
    }
    if (!values.empty()) {
        out << "\n" << indent;
    }
    out << "]";
}

void writeFieldSummaries(std::ostream& out,
    const std::vector<CommandFieldSummary>& fields,
    const std::string& indent) {
    out << "[";
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const auto& field = fields[i];
        out << (i == 0 ? "\n" : ",\n") << indent << "  {\"offset\":" << field.offset
            << ",\"width\":";
        writeJsonString(out, fieldWidthName(field.width));
        out << ",\"kind\":";
        writeJsonString(out, fieldKindName(field.kind));
        out << ",\"name\":";
        writeJsonString(out, field.name);
        out << ",\"evidence\":";
        writeJsonString(out, fieldEvidenceName(field.evidence));
        out << ",\"scope\":";
        writeJsonString(out, fieldScopeName(field.scope));
        out << ",\"provisional\":" << (field.provisional ? "true" : "false");
        if (!field.description.empty()) {
            out << ",\"description\":";
            writeJsonString(out, field.description);
        }
        out << "}";
    }
    if (!fields.empty()) {
        out << "\n" << indent;
    }
    out << "]";
}

void writeType0Summary(std::ostream& out, const SstCommandRecord& command, const std::string& indent) {
    if (command.type != 0 || !command.payloadInBounds) {
        out << "null";
        return;
    }

    const auto payload = std::span<const std::uint8_t>(command.payloadBytes.data(), command.payloadBytes.size());
    out << "{\n"
        << indent << "  \"lookupResourceIndex\":";
    writeOptionalI16(out, readI16(payload, 0x16U, command.sourceEndian));
    out << ",\n" << indent << "  \"battleObjectClassSelector\":";
    writeOptionalI16(out, readI16(payload, 0x18U, command.sourceEndian));
    out << ",\n" << indent << "  \"transformPosition\":{\"x\":";
    writeOptionalF32(out, readF32(payload, 0x1CU, command.sourceEndian));
    out << ",\"y\":";
    writeOptionalF32(out, readF32(payload, 0x20U, command.sourceEndian));
    out << ",\"z\":";
    writeOptionalF32(out, readF32(payload, 0x24U, command.sourceEndian));
    out << "},\n" << indent << "  \"rotationRaw\":{\"x\":";
    writeOptionalU32(out, readU32(payload, 0x28U, command.sourceEndian));
    out << ",\"y\":";
    writeOptionalU32(out, readU32(payload, 0x2CU, command.sourceEndian));
    out << ",\"z\":";
    writeOptionalU32(out, readU32(payload, 0x30U, command.sourceEndian));
    out << "},\n" << indent << "  \"scale\":{\"x\":";
    writeOptionalF32(out, readF32(payload, 0x34U, command.sourceEndian));
    out << ",\"y\":";
    writeOptionalF32(out, readF32(payload, 0x38U, command.sourceEndian));
    out << ",\"z\":";
    writeOptionalF32(out, readF32(payload, 0x3CU, command.sourceEndian));
    out << "},\n" << indent << "  \"renderActionByte\":";
    writeOptionalU8(out, readU8(payload, 0x44U));
    out << ",\n" << indent << "  \"renderActionWordRaw\":";
    writeOptionalU32(out, readU32(payload, 0x44U, command.sourceEndian));
    out << "\n" << indent << "}";
}

std::optional<std::uint32_t> localSlotCountForRecord(const SmlParseResult& sml, std::size_t topLevelRecordIndex) {
    if (topLevelRecordIndex >= sml.records.size()) {
        return std::nullopt;
    }
    const auto& summary = sml.records[topLevelRecordIndex].embeddedMldSummary;
    if (!summary.has_value() || !summary->validLookingHeader || !summary->entryCount.has_value()) {
        return std::nullopt;
    }
    return summary->entryCount;
}

void writeEmbeddedMldSummary(std::ostream& out,
    const std::optional<SmlEmbeddedMldSummary>& summary,
    const std::string& indent) {
    if (!summary.has_value()) {
        out << "null";
        return;
    }

    out << "{\n"
        << indent << "  \"parseAttempted\":" << (summary->parseAttempted ? "true" : "false")
        << ",\n" << indent << "  \"validLookingHeader\":" << (summary->validLookingHeader ? "true" : "false")
        << ",\n" << indent << "  \"entryCount\":";
    writeOptionalU32(out, summary->entryCount);
    out << ",\n" << indent << "  \"indexTableOffset\":";
    writeOptionalU32(out, summary->indexTableOffset);
    out << ",\n" << indent << "  \"textureTableOffset\":";
    writeOptionalU32(out, summary->textureTableOffset);
    out << ",\n" << indent << "  \"textureArchiveCount\":";
    writeOptionalU32(out, summary->textureArchiveCount);
    out << ",\n" << indent << "  \"markers\":{\"NJCM\":" << (summary->hasNjcm ? "true" : "false")
        << ",\"NJTL\":" << (summary->hasNjtl ? "true" : "false")
        << ",\"NMDM\":" << (summary->hasNmdm ? "true" : "false")
        << ",\"GCIX\":" << (summary->hasGcix ? "true" : "false")
        << ",\"GVRT\":" << (summary->hasGvrt ? "true" : "false")
        << "}\n" << indent << "}";
}

void writeLocalObjectSlotLink(std::ostream& out,
    const SstCommandRecord& command,
    std::size_t topLevelRecordIndex,
    const SmlParseResult& sml,
    const std::string& indent) {
    if (!command.modelIndexCandidate || !command.modelIndex.has_value()) {
        out << "null";
        return;
    }

    const auto localSlotCount = localSlotCountForRecord(sml, topLevelRecordIndex);
    const bool rangeKnown = localSlotCount.has_value();
    const bool inRange = rangeKnown && *command.modelIndex >= 0 &&
        static_cast<std::uint32_t>(*command.modelIndex) < *localSlotCount;

    out << "{\n"
        << indent << "  \"owningSmlRecordIndex\":";
    if (topLevelRecordIndex < sml.records.size()) {
        out << topLevelRecordIndex;
    } else {
        out << "null";
    }
    out << ",\n" << indent << "  \"localSlotIndex\":" << *command.modelIndex
        << ",\n" << indent << "  \"localSlotCount\":";
    if (localSlotCount.has_value()) {
        out << *localSlotCount;
    } else {
        out << "null";
    }
    out << ",\n" << indent << "  \"slotIndexRangeKnown\":" << (rangeKnown ? "true" : "false")
        << ",\n" << indent << "  \"slotIndexInRange\":" << (inRange ? "true" : "false")
        << ",\n" << indent << "  \"interpretation\":";
    writeJsonString(out, "local object/model slot within the same top-level SML/SST record");
    out << "\n" << indent << "}";
}

const SstCommandBlock* findBlockForIndex(const SstParseResult& sst, std::size_t index) {
    const auto it = std::find_if(sst.commandBlocks.begin(), sst.commandBlocks.end(), [&](const SstCommandBlock& block) {
        return block.topLevelRecordIndex == index;
    });
    return it == sst.commandBlocks.end() ? nullptr : &*it;
}

void writeCommand(std::ostream& out,
    const SstCommandRecord& command,
    std::size_t topLevelRecordIndex,
    const SmlParseResult& sml,
    const std::string& indent) {
    out << "{\n"
        << indent << "  \"index\":" << command.index
        << ",\n" << indent << "  \"recordOffset\":" << command.recordOffset
        << ",\n" << indent << "  \"type\":" << command.type
        << ",\n" << indent << "  \"argument\":" << command.argument
        << ",\n" << indent << "  \"rawWord4\":\"" << hex32(command.rawWord4)
        << "\",\n" << indent << "  \"rawWord8\":\"" << hex32(command.rawWord8)
        << "\",\n" << indent << "  \"onDiskWord12\":\"" << hex32(command.onDiskWord12)
        << "\",\n" << indent << "  \"payloadOffset\":" << command.payloadOffset
        << ",\n" << indent << "  \"payloadSize\":" << command.payloadSize
        << ",\n" << indent << "  \"typeKnown\":" << (command.typeKnown ? "true" : "false")
        << ",\n" << indent << "  \"payloadInBounds\":" << (command.payloadInBounds ? "true" : "false")
        << ",\n" << indent << "  \"modelIndexCandidate\":" << (command.modelIndexCandidate ? "true" : "false")
        << ",\n" << indent << "  \"modelIndex\":";
    if (command.modelIndex.has_value()) {
        out << *command.modelIndex;
    } else {
        out << "null";
    }
    out << ",\n" << indent << "  \"localObjectSlotLink\":";
    writeLocalObjectSlotLink(out, command, topLevelRecordIndex, sml, indent + "  ");
    out << ",\n" << indent << "  \"type0Summary\":";
    writeType0Summary(out, command, indent + "  ");
    out << ",\n" << indent << "  \"fieldSummaries\":";
    writeFieldSummaries(out, command.fieldSummaries, indent + "  ");
    out << "\n" << indent << "}";
}

void writeCommandBlock(std::ostream& out,
    const SstCommandBlock* block,
    const SmlParseResult& sml,
    const std::string& indent) {
    if (block == nullptr) {
        out << "null";
        return;
    }

    out << "{\n"
        << indent << "  \"blockOffset\":" << block->blockOffset
        << ",\n" << indent << "  \"commandCount\":" << block->commandCount
        << ",\n" << indent << "  \"sentinelOffset\":" << block->sentinelOffset
        << ",\n" << indent << "  \"sentinelType\":" << block->sentinelType
        << ",\n" << indent << "  \"sentinelArgument\":" << block->sentinelArgument
        << ",\n" << indent << "  \"sentinelStatus\":";
    writeJsonString(out, block->sentinelType < 0 ? "ok" : "invalid");
    out << ",\n" << indent << "  \"valid\":" << (block->valid ? "true" : "false")
        << ",\n" << indent << "  \"payloadStartOffset\":" << block->payloadStartOffset
        << ",\n" << indent << "  \"payloadEndOffset\":" << block->payloadEndOffset
        << ",\n" << indent << "  \"nextCommandBlockOffset\":" << block->nextCommandBlockOffset
        << ",\n" << indent << "  \"postCommandTail\":";
    writePostCommandTail(out, *block, indent + "  ");
    out << ",\n" << indent << "  \"battleGridTerrainSource9x9\":";
    writeBattleGridTerrainSource9x9(out, block->battleGridTerrainSource, indent + "  ");
    out
        << ",\n" << indent << "  \"commands\":[";
    for (std::size_t i = 0; i < block->commands.size(); ++i) {
        out << (i == 0 ? "\n" : ",\n") << indent << "    ";
        writeCommand(out, block->commands[i], block->topLevelRecordIndex, sml, indent + "    ");
    }
    if (!block->commands.empty()) {
        out << "\n" << indent << "  ";
    }
    out << "]\n" << indent << "}";
}

void writeSmlRecord(std::ostream& out,
    const SmlRecord* record,
    const SmlEmbeddedMldExportedEntry* exported,
    const std::string& indent) {
    if (record == nullptr) {
        out << "null";
        return;
    }

    out << "{\n"
        << indent << "  \"index\":" << record->index
        << ",\n" << indent << "  \"recordOffset\":" << record->recordOffset
        << ",\n" << indent << "  \"rawWord0\":\"" << hex32(record->rawWord0)
        << "\",\n" << indent << "  \"embeddedMldOffset\":" << record->embeddedMldOffset
        << ",\n" << indent << "  \"embeddedMldSize\":" << record->embeddedMldSize
        << ",\n" << indent << "  \"rawWord12\":\"" << hex32(record->rawWord12)
        << "\",\n" << indent << "  \"embeddedMldInBounds\":" << (record->embeddedMldInBounds ? "true" : "false")
        << ",\n" << indent << "  \"embeddedMldPath\":";
    if (exported != nullptr) {
        writeJsonPathOrNull(out, exported->embeddedMldPath);
    } else {
        out << "null";
    }
    out << ",\n" << indent << "  \"blenderIrPath\":";
    if (exported != nullptr && exported->blenderIrPath.has_value()) {
        writeJsonPathOrNull(out, *exported->blenderIrPath);
    } else {
        out << "null";
    }
    out << ",\n" << indent << "  \"embeddedMldSummary\":";
    writeEmbeddedMldSummary(out, record->embeddedMldSummary, indent + "  ");
    out << "\n" << indent << "}";
}

void writeBlenderIrSummary(std::ostream& out, const SmlBlenderIrEntrySummary* summary, const std::string& indent) {
    if (summary == nullptr) {
        out << "null";
        return;
    }

    out << "{\n"
        << indent << "  \"meshCount\":" << summary->meshCount
        << ",\n" << indent << "  \"objectTreeCount\":" << summary->objectTreeCount
        << ",\n" << indent << "  \"indexEntryCount\":" << summary->indexEntryCount
        << ",\n" << indent << "  \"textureCount\":" << summary->textureCount
        << ",\n" << indent << "  \"animationCount\":" << summary->animationCount
        << ",\n" << indent << "  \"animationNodeCount\":" << summary->animationNodeCount
        << ",\n" << indent << "  \"animationPositionKeyCount\":" << summary->animationPositionKeyCount
        << ",\n" << indent << "  \"animationRotationKeyCount\":" << summary->animationRotationKeyCount
        << ",\n" << indent << "  \"animationScaleKeyCount\":" << summary->animationScaleKeyCount
        << ",\n" << indent << "  \"animationQuaternionKeyCount\":" << summary->animationQuaternionKeyCount
        << ",\n" << indent << "  \"varyingAnimationChannelCount\":" << summary->varyingAnimationChannelCount
        << ",\n" << indent << "  \"hasVaryingAnimation\":" << (summary->varyingAnimationChannelCount > 0U ? "true" : "false")
        << ",\n" << indent << "  \"indexEntryNames\":";
    writeStringArray(out, summary->indexEntryNames, indent + "  ");
    out << ",\n" << indent << "  \"varyingAnimationChannels\":";
    writeStringArray(out, summary->varyingAnimationChannels, indent + "  ");
    out << "\n" << indent << "}";
}

void writeCommandTypeArray(std::ostream& out, const SstCommandBlock* block) {
    out << "[";
    if (block != nullptr) {
        for (std::size_t i = 0; i < block->commands.size(); ++i) {
            out << (i == 0 ? "" : ",") << block->commands[i].type;
        }
    }
    out << "]";
}

void writeCommandTypeHistogram(std::ostream& out, const SstCommandBlock* block, const std::string& indent) {
    std::map<int, std::size_t> histogram{};
    if (block != nullptr) {
        for (const auto& command : block->commands) {
            ++histogram[command.type];
        }
    }

    out << "{";
    std::size_t index = 0U;
    for (const auto& [type, count] : histogram) {
        out << (index == 0U ? "\n" : ",\n") << indent << "  ";
        writeJsonString(out, std::to_string(type));
        out << ":" << count;
        ++index;
    }
    if (!histogram.empty()) {
        out << "\n" << indent;
    }
    out << "}";
}

void writeAnnotationCommand(std::ostream& out,
    const SstCommandRecord& command,
    std::size_t topLevelRecordIndex,
    const SmlParseResult& sml,
    const std::string& indent) {
    out << "{\n"
        << indent << "  \"index\":" << command.index
        << ",\n" << indent << "  \"type\":" << command.type
        << ",\n" << indent << "  \"argument\":" << command.argument
        << ",\n" << indent << "  \"payloadOffset\":" << command.payloadOffset
        << ",\n" << indent << "  \"payloadSize\":" << command.payloadSize
        << ",\n" << indent << "  \"modelIndex\":";
    if (command.modelIndex.has_value()) {
        out << *command.modelIndex;
    } else {
        out << "null";
    }
    out << ",\n" << indent << "  \"localObjectSlotLink\":";
    writeLocalObjectSlotLink(out, command, topLevelRecordIndex, sml, indent + "  ");
    out << ",\n" << indent << "  \"type0Summary\":";
    writeType0Summary(out, command, indent + "  ");
    out << ",\n" << indent << "  \"fieldSummaryNames\":[";
    for (std::size_t i = 0; i < command.fieldSummaries.size(); ++i) {
        out << (i == 0U ? "" : ",");
        writeJsonString(out, command.fieldSummaries[i].name);
    }
    out << "]\n" << indent << "}";
}

void writeHumanAnnotationTemplate(std::ostream& out, const std::string& indent) {
    out << "{\n"
        << indent << "  \"visualRole\":\"\",\n"
        << indent << "  \"description\":\"\",\n"
        << indent << "  \"visibleInGame\":null,\n"
        << indent << "  \"blenderNotes\":\"\",\n"
        << indent << "  \"overlayOrFallbackRole\":\"\",\n"
        << indent << "  \"animationNotes\":\"\",\n"
        << indent << "  \"suspectedRuntimeBehavior\":\"\",\n"
        << indent << "  \"suspectedCommandSemantics\":\"\",\n"
        << indent << "  \"confidence\":\"\",\n"
        << indent << "  \"reviewedBy\":\"\",\n"
        << indent << "  \"reviewedAt\":\"\",\n"
        << indent << "  \"media\":[]\n"
        << indent << "}";
}

void writeStageNotesTemplate(std::ostream& out, const std::string& indent) {
    out << "{\n"
        << indent << "  \"overview\":\"\",\n"
        << indent << "  \"layoutNotes\":\"\",\n"
        << indent << "  \"runtimeNotes\":\"\",\n"
        << indent << "  \"smlSstNotes\":\"\",\n"
        << indent << "  \"openQuestions\":\"\",\n"
        << indent << "  \"reviewedBy\":\"\",\n"
        << indent << "  \"reviewedAt\":\"\",\n"
        << indent << "  \"resources\":[]\n"
        << indent << "}";
}

void writeStageAnnotationTemplate(const std::filesystem::path& path,
    const std::filesystem::path& mediaDir,
    const SmlParseResult& sml,
    const SstParseResult* sst,
    const SmlSstCommandMapExportResult& result,
    const SmlEmbeddedMldExportOptions& options) {
    std::ofstream out(path, std::ios::binary);
    const std::size_t maxRecords = sst == nullptr
        ? sml.records.size()
        : std::max(sml.records.size(), sst->commandBlocks.size());

    out << "{\n"
        << "  \"schema\":\"spice_sst_sml_stage_annotation_v1\",\n"
        << "  \"documentRole\":\"living_stage_annotation\",\n"
        << "  \"stageStem\":";
    writeJsonString(out, options.stem.empty() ? "stage" : options.stem);
    out << ",\n  \"sourceSml\":";
    writeJsonString(out, sml.sourcePath);
    out << ",\n  \"sourceSst\":";
    if (sst != nullptr) {
        writeJsonString(out, sst->sourcePath);
    } else {
        out << "null";
    }
    out << ",\n  \"mediaDirectory\":";
    writeJsonString(out, mediaDir.filename().generic_string());
    out << ",\n  \"combinedBlenderIrScene\":";
    if (result.stageAnnotationCombinedBlenderIrPath.has_value()) {
        writeJsonString(out, result.stageAnnotationCombinedBlenderIrPath->filename().generic_string());
    } else {
        out << "null";
    }
    out << ",\n  \"instructions\":\"Fill stageNotes and per-record humanAnnotations from Blender/in-game observation; keep computed fields as the current parser-derived snapshot. Re-exports preserve this file unless overwrite is explicitly requested.\",\n"
        << "  \"stageNotes\":";
    writeStageNotesTemplate(out, "  ");
    out << ",\n  \"records\":[";
    for (std::size_t index = 0; index < maxRecords; ++index) {
        const SmlRecord* smlRecord = index < sml.records.size() ? &sml.records[index] : nullptr;
        const SmlEmbeddedMldExportedEntry* exported = index < result.entries.size() ? &result.entries[index] : nullptr;
        const SstCommandBlock* block = sst == nullptr ? nullptr : findBlockForIndex(*sst, index);
        const auto summaryIt = options.blenderIrSummariesByRecordIndex.find(index);
        const SmlBlenderIrEntrySummary* blenderSummary =
            summaryIt == options.blenderIrSummariesByRecordIndex.end() ? nullptr : &summaryIt->second;

        out << (index == 0U ? "\n" : ",\n")
            << "    {\n"
            << "      \"index\":" << index
            << ",\n      \"mediaDirectory\":\"" << mediaDir.filename().generic_string() << "\""
            << ",\n      \"humanAnnotations\":";
        writeHumanAnnotationTemplate(out, "      ");
        out << ",\n      \"computed\":{\n"
            << "        \"smlRecord\":";
        writeSmlRecord(out, smlRecord, exported, "        ");
        out << ",\n        \"blenderIrSummary\":";
        writeBlenderIrSummary(out, blenderSummary, "        ");
        out << ",\n        \"sstCommandSummary\":";
        if (block == nullptr) {
            out << "null";
        } else {
            out << "{\n"
                << "          \"blockOffset\":" << block->blockOffset
                << ",\n          \"commandCount\":" << block->commandCount
                << ",\n          \"valid\":" << (block->valid ? "true" : "false")
                << ",\n          \"sentinelStatus\":";
            writeJsonString(out, block->sentinelType < 0 ? "ok" : "invalid");
            out << ",\n          \"commandTypes\":";
            writeCommandTypeArray(out, block);
            out << ",\n          \"commandTypeHistogram\":";
            writeCommandTypeHistogram(out, block, "          ");
            out << ",\n          \"commands\":[";
            for (std::size_t commandIndex = 0; commandIndex < block->commands.size(); ++commandIndex) {
                out << (commandIndex == 0U ? "\n" : ",\n") << "            ";
                writeAnnotationCommand(out,
                    block->commands[commandIndex],
                    block->topLevelRecordIndex,
                    sml,
                    "            ");
            }
            if (!block->commands.empty()) {
                out << "\n          ";
            }
            out << "]\n        }";
        }
        out << "\n      }\n    }";
    }
    if (maxRecords > 0U) {
        out << "\n  ";
    }
    out << "]\n}\n";
}

void writeManifest(const std::filesystem::path& path,
    const SmlParseResult& sml,
    const SstParseResult* sst,
    const SmlSstCommandMapExportResult& result) {
    std::ofstream out(path, std::ios::binary);
    out << "{\n"
        << "  \"sourceSml\":";
    writeJsonString(out, sml.sourcePath);
    out << ",\n  \"sourceSst\":";
    if (sst != nullptr) {
        writeJsonString(out, sst->sourcePath);
    } else {
        out << "null";
    }
    out << ",\n  \"recordCount\":" << sml.recordCount
        << ",\n  \"entries\":[";
    for (std::size_t i = 0; i < result.entries.size(); ++i) {
        const auto& entry = result.entries[i];
        out << (i == 0 ? "\n" : ",\n")
            << "    {\"recordIndex\":" << entry.recordIndex
            << ",\"embeddedMldInBounds\":" << (entry.embeddedMldInBounds ? "true" : "false")
            << ",\"wroteEmbeddedMld\":" << (entry.wroteEmbeddedMld ? "true" : "false")
            << ",\"embeddedMldPath\":";
        writeJsonPathOrNull(out, entry.embeddedMldPath);
        out << ",\"blenderIrPath\":";
        if (entry.blenderIrPath.has_value()) {
            writeJsonPathOrNull(out, *entry.blenderIrPath);
        } else {
            out << "null";
        }
        out << ",\"diagnostics\":";
        writeStringArray(out, entry.diagnostics, "    ");
        out << "}";
    }
    if (!result.entries.empty()) {
        out << "\n  ";
    }
    out << "],\n  \"diagnostics\":";
    writeStringArray(out, result.diagnostics, "  ");
    out << "\n}\n";
}

void writeCommandMap(const std::filesystem::path& path,
    const SmlParseResult& sml,
    const SstParseResult& sst,
    const SmlSstCommandMapExportResult& result) {
    std::ofstream out(path, std::ios::binary);
    const std::size_t maxRecords = std::max(sml.records.size(), sst.commandBlocks.size());

    out << "{\n"
        << "  \"sourceSml\":";
    writeJsonString(out, sml.sourcePath);
    out << ",\n  \"sourceSst\":";
    writeJsonString(out, sst.sourcePath);
    out << ",\n  \"recordCounts\":{\"sml\":" << sml.recordCount
        << ",\"sst\":" << sst.recordCount
        << ",\"agree\":" << (sml.recordCount == sst.recordCount ? "true" : "false")
        << "},\n  \"smlDiagnostics\":";
    writeDiagnosticsArray(out, sml.diagnostics, "  ");
    out << ",\n  \"sstDiagnostics\":";
    writeDiagnosticsArray(out, sst.diagnostics, "  ");
    out << ",\n  \"activeRowRuntimeContext\":";
    writeActiveRowRuntimeContext(out, "  ");
    out << ",\n  \"records\":[";
    for (std::size_t index = 0; index < maxRecords; ++index) {
        const SmlRecord* smlRecord = index < sml.records.size() ? &sml.records[index] : nullptr;
        const SmlEmbeddedMldExportedEntry* exported = index < result.entries.size() ? &result.entries[index] : nullptr;
        const SstCommandBlock* block = findBlockForIndex(sst, index);

        out << (index == 0 ? "\n" : ",\n")
            << "    {\n"
            << "      \"index\":" << index
            << ",\n      \"smlRecord\":";
        writeSmlRecord(out, smlRecord, exported, "      ");
        out << ",\n      \"sstCommandBlock\":";
        writeCommandBlock(out, block, sml, "      ");
        out << "\n    }";
    }
    if (maxRecords > 0U) {
        out << "\n  ";
    }
    out << "]\n}\n";
}

SmlEmbeddedMldSummary legacyInspection(const SmlEmbeddedResourceInspection& source) {
    SmlEmbeddedMldSummary result{};
    result.parseAttempted = true;
    result.validLookingHeader = source.validLookingHeader;
    result.entryCount = source.entryCount;
    result.indexTableOffset = source.indexTableOffset;
    result.textureTableOffset = source.textureTableOffset;
    result.textureArchiveCount = source.textureArchiveCount;
    result.hasNjcm = source.hasNjcm;
    result.hasNjtl = source.hasNjtl;
    result.hasNmdm = source.hasNmdm;
    result.hasGcix = source.hasGcix;
    result.hasGvrt = source.hasGvrt;
    result.hasGbix = source.hasGbix;
    result.hasPvrt = source.hasPvrt;
    result.hasPvmh = source.hasPvmh;
    return result;
}

const SmlEmbeddedResourceInspection* findInspection(const SstSmlDocumentAnalysis& analysis,
    SmlEmbeddedResourceId id) {
    const auto found = std::find_if(analysis.embeddedResources.begin(), analysis.embeddedResources.end(),
        [&](const auto& item) { return item.resourceId == id; });
    return found == analysis.embeddedResources.end() ? nullptr : &*found;
}

SmlParseResult makeLegacySmlView(const SstSmlDocument& document,
    const SstSmlDocumentImportReceipt& receipt,
    const SstSmlDocumentAnalysis& analysis,
    const std::optional<spice::mld::MldWriteTarget> fallbackTarget,
    std::vector<std::string>& diagnostics) {
    SmlParseResult result{};
    result.sourcePath = receipt.sml.path.has_value() ? receipt.sml.path->string() : std::string{};
    result.sourceWasCompressedAklz = receipt.sml.wrapper == SstSmlSourceWrapper::Aklz;
    result.sourceEndian = receipt.sml.endian.value_or(spice::root::Endian::Big);
    result.decodedSize = static_cast<std::uint32_t>(receipt.sml.decodedSize.value_or(0U));
    result.recordCount = static_cast<std::uint32_t>(document.members.size());
    result.rawHeader0 = result.sourceEndian == spice::root::Endian::Big
        ? (static_cast<std::uint32_t>(document.stageId) << 16U) | document.stageHeaderSentinel
        : (static_cast<std::uint32_t>(document.stageHeaderSentinel) << 16U) | document.stageId;
    result.rawRecordCountWord = result.sourceEndian == spice::root::Endian::Big
        ? (static_cast<std::uint32_t>(result.recordCount) << 16U) | document.recordCountSentinel
        : (static_cast<std::uint32_t>(document.recordCountSentinel) << 16U) | result.recordCount;

    std::map<std::uint64_t, std::vector<std::uint8_t>> resourceBytes;
    for (const auto& member : document.members) {
        auto materialized = materializeEmbeddedResource(member.sml.resource, receipt, fallbackTarget);
        for (const auto& diagnostic : materialized.diagnostics) {
            diagnostics.push_back("SML resource " + std::to_string(member.sml.resource.id.value) +
                ": " + diagnostic.message);
        }
        if (materialized.ok()) resourceBytes.emplace(member.sml.resource.id.value, std::move(materialized.bytes));
    }

    std::vector<std::pair<std::uint64_t, std::uint32_t>> offsets;
    std::uint64_t cursor = 8U + static_cast<std::uint64_t>(document.members.size()) * 16U;
    for (const auto& item : document.smlBodyLayout) {
        if (const auto id = std::get_if<SmlEmbeddedResourceId>(&item)) {
            offsets.emplace_back(id->value, static_cast<std::uint32_t>(cursor));
            const auto member = std::find_if(document.members.begin(), document.members.end(),
                [&](const auto& candidate) { return candidate.sml.resource.id == *id; });
            if (member != document.members.end()) {
                const auto bytes = resourceBytes.find(id->value);
                if (bytes != resourceBytes.end()) cursor += bytes->second.size();
            }
        } else {
            cursor += std::get<SstSmlOpaqueBlock>(item).bytes.size();
        }
    }
    result.records.reserve(document.members.size());
    for (std::size_t index = 0U; index < document.members.size(); ++index) {
        const auto& source = document.members[index].sml;
        SmlRecord record{};
        record.index = index;
        record.recordOffset = static_cast<std::uint32_t>(8U + index * 16U);
        record.rawWord0 = source.resourceIndexWord;
        const auto offset = std::find_if(offsets.begin(), offsets.end(),
            [&](const auto& item) { return item.first == source.resource.id.value; });
        record.embeddedMldOffset = offset == offsets.end() ? 0U : offset->second;
        const auto bytes = resourceBytes.find(source.resource.id.value);
        record.embeddedMldSize = bytes == resourceBytes.end()
            ? 0U : static_cast<std::uint32_t>(bytes->second.size());
        record.rawWord12 = source.reservedWord;
        record.embeddedMldInBounds = bytes != resourceBytes.end();
        if (bytes != resourceBytes.end()) record.embeddedMldBytes = bytes->second;
        if (const auto inspection = findInspection(analysis, source.resource.id)) {
            record.embeddedMldSummary = legacyInspection(*inspection);
        }
        result.records.push_back(std::move(record));
    }
    return result;
}

std::uint64_t documentBlockSize(const SstStageCommandBlock& block) {
    std::uint64_t size = 4U + static_cast<std::uint64_t>(block.commands.size()) * 16U + 16U;
    for (const auto& command : block.commands) {
        if (command.payloadSpanKnown) size += SstParser::commandPayloadSize(command.type);
    }
    if (block.battleGrid) size += block.battleGrid->values.size();
    return size + (block.trailingOpaque ? block.trailingOpaque->bytes.size() : 0U);
}

SstParseResult makeLegacySstView(const SstSmlDocument& document,
    const SstSmlDocumentImportReceipt& receipt) {
    SstParseResult result{};
    result.sourcePath = receipt.sst.path.has_value() ? receipt.sst.path->string() : std::string{};
    result.sourceWasCompressedAklz = receipt.sst.wrapper == SstSmlSourceWrapper::Aklz;
    result.sourceEndian = receipt.sst.endian.value_or(spice::root::Endian::Big);
    result.decodedSize = static_cast<std::uint32_t>(receipt.sst.decodedSize.value_or(0U));
    result.recordCount = static_cast<std::uint16_t>(document.members.size());

    std::vector<std::pair<std::uint64_t, std::uint32_t>> offsets;
    std::uint64_t cursor = static_cast<std::uint64_t>(document.members.size()) * 16U;
    for (const auto& item : document.sstBodyLayout) {
        if (const auto id = std::get_if<SstCommandBlockId>(&item)) {
            offsets.emplace_back(id->value, static_cast<std::uint32_t>(cursor));
            const auto member = std::find_if(document.members.begin(), document.members.end(),
                [&](const auto& candidate) { return candidate.sst.commandBlock.id == *id; });
            if (member != document.members.end()) cursor += documentBlockSize(member->sst.commandBlock);
        } else {
            cursor += std::get<SstSmlOpaqueBlock>(item).bytes.size();
        }
    }

    for (std::size_t index = 0U; index < document.members.size(); ++index) {
        const auto& source = document.members[index].sst;
        const auto blockOffset = std::find_if(offsets.begin(), offsets.end(),
            [&](const auto& item) { return item.first == source.commandBlock.id.value; });
        SstTopLevelRecord record{};
        record.index = index;
        record.recordOffset = static_cast<std::uint32_t>(index * 16U);
        if (index == 0U) {
            record.rawWord0 = result.sourceEndian == spice::root::Endian::Big
                ? (static_cast<std::uint32_t>(document.stageId) << 16U) | document.stageHeaderSentinel
                : (static_cast<std::uint32_t>(document.stageHeaderSentinel) << 16U) | document.stageId;
            record.rawWord4 = result.sourceEndian == spice::root::Endian::Big
                ? (static_cast<std::uint32_t>(result.recordCount) << 16U) | document.recordCountSentinel
                : (static_cast<std::uint32_t>(document.recordCountSentinel) << 16U) | result.recordCount;
        } else {
            record.rawWord0 = source.previousCommandBlockLength.value_or(0U);
            record.rawWord4 = source.reservedWord.value_or(0U);
        }
        record.rawWord8 = source.recordIndexWord;
        record.commandBlockOffset = blockOffset == offsets.end() ? 0U : blockOffset->second;
        result.topLevelRecords.push_back(record);

        const auto& blockSource = source.commandBlock;
        SstCommandBlock block{};
        block.topLevelRecordIndex = index;
        block.blockOffset = record.commandBlockOffset;
        block.commandCount = static_cast<std::uint32_t>(blockSource.commands.size());
        block.recordsOffset = block.blockOffset + 4U;
        block.sentinelOffset = block.recordsOffset + block.commandCount * 16U;
        block.payloadStartOffset = block.sentinelOffset + 16U;
        block.sentinelType = blockSource.sentinelType;
        block.sentinelArgument = blockSource.sentinelArgument;
        std::uint32_t payloadCursor = block.payloadStartOffset;
        for (std::size_t commandIndex = 0U; commandIndex < blockSource.commands.size(); ++commandIndex) {
            const auto& commandSource = blockSource.commands[commandIndex];
            const auto payload = materializeCommandPayload(commandSource, result.sourceEndian);
            SstCommandRecord command{};
            command.index = commandIndex;
            command.sourceEndian = result.sourceEndian;
            command.recordOffset = block.recordsOffset + static_cast<std::uint32_t>(commandIndex * 16U);
            command.type = commandSource.type;
            command.argument = commandSource.argument;
            command.rawWord4 = commandSource.rawWord4;
            command.rawWord8 = commandSource.rawWord8;
            command.onDiskWord12 = commandSource.onDiskWord12;
            command.payloadOffset = payloadCursor;
            command.payloadSize = static_cast<std::uint32_t>(payload.bytes.size());
            command.typeKnown = commandSource.payloadSpanKnown;
            command.payloadInBounds = true;
            command.payloadBytes = payload.bytes;
            command.fieldSummaries = SstParser::fieldSummariesForType(command.type);
            command.modelIndexCandidate = SstParser::isModelIndexCommandType(command.type);
            const auto modelField = std::find_if(commandSource.fields.begin(), commandSource.fields.end(),
                [](const auto& field) { return field.name == "modelIndex"; });
            if (modelField != commandSource.fields.end()) {
                if (const auto value = std::get_if<std::int16_t>(&modelField->value)) command.modelIndex = *value;
            }
            for (std::size_t rowIndex = 0U; rowIndex < commandSource.lightingRows.size(); ++rowIndex) {
                const auto& rowSource = commandSource.lightingRows[rowIndex];
                SstType1LightingRow row{};
                row.index = rowIndex;
                row.rowOffset = payloadCursor + static_cast<std::uint32_t>(rowIndex * 0x68U);
                row.state = rowSource.state;
                row.sentinel = rowSource.sentinel;
                row.classSelector = rowSource.classSelector;
                row.flags = rowSource.flags;
                row.enablesLightSetup = (row.flags & 0x40000000U) != 0U;
                row.enablesVectorSetup = (row.flags & 0x20000000U) != 0U;
                row.runtimeSlotId = rowSource.runtimeSlotId;
                row.lightVector = { rowSource.lightVector[0], rowSource.lightVector[1], rowSource.lightVector[2] };
                row.slotRgb = { rowSource.slotRgb[0], rowSource.slotRgb[1], rowSource.slotRgb[2] };
                row.globalRgb = { rowSource.globalRgb[0], rowSource.globalRgb[1], rowSource.globalRgb[2] };
                row.attenuationOrSpot0 = rowSource.attenuationOrSpot0;
                row.attenuationOrSpot1 = rowSource.attenuationOrSpot1;
                row.rawTailWord = rowSource.rawTailWord;
                if (rowIndex * 0x68U + 0x68U <= payload.bytes.size()) {
                    row.rawBytes.assign(payload.bytes.begin() + static_cast<std::ptrdiff_t>(rowIndex * 0x68U),
                        payload.bytes.begin() + static_cast<std::ptrdiff_t>((rowIndex + 1U) * 0x68U));
                }
                command.type1LightingRows.push_back(std::move(row));
            }
            payloadCursor += command.payloadSize;
            block.commands.push_back(std::move(command));
        }
        block.payloadEndOffset = payloadCursor;
        block.postCommandTailOffset = payloadCursor;
        if (blockSource.battleGrid) {
            block.postCommandTailBytes.assign(blockSource.battleGrid->values.begin(), blockSource.battleGrid->values.end());
        }
        if (blockSource.trailingOpaque) {
            block.postCommandTailBytes.insert(block.postCommandTailBytes.end(),
                blockSource.trailingOpaque->bytes.begin(), blockSource.trailingOpaque->bytes.end());
        }
        block.postCommandTailSize = static_cast<std::uint32_t>(block.postCommandTailBytes.size());
        block.nextCommandBlockOffset = block.postCommandTailOffset + block.postCommandTailSize;
        block.postCommandTailInBounds = true;
        block.valid = block.sentinelType < 0;
        if (blockSource.battleGrid) {
            SstBattleGridTerrainSource grid{};
            grid.sourceOffset = block.postCommandTailOffset;
            grid.inBounds = true;
            grid.source9x9 = blockSource.battleGrid->values;
            if (blockSource.trailingOpaque) {
                grid.paddingAfterSource = blockSource.trailingOpaque->bytes;
            }
            block.battleGridTerrainSource = std::move(grid);
        }
        result.commandBlocks.push_back(std::move(block));
    }
    return result;
}

} // namespace

static SmlSstCommandMapExportResult exportLegacySmlEmbeddedMldsAndCommandMap(
    const SmlParseResult& sml,
    const SstParseResult* sst,
    const SmlEmbeddedMldExportOptions& options) {
    SmlSstCommandMapExportResult result{};
    const std::string stem = options.stem.empty() ? "stage" : options.stem;
    const auto stageOutputDir = options.stageOutputDir.empty() ? std::filesystem::path(stem) : options.stageOutputDir;
    const auto embeddedMldDir = stageOutputDir / "embedded_mld";

    std::filesystem::create_directories(stageOutputDir);
    if (options.writeEmbeddedMldPayloads) {
        std::filesystem::create_directories(embeddedMldDir);
    }

    result.entries.reserve(sml.records.size());
    for (const auto& record : sml.records) {
        SmlEmbeddedMldExportedEntry entry{};
        entry.recordIndex = record.index;
        entry.embeddedMldInBounds = record.embeddedMldInBounds;

        if (const auto it = options.blenderIrPathsByRecordIndex.find(record.index);
            it != options.blenderIrPathsByRecordIndex.end()) {
            entry.blenderIrPath = it->second;
        }

        if (!record.embeddedMldInBounds) {
            entry.diagnostics.push_back("SML embedded MLD span is out of bounds; no payload was written.");
        } else if (options.writeEmbeddedMldPayloads) {
            entry.embeddedMldPath = embeddedMldDir /
                (stem + "_sml_entry_" + std::to_string(record.index) + ".mld");
            std::ofstream out(entry.embeddedMldPath, std::ios::binary);
            if (!out) {
                entry.diagnostics.push_back("Failed to open embedded MLD output path.");
            } else {
                out.write(reinterpret_cast<const char*>(record.embeddedMldBytes.data()),
                    static_cast<std::streamsize>(record.embeddedMldBytes.size()));
                entry.wroteEmbeddedMld = out.good();
                if (!entry.wroteEmbeddedMld) {
                    entry.diagnostics.push_back("Failed while writing embedded MLD payload bytes.");
                }
            }
        }

        result.entries.push_back(std::move(entry));
    }

    if (sst == nullptr) {
        result.diagnostics.push_back("No same-stem SST was provided; SML payload extraction was still attempted.");
    } else if (sml.recordCount != sst->recordCount) {
        result.diagnostics.push_back("SML and SST top-level record counts differ; command map preserves parsable records.");
    }

    if (options.writeCommandMap && sst != nullptr) {
        result.commandMapPath = stageOutputDir / (stem + ".sst_sml_command_map.json");
        writeCommandMap(*result.commandMapPath, sml, *sst, result);
        result.wroteCommandMap = std::filesystem::exists(*result.commandMapPath);
    }

    if (options.writeStageAnnotationTemplate) {
        const auto stageAnnotationDir = options.stageAnnotationRepositoryDir.empty()
            ? stageOutputDir
            : options.stageAnnotationRepositoryDir / stem;
        std::filesystem::create_directories(stageAnnotationDir);
        result.stageAnnotationTemplatePath = stageAnnotationDir / (stem + ".stage_annotation.json");
        result.stageAnnotationMediaDir = stageAnnotationDir / (stem + ".stage_annotation_media");
        std::filesystem::create_directories(*result.stageAnnotationMediaDir);
        result.createdStageAnnotationMediaDir = std::filesystem::exists(*result.stageAnnotationMediaDir);

        if (options.combinedBlenderIrPath.has_value()) {
            if (std::filesystem::exists(*options.combinedBlenderIrPath)) {
                result.stageAnnotationCombinedBlenderIrPath =
                    stageAnnotationDir / options.combinedBlenderIrPath->filename();
                std::error_code ec{};
                std::filesystem::copy_file(
                    *options.combinedBlenderIrPath,
                    *result.stageAnnotationCombinedBlenderIrPath,
                    std::filesystem::copy_options::overwrite_existing,
                    ec);
                if (ec) {
                    result.diagnostics.push_back(
                        "Failed to copy combined Blender IR into the stage annotation folder: " + ec.message());
                } else {
                    result.copiedStageAnnotationCombinedBlenderIr =
                        std::filesystem::exists(*result.stageAnnotationCombinedBlenderIrPath);
                }
            } else {
                result.diagnostics.push_back(
                    "Combined Blender IR path was provided but did not exist; no annotation-folder copy was made.");
            }
        }

        if (std::filesystem::exists(*result.stageAnnotationTemplatePath) && !options.overwriteStageAnnotationTemplate) {
            result.diagnostics.push_back(
                "Existing stage annotation document was preserved; delete it or enable overwrite to regenerate computed fields.");
        } else {
            writeStageAnnotationTemplate(
                *result.stageAnnotationTemplatePath,
                *result.stageAnnotationMediaDir,
                sml,
                sst,
                result,
                options);
        }
        result.wroteStageAnnotationTemplate = std::filesystem::exists(*result.stageAnnotationTemplatePath);
    }

    result.manifestPath = stageOutputDir / (stem + ".sml_embedded_mld_manifest.json");
    writeManifest(result.manifestPath, sml, sst, result);
    result.wroteManifest = std::filesystem::exists(result.manifestPath);

    return result;
}

SmlSstCommandMapExportResult exportSmlEmbeddedMldsAndCommandMap(
    const SstSmlDocument& document,
    const SstSmlDocumentImportReceipt& receipt,
    const SstSmlDocumentAnalysis& analysis,
    const SmlEmbeddedMldExportOptions& options) {
    std::vector<std::string> materializationDiagnostics;
    auto sml = makeLegacySmlView(document, receipt, analysis,
        options.constructedMldFallbackTarget, materializationDiagnostics);
    auto sst = makeLegacySstView(document, receipt);
    auto result = exportLegacySmlEmbeddedMldsAndCommandMap(sml, &sst, options);
    result.diagnostics.insert(result.diagnostics.begin(),
        materializationDiagnostics.begin(), materializationDiagnostics.end());
    return result;
}

} // namespace spice::sstsml
