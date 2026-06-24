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
constexpr std::uint32_t kType1LightingRowStride = 0x68U;
constexpr std::uint32_t kType1LightingRowCount = 2U;
constexpr std::uint32_t kType1EnableLightSetupFlag = 0x40000000U;
constexpr std::uint32_t kType1EnableVectorSetupFlag = 0x20000000U;

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
            CommandFieldKind::Duration,
            "cycleHoldFrames",
            CommandFieldEvidence::GekkoAndCorpus,
            true,
            "Copied to child-local +0x28; child type 12 uses it as a hold/wait duration before restarting or transitioning cycles.",
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
        "Keep separate from the command payload size; child-local +0x04 is sourced from "
        "active row +0x10, the secondary runtime model/effect buffer pointer.";

    window.inBounds = availableUntil >= windowOffset &&
        windowSize <= availableUntil - windowOffset &&
        canReadRange(bytes.size(), windowOffset, windowSize);
    if (window.inBounds) {
        window.bytes = copyRange(bytes, windowOffset, windowSize);
    }

    return window;
}

Float3 readFloat3(const EndianReader& reader, std::uint32_t offset) {
    return Float3{
        reader.read_f32(offset + 0x00U),
        reader.read_f32(offset + 0x04U),
        reader.read_f32(offset + 0x08U),
    };
}

std::vector<SstType1LightingRow> parseType1LightingRows(std::span<const std::uint8_t> decodedBytes,
    std::uint32_t payloadOffset,
    std::uint32_t payloadSize) {
    std::vector<SstType1LightingRow> rows{};
    if (!canReadRange(decodedBytes.size(), payloadOffset, payloadSize)) {
        return rows;
    }

    const EndianReader reader(decodedBytes, Endian::Big);
    const std::uint32_t maxRows = std::min<std::uint32_t>(kType1LightingRowCount,
        payloadSize / kType1LightingRowStride);
    rows.reserve(maxRows);
    for (std::uint32_t rowIndex = 0U; rowIndex < maxRows; ++rowIndex) {
        const std::uint32_t rowOffset = payloadOffset + (rowIndex * kType1LightingRowStride);
        SstType1LightingRow row{};
        row.index = rowIndex;
        row.rowOffset = rowOffset;
        row.state = reader.read_i8(rowOffset + 0x00U);
        row.sentinel = row.state < 0;
        row.classSelector = reader.read_i16(rowOffset + 0x02U);
        row.flags = reader.read_u32(rowOffset + 0x04U);
        row.enablesLightSetup = (row.flags & kType1EnableLightSetupFlag) != 0U;
        row.enablesVectorSetup = (row.flags & kType1EnableVectorSetupFlag) != 0U;
        row.runtimeSlotId = reader.read_i16(rowOffset + 0x08U);
        row.lightVector = readFloat3(reader, rowOffset + 0x0CU);
        row.slotRgb = readFloat3(reader, rowOffset + 0x30U);
        row.globalRgb = readFloat3(reader, rowOffset + 0x3CU);
        row.attenuationOrSpot0 = reader.read_f32(rowOffset + 0x48U);
        row.attenuationOrSpot1 = reader.read_f32(rowOffset + 0x4CU);
        row.rawTailWord = reader.read_u32(rowOffset + 0x64U);
        row.rawBytes = copyRange(decodedBytes, rowOffset, kType1LightingRowStride);
        rows.push_back(std::move(row));
        if (rows.back().sentinel) {
            break;
        }
    }

    return rows;
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
            field(0x1CU,
                CommandFieldWidth::F32,
                CommandFieldKind::VectorComponent,
                "transformPositionX",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "Selector callback 3 loads copied type 0 row +0x1c as f32 and passes it to FUN_802924e0."),
            field(0x20U,
                CommandFieldWidth::F32,
                CommandFieldKind::VectorComponent,
                "transformPositionY",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "Selector callback 3 loads copied type 0 row +0x20 as f32 and passes it to FUN_802924e0; runtime helper FUN_8007e264 later resets this copied local slot."),
            field(0x24U,
                CommandFieldWidth::F32,
                CommandFieldKind::VectorComponent,
                "transformPositionZ",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "Selector callback 3 loads copied type 0 row +0x24 as f32 and passes it to FUN_802924e0."),
            field(0x28U,
                CommandFieldWidth::U32,
                CommandFieldKind::RotationComponent,
                "rotationAngleX",
                CommandFieldEvidence::Gekko,
                true,
                "Selector callback 3 reads copied type 0 row +0x28 as a signed 32-bit angle component, scales it, and passes it to FUN_80292080."),
            field(0x2CU,
                CommandFieldWidth::U32,
                CommandFieldKind::RotationComponent,
                "rotationAngleY",
                CommandFieldEvidence::Gekko,
                true,
                "Selector callback 3 reads copied type 0 row +0x2c as a signed 32-bit angle component, scales it, and passes it to FUN_80292080."),
            field(0x30U,
                CommandFieldWidth::U32,
                CommandFieldKind::RotationComponent,
                "rotationAngleZ",
                CommandFieldEvidence::Gekko,
                true,
                "Selector callback 3 reads copied type 0 row +0x30 as a signed 32-bit angle component, scales it, and passes it to FUN_80292080."),
            field(0x34U,
                CommandFieldWidth::F32,
                CommandFieldKind::FloatParameter,
                "scaleX",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "Selector callback 3 loads copied type 0 row +0x34 as f32 scale X and passes it to FUN_802923e0; corpus value is stable 1.0f."),
            field(0x38U,
                CommandFieldWidth::F32,
                CommandFieldKind::FloatParameter,
                "scaleY",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "Selector callback 3 loads copied type 0 row +0x38 as f32 scale Y and passes it to FUN_802923e0; corpus value is stable 1.0f."),
            field(0x3CU,
                CommandFieldWidth::F32,
                CommandFieldKind::FloatParameter,
                "scaleZ",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "Selector callback 3 loads copied type 0 row +0x3c as f32 scale Z and passes it to FUN_802923e0; corpus value is stable 1.0f."),
            field(0x44U,
                CommandFieldWidth::U8,
                CommandFieldKind::HalfwordParameter,
                "renderActionByte",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "Selector callback 3 reads byte +0x44 and passes it to FUN_80035140 or FUN_80035380. Big-endian word values 0x00000000/0x02000000 correspond to byte values 0/2; trailing bytes +0x45..+0x47 remain raw."),
        };
    case 1:
        return {
            field(0x00U,
                CommandFieldWidth::I8,
                CommandFieldKind::HalfwordParameter,
                "rowState",
                CommandFieldEvidence::GekkoAndCorpus,
                false,
                "Active observed rows use state 4; a negative value is the row-walker sentinel."),
            field(0x02U,
                CommandFieldWidth::I16,
                CommandFieldKind::HalfwordParameter,
                "classSelector",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "Passed to FUN_8006bdb4 as the child/menu class selector; corpus value is currently always 2."),
            field(0x04U,
                CommandFieldWidth::U32,
                CommandFieldKind::RawWord,
                "lightingFlags",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "0x40000000 gates render-light setup; 0x20000000 gates vector setup."),
            field(0x08U,
                CommandFieldWidth::I16,
                CommandFieldKind::RuntimeSlot,
                "runtimeSlotId",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "Runtime slot id stored in the DAT_80309e88 four-slot table; corpus value is currently always 0."),
            field(0x0CU,
                CommandFieldWidth::F32,
                CommandFieldKind::VectorComponent,
                "lightVectorX",
                CommandFieldEvidence::GekkoAndCorpus,
                true),
            field(0x10U,
                CommandFieldWidth::F32,
                CommandFieldKind::VectorComponent,
                "lightVectorY",
                CommandFieldEvidence::GekkoAndCorpus,
                true),
            field(0x14U,
                CommandFieldWidth::F32,
                CommandFieldKind::VectorComponent,
                "lightVectorZ",
                CommandFieldEvidence::GekkoAndCorpus,
                true),
            field(0x30U,
                CommandFieldWidth::F32,
                CommandFieldKind::FloatParameter,
                "slotRgbR",
                CommandFieldEvidence::GekkoAndCorpus,
                true),
            field(0x34U,
                CommandFieldWidth::F32,
                CommandFieldKind::FloatParameter,
                "slotRgbG",
                CommandFieldEvidence::GekkoAndCorpus,
                true),
            field(0x38U,
                CommandFieldWidth::F32,
                CommandFieldKind::FloatParameter,
                "slotRgbB",
                CommandFieldEvidence::GekkoAndCorpus,
                true),
            field(0x3CU,
                CommandFieldWidth::F32,
                CommandFieldKind::FloatParameter,
                "globalRgbR",
                CommandFieldEvidence::GekkoAndCorpus,
                true),
            field(0x40U,
                CommandFieldWidth::F32,
                CommandFieldKind::FloatParameter,
                "globalRgbG",
                CommandFieldEvidence::GekkoAndCorpus,
                true),
            field(0x44U,
                CommandFieldWidth::F32,
                CommandFieldKind::FloatParameter,
                "globalRgbB",
                CommandFieldEvidence::GekkoAndCorpus,
                true),
            field(0x48U,
                CommandFieldWidth::F32,
                CommandFieldKind::FloatParameter,
                "attenuationOrSpot0",
                CommandFieldEvidence::GekkoAndCorpus,
                true),
            field(0x4CU,
                CommandFieldWidth::F32,
                CommandFieldKind::FloatParameter,
                "attenuationOrSpot1",
                CommandFieldEvidence::GekkoAndCorpus,
                true),
            field(0x64U,
                CommandFieldWidth::U32,
                CommandFieldKind::RawWord,
                "rowTailWord",
                CommandFieldEvidence::GekkoAndCorpus,
                true),
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
            field(0x1CU, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "minimumDistanceRange", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x20U, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "maximumDistanceRange", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x24U, CommandFieldWidth::U32, CommandFieldKind::BufferPointer, "sourceCoordinateSnapshotPointerRuntime", CommandFieldEvidence::GekkoAndCorpus, false),
            field(0x28U, CommandFieldWidth::U32, CommandFieldKind::RawWord, "packedControlWord", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x30U, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "falloffShapeScalar", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x34U, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "optionalYMinimum", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x38U, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "optionalYMaximum", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x3CU, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "optionalXZRadialMinimum", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x40U, CommandFieldWidth::F32, CommandFieldKind::FloatParameter, "optionalXZRadialMaximum", CommandFieldEvidence::GekkoAndCorpus, true),
        };
    case 3:
        return {
            field(0x00U, CommandFieldWidth::I16, CommandFieldKind::ModelIndex, "modelIndex", CommandFieldEvidence::GekkoAndCorpus, false),
            field(0x02U, CommandFieldWidth::U16, CommandFieldKind::LookupKey, "nodeTraversalLookupKey", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x04U, CommandFieldWidth::I16, CommandFieldKind::HalfwordParameter, "textureCoordinateDeltaU", CommandFieldEvidence::GekkoAndCorpus, true),
            field(0x06U, CommandFieldWidth::I16, CommandFieldKind::HalfwordParameter, "textureCoordinateDeltaV", CommandFieldEvidence::GekkoAndCorpus, true),
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
            field(0x00U,
                CommandFieldWidth::I16,
                CommandFieldKind::ModelIndex,
                "modelIndex",
                CommandFieldEvidence::GekkoAndCorpus,
                false,
                "Local model/object slot index in the active same-index SML/SST record."),
            field(0x02U,
                CommandFieldWidth::U16,
                CommandFieldKind::LookupKey,
                "nodeTraversalLookupKey",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "Passed to FUN_8006c9ac to select a node/model-data owner before child type 9 updates UV bound halfwords."),
            field(0x04U,
                CommandFieldWidth::I16,
                CommandFieldKind::HalfwordParameter,
                "textureTileWidth",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "Copied to child-local +0x00 and used to compute the animated UV page cell width."),
            field(0x06U,
                CommandFieldWidth::I16,
                CommandFieldKind::HalfwordParameter,
                "textureTileHeight",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "Copied to child-local +0x02 and used to compute the animated UV page cell height."),
            field(0x08U,
                CommandFieldWidth::I16,
                CommandFieldKind::HalfwordParameter,
                "texturePageSize",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "Copied to child-local +0x04 and used as the texture page dimension for UV bound scaling."),
            field(0x0AU,
                CommandFieldWidth::I16,
                CommandFieldKind::Counter,
                "textureAnimationFrameCount",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "Copied to child-local +0x06; child type 9 wraps the current frame counter against this value."),
            field(0x0CU,
                CommandFieldWidth::I16,
                CommandFieldKind::Duration,
                "frameHoldDuration",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "Copied to child-local +0x08 and used as the per-frame hold/delay duration."),
        };
    case 9:
        return {
            field(0x00U,
                CommandFieldWidth::I16,
                CommandFieldKind::ModelIndex,
                "modelIndex",
                CommandFieldEvidence::GekkoAndCorpus,
                false,
                "Local model/object slot index in the active same-index SML/SST record."),
            field(0x02U,
                CommandFieldWidth::I16,
                CommandFieldKind::ReservedRaw,
                "corpusOnlyRaw02",
                CommandFieldEvidence::Provisional,
                true,
                "Corpus-populated in some rows but not directly read by the traced setup or child type 10 callback path."),
            field(0x04U,
                CommandFieldWidth::U32,
                CommandFieldKind::ReservedRaw,
                "corpusOnlyRaw04",
                CommandFieldEvidence::Provisional,
                true,
                "Corpus-stable zero in current rows; no direct read found in the traced setup or child type 10 callback path."),
            field(0x08U,
                CommandFieldWidth::I16,
                CommandFieldKind::AxisSelector,
                "viewOrientationMode",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "Copied to child-local +0x04; child type 10 combines it with the published battle-view orientation and writes the attached object orientation at +0x2c."),
            field(0x0AU,
                CommandFieldWidth::I16,
                CommandFieldKind::ReservedRaw,
                "corpusOnlyRaw0A",
                CommandFieldEvidence::Provisional,
                true,
                "Sparse corpus values exist, but no direct read was found in the traced setup or child type 10 callback path."),
        };
    case 10:
        return {
            field(0x00U,
                CommandFieldWidth::I16,
                CommandFieldKind::ModelIndex,
                "modelIndex",
                CommandFieldEvidence::GekkoAndCorpus,
                false,
                "Local model/object slot index in the active same-index SML/SST record."),
            field(0x04U,
                CommandFieldWidth::U32,
                CommandFieldKind::RawWord,
                "vectorEndpointMode",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "Copied to child-local +0x08 and used by FUN_8000d488 to choose clamp/endpoint/oscillation behavior."),
            field(0x08U,
                CommandFieldWidth::F32,
                CommandFieldKind::VectorComponent,
                "targetVectorX",
                CommandFieldEvidence::GekkoAndCorpus,
                true),
            field(0x0CU,
                CommandFieldWidth::F32,
                CommandFieldKind::VectorComponent,
                "targetVectorY",
                CommandFieldEvidence::GekkoAndCorpus,
                true),
            field(0x10U,
                CommandFieldWidth::F32,
                CommandFieldKind::VectorComponent,
                "targetVectorZ",
                CommandFieldEvidence::GekkoAndCorpus,
                true),
            field(0x14U,
                CommandFieldWidth::I16,
                CommandFieldKind::Duration,
                "durationFrames",
                CommandFieldEvidence::GekkoAndCorpus,
                true,
                "Required nonzero; setup uses it to compute child-local vector deltas for child type 11."),
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
                if (command.type == 1) {
                    command.type1LightingRows = parseType1LightingRows(decodedBytes,
                        command.payloadOffset,
                        command.payloadSize);
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
