#include "StdDocumentImporter.h"

#include "StdSha256.h"
#include "../Compression/Aklz.h"
#include "../SpiceRoot/Binary/EndianReader.h"

#include <algorithm>
#include <array>
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

template <std::size_t Size>
std::array<std::uint8_t, Size> readBytes(const EndianReader& reader, const std::size_t offset) {
    std::array<std::uint8_t, Size> result{};
    std::copy_n(reader.bytes().begin() + static_cast<std::ptrdiff_t>(offset), Size, result.begin());
    return result;
}

template <std::size_t Size>
std::array<StdFloat32, Size> readFloatArray(const EndianReader& reader, const std::size_t offset) {
    std::array<StdFloat32, Size> result{};
    for (std::size_t index = 0U; index < Size; ++index) {
        result[index].bits = reader.read_u32(offset + index * 4U);
    }
    return result;
}

std::array<StdSparcChoice, 64U> readSparcChoices(
    const EndianReader& reader, const std::size_t offset) {
    std::array<StdSparcChoice, 64U> result{};
    for (std::size_t index = 0U; index < result.size(); ++index) {
        result[index] = StdSparcChoice{
            reader.read_i16(offset + index * 4U),
            reader.read_i16(offset + index * 4U + 2U),
        };
    }
    return result;
}

std::array<StdModelTimelineEntry, 64U> readModelTimeline(
    const EndianReader& reader, const std::size_t offset) {
    std::array<StdModelTimelineEntry, 64U> result{};
    for (std::size_t index = 0U; index < result.size(); ++index) {
        result[index] = StdModelTimelineEntry{
            reader.read_i16(offset + index * 4U),
            reader.read_i16(offset + index * 4U + 2U),
        };
    }
    return result;
}

StdPointLightWave readPointLightWave(const EndianReader& reader, const std::size_t offset) {
    return StdPointLightWave{
        .startFrame = reader.read_i16(offset + 0x00U),
        .endFrame = reader.read_i16(offset + 0x02U),
        .amplitude = StdFloat32{ reader.read_u32(offset + 0x04U) },
        .phaseStep = reader.read_i16(offset + 0x08U),
        .phaseBase = reader.read_i16(offset + 0x0aU),
    };
}

StdPointLightRamp readPointLightRamp(const EndianReader& reader, const std::size_t offset) {
    return StdPointLightRamp{
        .slope = StdFloat32{ reader.read_u32(offset + 0x00U) },
        .startFrame = reader.read_i16(offset + 0x04U),
        .endFrame = reader.read_i16(offset + 0x06U),
    };
}

StdHandledPayloadPrefix readHandledPrefix(const EndianReader& reader, const std::size_t offset) {
    return StdHandledPayloadPrefix{
        .primaryActionKey = reader.read_i16(offset + 0x00U),
        .applicabilitySelectorRaw = reader.read_i16(offset + 0x02U),
        .secondaryActionKey = reader.read_i16(offset + 0x04U),
        .commandFlags = reader.read_u16(offset + 0x06U),
    };
}

StdEntryPayloadContent parseTypedPayload(
    const std::uint32_t combinedType, const EndianReader& reader, const std::size_t offset) {
    switch (combinedType) {
    case kStdSparcCombinedType:
        return StdSparcPayload{
            .common = readHandledPrefix(reader, offset),
            .mldFilenameKey = reader.read_u32(offset + 0x08U),
            .setupWord0 = reader.read_i16(offset + 0x0cU),
            .setupWord1 = reader.read_i16(offset + 0x0eU),
            .childMetadataRaw = reader.read_u32(offset + 0x10U),
            .behaviorFlags = reader.read_u32(offset + 0x14U),
            .durationOrActivationCount = reader.read_i16(offset + 0x18U),
            .childParameterS16 = reader.read_i16(offset + 0x1aU),
            .raw1c = reader.read_u16(offset + 0x1cU),
            .reserved1e = reader.read_u16(offset + 0x1eU),
            .positionOrOffset = readFloatArray<3U>(reader, offset + 0x20U),
            .velocityVector = readFloatArray<3U>(reader, offset + 0x2cU),
            .spawnCount = reader.read_i16(offset + 0x38U),
            .randomRange = reader.read_i16(offset + 0x3aU),
            .spawnMode = reader.read_i16(offset + 0x3cU),
            .raw3e = reader.read_i16(offset + 0x3eU),
            .childDivisorOrParameter = StdFloat32{ reader.read_u32(offset + 0x40U) },
            .childParameters44 = readFloatArray<2U>(reader, offset + 0x44U),
            .vectorMultipliers = readFloatArray<3U>(reader, offset + 0x4cU),
            .secondaryVector = readFloatArray<3U>(reader, offset + 0x58U),
            .choices = readSparcChoices(reader, offset + 0x64U),
        };
    case kStdPutModelCombinedType:
        return StdPutModelPayload{
            .common = readHandledPrefix(reader, offset),
            .mldResourceId = reader.read_u32(offset + 0x08U),
            .reserved0c = readBytes<4U>(reader, offset + 0x0cU),
            .modelFlags = reader.read_u32(offset + 0x10U),
            .activationFrame = reader.read_i16(offset + 0x14U),
            .fadeOutStartFrame = reader.read_i16(offset + 0x16U),
            .fadeInStartFrame = reader.read_i16(offset + 0x18U),
            .fadeInFrames = reader.read_i16(offset + 0x1aU),
            .fadeOutFrames = reader.read_i16(offset + 0x1cU),
            .anchorNodeIndex = reader.read_u16(offset + 0x1eU),
            .initialPosition = readFloatArray<3U>(reader, offset + 0x20U),
            .initialEulerDegrees = readFloatArray<3U>(reader, offset + 0x2cU),
            .modelTimeline = readModelTimeline(reader, offset + 0x38U),
            .timelineRepeatFirst = reader.read_i16(offset + 0x138U),
            .timelineRepeatLast = reader.read_i16(offset + 0x13aU),
            .positionDeltaPerTick = readFloatArray<3U>(reader, offset + 0x13cU),
            .eulerDeltaDegreesPerTick = readFloatArray<3U>(reader, offset + 0x148U),
            .scaleDeltaA = readFloatArray<3U>(reader, offset + 0x154U),
            .scaleDeltaAStartFrame = reader.read_i16(offset + 0x160U),
            .scaleDeltaATicks = reader.read_i16(offset + 0x162U),
            .initialScale = readFloatArray<3U>(reader, offset + 0x164U),
            .scaleDeltaB = readFloatArray<3U>(reader, offset + 0x170U),
            .scaleDeltaBStartFrame = reader.read_i16(offset + 0x17cU),
            .scaleDeltaBTicks = reader.read_i16(offset + 0x17eU),
            .textureStripSelectorA = reader.read_i16(offset + 0x180U),
            .textureStripSelectorB = reader.read_i16(offset + 0x182U),
            .motionFrameDelta = StdFloat32{ reader.read_u32(offset + 0x184U) },
            .eulerDeltaStartFrame = reader.read_i16(offset + 0x188U),
            .eulerDeltaEndFrame = reader.read_i16(offset + 0x18aU),
            .fallbackRenderParam = reader.read_i16(offset + 0x18cU),
            .positionDeltaStartFrame = reader.read_i16(offset + 0x18eU),
            .positionDeltaEndFrame = reader.read_i16(offset + 0x190U),
            .rawTail192 = readBytes<2U>(reader, offset + 0x192U),
        };
    case kStdSetCommandCombinedType:
        return StdSetCommandPayload{
            .common = readHandledPrefix(reader, offset),
            .reserved08 = readBytes<8U>(reader, offset + 0x08U),
            .behaviorFlags = reader.read_u32(offset + 0x10U),
            .preApplicationCountdown = reader.read_i16(offset + 0x14U),
            .pendingTargetCommandOrStateCode = reader.read_i16(offset + 0x16U),
            .reserved18 = readBytes<4U>(reader, offset + 0x18U),
        };
    case kStdMotionPauseCombinedType:
        return StdMotionPausePayload{
            .common = readHandledPrefix(reader, offset),
            .reserved08 = readBytes<8U>(reader, offset + 0x08U),
            .pauseFlags = reader.read_u32(offset + 0x10U),
            .pauseStartFrame = reader.read_i16(offset + 0x14U),
            .pauseEndFrame = reader.read_i16(offset + 0x16U),
        };
    case kStdCollisionBoxCombinedType:
        return StdCollisionBoxPayload{
            .common = readHandledPrefix(reader, offset),
            .reserved08 = readBytes<8U>(reader, offset + 0x08U),
            .modeFlags = reader.read_u32(offset + 0x10U),
            .startFrame = reader.read_i16(offset + 0x14U),
            .endFrameOrLifetime = reader.read_i16(offset + 0x16U),
            .modelNodeOrdinal = reader.read_u16(offset + 0x18U),
            .reserved1a = reader.read_u16(offset + 0x1aU),
            .positionVector = readFloatArray<3U>(reader, offset + 0x1cU),
            .velocityVector = readFloatArray<3U>(reader, offset + 0x28U),
            .collisionFlags = reader.read_u32(offset + 0x34U),
        };
    case kStdMoveModelCombinedType:
        return StdMoveModelPayload{
            .common = readHandledPrefix(reader, offset),
            .mldResourceId = reader.read_u32(offset + 0x08U),
            .reserved0c = readBytes<4U>(reader, offset + 0x0cU),
            .modelFlags = reader.read_u32(offset + 0x10U),
            .conditionFlags = reader.read_u16(offset + 0x14U),
            .activationFrame = reader.read_i16(offset + 0x16U),
            .fadeOutStartFrame = reader.read_i16(offset + 0x18U),
            .fadeInStartFrame = reader.read_i16(offset + 0x1aU),
            .fadeInFrames = reader.read_i16(offset + 0x1cU),
            .fadeOutFrames = reader.read_i16(offset + 0x1eU),
            .anchorNodeIndex = reader.read_u16(offset + 0x20U),
            .reserved22 = readBytes<2U>(reader, offset + 0x22U),
            .initialPosition = readFloatArray<3U>(reader, offset + 0x24U),
            .initialEulerDegrees = readFloatArray<3U>(reader, offset + 0x30U),
            .modelTimeline = readModelTimeline(reader, offset + 0x3cU),
            .timelineRepeatFirst = reader.read_i16(offset + 0x13cU),
            .timelineRepeatLast = reader.read_i16(offset + 0x13eU),
            .positionDeltaPerTick = readFloatArray<3U>(reader, offset + 0x140U),
            .eulerDeltaDegreesPerTick = readFloatArray<3U>(reader, offset + 0x14cU),
            .scaleDelta = readFloatArray<3U>(reader, offset + 0x158U),
            .scaleDeltaStartFrame = reader.read_i16(offset + 0x164U),
            .scaleDeltaTicks = reader.read_i16(offset + 0x166U),
            .initialScale = readFloatArray<3U>(reader, offset + 0x168U),
            .textureStripSelectorA = reader.read_i16(offset + 0x174U),
            .textureStripSelectorB = reader.read_i16(offset + 0x176U),
            .motionFrameDelta = StdFloat32{ reader.read_u32(offset + 0x178U) },
            .eulerDeltaStartFrame = reader.read_i16(offset + 0x17cU),
            .eulerDeltaEndFrame = reader.read_i16(offset + 0x17eU),
            .fallbackRenderParam = reader.read_i16(offset + 0x180U),
            .reserved182 = readBytes<2U>(reader, offset + 0x182U),
            .alternateChildVector = readFloatArray<3U>(reader, offset + 0x184U),
            .alternateChildYAcceleration = StdFloat32{ reader.read_u32(offset + 0x190U) },
            .alternateChildFadeFrames = reader.read_i16(offset + 0x194U),
            .alternateChildFadeStartFrame = reader.read_i16(offset + 0x196U),
            .conditionalEffectStartFrame = reader.read_i16(offset + 0x198U),
            .conditionalEffectEndFrame = reader.read_i16(offset + 0x19aU),
            .conditionalEffectInterval = reader.read_i16(offset + 0x19cU),
            .conditionalChildFadeFrames = reader.read_i16(offset + 0x19eU),
        };
    case kStdHitWeaponCombinedType:
        return StdHitWeaponPayload{
            .common = readHandledPrefix(reader, offset),
            .raw08 = reader.read_u32(offset + 0x08U),
            .reserved0c = readBytes<4U>(reader, offset + 0x0cU),
            .modelFlags = reader.read_u32(offset + 0x10U),
            .activationFrame = reader.read_i16(offset + 0x14U),
            .pathStartFrame = reader.read_i16(offset + 0x16U),
            .pathDurationFrames = reader.read_i16(offset + 0x18U),
            .fadeInStartFrame = reader.read_i16(offset + 0x1aU),
            .fadeInFrames = reader.read_i16(offset + 0x1cU),
            .fadeOutFrames = reader.read_i16(offset + 0x1eU),
            .anchorNodeIndex = reader.read_u16(offset + 0x20U),
            .reserved22 = readBytes<2U>(reader, offset + 0x22U),
            .pathEndpointOffset = readFloatArray<3U>(reader, offset + 0x24U),
            .initialEulerDegrees = readFloatArray<3U>(reader, offset + 0x30U),
            .initialScale = readFloatArray<3U>(reader, offset + 0x3cU),
            .modelTimeline = readModelTimeline(reader, offset + 0x48U),
            .timelineRepeatFirst = reader.read_i16(offset + 0x148U),
            .timelineRepeatLast = reader.read_i16(offset + 0x14aU),
            .pathAngleDeltaDegreesPerTick = StdFloat32{ reader.read_u32(offset + 0x14cU) },
            .eulerDeltaDegreesPerTick = readFloatArray<3U>(reader, offset + 0x150U),
            .raw15c = StdFloat32{ reader.read_u32(offset + 0x15cU) },
            .textureStripSelectorA = reader.read_i16(offset + 0x160U),
            .textureStripSelectorB = reader.read_i16(offset + 0x162U),
            .motionFrameDelta = StdFloat32{ reader.read_u32(offset + 0x164U) },
            .eulerDeltaStartFrame = reader.read_i16(offset + 0x168U),
            .eulerDeltaEndFrame = reader.read_i16(offset + 0x16aU),
            .childEffectInterval = reader.read_i16(offset + 0x16cU),
            .childEffectFadeFrames = reader.read_i16(offset + 0x16eU),
            .fallbackRenderParam = reader.read_i16(offset + 0x170U),
            .targetStateFlagMode = reader.read_i16(offset + 0x172U),
        };
    case kStdPointLightCombinedType:
        return StdPointLightPayload{
            .common = readHandledPrefix(reader, offset),
            .raw08 = reader.read_u32(offset + 0x08U),
            .reserved0c = readBytes<4U>(reader, offset + 0x0cU),
            .lightFlags = reader.read_u32(offset + 0x10U),
            .activationFrame = reader.read_i16(offset + 0x14U),
            .fadeOutStartFrame = reader.read_i16(offset + 0x16U),
            .fadeInFrames = reader.read_i16(offset + 0x18U),
            .fadeOutFrames = reader.read_i16(offset + 0x1aU),
            .anchorNodeIndex = reader.read_u16(offset + 0x1cU),
            .reserved1e = readBytes<2U>(reader, offset + 0x1eU),
            .position = readFloatArray<3U>(reader, offset + 0x20U),
            .lightSlot = reader.read_i16(offset + 0x2cU),
            .reserved2e = readBytes<2U>(reader, offset + 0x2eU),
            .rgb = readFloatArray<3U>(reader, offset + 0x30U),
            .attenuationParameterA = StdFloat32{ reader.read_u32(offset + 0x3cU) },
            .attenuationParameterB = StdFloat32{ reader.read_u32(offset + 0x40U) },
            .rgbWave = readPointLightWave(reader, offset + 0x44U),
            .attenuationWave = readPointLightWave(reader, offset + 0x50U),
            .enablePulseInterval = reader.read_i16(offset + 0x5cU),
            .reserved5e = readBytes<2U>(reader, offset + 0x5eU),
            .attenuationARamp = readPointLightRamp(reader, offset + 0x60U),
            .attenuationBRamp = readPointLightRamp(reader, offset + 0x68U),
        };
    case kStdSystemCameraCombinedType:
        return StdSystemCameraPayload{
            .common = readHandledPrefix(reader, offset),
            .reserved08 = readBytes<8U>(reader, offset + 0x08U),
            .cameraBehaviorFlags = reader.read_u32(offset + 0x10U),
            .cameraModeParameter = StdFloat32{ reader.read_u32(offset + 0x14U) },
            .startFrame = reader.read_i16(offset + 0x18U),
            .reserved1a = reader.read_u16(offset + 0x1aU),
            .endFrame = reader.read_i16(offset + 0x1cU),
            .holdFrameCount = reader.read_i16(offset + 0x1eU),
            .stepFrameCount = reader.read_i16(offset + 0x20U),
            .requestedMode = reader.read_i16(offset + 0x22U),
        };
    case kStdEffectWaitCombinedType:
        return StdEffectWaitPayload{
            .primaryActionKey = reader.read_i16(offset + 0x00U),
            .routeOrScopeRaw = reader.read_i16(offset + 0x02U),
            .secondaryActionKey = reader.read_i16(offset + 0x04U),
            .flagsRaw = reader.read_u16(offset + 0x06U),
            .reserved08 = readBytes<8U>(reader, offset + 0x08U),
            .waitValueRaw = reader.read_i16(offset + 0x10U),
            .reserved12 = reader.read_u16(offset + 0x12U),
        };
    case kStdSeRequestCombinedType:
        return StdSeRequestPayload{
            .common = readHandledPrefix(reader, offset),
            .reserved08 = readBytes<8U>(reader, offset + 0x08U),
            .requestFlags = reader.read_u32(offset + 0x10U),
            .reserved14 = readBytes<4U>(reader, offset + 0x14U),
            .requestMode = reader.read_i16(offset + 0x18U),
            .playAtFrameOrDelay = reader.read_i16(offset + 0x1aU),
            .mode4Timeout = reader.read_i16(offset + 0x1cU),
            .soundBankGroupRaw = reader.read_i16(offset + 0x1eU),
            .primaryCueSelector = reader.read_i16(offset + 0x20U),
            .outputSlotAgeOrLifetime = reader.read_i16(offset + 0x22U),
            .alternateCueSelector1 = reader.read_i16(offset + 0x24U),
            .alternateCueSelector2 = reader.read_i16(offset + 0x26U),
            .trailingRaw28 = reader.read_u32(offset + 0x28U),
        };
    default:
        return StdOpaquePayload{};
    }
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
        const auto rowType = reader.read_i16(offset + 0x02U);
        StdActionRowFields fields{};
        if (rowType == 0) {
            fields = StdType0ActionRowFields{
                .reserved06 = reader.read_i16(offset + 0x06U),
                .raw08 = reader.read_u32(offset + 0x08U),
                .verticalExtentOverrideCode = reader.read_i16(offset + 0x0eU),
                .defaultPlanarMovementStep = StdFloat32{ reader.read_u32(offset + 0x10U) },
                .turningStepOrThreshold = StdFloat32{ reader.read_u32(offset + 0x14U) },
            };
        } else if (rowType == 1) {
            fields = StdMotionActionRowFields{
                .motionResourceSelector = reader.read_i16(offset + 0x06U),
                .motionFlags = reader.read_u32(offset + 0x08U),
                .actionParameterS16 = reader.read_i16(offset + 0x0eU),
                .timingOrTransitionScalar = StdFloat32{ reader.read_u32(offset + 0x10U) },
                .motionFrameIncrement = StdFloat32{ reader.read_u32(offset + 0x14U) },
            };
        } else if (rowType == 3) {
            return malformed(endian, StdDiagnosticCode::MalformedActionRows,
                "Serialized action-row type 3 is invalid; the game loader synthesizes this terminator at runtime.",
                offset + 0x02U);
        } else {
            fields = StdUnrecognizedActionRowFields{
                .rowType = rowType,
                .raw06 = reader.read_i16(offset + 0x06U),
                .raw08 = reader.read_u32(offset + 0x08U),
                .raw0e = reader.read_i16(offset + 0x0eU),
                .raw10 = StdFloat32{ reader.read_u32(offset + 0x10U) },
                .raw14 = StdFloat32{ reader.read_u32(offset + 0x14U) },
            };
            result.warnings.push_back(StdDocumentDiagnostic{
                StdDiagnosticCode::UnrecognizedActionRowType, StdDiagnosticSeverity::Warning,
                "Action row uses a source row type whose semantics are not established.", offset + 0x02U });
        }
        content.rows.push_back(StdActionRow{
            .id = StdActionRowId{ index + 1U },
            .actionId = reader.read_i16(offset + 0x00U),
            .selectorCallbackIndex = reader.read_i16(offset + 0x04U),
            .secondaryActionKey = reader.read_i16(offset + 0x0cU),
            .fields = std::move(fields),
        });
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
    const auto declaredEnd64 = static_cast<std::uint64_t>(kHeaderSize) + decodedSpan;
    if (declaredEnd64 > bytes.size()) {
        return malformed(endian, StdDiagnosticCode::MalformedEntryTable,
            "Entry-table decoded span extends beyond the decoded file.", 0x0cU);
    }
    const auto declaredEnd = static_cast<std::size_t>(declaredEnd64);
    const auto tableEnd64 = static_cast<std::uint64_t>(kHeaderSize) +
        static_cast<std::uint64_t>(count) * kEntryRecordSize;
    if (tableEnd64 > declaredEnd) {
        return malformed(endian, StdDiagnosticCode::MalformedEntryTable,
            "Entry-table record count extends beyond the declared decoded span.", 0x00U);
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
        const auto location = reader.read_i16(offset + 0x00U);
        const auto opcode = reader.read_i16(offset + 0x02U);
        const auto combinedType = (static_cast<std::uint32_t>(static_cast<std::uint16_t>(opcode)) << 16U) |
            static_cast<std::uint16_t>(location);
        const auto size = reader.read_u32(offset + 0x08U);
        const auto* descriptor = findStdCommandDescriptor(combinedType);
        if (size == 0U) {
            if (descriptor != nullptr) {
                return malformed(endian, StdDiagnosticCode::KnownPayloadMissing,
                    "Known STD command is missing the fixed-size payload materialized by the game loader.",
                    offset + 0x08U);
            }
            continue;
        }
        if (descriptor != nullptr && size != descriptor->loaderPayloadSize) {
            return malformed(endian, StdDiagnosticCode::KnownPayloadSizeMismatch,
                "Known STD command payload does not match its fixed loader size.", offset + 0x08U);
        }
        const auto relative = reader.read_u32(offset + 0x0cU);
        const auto begin64 = static_cast<std::uint64_t>(kHeaderSize) + relative;
        const auto end64 = begin64 + size;
        if (begin64 < tableEnd || end64 > declaredEnd) {
            return malformed(endian, StdDiagnosticCode::MalformedEntryTable,
                "Entry payload is out of bounds or overlaps the record table.", offset + 0x08U);
        }
        const auto begin = static_cast<std::size_t>(begin64);
        if (descriptor != nullptr && begin % 4U != 0U) {
            result.warnings.push_back(StdDocumentDiagnostic{
                StdDiagnosticCode::KnownPayloadMisaligned, StdDiagnosticSeverity::Warning,
                "Known fixed-size payload is not four-byte aligned.", begin });
        }
        spans.push_back(PayloadSpan{ index, begin, static_cast<std::size_t>(end64) });
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
        if (reader.read_u32(offset + 0x08U) != 0U) record.payload = StdEntryPayloadId{ index + 1U };
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
        StdEntryPayload payload{};
        payload.id = id;
        const auto* descriptor = findStdCommandDescriptor(record.combinedType());
        if (descriptor != nullptr && descriptor->hasTypedPayloadCodec) {
            payload.content = parseTypedPayload(record.combinedType(), reader, span.begin);
        } else {
            payload.content = StdOpaquePayload{ std::vector<std::uint8_t>(
                bytes.begin() + static_cast<std::ptrdiff_t>(span.begin),
                bytes.begin() + static_cast<std::ptrdiff_t>(span.end)) };
        }
        content.payloadLayout.push_back(id);
        content.payloads.push_back(std::move(payload));
        cursor = span.end;
    }
    if (cursor < declaredEnd) {
        StdOpaqueFragment fragment{ StdOpaqueFragmentId{ nextFragmentId++ },
            std::vector<std::uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                bytes.begin() + static_cast<std::ptrdiff_t>(declaredEnd)) };
        content.payloadLayout.push_back(fragment.id);
        content.opaqueFragments.push_back(std::move(fragment));
    }
    if (declaredEnd < bytes.size()) {
        content.fileTrailer = StdOpaqueFragment{
            StdOpaqueFragmentId{ nextFragmentId },
            std::vector<std::uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(declaredEnd), bytes.end()) };
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
    if (table->fileTrailer.has_value()) {
        receipt.opaqueEvidence.fileTrailerId = table->fileTrailer->id;
        receipt.opaqueEvidence.fileTrailerSha256 = detail::sha256(table->fileTrailer->bytes);
    }
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
        addDiagnostic(result, StdDiagnosticCode::EmptyInput, StdDiagnosticSeverity::Error, "STD input is empty.");
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
        addDiagnostic(result, StdDiagnosticCode::EmptyInput, StdDiagnosticSeverity::Error, "Decoded STD input is empty.");
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
        addDiagnostic(result, StdDiagnosticCode::FileReadFailed, StdDiagnosticSeverity::Error, "Unable to read STD file.");
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
    case StdDiagnosticCode::KnownPayloadSizeMismatch: return "known_payload_size_mismatch";
    case StdDiagnosticCode::KnownPayloadMissing: return "known_payload_missing";
    case StdDiagnosticCode::UnrecognizedActionRowType: return "unrecognized_action_row_type";
    case StdDiagnosticCode::KnownPayloadMisaligned: return "known_payload_misaligned";
    case StdDiagnosticCode::InvalidTimelineRepeatRange: return "invalid_timeline_repeat_range";
    case StdDiagnosticCode::UnsupportedPointLightSlot: return "unsupported_point_light_slot";
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
