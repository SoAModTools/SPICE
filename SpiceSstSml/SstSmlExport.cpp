#include "SstSmlExport.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <span>
#include <string_view>

namespace spice::sstsml {
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

std::optional<std::int16_t> readI16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (!canRead(bytes, offset, 2U)) {
        return std::nullopt;
    }
    const auto raw = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
        static_cast<std::uint16_t>(bytes[offset + 1U]));
    return static_cast<std::int16_t>(raw);
}

std::optional<std::uint32_t> readU32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (!canRead(bytes, offset, 4U)) {
        return std::nullopt;
    }
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
        static_cast<std::uint32_t>(bytes[offset + 3U]);
}

std::optional<float> readF32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    const auto raw = readU32(bytes, offset);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return std::bit_cast<float>(*raw);
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
    writeOptionalI16(out, readI16(payload, 0x16U));
    out << ",\n" << indent << "  \"battleObjectClassSelector\":";
    writeOptionalI16(out, readI16(payload, 0x18U));
    out << ",\n" << indent << "  \"transformPosition\":{\"x\":";
    writeOptionalF32(out, readF32(payload, 0x1CU));
    out << ",\"y\":";
    writeOptionalF32(out, readF32(payload, 0x20U));
    out << ",\"z\":";
    writeOptionalF32(out, readF32(payload, 0x24U));
    out << "},\n" << indent << "  \"rotationRaw\":{\"x\":";
    writeOptionalU32(out, readU32(payload, 0x28U));
    out << ",\"y\":";
    writeOptionalU32(out, readU32(payload, 0x2CU));
    out << ",\"z\":";
    writeOptionalU32(out, readU32(payload, 0x30U));
    out << "},\n" << indent << "  \"scale\":{\"x\":";
    writeOptionalF32(out, readF32(payload, 0x34U));
    out << ",\"y\":";
    writeOptionalF32(out, readF32(payload, 0x38U));
    out << ",\"z\":";
    writeOptionalF32(out, readF32(payload, 0x3CU));
    out << "},\n" << indent << "  \"renderActionByte\":";
    writeOptionalU8(out, readU8(payload, 0x44U));
    out << ",\n" << indent << "  \"renderActionWordRaw\":";
    writeOptionalU32(out, readU32(payload, 0x44U));
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
    std::size_t smlRecordCount,
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
    out << ",\n" << indent << "  \"resolvedSmlRecordIndex\":";
    if (command.modelIndex.has_value() && *command.modelIndex >= 0 &&
        static_cast<std::size_t>(*command.modelIndex) < smlRecordCount) {
        out << *command.modelIndex;
    } else {
        out << "null";
    }
    out << ",\n" << indent << "  \"type0Summary\":";
    writeType0Summary(out, command, indent + "  ");
    out << ",\n" << indent << "  \"fieldSummaries\":";
    writeFieldSummaries(out, command.fieldSummaries, indent + "  ");
    out << "\n" << indent << "}";
}

void writeCommandBlock(std::ostream& out,
    const SstCommandBlock* block,
    std::size_t smlRecordCount,
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
        << ",\n" << indent << "  \"commands\":[";
    for (std::size_t i = 0; i < block->commands.size(); ++i) {
        out << (i == 0 ? "\n" : ",\n") << indent << "    ";
        writeCommand(out, block->commands[i], smlRecordCount, indent + "    ");
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
    out << "\n" << indent << "}";
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
        writeCommandBlock(out, block, sml.records.size(), "      ");
        out << "\n    }";
    }
    if (maxRecords > 0U) {
        out << "\n  ";
    }
    out << "]\n}\n";
}

} // namespace

SmlSstCommandMapExportResult exportSmlEmbeddedMldsAndCommandMap(
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

    result.manifestPath = stageOutputDir / (stem + ".sml_embedded_mld_manifest.json");
    writeManifest(result.manifestPath, sml, sst, result);
    result.wroteManifest = std::filesystem::exists(result.manifestPath);

    if (options.writeCommandMap && sst != nullptr) {
        result.commandMapPath = stageOutputDir / (stem + ".sst_sml_command_map.json");
        writeCommandMap(*result.commandMapPath, sml, *sst, result);
        result.wroteCommandMap = std::filesystem::exists(*result.commandMapPath);
    }

    return result;
}

} // namespace spice::sstsml
