#include "StdDocumentWriter.h"

#include "../Compression/Aklz.h"
#include "../SpiceRoot/Binary/EndianWriter.h"

#include <algorithm>
#include <map>
#include <type_traits>

namespace spice::stdfile {
namespace {

using spice::root::EndianWriter;

void writePrefix(EndianWriter& writer, const StdHandledPayloadPrefix& value) {
    writer.write_i16(value.primaryActionKey);
    writer.write_i16(value.applicabilitySelectorRaw);
    writer.write_i16(value.secondaryActionKey);
    writer.write_u16(value.commandFlags);
}

template <std::size_t Size>
void writeFloats(EndianWriter& writer, const std::array<StdFloat32, Size>& values) {
    for (const auto& value : values) writer.write_u32(value.bits);
}

void writeModelTimeline(
    EndianWriter& writer, const std::array<StdModelTimelineEntry, 64U>& values) {
    for (const auto& value : values) {
        writer.write_i16(value.modelIndex);
        writer.write_i16(value.durationTicks);
    }
}

void writePointLightWave(EndianWriter& writer, const StdPointLightWave& value) {
    writer.write_i16(value.startFrame);
    writer.write_i16(value.endFrame);
    writer.write_u32(value.amplitude.bits);
    writer.write_i16(value.phaseStep);
    writer.write_i16(value.phaseBase);
}

void writePointLightRamp(EndianWriter& writer, const StdPointLightRamp& value) {
    writer.write_u32(value.slope.bits);
    writer.write_i16(value.startFrame);
    writer.write_i16(value.endFrame);
}

std::vector<std::uint8_t> encodePayload(const StdEntryPayload& payload, const spice::root::Endian endian) {
    if (const auto* opaque = std::get_if<StdOpaquePayload>(&payload.content)) return opaque->bytes;
    EndianWriter writer(endian);
    std::visit([&](const auto& value) {
        using Payload = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Payload, StdSparcPayload>) {
            writer.reserve(kStdSparcPayloadSize);
            writePrefix(writer, value.common);
            writer.write_u32(value.mldFilenameKey);
            writer.write_i16(value.setupWord0);
            writer.write_i16(value.setupWord1);
            writer.write_u32(value.childMetadataRaw);
            writer.write_u32(value.behaviorFlags);
            writer.write_i16(value.durationOrActivationCount);
            writer.write_i16(value.childParameterS16);
            writer.write_u16(value.raw1c);
            writer.write_u16(value.reserved1e);
            writeFloats(writer, value.positionOrOffset);
            writeFloats(writer, value.velocityVector);
            writer.write_i16(value.spawnCount);
            writer.write_i16(value.randomRange);
            writer.write_i16(value.spawnMode);
            writer.write_i16(value.raw3e);
            writer.write_u32(value.childDivisorOrParameter.bits);
            writeFloats(writer, value.childParameters44);
            writeFloats(writer, value.vectorMultipliers);
            writeFloats(writer, value.secondaryVector);
            for (const auto& choice : value.choices) {
                writer.write_i16(choice.value);
                writer.write_i16(choice.weight);
            }
        } else if constexpr (std::is_same_v<Payload, StdPutModelPayload>) {
            writer.reserve(kStdPutModelPayloadSize);
            writePrefix(writer, value.common);
            writer.write_u32(value.mldResourceId);
            writer.write_bytes(value.reserved0c);
            writer.write_u32(value.modelFlags);
            writer.write_i16(value.activationFrame);
            writer.write_i16(value.fadeOutStartFrame);
            writer.write_i16(value.fadeInStartFrame);
            writer.write_i16(value.fadeInFrames);
            writer.write_i16(value.fadeOutFrames);
            writer.write_u16(value.anchorNodeIndex);
            writeFloats(writer, value.initialPosition);
            writeFloats(writer, value.initialEulerDegrees);
            writeModelTimeline(writer, value.modelTimeline);
            writer.write_i16(value.timelineRepeatFirst);
            writer.write_i16(value.timelineRepeatLast);
            writeFloats(writer, value.positionDeltaPerTick);
            writeFloats(writer, value.eulerDeltaDegreesPerTick);
            writeFloats(writer, value.scaleDeltaA);
            writer.write_i16(value.scaleDeltaAStartFrame);
            writer.write_i16(value.scaleDeltaATicks);
            writeFloats(writer, value.initialScale);
            writeFloats(writer, value.scaleDeltaB);
            writer.write_i16(value.scaleDeltaBStartFrame);
            writer.write_i16(value.scaleDeltaBTicks);
            writer.write_i16(value.textureStripSelectorA);
            writer.write_i16(value.textureStripSelectorB);
            writer.write_u32(value.motionFrameDelta.bits);
            writer.write_i16(value.eulerDeltaStartFrame);
            writer.write_i16(value.eulerDeltaEndFrame);
            writer.write_i16(value.fallbackRenderParam);
            writer.write_i16(value.positionDeltaStartFrame);
            writer.write_i16(value.positionDeltaEndFrame);
            writer.write_bytes(value.rawTail192);
        } else if constexpr (std::is_same_v<Payload, StdSetCommandPayload>) {
            writer.reserve(kStdSetCommandPayloadSize);
            writePrefix(writer, value.common);
            writer.write_bytes(value.reserved08);
            writer.write_u32(value.behaviorFlags);
            writer.write_i16(value.preApplicationCountdown);
            writer.write_i16(value.pendingTargetCommandOrStateCode);
            writer.write_bytes(value.reserved18);
        } else if constexpr (std::is_same_v<Payload, StdMotionPausePayload>) {
            writer.reserve(kStdMotionPausePayloadSize);
            writePrefix(writer, value.common);
            writer.write_bytes(value.reserved08);
            writer.write_u32(value.pauseFlags);
            writer.write_i16(value.pauseStartFrame);
            writer.write_i16(value.pauseEndFrame);
        } else if constexpr (std::is_same_v<Payload, StdCollisionBoxPayload>) {
            writer.reserve(kStdCollisionBoxPayloadSize);
            writePrefix(writer, value.common);
            writer.write_bytes(value.reserved08);
            writer.write_u32(value.modeFlags);
            writer.write_i16(value.startFrame);
            writer.write_i16(value.endFrameOrLifetime);
            writer.write_u16(value.modelNodeOrdinal);
            writer.write_u16(value.reserved1a);
            writeFloats(writer, value.positionVector);
            writeFloats(writer, value.velocityVector);
            writer.write_u32(value.collisionFlags);
        } else if constexpr (std::is_same_v<Payload, StdMoveModelPayload>) {
            writer.reserve(kStdMoveModelPayloadSize);
            writePrefix(writer, value.common);
            writer.write_u32(value.mldResourceId);
            writer.write_bytes(value.reserved0c);
            writer.write_u32(value.modelFlags);
            writer.write_u16(value.conditionFlags);
            writer.write_i16(value.activationFrame);
            writer.write_i16(value.fadeOutStartFrame);
            writer.write_i16(value.fadeInStartFrame);
            writer.write_i16(value.fadeInFrames);
            writer.write_i16(value.fadeOutFrames);
            writer.write_u16(value.anchorNodeIndex);
            writer.write_bytes(value.reserved22);
            writeFloats(writer, value.initialPosition);
            writeFloats(writer, value.initialEulerDegrees);
            writeModelTimeline(writer, value.modelTimeline);
            writer.write_i16(value.timelineRepeatFirst);
            writer.write_i16(value.timelineRepeatLast);
            writeFloats(writer, value.positionDeltaPerTick);
            writeFloats(writer, value.eulerDeltaDegreesPerTick);
            writeFloats(writer, value.scaleDelta);
            writer.write_i16(value.scaleDeltaStartFrame);
            writer.write_i16(value.scaleDeltaTicks);
            writeFloats(writer, value.initialScale);
            writer.write_i16(value.textureStripSelectorA);
            writer.write_i16(value.textureStripSelectorB);
            writer.write_u32(value.motionFrameDelta.bits);
            writer.write_i16(value.eulerDeltaStartFrame);
            writer.write_i16(value.eulerDeltaEndFrame);
            writer.write_i16(value.fallbackRenderParam);
            writer.write_bytes(value.reserved182);
            writeFloats(writer, value.alternateChildVector);
            writer.write_u32(value.alternateChildYAcceleration.bits);
            writer.write_i16(value.alternateChildFadeFrames);
            writer.write_i16(value.alternateChildFadeStartFrame);
            writer.write_i16(value.conditionalEffectStartFrame);
            writer.write_i16(value.conditionalEffectEndFrame);
            writer.write_i16(value.conditionalEffectInterval);
            writer.write_i16(value.conditionalChildFadeFrames);
        } else if constexpr (std::is_same_v<Payload, StdHitWeaponPayload>) {
            writer.reserve(kStdHitWeaponPayloadSize);
            writePrefix(writer, value.common);
            writer.write_u32(value.raw08);
            writer.write_bytes(value.reserved0c);
            writer.write_u32(value.modelFlags);
            writer.write_i16(value.activationFrame);
            writer.write_i16(value.pathStartFrame);
            writer.write_i16(value.pathDurationFrames);
            writer.write_i16(value.fadeInStartFrame);
            writer.write_i16(value.fadeInFrames);
            writer.write_i16(value.fadeOutFrames);
            writer.write_u16(value.anchorNodeIndex);
            writer.write_bytes(value.reserved22);
            writeFloats(writer, value.pathEndpointOffset);
            writeFloats(writer, value.initialEulerDegrees);
            writeFloats(writer, value.initialScale);
            writeModelTimeline(writer, value.modelTimeline);
            writer.write_i16(value.timelineRepeatFirst);
            writer.write_i16(value.timelineRepeatLast);
            writer.write_u32(value.pathAngleDeltaDegreesPerTick.bits);
            writeFloats(writer, value.eulerDeltaDegreesPerTick);
            writer.write_u32(value.raw15c.bits);
            writer.write_i16(value.textureStripSelectorA);
            writer.write_i16(value.textureStripSelectorB);
            writer.write_u32(value.motionFrameDelta.bits);
            writer.write_i16(value.eulerDeltaStartFrame);
            writer.write_i16(value.eulerDeltaEndFrame);
            writer.write_i16(value.childEffectInterval);
            writer.write_i16(value.childEffectFadeFrames);
            writer.write_i16(value.fallbackRenderParam);
            writer.write_i16(value.targetStateFlagMode);
        } else if constexpr (std::is_same_v<Payload, StdPointLightPayload>) {
            writer.reserve(kStdPointLightPayloadSize);
            writePrefix(writer, value.common);
            writer.write_u32(value.raw08);
            writer.write_bytes(value.reserved0c);
            writer.write_u32(value.lightFlags);
            writer.write_i16(value.activationFrame);
            writer.write_i16(value.fadeOutStartFrame);
            writer.write_i16(value.fadeInFrames);
            writer.write_i16(value.fadeOutFrames);
            writer.write_u16(value.anchorNodeIndex);
            writer.write_bytes(value.reserved1e);
            writeFloats(writer, value.position);
            writer.write_i16(value.lightSlot);
            writer.write_bytes(value.reserved2e);
            writeFloats(writer, value.rgb);
            writer.write_u32(value.attenuationParameterA.bits);
            writer.write_u32(value.attenuationParameterB.bits);
            writePointLightWave(writer, value.rgbWave);
            writePointLightWave(writer, value.attenuationWave);
            writer.write_i16(value.enablePulseInterval);
            writer.write_bytes(value.reserved5e);
            writePointLightRamp(writer, value.attenuationARamp);
            writePointLightRamp(writer, value.attenuationBRamp);
        } else if constexpr (std::is_same_v<Payload, StdSystemCameraPayload>) {
            writer.reserve(kStdSystemCameraPayloadSize);
            writePrefix(writer, value.common);
            writer.write_bytes(value.reserved08);
            writer.write_u32(value.cameraBehaviorFlags);
            writer.write_u32(value.cameraModeParameter.bits);
            writer.write_i16(value.startFrame);
            writer.write_u16(value.reserved1a);
            writer.write_i16(value.endFrame);
            writer.write_i16(value.holdFrameCount);
            writer.write_i16(value.stepFrameCount);
            writer.write_i16(value.requestedMode);
        } else if constexpr (std::is_same_v<Payload, StdEffectWaitPayload>) {
            writer.reserve(kStdEffectWaitPayloadSize);
            writer.write_i16(value.primaryActionKey);
            writer.write_i16(value.routeOrScopeRaw);
            writer.write_i16(value.secondaryActionKey);
            writer.write_u16(value.flagsRaw);
            writer.write_bytes(value.reserved08);
            writer.write_i16(value.waitValueRaw);
            writer.write_u16(value.reserved12);
        } else if constexpr (std::is_same_v<Payload, StdSeRequestPayload>) {
            writer.reserve(kStdSeRequestPayloadSize);
            writePrefix(writer, value.common);
            writer.write_bytes(value.reserved08);
            writer.write_u32(value.requestFlags);
            writer.write_bytes(value.reserved14);
            writer.write_i16(value.requestMode);
            writer.write_i16(value.playAtFrameOrDelay);
            writer.write_i16(value.mode4Timeout);
            writer.write_i16(value.soundBankGroupRaw);
            writer.write_i16(value.primaryCueSelector);
            writer.write_i16(value.outputSlotAgeOrLifetime);
            writer.write_i16(value.alternateCueSelector1);
            writer.write_i16(value.alternateCueSelector2);
            writer.write_u32(value.trailingRaw28);
        }
    }, payload.content);
    return writer.take_data();
}

void writeActionRow(EndianWriter& writer, const StdActionRow& row) {
    writer.write_i16(row.actionId);
    writer.write_i16(row.rowType());
    writer.write_i16(row.selectorCallbackIndex);
    std::visit([&](const auto& fields) {
        using Fields = std::decay_t<decltype(fields)>;
        if constexpr (std::is_same_v<Fields, StdMotionActionRowFields>) {
            writer.write_i16(fields.motionResourceSelector);
            writer.write_u32(fields.motionFlags);
        } else if constexpr (std::is_same_v<Fields, StdType0ActionRowFields>) {
            writer.write_i16(fields.reserved06);
            writer.write_u32(fields.raw08);
        } else {
            writer.write_i16(fields.raw06);
            writer.write_u32(fields.raw08);
        }
    }, row.fields);
    writer.write_i16(row.secondaryActionKey);
    std::visit([&](const auto& fields) {
        using Fields = std::decay_t<decltype(fields)>;
        if constexpr (std::is_same_v<Fields, StdType0ActionRowFields>) {
            writer.write_i16(fields.verticalExtentOverrideCode);
            writer.write_u32(fields.defaultPlanarMovementStep.bits);
            writer.write_u32(fields.turningStepOrThreshold.bits);
        } else if constexpr (std::is_same_v<Fields, StdMotionActionRowFields>) {
            writer.write_i16(fields.actionParameterS16);
            writer.write_u32(fields.timingOrTransitionScalar.bits);
            writer.write_u32(fields.motionFrameIncrement.bits);
        } else {
            writer.write_i16(fields.raw0e);
            writer.write_u32(fields.raw10.bits);
            writer.write_u32(fields.raw14.bits);
        }
    }, row.fields);
}

std::vector<std::uint8_t> serializeDecoded(const StdDocument& document, const spice::root::Endian endian) {
    if (const auto* opaque = std::get_if<StdOpaqueContent>(&document.content)) return opaque->decodedBytes;

    EndianWriter writer(endian);
    if (const auto* rows = std::get_if<StdActionRowsContent>(&document.content)) {
        writer.reserve(0x10U + rows->rows.size() * 0x18U);
        writer.write_u16(rows->rawCommandLow);
        writer.write_u16(rows->rawCommandHigh);
        writer.write_u32(rows->rawLoaderContextWord);
        writer.write_u32(static_cast<std::uint32_t>(rows->rows.size()));
        writer.write_u32(rows->rawRowTablePointerWord);
        for (const auto& row : rows->rows) writeActionRow(writer, row);
        return writer.take_data();
    }

    const auto& table = std::get<StdEntryTableContent>(document.content);
    const auto recordCount = table.records.size() + 1U;
    writer.resize(0x10U + recordCount * 0x10U, 0U);
    writer.write_u16_at(0x00U, static_cast<std::uint16_t>(recordCount));
    writer.write_u16_at(0x02U, table.kind);
    writer.write_u32_at(0x04U, table.rawHeader04);
    writer.write_u32_at(0x08U, table.rawHeader08);

    struct PayloadLocation { std::uint32_t relativeOffset{ 0U }; std::uint32_t size{ 0U }; };
    std::map<std::uint64_t, PayloadLocation> locations{};
    for (const auto& item : table.payloadLayout) {
        std::visit([&](const auto id) {
            using Id = std::remove_cv_t<decltype(id)>;
            if constexpr (std::is_same_v<Id, StdEntryPayloadId>) {
                const auto* payload = findEntryPayload(table, id);
                const auto bytes = encodePayload(*payload, endian);
                locations[id.value] = PayloadLocation{
                    static_cast<std::uint32_t>(writer.data().size() - 0x10U),
                    static_cast<std::uint32_t>(bytes.size()) };
                writer.write_bytes(bytes);
            } else {
                writer.write_bytes(findOpaqueFragment(table, id)->bytes);
            }
        }, item);
    }

    writer.write_u32_at(0x0cU, static_cast<std::uint32_t>(writer.data().size() - 0x10U));
    for (std::size_t index = 0U; index < table.records.size(); ++index) {
        const auto& record = table.records[index];
        const auto offset = 0x10U + index * 0x10U;
        writer.write_i16_at(offset + 0x00U, record.locationCode);
        writer.write_i16_at(offset + 0x02U, record.opcode);
        writer.write_u32_at(offset + 0x04U, record.raw04);
        if (record.payload.has_value()) {
            const auto location = locations.at(record.payload->value);
            writer.write_u32_at(offset + 0x08U, location.size);
            writer.write_u32_at(offset + 0x0cU, location.relativeOffset);
        }
    }
    const auto terminatorOffset = 0x10U + table.records.size() * 0x10U;
    writer.write_i16_at(terminatorOffset + 0x00U, table.terminator.negativeLocation);
    writer.write_i16_at(terminatorOffset + 0x02U, table.terminator.raw02);
    writer.write_u32_at(terminatorOffset + 0x04U, table.terminator.raw04);
    writer.write_u32_at(terminatorOffset + 0x08U, table.terminator.raw08);
    writer.write_u32_at(terminatorOffset + 0x0cU, table.terminator.raw0c);
    if (table.fileTrailer.has_value()) writer.write_bytes(table.fileTrailer->bytes);
    return writer.take_data();
}

} // namespace

bool StdDocumentWriteResult::ok() const noexcept {
    return !bytes.empty() && std::none_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.severity == StdDiagnosticSeverity::Error;
    });
}

StdDocumentWriteResult StdDocumentWriter::write(
    const StdDocument& document, const StdWriteTarget& target, const StdImportReceipt* receipt) {
    StdDocumentWriteResult result{};
    auto validation = StdDocumentValidator::validate(document, target, receipt);
    result.diagnostics = std::move(validation.diagnostics);
    if (!validation.ok()) return result;

    auto decoded = serializeDecoded(document, byteOrderFor(target.platform));
    if (target.compression == StdCompression::None) {
        result.bytes = std::move(decoded);
        return result;
    }
    auto encoded = spice::compression::aklz::compress(decoded);
    if (!encoded.ok()) {
        result.diagnostics.push_back(StdDocumentDiagnostic{
            StdDiagnosticCode::AklzEncodeFailed, StdDiagnosticSeverity::Error,
            "AKLZ compression failed: " + std::string(spice::compression::aklz::errorToString(encoded.error)), std::nullopt });
        return result;
    }
    result.bytes = std::move(encoded.bytes);
    return result;
}

} // namespace spice::stdfile
