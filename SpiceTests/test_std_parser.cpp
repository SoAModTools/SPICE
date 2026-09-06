#include "../Compression/Aklz.h"
#include "../SpiceRoot/Binary/EndianReader.h"
#include "../SpiceRoot/Binary/EndianWriter.h"
#include "../SpiceStd/SpiceStd.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

namespace {

using spice::root::Endian;
using spice::root::EndianReader;
using spice::root::EndianSpanWriter;
using namespace spice::stdfile;

std::vector<std::uint8_t> makeActionRows(const Endian endian, const std::uint32_t rowCount = 2U) {
    std::vector<std::uint8_t> bytes(0x10U + rowCount * 0x18U, 0U);
    EndianSpanWriter writer(bytes, endian);
    writer.write_u16_at(0x00U, 0x34U);
    writer.write_u16_at(0x02U, 0x12U);
    writer.write_u32_at(0x04U, 0x10203040U);
    writer.write_u32_at(0x08U, rowCount);
    writer.write_u32_at(0x0cU, 0x50607080U);
    for (std::uint32_t index = 0U; index < rowCount; ++index) {
        const auto offset = 0x10U + index * 0x18U;
        writer.write_i16_at(offset + 0x00U, static_cast<std::int16_t>(10 + index));
        writer.write_i16_at(offset + 0x02U, index == 0U ? 0 : 1);
        writer.write_i16_at(offset + 0x04U, static_cast<std::int16_t>(20 + index));
        writer.write_i16_at(offset + 0x06U, static_cast<std::int16_t>(30 + index));
        writer.write_u32_at(offset + 0x08U, 0x80000000U | index);
        writer.write_i16_at(offset + 0x0cU, static_cast<std::int16_t>(40 + index));
        writer.write_i16_at(offset + 0x0eU, static_cast<std::int16_t>(50 + index));
        writer.write_u32_at(offset + 0x10U, 0x3f800000U + index);
        writer.write_u32_at(offset + 0x14U, 0x40000000U + index);
    }
    return bytes;
}
std::vector<std::uint8_t> makeEntryTable(const Endian endian, const bool typed = true,
    const bool includeGap = false) {
    const std::uint32_t payloadSize = typed ? kStdSystemCameraPayloadSize : 3U;
    const std::uint32_t gapSize = includeGap ? 4U : 0U;
    const std::uint32_t relativePayloadOffset = 0x20U + gapSize;
    std::vector<std::uint8_t> bytes(0x30U + gapSize + payloadSize, 0U);
    EndianSpanWriter writer(bytes, endian);
    writer.write_u16_at(0x00U, 2U);
    writer.write_u16_at(0x02U, 4U);
    writer.write_u32_at(0x04U, 0x11223344U);
    writer.write_u32_at(0x08U, 0x55667788U);
    writer.write_u32_at(0x0cU, static_cast<std::uint32_t>(bytes.size() - 0x10U));
    writer.write_i16_at(0x10U, typed ? 0x2a : 1);
    writer.write_i16_at(0x12U, typed ? 3 : 2);
    writer.write_u32_at(0x14U, 0xabcdef01U);
    writer.write_u32_at(0x18U, payloadSize);
    writer.write_u32_at(0x1cU, relativePayloadOffset);
    writer.write_i16_at(0x20U, -1);
    writer.write_i16_at(0x22U, 7);
    writer.write_u32_at(0x24U, 0x01020304U);
    writer.write_u32_at(0x28U, 0x05060708U);
    writer.write_u32_at(0x2cU, 0x090a0b0cU);
    if (includeGap) {
        bytes[0x30U] = 0xdeU; bytes[0x31U] = 0xadU; bytes[0x32U] = 0xbeU; bytes[0x33U] = 0xefU;
    }
    const auto payloadOffset = 0x10U + relativePayloadOffset;
    if (typed) {
        writer.write_i16_at(payloadOffset + 0x00U, 11);
        writer.write_i16_at(payloadOffset + 0x02U, 12);
        writer.write_i16_at(payloadOffset + 0x04U, 13);
        writer.write_u16_at(payloadOffset + 0x06U, 0x14U);
        for (std::size_t index = 0U; index < 8U; ++index) {
            bytes[payloadOffset + 0x08U + index] = static_cast<std::uint8_t>(0x15U + index);
        }
        writer.write_u32_at(payloadOffset + 0x10U, 0x1d1e1f20U);
        writer.write_u32_at(payloadOffset + 0x14U, 0x21222324U);
        writer.write_i16_at(payloadOffset + 0x18U, 25);
        writer.write_u16_at(payloadOffset + 0x1aU, 26U);
        writer.write_i16_at(payloadOffset + 0x1cU, 27);
        writer.write_i16_at(payloadOffset + 0x1eU, 28);
        writer.write_i16_at(payloadOffset + 0x20U, 29);
        writer.write_i16_at(payloadOffset + 0x22U, 30);
    } else {
        bytes[payloadOffset] = 0xaaU; bytes[payloadOffset + 1U] = 0xbbU; bytes[payloadOffset + 2U] = 0xccU;
    }
    return bytes;
}

std::vector<std::uint8_t> makeTerminatorOnly(const Endian endian) {
    std::vector<std::uint8_t> bytes(0x20U, 0U);
    EndianSpanWriter writer(bytes, endian);
    writer.write_u16_at(0x00U, 1U);
    writer.write_u16_at(0x02U, 4U);
    writer.write_u32_at(0x0cU, 0x10U);
    writer.write_i16_at(0x10U, -1);
    return bytes;
}

std::vector<std::uint8_t> compressed(const std::vector<std::uint8_t>& bytes) {
    auto result = spice::compression::aklz::compress(bytes);
    EXPECT_TRUE(result.ok());
    return result.bytes;
}

bool hasCode(const std::vector<StdDocumentDiagnostic>& diagnostics, const StdDiagnosticCode code) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const auto& item) { return item.code == code; });
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

StdPlatform platformFor(const Endian endian) {
    return endian == Endian::Little ? StdPlatform::Dreamcast : StdPlatform::GameCube;
}

StdHandledPayloadPrefix samplePrefix() {
    return StdHandledPayloadPrefix{ -11, 2, 13, 0xa001U };
}

template <std::size_t Size>
std::array<StdFloat32, Size> sampleFloats(const std::uint32_t base) {
    std::array<StdFloat32, Size> values{};
    for (std::size_t index = 0U; index < Size; ++index) values[index].bits = base + static_cast<std::uint32_t>(index);
    return values;
}

std::array<StdModelTimelineEntry, 64U> sampleTimeline(const std::int16_t base) {
    std::array<StdModelTimelineEntry, 64U> values{};
    for (std::size_t index = 0U; index < values.size(); ++index) {
        values[index] = StdModelTimelineEntry{
            static_cast<std::int16_t>(base + static_cast<std::int16_t>(index)),
            static_cast<std::int16_t>(index < 4U ? index + 1U : 0U),
        };
    }
    return values;
}

StdEntryPayloadContent samplePayload(const std::uint32_t combinedType) {
    if (combinedType == kStdSparcCombinedType) {
        StdSparcPayload value{};
        value.common = samplePrefix();
        value.mldFilenameKey = 5100000U;
        value.setupWord0 = -14;
        value.setupWord1 = 15;
        value.childMetadataRaw = 0x16171819U;
        value.behaviorFlags = 0x20008a0fU;
        value.durationOrActivationCount = 21;
        value.childParameterS16 = -22;
        value.raw1c = 23U;
        value.reserved1e = 24U;
        value.positionOrOffset = sampleFloats<3U>(0x3f800000U);
        value.velocityVector = sampleFloats<3U>(0x40000000U);
        value.spawnCount = 31;
        value.randomRange = 32;
        value.spawnMode = 33;
        value.raw3e = -34;
        value.childDivisorOrParameter.bits = 0x40400000U;
        value.childParameters44 = sampleFloats<2U>(0x40800000U);
        value.vectorMultipliers = sampleFloats<3U>(0x40a00000U);
        value.secondaryVector = sampleFloats<3U>(0x40c00000U);
        for (std::size_t index = 0U; index < value.choices.size(); ++index) {
            value.choices[index] = StdSparcChoice{
                static_cast<std::int16_t>(static_cast<int>(index) - 8),
                static_cast<std::int16_t>(index < 4U ? index + 1U : 0U),
            };
        }
        return value;
    }
    if (combinedType == kStdPutModelCombinedType) {
        StdPutModelPayload value{};
        value.common = samplePrefix();
        value.mldResourceId = 5100000U;
        value.reserved0c.fill(0x91U);
        value.modelFlags = 0x2c8020d4U;
        value.activationFrame = 5;
        value.fadeOutStartFrame = 80;
        value.fadeInStartFrame = 2;
        value.fadeInFrames = 3;
        value.fadeOutFrames = 4;
        value.anchorNodeIndex = 17U;
        value.initialPosition = sampleFloats<3U>(0x3f000000U);
        value.initialEulerDegrees = sampleFloats<3U>(0x40000000U);
        value.modelTimeline = sampleTimeline(10);
        value.timelineRepeatFirst = 1;
        value.timelineRepeatLast = 3;
        value.positionDeltaPerTick = sampleFloats<3U>(0x40400000U);
        value.eulerDeltaDegreesPerTick = sampleFloats<3U>(0x40800000U);
        value.scaleDeltaA = sampleFloats<3U>(0x40a00000U);
        value.scaleDeltaAStartFrame = 8;
        value.scaleDeltaATicks = 9;
        value.initialScale = sampleFloats<3U>(0x40c00000U);
        value.scaleDeltaB = sampleFloats<3U>(0x40e00000U);
        value.scaleDeltaBStartFrame = 10;
        value.scaleDeltaBTicks = 11;
        value.textureStripSelectorA = -12;
        value.textureStripSelectorB = 13;
        value.motionFrameDelta.bits = 0x3f800001U;
        value.eulerDeltaStartFrame = 14;
        value.eulerDeltaEndFrame = 15;
        value.fallbackRenderParam = 3;
        value.positionDeltaStartFrame = 16;
        value.positionDeltaEndFrame = 17;
        value.rawTail192 = { 0x89U, 0x7bU };
        return value;
    }
    if (combinedType == kStdSetCommandCombinedType) {
        StdSetCommandPayload value{};
        value.common = samplePrefix();
        value.reserved08.fill(0x81U);
        value.behaviorFlags = 0x10000400U;
        value.preApplicationCountdown = 12;
        value.pendingTargetCommandOrStateCode = -13;
        value.reserved18.fill(0x82U);
        return value;
    }
    if (combinedType == kStdMotionPauseCombinedType) {
        StdMotionPausePayload value{};
        value.common = samplePrefix();
        value.reserved08.fill(0x92U);
        value.pauseFlags = 0x80000001U;
        value.pauseStartFrame = -4;
        value.pauseEndFrame = 73;
        return value;
    }
    if (combinedType == kStdCollisionBoxCombinedType) {
        StdCollisionBoxPayload value{};
        value.common = samplePrefix();
        value.reserved08.fill(0x83U);
        value.modeFlags = 0x410U;
        value.startFrame = -4;
        value.endFrameOrLifetime = 20;
        value.modelNodeOrdinal = 9U;
        value.reserved1a = 0x8485U;
        value.positionVector = sampleFloats<3U>(0x3f000000U);
        value.velocityVector = sampleFloats<3U>(0x3f400000U);
        value.collisionFlags = 0x0000fc00U;
        return value;
    }
    if (combinedType == kStdMoveModelCombinedType) {
        StdMoveModelPayload value{};
        value.common = samplePrefix();
        value.mldResourceId = 1002105U;
        value.reserved0c.fill(0x93U);
        value.modelFlags = 0x2c802205U;
        value.conditionFlags = 0xb000U;
        value.activationFrame = 14;
        value.fadeOutStartFrame = 50;
        value.fadeInStartFrame = 3;
        value.fadeInFrames = 4;
        value.fadeOutFrames = 5;
        value.anchorNodeIndex = 85U;
        value.reserved22 = { 0x94U, 0x95U };
        value.initialPosition = sampleFloats<3U>(0x41000000U);
        value.initialEulerDegrees = sampleFloats<3U>(0x41200000U);
        value.modelTimeline = sampleTimeline(20);
        value.timelineRepeatFirst = 0;
        value.timelineRepeatLast = 2;
        value.positionDeltaPerTick = sampleFloats<3U>(0x41400000U);
        value.eulerDeltaDegreesPerTick = sampleFloats<3U>(0x41600000U);
        value.scaleDelta = sampleFloats<3U>(0x41800000U);
        value.scaleDeltaStartFrame = 16;
        value.scaleDeltaTicks = 10;
        value.initialScale = sampleFloats<3U>(0x41a00000U);
        value.textureStripSelectorA = -5;
        value.textureStripSelectorB = 5;
        value.motionFrameDelta.bits = 0x3fc00001U;
        value.eulerDeltaStartFrame = 21;
        value.eulerDeltaEndFrame = 42;
        value.fallbackRenderParam = 6;
        value.reserved182 = { 0x96U, 0x97U };
        value.alternateChildVector = sampleFloats<3U>(0x41c00000U);
        value.alternateChildYAcceleration.bits = 0x3ca3d70aU;
        value.alternateChildFadeFrames = 3;
        value.alternateChildFadeStartFrame = 4;
        value.conditionalEffectStartFrame = 25;
        value.conditionalEffectEndFrame = 70;
        value.conditionalEffectInterval = 2;
        value.conditionalChildFadeFrames = 15;
        return value;
    }
    if (combinedType == kStdHitWeaponCombinedType) {
        StdHitWeaponPayload value{};
        value.common = samplePrefix();
        value.raw08 = 9901000U;
        value.reserved0c.fill(0x98U);
        value.modelFlags = 0x08002052U;
        value.activationFrame = 13;
        value.pathStartFrame = 0;
        value.pathDurationFrames = 31;
        value.fadeInStartFrame = 1;
        value.fadeInFrames = 2;
        value.fadeOutFrames = 3;
        value.anchorNodeIndex = 19U;
        value.reserved22 = { 0x99U, 0x9aU };
        value.pathEndpointOffset = sampleFloats<3U>(0x41e00000U);
        value.initialEulerDegrees = sampleFloats<3U>(0x42000000U);
        value.initialScale = sampleFloats<3U>(0x42200000U);
        value.modelTimeline = sampleTimeline(30);
        value.timelineRepeatFirst = 1;
        value.timelineRepeatLast = 3;
        value.pathAngleDeltaDegreesPerTick.bits = 0xc14198e0U;
        value.eulerDeltaDegreesPerTick = sampleFloats<3U>(0x42400000U);
        value.raw15c.bits = 0x3ecccc84U;
        value.textureStripSelectorA = -10;
        value.textureStripSelectorB = 10;
        value.motionFrameDelta.bits = 0x3f000001U;
        value.eulerDeltaStartFrame = 6;
        value.eulerDeltaEndFrame = 7;
        value.childEffectInterval = 1;
        value.childEffectFadeFrames = 20;
        value.fallbackRenderParam = 2;
        value.targetStateFlagMode = 2;
        return value;
    }
    if (combinedType == kStdPointLightCombinedType) {
        StdPointLightPayload value{};
        value.common = samplePrefix();
        value.raw08 = 1500832U;
        value.reserved0c.fill(0x9bU);
        value.lightFlags = 0x0c000004U;
        value.activationFrame = 8;
        value.fadeOutStartFrame = 90;
        value.fadeInFrames = 5;
        value.fadeOutFrames = 10;
        value.anchorNodeIndex = 56U;
        value.reserved1e = { 0x9cU, 0x9dU };
        value.position = sampleFloats<3U>(0x42600000U);
        value.lightSlot = 3;
        value.reserved2e = { 0x9eU, 0x9fU };
        value.rgb = sampleFloats<3U>(0x42800000U);
        value.attenuationParameterA.bits = 0x41f0cc68U;
        value.attenuationParameterB.bits = 0x4258662aU;
        value.rgbWave = StdPointLightWave{ 3, 35, StdFloat32{ 0x3f800001U }, 30, 9 };
        value.attenuationWave = StdPointLightWave{ 5, 40, StdFloat32{ 0x40000001U }, 40, 10 };
        value.enablePulseInterval = 12;
        value.reserved5e = { 0xa0U, 0xa1U };
        value.attenuationARamp = StdPointLightRamp{ StdFloat32{ 0x3f000001U }, 5, 13 };
        value.attenuationBRamp = StdPointLightRamp{ StdFloat32{ 0x3f800001U }, 10, 20 };
        return value;
    }
    if (combinedType == kStdSystemCameraCombinedType) {
        StdSystemCameraPayload value{};
        value.common = samplePrefix();
        value.reserved08.fill(0x86U);
        value.cameraBehaviorFlags = 0x80000001U;
        value.cameraModeParameter.bits = 0x7fc01234U;
        value.startFrame = -7;
        value.reserved1a = 0x8788U;
        value.endFrame = 45;
        value.holdFrameCount = 6;
        value.stepFrameCount = 7;
        value.requestedMode = 8;
        return value;
    }
    if (combinedType == kStdEffectWaitCombinedType) {
        StdEffectWaitPayload value{};
        value.primaryActionKey = -1;
        value.routeOrScopeRaw = 2;
        value.secondaryActionKey = 3;
        value.flagsRaw = 0x8000U;
        value.reserved08.fill(0x89U);
        value.waitValueRaw = 10;
        value.reserved12 = 0x8a8bU;
        return value;
    }
    StdSeRequestPayload value{};
    value.common = samplePrefix();
    value.reserved08.fill(0x8cU);
    value.requestFlags = 0x8001U;
    value.reserved14.fill(0x8dU);
    value.requestMode = 6;
    value.playAtFrameOrDelay = 11;
    value.mode4Timeout = 12;
    value.soundBankGroupRaw = static_cast<std::int16_t>(0x7ff3U);
    value.primaryCueSelector = 14;
    value.outputSlotAgeOrLifetime = 15;
    value.alternateCueSelector1 = 16;
    value.alternateCueSelector2 = 17;
    value.trailingRaw28 = 0x8e8f9091U;
    return value;
}

StdDocument sampleTypedDocument(const std::uint32_t combinedType) {
    StdEntryTableContent table{};
    table.records.push_back(StdEntryRecord{
        .id = StdEntryRecordId{ 1U },
        .locationCode = static_cast<std::int16_t>(combinedType & 0xffffU),
        .opcode = static_cast<std::int16_t>(combinedType >> 16U),
        .raw04 = 0x12345678U,
        .payload = StdEntryPayloadId{ 1U },
    });
    table.terminator = StdEntryTerminator{ .id = StdEntryTerminatorId{ 1U }, .negativeLocation = -1 };
    table.payloads.push_back(StdEntryPayload{ StdEntryPayloadId{ 1U }, samplePayload(combinedType) });
    table.payloadLayout.push_back(StdEntryPayloadId{ 1U });
    return StdDocument{ std::move(table) };
}

std::vector<std::uint8_t> makeKnownPayloadMissing(const Endian endian) {
    std::vector<std::uint8_t> bytes(0x30U, 0U);
    EndianSpanWriter writer(bytes, endian);
    writer.write_u16_at(0x00U, 2U);
    writer.write_u16_at(0x02U, 4U);
    writer.write_u32_at(0x0cU, 0x20U);
    writer.write_i16_at(0x10U, 0x2a);
    writer.write_i16_at(0x12U, 3);
    writer.write_i16_at(0x20U, -1);
    return bytes;
}

std::vector<std::uint8_t> makeKnownOpaquePayload(
    const Endian endian, const std::uint32_t combinedType, const std::uint32_t payloadSize) {
    std::vector<std::uint8_t> bytes(0x30U + payloadSize, 0U);
    EndianSpanWriter writer(bytes, endian);
    writer.write_u16_at(0x00U, 2U);
    writer.write_u16_at(0x02U, 4U);
    writer.write_u32_at(0x0cU, static_cast<std::uint32_t>(bytes.size() - 0x10U));
    writer.write_i16_at(0x10U, static_cast<std::int16_t>(combinedType & 0xffffU));
    writer.write_i16_at(0x12U, static_cast<std::int16_t>(combinedType >> 16U));
    writer.write_u32_at(0x18U, payloadSize);
    writer.write_u32_at(0x1cU, 0x20U);
    writer.write_i16_at(0x20U, -1);
    for (std::size_t index = 0U; index < payloadSize; ++index) {
        bytes[0x30U + index] = static_cast<std::uint8_t>((index * 37U + 11U) & 0xffU);
    }
    return bytes;
}

} // namespace

TEST(SpiceStdDocumentImporter, EquivalentActionRowsAcrossEndianAndCompression) {
    std::optional<StdDocument> expected{};
    for (const auto endian : { Endian::Little, Endian::Big }) {
        const auto raw = makeActionRows(endian);
        for (const auto& bytes : { raw, compressed(raw) }) {
            const auto imported = StdDocumentImporter::importBytes(bytes);
            ASSERT_TRUE(imported.ok());
            EXPECT_EQ(imported.receipt.byteOrder, endian);
            ASSERT_TRUE(std::holds_alternative<StdActionRowsContent>(imported.document->content));
            if (!expected.has_value()) expected = imported.document;
            else EXPECT_EQ(*imported.document, *expected);
        }
    }
}

TEST(SpiceStdDocumentImporter, EquivalentTypedEntryTablesAcrossEndianAndCompression) {
    std::optional<StdDocument> expected{};
    for (const auto endian : { Endian::Little, Endian::Big }) {
        const auto raw = makeEntryTable(endian);
        for (const auto& bytes : { raw, compressed(raw) }) {
            const auto imported = StdDocumentImporter::importBytes(bytes);
            ASSERT_TRUE(imported.ok());
            const auto& table = std::get<StdEntryTableContent>(imported.document->content);
            ASSERT_EQ(table.records.size(), 1U);
            ASSERT_EQ(table.payloads.size(), 1U);
            EXPECT_TRUE(std::holds_alternative<StdSystemCameraPayload>(table.payloads[0].content));
            if (!expected.has_value()) expected = imported.document;
            else EXPECT_EQ(*imported.document, *expected);
        }
    }
}

TEST(SpiceStdDocumentImporter, AllResearchBackedPayloadsRoundTripAcrossEncodings) {
    const std::array combinedTypes{
        kStdSparcCombinedType,
        kStdPutModelCombinedType,
        kStdSetCommandCombinedType,
        kStdMotionPauseCombinedType,
        kStdCollisionBoxCombinedType,
        kStdMoveModelCombinedType,
        kStdHitWeaponCombinedType,
        kStdPointLightCombinedType,
        kStdSystemCameraCombinedType,
        kStdEffectWaitCombinedType,
        kStdSeRequestCombinedType,
    };
    for (const auto combinedType : combinedTypes) {
        const auto expected = sampleTypedDocument(combinedType);
        for (const auto endian : { Endian::Little, Endian::Big }) {
            for (const auto compression : { StdCompression::None, StdCompression::Aklz }) {
                const auto written = StdDocumentWriter::write(expected,
                    { platformFor(endian), compression });
                ASSERT_TRUE(written.ok()) << std::hex << combinedType;
                const auto imported = StdDocumentImporter::importBytes(written.bytes);
                ASSERT_TRUE(imported.ok()) << std::hex << combinedType;
                EXPECT_EQ(*imported.document, expected) << std::hex << combinedType;
            }
        }
    }
}

TEST(SpiceStdDocumentImporter, KnownPayloadContractsRejectMissingAndWrongSize) {
    const auto missing = StdDocumentImporter::importBytes(makeKnownPayloadMissing(Endian::Big));
    ASSERT_FALSE(missing.ok());
    EXPECT_TRUE(hasCode(missing.diagnostics, StdDiagnosticCode::KnownPayloadMissing));

    auto wrongSize = makeEntryTable(Endian::Big);
    EndianSpanWriter(wrongSize, Endian::Big).write_u32_at(0x18U, kStdSystemCameraPayloadSize - 1U);
    const auto rejected = StdDocumentImporter::importBytes(wrongSize);
    EXPECT_FALSE(rejected.ok());
    EXPECT_TRUE(hasCode(rejected.diagnostics, StdDiagnosticCode::KnownPayloadSizeMismatch));

    const auto putModelWrongSize = StdDocumentImporter::importBytes(
        makeKnownOpaquePayload(Endian::Big, kStdPutModelCombinedType, kStdPutModelPayloadSize - 1U));
    EXPECT_FALSE(putModelWrongSize.ok());
    EXPECT_TRUE(hasCode(putModelWrongSize.diagnostics, StdDiagnosticCode::KnownPayloadSizeMismatch));
}

TEST(SpiceStdDocumentImporter, NewlyTypedCommandsAreReceiptFreeAndCrossEndian) {
    const std::array types{
        kStdPutModelCombinedType,
        kStdMotionPauseCombinedType,
        kStdMoveModelCombinedType,
        kStdHitWeaponCombinedType,
        kStdPointLightCombinedType,
    };
    for (const auto combinedType : types) {
        const auto* descriptor = findStdCommandDescriptor(combinedType);
        ASSERT_NE(descriptor, nullptr);
        ASSERT_TRUE(descriptor->hasTypedPayloadCodec);
        const auto expected = sampleTypedDocument(combinedType);
        EXPECT_FALSE(expected.hasOpaqueContent());
        const auto dreamcast = StdDocumentWriter::write(
            expected, { StdPlatform::Dreamcast, StdCompression::None });
        ASSERT_TRUE(dreamcast.ok()) << std::hex << combinedType;
        const auto imported = StdDocumentImporter::importBytes(dreamcast.bytes);
        ASSERT_TRUE(imported.ok()) << std::hex << combinedType;
        EXPECT_EQ(*imported.document, expected);
        const auto gameCube = StdDocumentWriter::write(
            *imported.document, { StdPlatform::GameCube, StdCompression::Aklz });
        ASSERT_TRUE(gameCube.ok()) << std::hex << combinedType;
        const auto reparsed = StdDocumentImporter::importBytes(gameCube.bytes);
        ASSERT_TRUE(reparsed.ok()) << std::hex << combinedType;
        EXPECT_EQ(*reparsed.document, expected);
    }
}

TEST(SpiceStdDocumentWriter, PreservesPutModelRawTailAcrossByteOrders) {
    const auto document = sampleTypedDocument(kStdPutModelCombinedType);
    for (const auto platform : { StdPlatform::Dreamcast, StdPlatform::GameCube }) {
        const auto written = StdDocumentWriter::write(
            document, { platform, StdCompression::None });
        ASSERT_TRUE(written.ok());
        ASSERT_GE(written.bytes.size(), 0x30U + kStdPutModelPayloadSize);
        EXPECT_EQ(written.bytes[0x30U + 0x192U], 0x89U);
        EXPECT_EQ(written.bytes[0x30U + 0x193U], 0x7bU);
        const EndianReader reader(written.bytes, byteOrderFor(platform));
        EXPECT_EQ(reader.read_u32(0x30U + 0x08U), 5100000U);
    }
}

TEST(SpiceStdDocumentWriter, DoesNotSerializeMotionPauseRuntimeFlag) {
    auto document = sampleTypedDocument(kStdMotionPauseCombinedType);
    auto& payload = std::get<StdMotionPausePayload>(
        std::get<StdEntryTableContent>(document.content).payloads[0].content);
    payload.common.commandFlags = 0U;
    const auto written = StdDocumentWriter::write(
        document, { StdPlatform::Dreamcast, StdCompression::None });
    ASSERT_TRUE(written.ok());
    EXPECT_EQ(EndianReader(written.bytes, Endian::Little).read_u16(0x30U + 0x06U), 0U);
}

TEST(SpiceStdDocumentImporter, UnrecognizedRowsRemainEditableWithWarning) {
    auto bytes = makeActionRows(Endian::Little, 1U);
    EndianSpanWriter(bytes, Endian::Little).write_i16_at(0x12U, 9);
    const auto imported = StdDocumentImporter::importBytes(bytes);
    ASSERT_TRUE(imported.ok());
    EXPECT_TRUE(hasCode(imported.diagnostics, StdDiagnosticCode::UnrecognizedActionRowType));
    const auto& row = std::get<StdActionRowsContent>(imported.document->content).rows[0];
    EXPECT_TRUE(std::holds_alternative<StdUnrecognizedActionRowFields>(row.fields));
    EXPECT_EQ(row.rowType(), 9);
    const auto written = StdDocumentWriter::write(
        *imported.document, { StdPlatform::GameCube, StdCompression::None });
    ASSERT_TRUE(written.ok());
    const auto reparsed = StdDocumentImporter::importBytes(written.bytes);
    ASSERT_TRUE(reparsed.ok());
    EXPECT_EQ(*reparsed.document, *imported.document);
}

TEST(SpiceStdDocumentImporter, RejectsSerializedLoaderGeneratedActionTerminator) {
    auto bytes = makeActionRows(Endian::Little, 1U);
    EndianSpanWriter(bytes, Endian::Little).write_i16_at(0x12U, 3);
    const auto imported = StdDocumentImporter::importBytes(bytes, { .byteOrder = Endian::Little });
    EXPECT_FALSE(imported.ok());
    EXPECT_TRUE(hasCode(imported.diagnostics, StdDiagnosticCode::MalformedActionRows));
}

TEST(SpiceStdDocumentImporter, MisalignedTypedPayloadWarnsWithoutInventingPadding) {
    auto bytes = makeEntryTable(Endian::Little);
    bytes.insert(bytes.begin() + 0x30, 0x7fU);
    EndianSpanWriter writer(bytes, Endian::Little);
    writer.write_u32_at(0x0cU, static_cast<std::uint32_t>(bytes.size() - 0x10U));
    writer.write_u32_at(0x1cU, 0x21U);
    const auto imported = StdDocumentImporter::importBytes(bytes);
    ASSERT_TRUE(imported.ok());
    EXPECT_TRUE(hasCode(imported.diagnostics, StdDiagnosticCode::KnownPayloadMisaligned));
    const auto validated = StdDocumentValidator::validate(*imported.document,
        { StdPlatform::Dreamcast, StdCompression::None }, &imported.receipt);
    EXPECT_TRUE(validated.ok());
    EXPECT_TRUE(hasCode(validated.diagnostics, StdDiagnosticCode::KnownPayloadMisaligned));
    const auto written = StdDocumentWriter::write(*imported.document,
        { StdPlatform::Dreamcast, StdCompression::None }, &imported.receipt);
    ASSERT_TRUE(written.ok());
    EXPECT_EQ(written.bytes, bytes);
}

TEST(SpiceStdDocumentImporter, AuthoritativeByteOrderDoesNotRetry) {
    const auto bytes = makeActionRows(Endian::Little);
    const auto correct = StdDocumentImporter::importBytes(bytes, { .byteOrder = Endian::Little });
    ASSERT_TRUE(correct.ok());
    EXPECT_EQ(correct.receipt.byteOrderSelection, StdByteOrderSelection::CallerSpecified);
    const auto wrong = StdDocumentImporter::importBytes(bytes, { .byteOrder = Endian::Big });
    ASSERT_TRUE(wrong.ok());
    EXPECT_TRUE(std::holds_alternative<StdOpaqueContent>(wrong.document->content));
    EXPECT_EQ(wrong.receipt.byteOrder, Endian::Big);

    std::vector<std::uint8_t> malformedBytes(0x10U, 0U);
    const auto malformed = StdDocumentImporter::importBytes(malformedBytes, { .byteOrder = Endian::Little });
    EXPECT_FALSE(malformed.ok());
    EXPECT_TRUE(hasCode(malformed.diagnostics, StdDiagnosticCode::MalformedActionRows));
}

TEST(SpiceStdDocumentImporter, DistinguishesMalformedUnknownAndAmbiguousInputs) {
    std::vector<std::uint8_t> zeroRows(0x10U, 0U);
    const auto malformed = StdDocumentImporter::importBytes(zeroRows, { .byteOrder = Endian::Little });
    EXPECT_FALSE(malformed.ok());
    EXPECT_TRUE(hasCode(malformed.diagnostics, StdDiagnosticCode::MalformedActionRows));

    const std::vector<std::uint8_t> unknown{ 1U, 2U, 3U, 4U, 5U };
    const auto undetectable = StdDocumentImporter::importBytes(unknown);
    EXPECT_FALSE(undetectable.ok());
    EXPECT_TRUE(hasCode(undetectable.diagnostics, StdDiagnosticCode::ByteOrderUndetectable));
    const auto preserved = StdDocumentImporter::importBytes(unknown, { .byteOrder = Endian::Big });
    ASSERT_TRUE(preserved.ok());
    EXPECT_TRUE(std::holds_alternative<StdOpaqueContent>(preserved.document->content));

    auto ambiguous = makeActionRows(Endian::Little, 1U);
    EndianSpanWriter writer(ambiguous, Endian::Little);
    writer.write_u16_at(0x00U, 1U);
    writer.write_u16_at(0x02U, 4U);
    writer.write_u32_at(0x08U, 1U);
    writer.write_u32_at(0x0cU, 0x18U);
    writer.write_i16_at(0x10U, -1);
    const auto ambiguousResult = StdDocumentImporter::importBytes(ambiguous, { .byteOrder = Endian::Little });
    EXPECT_FALSE(ambiguousResult.ok());
    EXPECT_TRUE(hasCode(ambiguousResult.diagnostics, StdDiagnosticCode::LayoutAmbiguous));
}

TEST(SpiceStdDocumentImporter, AcceptsTerminatorOnlyEntryTable) {
    const auto imported = StdDocumentImporter::importBytes(makeTerminatorOnly(Endian::Big));
    ASSERT_TRUE(imported.ok());
    const auto& table = std::get<StdEntryTableContent>(imported.document->content);
    EXPECT_TRUE(table.records.empty());
    EXPECT_EQ(table.terminator.negativeLocation, -1);
    const auto written = StdDocumentWriter::write(*imported.document,
        { StdPlatform::GameCube, StdCompression::None });
    ASSERT_TRUE(written.ok());
    EXPECT_EQ(written.bytes, makeTerminatorOnly(Endian::Big));
}

TEST(SpiceStdDocumentWriter, ActionRowEditsSurviveAllTargets) {
    auto imported = StdDocumentImporter::importBytes(makeActionRows(Endian::Little));
    ASSERT_TRUE(imported.ok());
    auto& rows = std::get<StdActionRowsContent>(imported.document->content);
    const auto& neutral = std::get<StdType0ActionRowFields>(rows.rows[0].fields);
    EXPECT_EQ(neutral.verticalExtentOverrideCode, 50);
    EXPECT_EQ(neutral.defaultPlanarMovementStep.bits, 0x3f800000U);
    EXPECT_EQ(neutral.turningStepOrThreshold.bits, 0x40000000U);
    std::get<StdType0ActionRowFields>(rows.rows[0].fields).raw08 = 0x89abcdefU;
    std::get<StdMotionActionRowFields>(rows.rows[1].fields).motionFrameIncrement.bits = 0x13572468U;
    const auto expected = *imported.document;
    for (const auto platform : { StdPlatform::Dreamcast, StdPlatform::GameCube }) {
        for (const auto compression : { StdCompression::None, StdCompression::Aklz }) {
            const auto written = StdDocumentWriter::write(expected, { platform, compression });
            ASSERT_TRUE(written.ok());
            const auto reparsed = StdDocumentImporter::importBytes(written.bytes);
            ASSERT_TRUE(reparsed.ok());
            EXPECT_EQ(*reparsed.document, expected);
        }
    }
}

TEST(SpiceStdDocumentWriter, TypedSystemCameraEditsSurviveAllTargetsWithoutReceipt) {
    auto imported = StdDocumentImporter::importBytes(makeEntryTable(Endian::Big));
    ASSERT_TRUE(imported.ok());
    auto& table = std::get<StdEntryTableContent>(imported.document->content);
    auto& payload = std::get<StdSystemCameraPayload>(table.payloads[0].content);
    payload.requestedMode = 99;
    payload.cameraBehaviorFlags ^= 0x80000000U;
    const auto expected = *imported.document;
    for (const auto platform : { StdPlatform::Dreamcast, StdPlatform::GameCube }) {
        for (const auto compression : { StdCompression::None, StdCompression::Aklz }) {
            const auto written = StdDocumentWriter::write(expected, { platform, compression });
            ASSERT_TRUE(written.ok());
            const auto reparsed = StdDocumentImporter::importBytes(written.bytes);
            ASSERT_TRUE(reparsed.ok());
            EXPECT_EQ(*reparsed.document, expected);
        }
    }
}

TEST(SpiceStdDocumentWriter, PreservesFileTrailerOutsideDeclaredSpan) {
    const auto declared = makeEntryTable(Endian::Little);
    auto bytes = declared;
    for (std::uint8_t value = 0U; value < 48U; ++value) bytes.push_back(static_cast<std::uint8_t>(0xa0U + value));
    auto imported = StdDocumentImporter::importBytes(bytes);
    ASSERT_TRUE(imported.ok());
    auto& table = std::get<StdEntryTableContent>(imported.document->content);
    ASSERT_TRUE(table.fileTrailer.has_value());
    EXPECT_EQ(table.fileTrailer->bytes.size(), 48U);
    EXPECT_EQ(imported.receipt.opaqueEvidence.fileTrailerId, table.fileTrailer->id);
    EXPECT_TRUE(imported.receipt.opaqueEvidence.fileTrailerSha256.has_value());
    const auto originalDocument = *imported.document;

    EXPECT_FALSE(StdDocumentWriter::write(*imported.document,
        { StdPlatform::Dreamcast, StdCompression::None }).ok());
    const auto written = StdDocumentWriter::write(originalDocument,
        { StdPlatform::Dreamcast, StdCompression::Aklz }, &imported.receipt);
    ASSERT_TRUE(written.ok());
    const auto decoded = spice::compression::aklz::decompress(written.bytes);
    ASSERT_TRUE(decoded.ok());
    const EndianReader reader(decoded.bytes, Endian::Little);
    EXPECT_EQ(reader.read_u32(0x0cU), declared.size() - 0x10U);
    EXPECT_EQ(decoded.bytes.size(), bytes.size());
    EXPECT_EQ(decoded.bytes[declared.size()], 0xa0U);
    const auto reparsed = StdDocumentImporter::importBytes(written.bytes);
    ASSERT_TRUE(reparsed.ok());
    EXPECT_EQ(*reparsed.document, originalDocument);

    table.fileTrailer->bytes[0] ^= 0xffU;
    const auto modified = StdDocumentWriter::write(*imported.document,
        { StdPlatform::Dreamcast, StdCompression::None }, &imported.receipt);
    EXPECT_FALSE(modified.ok());
    EXPECT_TRUE(hasCode(modified.diagnostics, StdDiagnosticCode::ReceiptMismatch));

    const auto crossEndian = StdDocumentWriter::write(originalDocument,
        { StdPlatform::GameCube, StdCompression::None }, &imported.receipt);
    EXPECT_FALSE(crossEndian.ok());
    EXPECT_TRUE(hasCode(crossEndian.diagnostics, StdDiagnosticCode::OpaqueByteOrderMismatch));
}

TEST(SpiceStdDocumentWriter, SparcIsEditableRelocatableAndCrossEndianWithoutReceipt) {
    const auto sourceDocument = sampleTypedDocument(kStdSparcCombinedType);
    const auto source = StdDocumentWriter::write(sourceDocument,
        { StdPlatform::Dreamcast, StdCompression::None });
    ASSERT_TRUE(source.ok());
    auto imported = StdDocumentImporter::importBytes(source.bytes);
    ASSERT_TRUE(imported.ok());
    auto& table = std::get<StdEntryTableContent>(imported.document->content);
    auto& sparc = std::get<StdSparcPayload>(table.payloads[0].content);
    sparc.spawnCount += 1;
    sparc.choices[0] = StdSparcChoice{ 44, 5 };
    sparc.choices[63] = StdSparcChoice{ -77, 0 };
    EXPECT_EQ(sparc.formattedMldFilename(), std::optional<std::string>{ "E5100000.MLD" });

    EXPECT_TRUE(StdDocumentWriter::write(*imported.document,
        { StdPlatform::Dreamcast, StdCompression::None }).ok());

    table.records.push_back(StdEntryRecord{
        .id = StdEntryRecordId{ 2U },
        .locationCode = 1,
        .opcode = 1,
    });
    const auto expected = *imported.document;
    const auto relocated = StdDocumentWriter::write(expected,
        { StdPlatform::GameCube, StdCompression::Aklz });
    ASSERT_TRUE(relocated.ok());
    const auto reparsed = StdDocumentImporter::importBytes(relocated.bytes);
    ASSERT_TRUE(reparsed.ok());
    EXPECT_EQ(*reparsed.document, expected);
}

TEST(SpiceStdDocument, FloatAndCommandMetadataAreBitExact) {
    const StdFloat32 negativeZero{ 0x80000000U };
    EXPECT_EQ(std::bit_cast<std::uint32_t>(negativeZero.value()), 0x80000000U);
    const StdFloat32 nanPayload{ 0x7fc01234U };
    EXPECT_TRUE(std::isnan(nanPayload.value()));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(nanPayload.value()), 0x7fc01234U);
    auto edited = StdFloat32::fromValue(1.5F);
    EXPECT_FLOAT_EQ(edited.value(), 1.5F);
    edited.setValue(-2.25F);
    EXPECT_FLOAT_EQ(edited.value(), -2.25F);

    struct Expected { std::uint32_t type; std::string_view name; std::uint32_t size; bool typed; };
    const std::array expected{
        Expected{ kStdSparcCombinedType, "SPARC", kStdSparcPayloadSize, true },
        Expected{ kStdPutModelCombinedType, "PUTMODEL", kStdPutModelPayloadSize, true },
        Expected{ kStdSetCommandCombinedType, "SET COMMAND", kStdSetCommandPayloadSize, true },
        Expected{ kStdMotionPauseCombinedType, "MOTION PAUSE", kStdMotionPausePayloadSize, true },
        Expected{ kStdCollisionBoxCombinedType, "COLISION BOX", kStdCollisionBoxPayloadSize, true },
        Expected{ kStdMoveModelCombinedType, "MOVE MODEL", kStdMoveModelPayloadSize, true },
        Expected{ kStdHitWeaponCombinedType, "HIT WEAPON", kStdHitWeaponPayloadSize, true },
        Expected{ kStdPointLightCombinedType, "POINT LIGHT", kStdPointLightPayloadSize, true },
        Expected{ kStdSystemCameraCombinedType, "SYSTEM CAMERA", kStdSystemCameraPayloadSize, true },
        Expected{ kStdEffectWaitCombinedType, "EFFECT WAIT", kStdEffectWaitPayloadSize, true },
        Expected{ kStdSeRequestCombinedType, "SE REQUEST", kStdSeRequestPayloadSize, true },
    };
    for (const auto& item : expected) {
        const auto* descriptor = findStdCommandDescriptor(item.type);
        ASSERT_NE(descriptor, nullptr);
        EXPECT_EQ(descriptor->binaryName, item.name);
        EXPECT_EQ(descriptor->loaderPayloadSize, item.size);
        EXPECT_EQ(descriptor->hasTypedPayloadCodec, item.typed);
    }
    EXPECT_EQ(findStdCommandDescriptor(0xdeadbeefU), nullptr);
}

TEST(SpiceStdDocument, ResearchBackedSemanticHelpersPreserveRawValues) {
    StdType0ActionRowFields neutral{};
    EXPECT_FALSE(neutral.verticalExtentOverrideWorldUnits().has_value());
    neutral.verticalExtentOverrideCode = -4;
    ASSERT_TRUE(neutral.verticalExtentOverrideWorldUnits().has_value());
    EXPECT_FLOAT_EQ(*neutral.verticalExtentOverrideWorldUnits(), -15.0F);

    StdHandledPayloadPrefix prefix{};
    prefix.commandFlags = 0xa000U;
    EXPECT_TRUE(prefix.optsIntoGlobalEventCleanupOrAbort());

    StdSparcPayload sparc{};
    sparc.mldFilenameKey = 158U;
    EXPECT_EQ(sparc.formattedMldFilename(), std::optional<std::string>{ "E0000158.MLD" });
    sparc.mldFilenameKey = 10001042U;
    EXPECT_EQ(sparc.formattedMldFilename(), std::optional<std::string>{ "MB042.MLD" });
    sparc.mldFilenameKey = 9999999U;
    EXPECT_FALSE(sparc.formattedMldFilename().has_value());
    sparc.mldFilenameKey = 10003000U;
    EXPECT_FALSE(sparc.formattedMldFilename().has_value());

    StdPutModelPayload putModel{};
    putModel.modelFlags = 0x0c0020d4U;
    EXPECT_EQ(putModel.anchorCoordinateMode(), 4U);
    EXPECT_EQ(putModel.fallbackDrawMode(), std::optional<std::uint8_t>{ 1U });
    EXPECT_TRUE(putModel.usesExtraNinjaRenderOption());
    EXPECT_TRUE(putModel.usesFallbackRenderOrSuppressesMode0Anchor());
    EXPECT_TRUE(putModel.holdsFinalMotionFrame());

    StdMotionPausePayload motionPause{};
    motionPause.pauseFlags = 0x8000U;
    EXPECT_TRUE(motionPause.latchesPauseAndRequestsTargetState());

    StdMoveModelPayload moveModel{};
    moveModel.modelFlags = 0x200U;
    moveModel.conditionFlags = 0xb000U;
    EXPECT_TRUE(moveModel.rotatesAlternateChildFromVelocity());
    EXPECT_TRUE(moveModel.usesInstructionFlagCondition());
    EXPECT_TRUE(moveModel.usesAlternateParticleChildWhenConditionTrue());
    EXPECT_TRUE(moveModel.usesConditionPolarityAndStartupGate());

    StdHitWeaponPayload hitWeapon{};
    hitWeapon.targetStateFlagMode = 2;
    EXPECT_EQ(hitWeapon.targetStateWorksheetFlagMask(), std::optional<std::uint32_t>{ 0x40000000U });

    StdPointLightPayload pointLight{};
    pointLight.lightFlags = 0x04000002U;
    EXPECT_EQ(pointLight.anchorCoordinateMode(), 2U);
    EXPECT_TRUE(pointLight.suppressesMode0AnchorMatrix());

    StdCollisionBoxPayload collision{};
    collision.modeFlags = 2U;
    collision.collisionFlags = 0xfc00U;
    EXPECT_TRUE(collision.registersModelNode());
    EXPECT_TRUE(collision.loadsHitCombatantMotion());
    EXPECT_TRUE(collision.usesFiveHorizontalProbes());
    EXPECT_TRUE(collision.usesThreeHorizontalProbes());
    EXPECT_TRUE(collision.enablesCombatantInteraction());
    EXPECT_TRUE(collision.continuesAfterRegisteredHit());
    EXPECT_TRUE(collision.forcesFinishAfterHit());

    StdSystemCameraPayload camera{};
    camera.cameraBehaviorFlags = 0x7f000800U;
    EXPECT_TRUE(camera.selectsActorOrTarget());
    EXPECT_TRUE(camera.usesAlternatePositionOrLookSource());
    EXPECT_TRUE(camera.continuouslyTracksOrUpdatesAngle());
    EXPECT_TRUE(camera.suppressesOrdinaryCombatantStateCalls());
    EXPECT_TRUE(camera.usesComputedOrRandomYaw());
    EXPECT_TRUE(camera.usesModeParameterAsOrbitStep());
    EXPECT_TRUE(camera.hasCombatantStateTerminationLatch());
    EXPECT_TRUE(camera.selectsAlternateSetupHelper());

    StdSeRequestPayload sound{};
    sound.soundBankGroupRaw = static_cast<std::int16_t>(0xab00U);
    EXPECT_TRUE(sound.usesDefaultSoundBankGroup());
    sound.setSoundBankGroupSelector(-7);
    EXPECT_EQ(sound.soundBankGroupSelector(), -7);
    EXPECT_EQ(static_cast<std::uint16_t>(sound.soundBankGroupRaw) & 0xff00U, 0xab00U);
}

TEST(SpiceStdDocument, AllocatesStableIdsIndependentOfOrder) {
    auto imported = StdDocumentImporter::importBytes(makeActionRows(Endian::Little));
    ASSERT_TRUE(imported.ok());
    auto& rows = std::get<StdActionRowsContent>(imported.document->content).rows;
    const auto firstId = rows[0].id;
    const auto secondId = rows[1].id;
    std::swap(rows[0], rows[1]);
    EXPECT_EQ(rows[0].id, secondId);
    EXPECT_EQ(rows[1].id, firstId);
    const auto newId = imported.document->allocateActionRowId();
    EXPECT_GT(newId.value, std::max(firstId.value, secondId.value));
    rows.push_back(StdActionRow{ .id = newId });
    rows.erase(rows.begin() + 1);
    EXPECT_EQ(rows[0].id, secondId);
    EXPECT_EQ(rows[1].id, newId);
}

TEST(SpiceStdDocumentValidator, RejectsDuplicateDanglingAndDuplicateLayoutOwnership) {
    auto imported = StdDocumentImporter::importBytes(makeEntryTable(Endian::Big));
    ASSERT_TRUE(imported.ok());
    auto duplicate = *imported.document;
    auto& duplicateTable = std::get<StdEntryTableContent>(duplicate.content);
    duplicateTable.payloads.push_back(duplicateTable.payloads.front());
    EXPECT_TRUE(hasCode(StdDocumentValidator::validate(duplicate,
        { StdPlatform::GameCube, StdCompression::None }).diagnostics, StdDiagnosticCode::DuplicateId));

    auto dangling = *imported.document;
    std::get<StdEntryTableContent>(dangling.content).records[0].payload = StdEntryPayloadId{ 999U };
    EXPECT_TRUE(hasCode(StdDocumentValidator::validate(dangling,
        { StdPlatform::GameCube, StdCompression::None }).diagnostics, StdDiagnosticCode::DanglingReference));

    auto repeated = *imported.document;
    auto& repeatedTable = std::get<StdEntryTableContent>(repeated.content);
    repeatedTable.payloadLayout.push_back(repeatedTable.payloadLayout.front());
    EXPECT_TRUE(hasCode(StdDocumentValidator::validate(repeated,
        { StdPlatform::GameCube, StdCompression::None }).diagnostics, StdDiagnosticCode::DuplicateLayoutOwnership));
}

TEST(SpiceStdDocumentValidator, RequiresDedicatedVariantsForEstablishedRowTypes) {
    auto imported = StdDocumentImporter::importBytes(makeActionRows(Endian::Little));
    ASSERT_TRUE(imported.ok());
    auto& row = std::get<StdActionRowsContent>(imported.document->content).rows[0];
    row.fields = StdUnrecognizedActionRowFields{ .rowType = 1 };
    const auto validation = StdDocumentValidator::validate(*imported.document,
        { StdPlatform::Dreamcast, StdCompression::None });
    EXPECT_FALSE(validation.ok());
    EXPECT_TRUE(hasCode(validation.diagnostics, StdDiagnosticCode::InvalidDocument));
}

TEST(SpiceStdDocumentValidator, RejectsUnsafeModelTimelineAndPointLightSlot) {
    auto putModel = sampleTypedDocument(kStdPutModelCombinedType);
    auto& putPayload = std::get<StdPutModelPayload>(
        std::get<StdEntryTableContent>(putModel.content).payloads[0].content);
    putPayload.timelineRepeatFirst = -1;
    putPayload.timelineRepeatLast = 2;
    const auto invalidTimeline = StdDocumentValidator::validate(
        putModel, { StdPlatform::Dreamcast, StdCompression::None });
    EXPECT_FALSE(invalidTimeline.ok());
    EXPECT_TRUE(hasCode(invalidTimeline.diagnostics, StdDiagnosticCode::InvalidTimelineRepeatRange));

    auto pointLight = sampleTypedDocument(kStdPointLightCombinedType);
    auto& lightPayload = std::get<StdPointLightPayload>(
        std::get<StdEntryTableContent>(pointLight.content).payloads[0].content);
    lightPayload.lightSlot = 4;
    const auto invalidLight = StdDocumentValidator::validate(
        pointLight, { StdPlatform::GameCube, StdCompression::None });
    EXPECT_FALSE(invalidLight.ok());
    EXPECT_TRUE(hasCode(invalidLight.diagnostics, StdDiagnosticCode::UnsupportedPointLightSlot));
}

TEST(SpiceStdDocumentWriter, RebuildsOffsetsAfterOpaquePayloadResizeAndPreservesGaps) {
    auto imported = StdDocumentImporter::importBytes(makeEntryTable(Endian::Little, false, true));
    ASSERT_TRUE(imported.ok());
    auto& table = std::get<StdEntryTableContent>(imported.document->content);
    ASSERT_EQ(table.opaqueFragments.size(), 1U);
    ASSERT_TRUE(std::holds_alternative<StdOpaquePayload>(table.payloads[0].content));
    std::get<StdOpaquePayload>(table.payloads[0].content).bytes.push_back(0xddU);
    const auto expected = *imported.document;
    EXPECT_FALSE(StdDocumentWriter::write(expected,
        { StdPlatform::Dreamcast, StdCompression::None }).ok());
    const auto written = StdDocumentWriter::write(expected,
        { StdPlatform::Dreamcast, StdCompression::Aklz }, &imported.receipt);
    ASSERT_TRUE(written.ok());
    const auto reparsed = StdDocumentImporter::importBytes(written.bytes);
    ASSERT_TRUE(reparsed.ok());
    EXPECT_EQ(*reparsed.document, expected);
    const auto crossEndian = StdDocumentWriter::write(expected,
        { StdPlatform::GameCube, StdCompression::None }, &imported.receipt);
    EXPECT_FALSE(crossEndian.ok());
    EXPECT_TRUE(hasCode(crossEndian.diagnostics, StdDiagnosticCode::OpaqueByteOrderMismatch));
}

TEST(SpiceStdDocumentWriter, TopLevelOpaqueRequiresExactReceiptButAllowsWrapperChange) {
    const std::vector<std::uint8_t> bytes{ 9U, 8U, 7U, 6U, 5U };
    auto imported = StdDocumentImporter::importBytes(bytes, { .byteOrder = Endian::Big });
    ASSERT_TRUE(imported.ok());
    EXPECT_FALSE(StdDocumentWriter::write(*imported.document,
        { StdPlatform::GameCube, StdCompression::None }).ok());
    const auto compressedOutput = StdDocumentWriter::write(*imported.document,
        { StdPlatform::GameCube, StdCompression::Aklz }, &imported.receipt);
    ASSERT_TRUE(compressedOutput.ok());
    const auto decoded = spice::compression::aklz::decompress(compressedOutput.bytes);
    ASSERT_TRUE(decoded.ok());
    EXPECT_EQ(decoded.bytes, bytes);
    std::get<StdOpaqueContent>(imported.document->content).decodedBytes[0] ^= 1U;
    const auto modified = StdDocumentWriter::write(*imported.document,
        { StdPlatform::GameCube, StdCompression::None }, &imported.receipt);
    EXPECT_FALSE(modified.ok());
    EXPECT_TRUE(hasCode(modified.diagnostics, StdDiagnosticCode::ReceiptMismatch));
}

TEST(SpiceStdJsonExporter, SeparatesReceiptFromSemanticDocument) {
    const auto imported = StdDocumentImporter::importBytes(makeActionRows(Endian::Little));
    ASSERT_TRUE(imported.ok());
    const auto json = StdJsonExporter{}.toJson(imported);
    EXPECT_NE(json.find("\"schema\": \"spice_std_json_export\""), std::string::npos);
    EXPECT_NE(json.find("\"schemaVersion\": 5"), std::string::npos);
    EXPECT_EQ(json.find("spice_std_document_v5"), std::string::npos);
    EXPECT_NE(json.find("\"receipt\""), std::string::npos);
    EXPECT_NE(json.find("\"document\""), std::string::npos);
    EXPECT_NE(json.find("\"selectorCallbackIndex\""), std::string::npos);
    EXPECT_NE(json.find("\"verticalExtentOverrideCode\""), std::string::npos);
    EXPECT_EQ(json.find("rawBytesHex"), std::string::npos);
    EXPECT_EQ(json.find("sourceBoundPayloads"), std::string::npos);
}

TEST(SpiceStdJsonExporter, EmitsStructuredModelAndLightPayloadsInExportSchemaV5) {
    const auto putModelJson = StdJsonExporter{}.toJson(sampleTypedDocument(kStdPutModelCombinedType));
    EXPECT_NE(putModelJson.find("\"schema\": \"spice_std_json_export\""), std::string::npos);
    EXPECT_NE(putModelJson.find("\"schemaVersion\": 5"), std::string::npos);
    EXPECT_EQ(putModelJson.find("spice_std_document_v5"), std::string::npos);
    EXPECT_NE(putModelJson.find("\"kind\":\"putModel\""), std::string::npos);
    EXPECT_NE(putModelJson.find("\"modelTimeline\""), std::string::npos);
    EXPECT_NE(putModelJson.find("\"rawTail192Hex\":\"897b\""), std::string::npos);

    const auto pointLightJson = StdJsonExporter{}.toJson(sampleTypedDocument(kStdPointLightCombinedType));
    EXPECT_NE(pointLightJson.find("\"kind\":\"pointLight\""), std::string::npos);
    EXPECT_NE(pointLightJson.find("\"rgbWave\""), std::string::npos);
    EXPECT_NE(pointLightJson.find("\"attenuationARamp\""), std::string::npos);
}
