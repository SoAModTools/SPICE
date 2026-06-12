#include "SstParser.h"

#include "../Compression/Aklz.h"
#include "../SpiceCore/Binary/EndianReader.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <limits>

namespace spice::sstsml {
namespace {

using spice::core::Endian;
using spice::core::EndianReader;

constexpr std::uint32_t kSstTopLevelRecordStride = 0x10U;
constexpr std::uint32_t kSstCommandRecordStride = 0x10U;

void addDiagnostic(std::vector<ParseDiagnostic>& diagnostics,
    DiagnosticSeverity severity,
    std::string message,
    std::uint32_t offset = 0U) {
    diagnostics.push_back(ParseDiagnostic{ severity, std::move(message), offset });
}

bool canReadRange(std::size_t size, std::uint32_t offset, std::uint32_t length) {
    return offset <= size && length <= size - offset;
}

std::vector<std::uint8_t> copyRange(std::span<const std::uint8_t> bytes,
    std::uint32_t offset,
    std::uint32_t length) {
    if (!canReadRange(bytes.size(), offset, length)) {
        return {};
    }
    return std::vector<std::uint8_t>(bytes.begin() + offset, bytes.begin() + offset + length);
}

std::span<const std::uint8_t> decodeIfNeeded(std::span<const std::uint8_t> input,
    SstParseResult& result,
    std::vector<std::uint8_t>& decodedStorage) {
    if (!spice::compression::aklz::isAklz(input)) {
        return input;
    }

    result.sourceWasCompressedAklz = true;
    const auto decoded = spice::compression::aklz::decompress(input);
    if (!decoded.ok()) {
        addDiagnostic(result.diagnostics,
            DiagnosticSeverity::Error,
            std::string("AKLZ decompression failed: ") +
                std::string(spice::compression::aklz::errorToString(decoded.error)));
        return {};
    }

    decodedStorage = decoded.bytes;
    return decodedStorage;
}

bool isType(std::int16_t value, std::initializer_list<std::int16_t> candidates) {
    return std::find(candidates.begin(), candidates.end(), value) != candidates.end();
}

CommandFieldSummary field(std::uint32_t offset,
    CommandFieldWidth width,
    CommandFieldKind kind,
    std::string name,
    CommandFieldEvidence evidence = CommandFieldEvidence::Gekko,
    bool provisional = true,
    std::string description = {},
    CommandFieldScope scope = CommandFieldScope::StructuralPayload) {
    CommandFieldSummary summary{};
    summary.offset = offset;
    summary.width = width;
    summary.kind = kind;
    summary.name = std::move(name);
    summary.evidence = evidence;
    summary.scope = scope;
    summary.provisional = provisional;
    summary.description = std::move(description);
    return summary;
}

std::uint32_t nextCommandBlockOffsetAfter(const std::vector<SstTopLevelRecord>& records,
    std::uint32_t offset,
    std::uint32_t decodedSize) {
    std::uint32_t nextOffset = decodedSize;
    for (const auto& record : records) {
        if (record.commandBlockOffset > offset && record.commandBlockOffset < nextOffset) {
            nextOffset = record.commandBlockOffset;
        }
    }
    return nextOffset;
}

std::vector<CommandFieldSummary> type11TrailingFieldSummaries() {
    return {
        field(0x20U,
            CommandFieldWidth::I16,
            CommandFieldKind::Duration,
            "motionStepMagnitude",
            CommandFieldEvidence::GekkoAndCorpus,
            false,
            "Required nonzero by FUN_8000be28; copied to child-local +0x24.",
            CommandFieldScope::ConsumerTrailing),
        field(0x22U,
            CommandFieldWidth::I16,
            CommandFieldKind::Duration,
            "rampDuration",
            CommandFieldEvidence::GekkoAndCorpus,
            true,
            "Copied to child-local +0x26 and used to compute the related-object ramp step when ramp mode allows it.",
            CommandFieldScope::ConsumerTrailing),
        field(0x24U,
            CommandFieldWidth::I16,
            CommandFieldKind::HalfwordParameter,
            "trailingRampParameter",
            CommandFieldEvidence::GekkoAndCorpus,
            true,
            "Copied to child-local +0x28; direct downstream use remains unresolved.",
            CommandFieldScope::ConsumerTrailing),
    };
}

std::optional<CommandConsumerWindow> type11TrailingConsumerWindow(std::span<const std::uint8_t> bytes,
    std::uint32_t payloadOffset,
    std::uint32_t availableUntil) {
    constexpr std::uint32_t kType11WalkerSize = 0x18U;
    constexpr std::uint32_t kType11ConsumerEnd = 0x26U;
    const std::uint32_t windowOffset = payloadOffset + kType11WalkerSize;
    const std::uint32_t windowSize = kType11ConsumerEnd - kType11WalkerSize;

    CommandConsumerWindow window{};
    window.name = "type11TrailingConsumerWindow";
    window.offset = windowOffset;
    window.size = windowSize;
    window.fieldSummaries = type11TrailingFieldSummaries();
    window.description =
        "Direct FUN_8000be28 consumer reads outside the 0x18 structural walker span. "
        "Keep separate from the command payload size.";

    window.inBounds = availableUntil >= windowOffset &&
        windowSize <= availableUntil - windowOffset &&
        canReadRange(bytes.size(), windowOffset, windowSize);
    if (window.inBounds) {
        window.bytes = copyRange(bytes, windowOffset, windowSize);
    }

    return window;
}

} // namespace

bool SstParseResult::ok() const {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const ParseDiagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

std::uint32_t SstParser::commandPayloadSize(std::int16_t type) {
    switch (type) {
    case 0: return 0x4CU;
    case 1: return 0xD0U;
    case 2: return 0x44U;
    case 3: return 0x08U;
    case 4: return 0x18U;
    case 5: return 0x00U;
    case 6: return 0x10U;
    case 7: return 0x14U;
    case 8: return 0x14U;
    case 9: return 0x0CU;
    case 10: return 0x18U;
    case 11: return 0x18U;
    default: return 0U;
    }
}

bool SstParser::isKnownCommandType(std::int16_t type) {
    return type >= 0 && type <= 11 && type != 5;
}

bool SstParser::isModelIndexCommandType(std::int16_t type) {
    return isType(type, { 2, 3, 4, 6, 7, 8, 9, 10, 11 });
}

std::vector<CommandFieldSummary> SstParser::fieldSummariesForType(std::int16_t type) {
    switch (type) {
    case 0:
        return {
            field(0x16U,
                CommandFieldWidth::I16,
                CommandFieldKind::LookupKey,
                "lookupResourceIndex",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "Runtime consumers read copied type 0 row +0x16 as a signed lookup/resource index."),
            field(0x18U,
                CommandFieldWidth::I16,
                CommandFieldKind::HalfwordParameter,
                "battleObjectClassSelector",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "Runtime consumers read copied type 0 row +0x18 as a signed battle object class selector."),
            field(0x34U,
                CommandFieldWidth::F32,
                CommandFieldKind::FloatParameter,
                "scaleOrDefaultX",
                CommandFieldEvidence::CorpusStable,
                true,
                "Corpus-stable 1.0f candidate; downstream consumer still not traced."),
            field(0x38U,
                CommandFieldWidth::F32,
                CommandFieldKind::FloatParameter,
                "scaleOrDefaultY",
                CommandFieldEvidence::CorpusStable,
                true,
                "Corpus-stable 1.0f candidate; downstream consumer still not traced."),
            field(0x3CU,
                CommandFieldWidth::F32,
                CommandFieldKind::FloatParameter,
                "scaleOrDefaultZ",
                CommandFieldEvidence::CorpusStable,
                true,
                "Corpus-stable 1.0f candidate; downstream consumer still not traced."),
            field(0x44U,
                CommandFieldWidth::U32,
                CommandFieldKind::RawWord,
                "flagWordCandidate",
                CommandFieldEvidence::CorpusStable,
                true,
                "Corpus values are 0 or 0x02000000; exact runtime consumer remains provisional."),
        };
    case 1:
        return {
            field(0x00U, CommandFieldWidth::I8, CommandFieldKind::HalfwordParameter, "subrecord0Active"),
            field(0x02U, CommandFieldWidth::I16, CommandFieldKind::HalfwordParameter, "subrecord0HalfwordParameter"),
            field(0x08U, CommandFieldWidth::I16, CommandFieldKind::HalfwordParameter, "subrecord0HalfwordParameter"),
            field(0x64U, CommandFieldWidth::U32, CommandFieldKind::RawWord, "subrecord0RawWord"),
            field(0x68U, CommandFieldWidth::I8, CommandFieldKind::HalfwordParameter, "subrecord1Active"),
            field(0x6AU, CommandFieldWidth::I16, CommandFieldKind::HalfwordParameter, "subrecord1HalfwordParameter"),
            field(0x70U, CommandFieldWidth::I16, CommandFieldKind::HalfwordParameter, "subrecord1HalfwordParameter"),
            field(0xCCU, CommandFieldWidth::U32, CommandFieldKind::RawWord, "subrecord1RawWord"),
        };
    case 2:
        return {
            field(0x00U, CommandFieldWidth::I16, CommandFieldKind::ModelIndex, "modelIndex", CommandFieldEvidence::GekkoAndCorpus, false),
            field(0x02U, CommandFieldWidth::U16, CommandFieldKind::LookupKey, "nodeTraversalLookupKey", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x04U, CommandFieldWidth::F32, CommandFieldKind::VectorComponent, "centerVectorX", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x08U, CommandFieldWidth::F32, CommandFieldKind::VectorComponent, "centerVectorY", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x0CU, CommandFieldWidth::F32, CommandFieldKind::VectorComponent, "centerVectorZ", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x10U, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "sinePhaseFrequencyScalar", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x14U, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "sineDisplacementScale", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x18U, CommandFieldWidth::U32, CommandFieldKind::BufferPointer, "distanceWeightBufferPointerRuntime", CommandFieldEvidence::GekkoAndCorpus, false),
            field(0x20U, CommandFieldWidth::F32, CommandFieldKind::Duration, "maximumDistanceRange", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x24U, CommandFieldWidth::U32, CommandFieldKind::BufferPointer, "sourceCoordinateSnapshotPointerRuntime", CommandFieldEvidence::GekkoAndCorpus, false),
            field(0x28U, CommandFieldWidth::U32, CommandFieldKind::RawWord, "packedControlWord", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x30U, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "optionalYMinimum", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x34U, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "optionalYMaximum", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x38U, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "optionalXZRadialMinimum", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x3CU, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "optionalXZRadialMaximum", CommandFieldEvidence::GekkoAndCorpus, true),
        };
    case 3:
        return {
            field(0x00U, CommandFieldWidth::I16, CommandFieldKind::ModelIndex, "modelIndex"),
            field(0x02U, CommandFieldWidth::U16, CommandFieldKind::LookupKey, "lookupKey"),
            field(0x04U, CommandFieldWidth::I16, CommandFieldKind::HalfwordParameter, "halfwordParameter"),
            field(0x06U, CommandFieldWidth::I16, CommandFieldKind::HalfwordParameter, "halfwordParameter"),
        };
    case 4:
        return {
            field(0x00U, CommandFieldWidth::I16, CommandFieldKind::ModelIndex, "modelIndex"),
            field(0x04U, CommandFieldWidth::U32, CommandFieldKind::RawWord, "rawWord"),
            field(0x08U, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "floatParameter"),
            field(0x0CU, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "floatParameter"),
            field(0x10U, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "floatParameter"),
            field(0x14U, CommandFieldWidth::U32, CommandFieldKind::ReservedRaw, "reservedRaw"),
        };
    case 6:
        return {
            field(0x00U, CommandFieldWidth::I16, CommandFieldKind::ModelIndex, "modelIndex", CommandFieldEvidence::CodeSupportedCorpusAbsent, true),
            field(0x04U, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "runtimeFloatStepScalar", CommandFieldEvidence::CodeSupportedCorpusAbsent, true),
            field(0x08U, CommandFieldWidth::I16, CommandFieldKind::HalfwordParameter, "stepGateMode", CommandFieldEvidence::CodeSupportedCorpusAbsent, true),
        };
    case 7:
        return {
            field(0x00U, CommandFieldWidth::I16, CommandFieldKind::ModelIndex, "modelIndex", CommandFieldEvidence::CodeSupportedCorpusAbsent, true),
            field(0x04U, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "sineAmplitudeScalar", CommandFieldEvidence::CodeSupportedCorpusAbsent, true),
            field(0x08U, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "sinePhaseStep", CommandFieldEvidence::CodeSupportedCorpusAbsent, true),
        };
    case 8:
        return {
            field(0x00U, CommandFieldWidth::I16, CommandFieldKind::ModelIndex, "modelIndex"),
            field(0x02U, CommandFieldWidth::U16, CommandFieldKind::LookupKey, "lookupKey"),
            field(0x04U, CommandFieldWidth::I16, CommandFieldKind::HalfwordParameter, "halfwordParameter"),
            field(0x06U, CommandFieldWidth::I16, CommandFieldKind::HalfwordParameter, "halfwordParameter"),
            field(0x08U, CommandFieldWidth::I16, CommandFieldKind::HalfwordParameter, "halfwordParameter"),
            field(0x0AU, CommandFieldWidth::I16, CommandFieldKind::HalfwordParameter, "halfwordParameter"),
            field(0x0CU, CommandFieldWidth::I16, CommandFieldKind::HalfwordParameter, "halfwordParameter"),
        };
    case 9:
        return {
            field(0x00U, CommandFieldWidth::I16, CommandFieldKind::ModelIndex, "modelIndex"),
            field(0x08U, CommandFieldWidth::I16, CommandFieldKind::HalfwordParameter, "halfwordParameter"),
        };
    case 10:
        return {
            field(0x00U, CommandFieldWidth::I16, CommandFieldKind::ModelIndex, "modelIndex"),
            field(0x04U, CommandFieldWidth::U32, CommandFieldKind::RawWord, "rawWord"),
            field(0x08U, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "floatParameter"),
            field(0x0CU, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "floatParameter"),
            field(0x10U, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "floatParameter"),
            field(0x14U, CommandFieldWidth::I16, CommandFieldKind::HalfwordParameter, "halfwordParameter"),
        };
    case 11:
        return {
            field(0x00U, CommandFieldWidth::I16, CommandFieldKind::ModelIndex, "modelIndex", CommandFieldEvidence::GekkoAndCorpus, false),
            field(0x04U, CommandFieldWidth::I16, CommandFieldKind::HalfwordParameter, "fadeRampModeGate", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x06U, CommandFieldWidth::I16, CommandFieldKind::Counter, "repeatCycleLimit", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x08U, CommandFieldWidth::F32, CommandFieldKind::VectorComponent, "targetVectorX", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x0CU, CommandFieldWidth::F32, CommandFieldKind::VectorComponent, "targetVectorY", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x10U, CommandFieldWidth::F32, CommandFieldKind::VectorComponent, "targetVectorZ", CommandFieldEvidence::GekkoAndCorpus, true),
        };
    default:
        return {};
    }
}

SstParseResult SstParser::parse(std::span<const std::uint8_t> bytes, std::string sourcePath) {
    SstParseResult result{};
    result.sourcePath = std::move(sourcePath);

    std::vector<std::uint8_t> decodedStorage;
    const auto decodedBytes = decodeIfNeeded(bytes, result, decodedStorage);
    result.decodedSize = static_cast<std::uint32_t>(
        std::min<std::size_t>(decodedBytes.size(), std::numeric_limits<std::uint32_t>::max()));

    EndianReader reader(decodedBytes, Endian::Big);
    const auto recordCount = reader.try_read_u16(0x04U);
    if (!recordCount.has_value()) {
        addDiagnostic(result.diagnostics, DiagnosticSeverity::Error, "SST is too small for header/count");
        return result;
    }

    result.recordCount = *recordCount;
    const std::uint64_t topTableEnd =
        static_cast<std::uint64_t>(result.recordCount) * kSstTopLevelRecordStride;
    if (topTableEnd > decodedBytes.size()) {
        addDiagnostic(result.diagnostics,
            DiagnosticSeverity::Error,
            "SST top-level record table extends beyond decoded file");
        return result;
    }

    result.topLevelRecords.reserve(result.recordCount);
    for (std::uint32_t i = 0U; i < result.recordCount; ++i) {
        const std::uint32_t offset = i * kSstTopLevelRecordStride;
        SstTopLevelRecord record{};
        record.index = i;
        record.recordOffset = offset;
        record.rawWord0 = reader.read_u32(offset + 0x00U);
        record.rawWord4 = reader.read_u32(offset + 0x04U);
        record.rawWord8 = reader.read_u32(offset + 0x08U);
        record.commandBlockOffset = reader.read_u32(offset + 0x0CU);
        result.topLevelRecords.push_back(record);
    }

    result.commandBlocks.reserve(result.topLevelRecords.size());
    for (const auto& topRecord : result.topLevelRecords) {
        SstCommandBlock block{};
        block.topLevelRecordIndex = topRecord.index;
        block.blockOffset = topRecord.commandBlockOffset;
        block.recordsOffset = block.blockOffset + 0x04U;

        const auto commandCount = reader.try_read_u32(block.blockOffset);
        if (!commandCount.has_value()) {
            addDiagnostic(result.diagnostics,
                DiagnosticSeverity::Error,
                "SST command block offset is out of bounds",
                block.blockOffset);
            result.commandBlocks.push_back(std::move(block));
            continue;
        }

        block.commandCount = *commandCount;
        const std::uint64_t sentinelOffset64 =
            static_cast<std::uint64_t>(block.recordsOffset) +
            static_cast<std::uint64_t>(block.commandCount) * kSstCommandRecordStride;
        if (sentinelOffset64 > std::numeric_limits<std::uint32_t>::max()) {
            addDiagnostic(result.diagnostics,
                DiagnosticSeverity::Error,
                "SST command block sentinel offset overflows 32-bit range",
                block.blockOffset);
            result.commandBlocks.push_back(std::move(block));
            continue;
        }

        block.sentinelOffset = static_cast<std::uint32_t>(sentinelOffset64);
        block.payloadStartOffset = block.sentinelOffset + kSstCommandRecordStride;
        if (!canReadRange(decodedBytes.size(), block.sentinelOffset, kSstCommandRecordStride)) {
            addDiagnostic(result.diagnostics,
                DiagnosticSeverity::Error,
                "SST command block sentinel is out of bounds",
                block.sentinelOffset);
            result.commandBlocks.push_back(std::move(block));
            continue;
        }

        block.sentinelType = reader.read_i16(block.sentinelOffset + 0x00U);
        block.sentinelArgument = reader.read_i16(block.sentinelOffset + 0x02U);
        if (block.sentinelType >= 0) {
            addDiagnostic(result.diagnostics,
                DiagnosticSeverity::Error,
                "SST command block sentinel type is not negative",
                block.sentinelOffset);
        }

        block.commands.reserve(static_cast<std::size_t>(block.commandCount));
        std::uint32_t payloadCursor = block.payloadStartOffset;
        for (std::uint32_t commandIndex = 0U; commandIndex < block.commandCount; ++commandIndex) {
            const std::uint32_t recordOffset =
                block.recordsOffset + (commandIndex * kSstCommandRecordStride);
            SstCommandRecord command{};
            command.index = commandIndex;
            command.recordOffset = recordOffset;
            command.type = reader.read_i16(recordOffset + 0x00U);
            command.argument = reader.read_i16(recordOffset + 0x02U);
            command.rawWord4 = reader.read_u32(recordOffset + 0x04U);
            command.rawWord8 = reader.read_u32(recordOffset + 0x08U);
            command.onDiskWord12 = reader.read_u32(recordOffset + 0x0CU);
            command.typeKnown = isKnownCommandType(command.type);
            command.payloadOffset = payloadCursor;
            command.payloadSize = commandPayloadSize(command.type);
            command.payloadInBounds =
                canReadRange(decodedBytes.size(), command.payloadOffset, command.payloadSize);
            command.fieldSummaries = fieldSummariesForType(command.type);
            command.modelIndexCandidate = isModelIndexCommandType(command.type);

            if (!command.typeKnown) {
                addDiagnostic(result.diagnostics,
                    DiagnosticSeverity::Warning,
                    "SST command type is not in the Gekko-backed payload table",
                    recordOffset);
            }
            if (command.payloadInBounds) {
                command.payloadBytes =
                    copyRange(decodedBytes, command.payloadOffset, command.payloadSize);
                if (command.modelIndexCandidate && command.payloadBytes.size() >= 2U) {
                    EndianReader payloadReader(command.payloadBytes, Endian::Big);
                    command.modelIndex = payloadReader.read_i16(0x00U);
                }
            } else {
                addDiagnostic(result.diagnostics,
                    DiagnosticSeverity::Error,
                    "SST command payload span is out of bounds",
                    command.payloadOffset);
            }

            if (command.type == 11) {
                const std::uint32_t availableUntil =
                    nextCommandBlockOffsetAfter(result.topLevelRecords,
                        command.payloadOffset,
                        result.decodedSize);
                if (auto window = type11TrailingConsumerWindow(decodedBytes,
                        command.payloadOffset,
                        availableUntil);
                    window.has_value()) {
                    command.consumerWindows.push_back(std::move(*window));
                    if (!command.consumerWindows.back().inBounds) {
                        addDiagnostic(result.diagnostics,
                            DiagnosticSeverity::Warning,
                            "SST type 11 trailing consumer window is not fully available before the next command block",
                            command.payloadOffset + command.payloadSize);
                    }
                }
                addDiagnostic(result.diagnostics,
                    DiagnosticSeverity::Info,
                    "SST type 11 uses a 0x18 walker span; trailing consumer fields are exposed separately when available",
                    command.payloadOffset);
            }

            payloadCursor += command.payloadSize;
            block.commands.push_back(std::move(command));
        }

        block.payloadEndOffset = payloadCursor;
        block.valid = block.sentinelType < 0;
        result.commandBlocks.push_back(std::move(block));
    }

    return result;
}

} // namespace spice::sstsml
