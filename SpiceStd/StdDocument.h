#pragma once

#include <array>
#include <bit>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace spice::stdfile {

template <typename Tag>
struct StdId {
    std::uint64_t value{ 0U };
    [[nodiscard]] explicit operator bool() const noexcept { return value != 0U; }
    auto operator<=>(const StdId&) const = default;
};

using StdActionRowId = StdId<struct StdActionRowIdTag>;
using StdEntryRecordId = StdId<struct StdEntryRecordIdTag>;
using StdEntryPayloadId = StdId<struct StdEntryPayloadIdTag>;
using StdEntryTerminatorId = StdId<struct StdEntryTerminatorIdTag>;
using StdOpaqueFragmentId = StdId<struct StdOpaqueFragmentIdTag>;

struct StdFloat32 {
    std::uint32_t bits{ 0U };

    [[nodiscard]] float value() const noexcept { return std::bit_cast<float>(bits); }
    void setValue(const float value) noexcept { bits = std::bit_cast<std::uint32_t>(value); }
    [[nodiscard]] static StdFloat32 fromValue(const float value) noexcept {
        return StdFloat32{ std::bit_cast<std::uint32_t>(value) };
    }
    bool operator==(const StdFloat32&) const = default;
};

struct StdType0ActionRowFields {
    std::int16_t reserved06{ 0 };
    std::uint32_t raw08{ 0U };
    std::int16_t verticalExtentOverrideCode{ 0 };
    StdFloat32 defaultPlanarMovementStep{};
    StdFloat32 turningStepOrThreshold{};

    [[nodiscard]] std::optional<float> verticalExtentOverrideWorldUnits() const noexcept {
        if (verticalExtentOverrideCode == 0) return std::nullopt;
        return static_cast<float>(verticalExtentOverrideCode) * 3.75F;
    }
    bool operator==(const StdType0ActionRowFields&) const = default;
};

struct StdMotionActionRowFields {
    std::int16_t motionResourceSelector{ 0 };
    std::uint32_t motionFlags{ 0U };
    std::int16_t actionParameterS16{ 0 };
    StdFloat32 timingOrTransitionScalar{};
    StdFloat32 motionFrameIncrement{};
    bool operator==(const StdMotionActionRowFields&) const = default;
};

struct StdUnrecognizedActionRowFields {
    std::int16_t rowType{ 0 };
    std::int16_t raw06{ 0 };
    std::uint32_t raw08{ 0U };
    std::int16_t raw0e{ 0 };
    StdFloat32 raw10{};
    StdFloat32 raw14{};
    bool operator==(const StdUnrecognizedActionRowFields&) const = default;
};

using StdActionRowFields = std::variant<
    StdType0ActionRowFields,
    StdMotionActionRowFields,
    StdUnrecognizedActionRowFields>;

struct StdActionRow {
    StdActionRowId id{};
    std::int16_t actionId{ 0 };
    std::int16_t selectorCallbackIndex{ 0 };
    std::int16_t secondaryActionKey{ 0 };
    StdActionRowFields fields{ StdType0ActionRowFields{} };

    [[nodiscard]] std::int16_t rowType() const noexcept;
    bool operator==(const StdActionRow&) const = default;
};

struct StdActionRowsContent {
    std::uint16_t rawCommandLow{ 0U };
    std::uint16_t rawCommandHigh{ 0U };
    std::uint32_t rawLoaderContextWord{ 0U };
    std::uint32_t rawRowTablePointerWord{ 0U };
    std::vector<StdActionRow> rows{};

    [[nodiscard]] std::uint32_t combinedCommandKind() const noexcept {
        return (static_cast<std::uint32_t>(rawCommandHigh) << 16U) | rawCommandLow;
    }
    bool operator==(const StdActionRowsContent&) const = default;
};

struct StdHandledPayloadPrefix {
    std::int16_t primaryActionKey{ 0 };
    std::int16_t applicabilitySelectorRaw{ 0 };
    std::int16_t secondaryActionKey{ 0 };
    std::uint16_t commandFlags{ 0U };

    [[nodiscard]] bool optsIntoGlobalEventCleanupOrAbort() const noexcept {
        return (commandFlags & 0x2000U) != 0U;
    }
    bool operator==(const StdHandledPayloadPrefix&) const = default;
};

struct StdSparcChoice {
    std::int16_t value{ 0 };
    std::int16_t weight{ 0 };
    bool operator==(const StdSparcChoice&) const = default;
};

struct StdSparcPayload {
    StdHandledPayloadPrefix common{};
    std::uint32_t mldFilenameKey{ 0U };
    std::int16_t setupWord0{ 0 };
    std::int16_t setupWord1{ 0 };
    std::uint32_t childMetadataRaw{ 0U };
    std::uint32_t behaviorFlags{ 0U };
    std::int16_t durationOrActivationCount{ 0 };
    std::int16_t childParameterS16{ 0 };
    std::uint16_t raw1c{ 0U };
    std::uint16_t reserved1e{ 0U };
    std::array<StdFloat32, 3U> positionOrOffset{};
    std::array<StdFloat32, 3U> velocityVector{};
    std::int16_t spawnCount{ 0 };
    std::int16_t randomRange{ 0 };
    std::int16_t spawnMode{ 0 };
    std::int16_t raw3e{ 0 };
    StdFloat32 childDivisorOrParameter{};
    std::array<StdFloat32, 2U> childParameters44{};
    std::array<StdFloat32, 3U> vectorMultipliers{};
    std::array<StdFloat32, 3U> secondaryVector{};
    std::array<StdSparcChoice, 64U> choices{};

    [[nodiscard]] std::optional<std::string> formattedMldFilename() const;
    bool operator==(const StdSparcPayload&) const = default;
};

struct StdModelTimelineEntry {
    std::int16_t modelIndex{ 0 };
    std::int16_t durationTicks{ 0 };
    bool operator==(const StdModelTimelineEntry&) const = default;
};

[[nodiscard]] constexpr std::uint8_t stdModelAnchorCoordinateMode(const std::uint32_t flags) noexcept {
    return static_cast<std::uint8_t>(flags & 0x0fU);
}

[[nodiscard]] constexpr std::optional<std::uint8_t> stdModelFallbackDrawMode(
    const std::uint32_t flags) noexcept {
    if ((flags & 0x80U) != 0U) return 1U;
    if ((flags & 0x40U) != 0U) return 2U;
    if ((flags & 0x10U) != 0U) return 3U;
    return std::nullopt;
}

struct StdPutModelPayload {
    StdHandledPayloadPrefix common{};
    std::uint32_t mldResourceId{ 0U };
    std::array<std::uint8_t, 4U> reserved0c{};
    std::uint32_t modelFlags{ 0U };
    std::int16_t activationFrame{ 0 };
    std::int16_t fadeOutStartFrame{ 0 };
    std::int16_t fadeInStartFrame{ 0 };
    std::int16_t fadeInFrames{ 0 };
    std::int16_t fadeOutFrames{ 0 };
    std::uint16_t anchorNodeIndex{ 0U };
    std::array<StdFloat32, 3U> initialPosition{};
    std::array<StdFloat32, 3U> initialEulerDegrees{};
    std::array<StdModelTimelineEntry, 64U> modelTimeline{};
    std::int16_t timelineRepeatFirst{ 0 };
    std::int16_t timelineRepeatLast{ 0 };
    std::array<StdFloat32, 3U> positionDeltaPerTick{};
    std::array<StdFloat32, 3U> eulerDeltaDegreesPerTick{};
    std::array<StdFloat32, 3U> scaleDeltaA{};
    std::int16_t scaleDeltaAStartFrame{ 0 };
    std::int16_t scaleDeltaATicks{ 0 };
    std::array<StdFloat32, 3U> initialScale{};
    std::array<StdFloat32, 3U> scaleDeltaB{};
    std::int16_t scaleDeltaBStartFrame{ 0 };
    std::int16_t scaleDeltaBTicks{ 0 };
    std::int16_t textureStripSelectorA{ 0 };
    std::int16_t textureStripSelectorB{ 0 };
    StdFloat32 motionFrameDelta{};
    std::int16_t eulerDeltaStartFrame{ 0 };
    std::int16_t eulerDeltaEndFrame{ 0 };
    std::int16_t fallbackRenderParam{ 0 };
    std::int16_t positionDeltaStartFrame{ 0 };
    std::int16_t positionDeltaEndFrame{ 0 };
    std::array<std::uint8_t, 2U> rawTail192{};

    [[nodiscard]] std::uint8_t anchorCoordinateMode() const noexcept {
        return stdModelAnchorCoordinateMode(modelFlags);
    }
    [[nodiscard]] std::optional<std::uint8_t> fallbackDrawMode() const noexcept {
        return stdModelFallbackDrawMode(modelFlags);
    }
    [[nodiscard]] bool usesExtraNinjaRenderOption() const noexcept { return (modelFlags & 0x00002000U) != 0U; }
    [[nodiscard]] bool usesFallbackRenderOrSuppressesMode0Anchor() const noexcept {
        return (modelFlags & 0x04000000U) != 0U;
    }
    [[nodiscard]] bool holdsFinalMotionFrame() const noexcept { return (modelFlags & 0x08000000U) != 0U; }
    bool operator==(const StdPutModelPayload&) const = default;
};

struct StdSetCommandPayload {
    StdHandledPayloadPrefix common{};
    std::array<std::uint8_t, 8U> reserved08{};
    std::uint32_t behaviorFlags{ 0U };
    std::int16_t preApplicationCountdown{ 0 };
    std::int16_t pendingTargetCommandOrStateCode{ 0 };
    std::array<std::uint8_t, 4U> reserved18{};
    bool operator==(const StdSetCommandPayload&) const = default;
};

struct StdMotionPausePayload {
    StdHandledPayloadPrefix common{};
    std::array<std::uint8_t, 8U> reserved08{};
    std::uint32_t pauseFlags{ 0U };
    std::int16_t pauseStartFrame{ 0 };
    std::int16_t pauseEndFrame{ 0 };

    [[nodiscard]] bool latchesPauseAndRequestsTargetState() const noexcept {
        return (pauseFlags & 0x8000U) != 0U;
    }
    bool operator==(const StdMotionPausePayload&) const = default;
};

struct StdCollisionBoxPayload {
    StdHandledPayloadPrefix common{};
    std::array<std::uint8_t, 8U> reserved08{};
    std::uint32_t modeFlags{ 0U };
    std::int16_t startFrame{ 0 };
    std::int16_t endFrameOrLifetime{ 0 };
    std::uint16_t modelNodeOrdinal{ 0U };
    std::uint16_t reserved1a{ 0U };
    std::array<StdFloat32, 3U> positionVector{};
    std::array<StdFloat32, 3U> velocityVector{};
    std::uint32_t collisionFlags{ 0U };

    [[nodiscard]] bool registersModelNode() const noexcept { return (modeFlags & 0x0fU) == 2U; }
    [[nodiscard]] bool usesFiveHorizontalProbes() const noexcept { return (collisionFlags & 0x0800U) != 0U; }
    [[nodiscard]] bool usesThreeHorizontalProbes() const noexcept { return (collisionFlags & 0x1000U) != 0U; }
    [[nodiscard]] bool enablesCombatantInteraction() const noexcept { return (collisionFlags & 0x2000U) != 0U; }
    [[nodiscard]] bool loadsHitCombatantMotion() const noexcept { return (collisionFlags & 0x0400U) != 0U; }
    [[nodiscard]] bool continuesAfterRegisteredHit() const noexcept { return (collisionFlags & 0x4000U) != 0U; }
    [[nodiscard]] bool forcesFinishAfterHit() const noexcept { return (collisionFlags & 0x8000U) != 0U; }
    bool operator==(const StdCollisionBoxPayload&) const = default;
};

struct StdMoveModelPayload {
    StdHandledPayloadPrefix common{};
    std::uint32_t mldResourceId{ 0U };
    std::array<std::uint8_t, 4U> reserved0c{};
    std::uint32_t modelFlags{ 0U };
    std::uint16_t conditionFlags{ 0U };
    std::int16_t activationFrame{ 0 };
    std::int16_t fadeOutStartFrame{ 0 };
    std::int16_t fadeInStartFrame{ 0 };
    std::int16_t fadeInFrames{ 0 };
    std::int16_t fadeOutFrames{ 0 };
    std::uint16_t anchorNodeIndex{ 0U };
    std::array<std::uint8_t, 2U> reserved22{};
    std::array<StdFloat32, 3U> initialPosition{};
    std::array<StdFloat32, 3U> initialEulerDegrees{};
    std::array<StdModelTimelineEntry, 64U> modelTimeline{};
    std::int16_t timelineRepeatFirst{ 0 };
    std::int16_t timelineRepeatLast{ 0 };
    std::array<StdFloat32, 3U> positionDeltaPerTick{};
    std::array<StdFloat32, 3U> eulerDeltaDegreesPerTick{};
    std::array<StdFloat32, 3U> scaleDelta{};
    std::int16_t scaleDeltaStartFrame{ 0 };
    std::int16_t scaleDeltaTicks{ 0 };
    std::array<StdFloat32, 3U> initialScale{};
    std::int16_t textureStripSelectorA{ 0 };
    std::int16_t textureStripSelectorB{ 0 };
    StdFloat32 motionFrameDelta{};
    std::int16_t eulerDeltaStartFrame{ 0 };
    std::int16_t eulerDeltaEndFrame{ 0 };
    std::int16_t fallbackRenderParam{ 0 };
    std::array<std::uint8_t, 2U> reserved182{};
    std::array<StdFloat32, 3U> alternateChildVector{};
    StdFloat32 alternateChildYAcceleration{};
    std::int16_t alternateChildFadeFrames{ 0 };
    std::int16_t alternateChildFadeStartFrame{ 0 };
    std::int16_t conditionalEffectStartFrame{ 0 };
    std::int16_t conditionalEffectEndFrame{ 0 };
    std::int16_t conditionalEffectInterval{ 0 };
    std::int16_t conditionalChildFadeFrames{ 0 };

    [[nodiscard]] std::uint8_t anchorCoordinateMode() const noexcept {
        return stdModelAnchorCoordinateMode(modelFlags);
    }
    [[nodiscard]] std::optional<std::uint8_t> fallbackDrawMode() const noexcept {
        return stdModelFallbackDrawMode(modelFlags);
    }
    [[nodiscard]] bool rotatesAlternateChildFromVelocity() const noexcept {
        return (modelFlags & 0x00000200U) != 0U;
    }
    [[nodiscard]] bool usesExtraNinjaRenderOption() const noexcept { return (modelFlags & 0x00002000U) != 0U; }
    [[nodiscard]] bool usesFallbackRenderOrSuppressesMode0Anchor() const noexcept {
        return (modelFlags & 0x04000000U) != 0U;
    }
    [[nodiscard]] bool holdsFinalMotionFrame() const noexcept { return (modelFlags & 0x08000000U) != 0U; }
    [[nodiscard]] bool usesInstructionFlagCondition() const noexcept { return (conditionFlags & 0x1000U) != 0U; }
    [[nodiscard]] bool usesAlternateParticleChildWhenConditionTrue() const noexcept {
        return (conditionFlags & 0x2000U) != 0U;
    }
    [[nodiscard]] bool usesConditionPolarityAndStartupGate() const noexcept {
        return (conditionFlags & 0x8000U) != 0U;
    }
    bool operator==(const StdMoveModelPayload&) const = default;
};

struct StdHitWeaponPayload {
    StdHandledPayloadPrefix common{};
    std::uint32_t raw08{ 0U };
    std::array<std::uint8_t, 4U> reserved0c{};
    std::uint32_t modelFlags{ 0U };
    std::int16_t activationFrame{ 0 };
    std::int16_t pathStartFrame{ 0 };
    std::int16_t pathDurationFrames{ 0 };
    std::int16_t fadeInStartFrame{ 0 };
    std::int16_t fadeInFrames{ 0 };
    std::int16_t fadeOutFrames{ 0 };
    std::uint16_t anchorNodeIndex{ 0U };
    std::array<std::uint8_t, 2U> reserved22{};
    std::array<StdFloat32, 3U> pathEndpointOffset{};
    std::array<StdFloat32, 3U> initialEulerDegrees{};
    std::array<StdFloat32, 3U> initialScale{};
    std::array<StdModelTimelineEntry, 64U> modelTimeline{};
    std::int16_t timelineRepeatFirst{ 0 };
    std::int16_t timelineRepeatLast{ 0 };
    StdFloat32 pathAngleDeltaDegreesPerTick{};
    std::array<StdFloat32, 3U> eulerDeltaDegreesPerTick{};
    StdFloat32 raw15c{};
    std::int16_t textureStripSelectorA{ 0 };
    std::int16_t textureStripSelectorB{ 0 };
    StdFloat32 motionFrameDelta{};
    std::int16_t eulerDeltaStartFrame{ 0 };
    std::int16_t eulerDeltaEndFrame{ 0 };
    std::int16_t childEffectInterval{ 0 };
    std::int16_t childEffectFadeFrames{ 0 };
    std::int16_t fallbackRenderParam{ 0 };
    std::int16_t targetStateFlagMode{ 0 };

    [[nodiscard]] std::uint8_t anchorCoordinateMode() const noexcept {
        return stdModelAnchorCoordinateMode(modelFlags);
    }
    [[nodiscard]] std::optional<std::uint8_t> fallbackDrawMode() const noexcept {
        return stdModelFallbackDrawMode(modelFlags);
    }
    [[nodiscard]] bool usesExtraNinjaRenderOption() const noexcept { return (modelFlags & 0x00002000U) != 0U; }
    [[nodiscard]] bool usesFallbackRenderOrSuppressesMode0Anchor() const noexcept {
        return (modelFlags & 0x04000000U) != 0U;
    }
    [[nodiscard]] bool holdsFinalMotionFrame() const noexcept { return (modelFlags & 0x08000000U) != 0U; }
    [[nodiscard]] std::optional<std::uint32_t> targetStateWorksheetFlagMask() const noexcept {
        if (targetStateFlagMode == 1) return 0x80000000U;
        if (targetStateFlagMode == 2) return 0x40000000U;
        return std::nullopt;
    }
    bool operator==(const StdHitWeaponPayload&) const = default;
};

struct StdPointLightWave {
    std::int16_t startFrame{ 0 };
    std::int16_t endFrame{ 0 };
    StdFloat32 amplitude{};
    std::int16_t phaseStep{ 0 };
    std::int16_t phaseBase{ 0 };
    bool operator==(const StdPointLightWave&) const = default;
};

struct StdPointLightRamp {
    StdFloat32 slope{};
    std::int16_t startFrame{ 0 };
    std::int16_t endFrame{ 0 };
    bool operator==(const StdPointLightRamp&) const = default;
};

struct StdPointLightPayload {
    StdHandledPayloadPrefix common{};
    std::uint32_t raw08{ 0U };
    std::array<std::uint8_t, 4U> reserved0c{};
    std::uint32_t lightFlags{ 0U };
    std::int16_t activationFrame{ 0 };
    std::int16_t fadeOutStartFrame{ 0 };
    std::int16_t fadeInFrames{ 0 };
    std::int16_t fadeOutFrames{ 0 };
    std::uint16_t anchorNodeIndex{ 0U };
    std::array<std::uint8_t, 2U> reserved1e{};
    std::array<StdFloat32, 3U> position{};
    std::int16_t lightSlot{ 0 };
    std::array<std::uint8_t, 2U> reserved2e{};
    std::array<StdFloat32, 3U> rgb{};
    StdFloat32 attenuationParameterA{};
    StdFloat32 attenuationParameterB{};
    StdPointLightWave rgbWave{};
    StdPointLightWave attenuationWave{};
    std::int16_t enablePulseInterval{ 0 };
    std::array<std::uint8_t, 2U> reserved5e{};
    StdPointLightRamp attenuationARamp{};
    StdPointLightRamp attenuationBRamp{};

    [[nodiscard]] std::uint8_t anchorCoordinateMode() const noexcept {
        return stdModelAnchorCoordinateMode(lightFlags);
    }
    [[nodiscard]] bool suppressesMode0AnchorMatrix() const noexcept {
        return (lightFlags & 0x04000000U) != 0U;
    }
    bool operator==(const StdPointLightPayload&) const = default;
};

struct StdSystemCameraPayload {
    StdHandledPayloadPrefix common{};
    std::array<std::uint8_t, 8U> reserved08{};
    std::uint32_t cameraBehaviorFlags{ 0U };
    StdFloat32 cameraModeParameter{};
    std::int16_t startFrame{ 0 };
    std::uint16_t reserved1a{ 0U };
    std::int16_t endFrame{ 0 };
    std::int16_t holdFrameCount{ 0 };
    std::int16_t stepFrameCount{ 0 };
    std::int16_t requestedMode{ 0 };

    [[nodiscard]] bool selectsActorOrTarget() const noexcept { return (cameraBehaviorFlags & 0x40000000U) != 0U; }
    [[nodiscard]] bool usesAlternatePositionOrLookSource() const noexcept { return (cameraBehaviorFlags & 0x08000000U) != 0U; }
    [[nodiscard]] bool continuouslyTracksOrUpdatesAngle() const noexcept { return (cameraBehaviorFlags & 0x20000000U) != 0U; }
    [[nodiscard]] bool suppressesOrdinaryCombatantStateCalls() const noexcept { return (cameraBehaviorFlags & 0x10000000U) != 0U; }
    [[nodiscard]] bool usesComputedOrRandomYaw() const noexcept { return (cameraBehaviorFlags & 0x04000000U) != 0U; }
    [[nodiscard]] bool usesModeParameterAsOrbitStep() const noexcept { return (cameraBehaviorFlags & 0x02000000U) != 0U; }
    [[nodiscard]] bool hasCombatantStateTerminationLatch() const noexcept { return (cameraBehaviorFlags & 0x01000000U) != 0U; }
    [[nodiscard]] bool selectsAlternateSetupHelper() const noexcept { return (cameraBehaviorFlags & 0x00000800U) != 0U; }
    bool operator==(const StdSystemCameraPayload&) const = default;
};

struct StdEffectWaitPayload {
    std::int16_t primaryActionKey{ 0 };
    std::int16_t routeOrScopeRaw{ 0 };
    std::int16_t secondaryActionKey{ 0 };
    std::uint16_t flagsRaw{ 0U };
    std::array<std::uint8_t, 8U> reserved08{};
    std::int16_t waitValueRaw{ 0 };
    std::uint16_t reserved12{ 0U };
    bool operator==(const StdEffectWaitPayload&) const = default;
};

struct StdSeRequestPayload {
    StdHandledPayloadPrefix common{};
    std::array<std::uint8_t, 8U> reserved08{};
    std::uint32_t requestFlags{ 0U };
    std::array<std::uint8_t, 4U> reserved14{};
    std::int16_t requestMode{ 0 };
    std::int16_t playAtFrameOrDelay{ 0 };
    std::int16_t mode4Timeout{ 0 };
    std::int16_t soundBankGroupRaw{ 0 };
    std::int16_t primaryCueSelector{ 0 };
    std::int16_t outputSlotAgeOrLifetime{ 0 };
    std::int16_t alternateCueSelector1{ 0 };
    std::int16_t alternateCueSelector2{ 0 };
    std::uint32_t trailingRaw28{ 0U };

    [[nodiscard]] bool waitsForCompletion() const noexcept {
        return (requestFlags & 0x8000U) != 0U;
    }
    [[nodiscard]] std::int8_t soundBankGroupSelector() const noexcept {
        return static_cast<std::int8_t>(static_cast<std::uint16_t>(soundBankGroupRaw) & 0xffU);
    }
    void setSoundBankGroupSelector(const std::int8_t value) noexcept {
        const auto preservedHigh = static_cast<std::uint16_t>(soundBankGroupRaw) & 0xff00U;
        soundBankGroupRaw = static_cast<std::int16_t>(preservedHigh | static_cast<std::uint8_t>(value));
    }
    [[nodiscard]] bool usesDefaultSoundBankGroup() const noexcept { return soundBankGroupSelector() == 0; }
    bool operator==(const StdSeRequestPayload&) const = default;
};

struct StdOpaquePayload {
    std::vector<std::uint8_t> bytes{};
    bool operator==(const StdOpaquePayload&) const = default;
};

using StdEntryPayloadContent = std::variant<
    StdSparcPayload,
    StdPutModelPayload,
    StdSetCommandPayload,
    StdMotionPausePayload,
    StdCollisionBoxPayload,
    StdMoveModelPayload,
    StdHitWeaponPayload,
    StdPointLightPayload,
    StdSystemCameraPayload,
    StdEffectWaitPayload,
    StdSeRequestPayload,
    StdOpaquePayload>;

struct StdEntryPayload {
    StdEntryPayloadId id{};
    StdEntryPayloadContent content{ StdOpaquePayload{} };
    bool operator==(const StdEntryPayload&) const = default;
};

struct StdEntryRecord {
    StdEntryRecordId id{};
    std::int16_t locationCode{ 0 };
    std::int16_t opcode{ 0 };
    std::uint32_t raw04{ 0U };
    std::optional<StdEntryPayloadId> payload{};

    [[nodiscard]] std::uint32_t combinedType() const noexcept {
        return (static_cast<std::uint32_t>(static_cast<std::uint16_t>(opcode)) << 16U) |
            static_cast<std::uint16_t>(locationCode);
    }
    bool operator==(const StdEntryRecord&) const = default;
};

struct StdEntryTerminator {
    StdEntryTerminatorId id{};
    std::int16_t negativeLocation{ -1 };
    std::int16_t raw02{ 0 };
    std::uint32_t raw04{ 0U };
    std::uint32_t raw08{ 0U };
    std::uint32_t raw0c{ 0U };
    bool operator==(const StdEntryTerminator&) const = default;
};

struct StdOpaqueFragment {
    StdOpaqueFragmentId id{};
    std::vector<std::uint8_t> bytes{};
    bool operator==(const StdOpaqueFragment&) const = default;
};

using StdEntryPayloadLayoutItem = std::variant<StdEntryPayloadId, StdOpaqueFragmentId>;

struct StdEntryTableContent {
    std::uint16_t kind{ 4U };
    std::uint32_t rawHeader04{ 0U };
    std::uint32_t rawHeader08{ 0U };
    std::vector<StdEntryRecord> records{};
    StdEntryTerminator terminator{};
    std::vector<StdEntryPayload> payloads{};
    std::vector<StdOpaqueFragment> opaqueFragments{};
    std::vector<StdEntryPayloadLayoutItem> payloadLayout{};
    std::optional<StdOpaqueFragment> fileTrailer{};
    bool operator==(const StdEntryTableContent&) const = default;
};

struct StdOpaqueContent {
    std::vector<std::uint8_t> decodedBytes{};
    bool operator==(const StdOpaqueContent&) const = default;
};

using StdDocumentContent = std::variant<StdActionRowsContent, StdEntryTableContent, StdOpaqueContent>;

struct StdDocument {
    StdDocumentContent content{ StdOpaqueContent{} };

    [[nodiscard]] StdActionRowId allocateActionRowId() const noexcept;
    [[nodiscard]] StdEntryRecordId allocateEntryRecordId() const noexcept;
    [[nodiscard]] StdEntryPayloadId allocateEntryPayloadId() const noexcept;
    [[nodiscard]] StdEntryTerminatorId allocateEntryTerminatorId() const noexcept;
    [[nodiscard]] StdOpaqueFragmentId allocateOpaqueFragmentId() const noexcept;
    [[nodiscard]] bool hasOpaqueContent() const noexcept;
    bool operator==(const StdDocument&) const = default;
};

enum class StdCommandKind {
    Sparc,
    PutModel,
    SetCommand,
    MotionPause,
    CollisionBox,
    MoveModel,
    HitWeapon,
    PointLight,
    SystemCamera,
    EffectWait,
    SeRequest,
};

struct StdCommandDescriptor {
    StdCommandKind kind{ StdCommandKind::Sparc };
    std::uint32_t combinedType{ 0U };
    std::string_view binaryName{};
    std::uint32_t loaderPayloadSize{ 0U };
    bool hasTypedPayloadCodec{ false };
};

[[nodiscard]] const StdCommandDescriptor* findStdCommandDescriptor(std::uint32_t combinedType) noexcept;
[[nodiscard]] const StdEntryPayload* findEntryPayload(
    const StdEntryTableContent& table, StdEntryPayloadId id) noexcept;
[[nodiscard]] StdEntryPayload* findEntryPayload(
    StdEntryTableContent& table, StdEntryPayloadId id) noexcept;
[[nodiscard]] const StdOpaqueFragment* findOpaqueFragment(
    const StdEntryTableContent& table, StdOpaqueFragmentId id) noexcept;
[[nodiscard]] StdOpaqueFragment* findOpaqueFragment(
    StdEntryTableContent& table, StdOpaqueFragmentId id) noexcept;

inline constexpr std::uint32_t kStdSparcCombinedType = 0x00030002U;
inline constexpr std::uint32_t kStdPutModelCombinedType = 0x00030003U;
inline constexpr std::uint32_t kStdSetCommandCombinedType = 0x00030004U;
inline constexpr std::uint32_t kStdMotionPauseCombinedType = 0x0003000aU;
inline constexpr std::uint32_t kStdCollisionBoxCombinedType = 0x0003000bU;
inline constexpr std::uint32_t kStdMoveModelCombinedType = 0x0003000cU;
inline constexpr std::uint32_t kStdHitWeaponCombinedType = 0x0003000dU;
inline constexpr std::uint32_t kStdPointLightCombinedType = 0x0003001dU;
inline constexpr std::uint32_t kStdSystemCameraCombinedType = 0x0003002aU;
inline constexpr std::uint32_t kStdEffectWaitCombinedType = 0x00030032U;
inline constexpr std::uint32_t kStdSeRequestCombinedType = 0x00030036U;

inline constexpr std::uint32_t kStdSparcPayloadSize = 0x164U;
inline constexpr std::uint32_t kStdPutModelPayloadSize = 0x194U;
inline constexpr std::uint32_t kStdSetCommandPayloadSize = 0x1cU;
inline constexpr std::uint32_t kStdMotionPausePayloadSize = 0x18U;
inline constexpr std::uint32_t kStdCollisionBoxPayloadSize = 0x38U;
inline constexpr std::uint32_t kStdMoveModelPayloadSize = 0x1a0U;
inline constexpr std::uint32_t kStdHitWeaponPayloadSize = 0x174U;
inline constexpr std::uint32_t kStdPointLightPayloadSize = 0x70U;
inline constexpr std::uint32_t kStdSystemCameraPayloadSize = 0x24U;
inline constexpr std::uint32_t kStdEffectWaitPayloadSize = 0x14U;
inline constexpr std::uint32_t kStdSeRequestPayloadSize = 0x2cU;

} // namespace spice::stdfile
