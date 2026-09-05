#include "StdJsonExporter.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>

namespace spice::stdfile {
namespace {

std::string jsonEscape(const std::string& value) {
    std::string escaped{};
    escaped.reserve(value.size() + 8U);
    for (const char c : value) {
        switch (c) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(c); break;
        }
    }
    return escaped;
}

template <typename Range>
std::string bytesHex(const Range& bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto byte : bytes) out << std::setw(2) << static_cast<unsigned>(byte);
    return out.str();
}

const char* endianName(const spice::root::Endian endian) {
    return endian == spice::root::Endian::Little ? "little" : "big";
}

void writeFloat(std::ostringstream& out, const StdFloat32 value) {
    out << "{\"bits\":" << value.bits << ",\"value\":";
    const auto decoded = value.value();
    if (std::isfinite(decoded)) out << std::setprecision(std::numeric_limits<float>::max_digits10) << decoded;
    else if (std::isnan(decoded)) out << "\"nan\"";
    else out << (decoded < 0.0F ? "\"-infinity\"" : "\"infinity\"");
    out << '}';
}

template <std::size_t Size>
void writeFloatArray(std::ostringstream& out, const std::array<StdFloat32, Size>& values) {
    out << '[';
    for (std::size_t index = 0U; index < Size; ++index) {
        if (index != 0U) out << ',';
        writeFloat(out, values[index]);
    }
    out << ']';
}

void writeModelTimeline(
    std::ostringstream& out, const std::array<StdModelTimelineEntry, 64U>& values) {
    out << '[';
    for (std::size_t index = 0U; index < values.size(); ++index) {
        if (index != 0U) out << ',';
        out << "{\"modelIndex\":" << values[index].modelIndex
            << ",\"durationTicks\":" << values[index].durationTicks << '}';
    }
    out << ']';
}

void writePointLightWave(std::ostringstream& out, const StdPointLightWave& value) {
    out << "{\"startFrame\":" << value.startFrame << ",\"endFrame\":" << value.endFrame
        << ",\"amplitude\":";
    writeFloat(out, value.amplitude);
    out << ",\"phaseStep\":" << value.phaseStep << ",\"phaseBase\":" << value.phaseBase << '}';
}

void writePointLightRamp(std::ostringstream& out, const StdPointLightRamp& value) {
    out << "{\"slope\":";
    writeFloat(out, value.slope);
    out << ",\"startFrame\":" << value.startFrame << ",\"endFrame\":" << value.endFrame << '}';
}

void writeDiagnostics(std::ostringstream& out, const std::vector<StdDocumentDiagnostic>& diagnostics) {
    out << '[';
    for (std::size_t index = 0U; index < diagnostics.size(); ++index) {
        if (index != 0U) out << ',';
        const auto& diagnostic = diagnostics[index];
        out << "{\"code\":\"" << toString(diagnostic.code)
            << "\",\"severity\":\"" << toString(diagnostic.severity)
            << "\",\"decodedOffset\":";
        if (diagnostic.decodedOffset.has_value()) out << *diagnostic.decodedOffset;
        else out << "null";
        out << ",\"text\":\"" << jsonEscape(diagnostic.message) << "\"}";
    }
    out << ']';
}

template <typename Id>
void writeIds(std::ostringstream& out, const std::vector<Id>& ids) {
    out << '[';
    for (std::size_t index = 0U; index < ids.size(); ++index) {
        if (index != 0U) out << ',';
        out << ids[index].value;
    }
    out << ']';
}

void writeReceipt(std::ostringstream& out, const StdImportReceipt& receipt) {
    out << "{\"path\":";
    if (receipt.path.has_value()) out << '"' << jsonEscape(receipt.path->string()) << '"';
    else out << "null";
    out << ",\"sourceSha256\":\"" << bytesHex(receipt.sourceSha256)
        << "\",\"sourceSize\":" << receipt.sourceSize
        << ",\"decodedSize\":" << receipt.decodedSize
        << ",\"compression\":\"" << toString(receipt.compression)
        << "\",\"byteOrder\":\"" << endianName(receipt.byteOrder)
        << "\",\"byteOrderSelection\":\"" << toString(receipt.byteOrderSelection)
        << "\",\"opaqueEvidence\":{\"payloadIds\":";
    writeIds(out, receipt.opaqueEvidence.payloadIds);
    out << ",\"fragmentIds\":";
    writeIds(out, receipt.opaqueEvidence.fragmentIds);
    out << ",\"fileTrailerId\":";
    if (receipt.opaqueEvidence.fileTrailerId.has_value()) out << receipt.opaqueEvidence.fileTrailerId->value;
    else out << "null";
    out << ",\"fileTrailerSha256\":";
    if (receipt.opaqueEvidence.fileTrailerSha256.has_value()) {
        out << '"' << bytesHex(*receipt.opaqueEvidence.fileTrailerSha256) << '"';
    } else {
        out << "null";
    }
    out << ",\"topLevelDecodedSha256\":";
    if (receipt.opaqueEvidence.topLevelDecodedSha256.has_value()) {
        out << '"' << bytesHex(*receipt.opaqueEvidence.topLevelDecodedSha256) << '"';
    } else {
        out << "null";
    }
    out << "}}";
}

void writePrefix(std::ostringstream& out, const StdHandledPayloadPrefix& value) {
    out << "\"primaryActionKey\":" << value.primaryActionKey
        << ",\"applicabilitySelectorRaw\":" << value.applicabilitySelectorRaw
        << ",\"secondaryActionKey\":" << value.secondaryActionKey
        << ",\"commandFlags\":" << value.commandFlags;
}

void writePayload(std::ostringstream& out, const StdEntryPayloadContent& content) {
    std::visit([&](const auto& value) {
        using Payload = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Payload, StdOpaquePayload>) {
            out << "{\"kind\":\"opaque\",\"bytesHex\":\"" << bytesHex(value.bytes) << "\"}";
        } else if constexpr (std::is_same_v<Payload, StdSparcPayload>) {
            out << "{\"kind\":\"sparc\","; writePrefix(out, value.common);
            out << ",\"mldFilenameKey\":" << value.mldFilenameKey
                << ",\"formattedMldFilename\":";
            if (const auto filename = value.formattedMldFilename()) out << '"' << *filename << '"';
            else out << "null";
            out
                << ",\"setupWord0\":" << value.setupWord0 << ",\"setupWord1\":" << value.setupWord1
                << ",\"childMetadataRaw\":" << value.childMetadataRaw
                << ",\"behaviorFlags\":" << value.behaviorFlags
                << ",\"durationOrActivationCount\":" << value.durationOrActivationCount
                << ",\"childParameterS16\":" << value.childParameterS16
                << ",\"raw1c\":" << value.raw1c
                << ",\"reserved1e\":" << value.reserved1e << ",\"positionOrOffset\":";
            writeFloatArray(out, value.positionOrOffset); out << ",\"velocityVector\":";
            writeFloatArray(out, value.velocityVector);
            out << ",\"spawnCount\":" << value.spawnCount << ",\"randomRange\":" << value.randomRange
                << ",\"spawnMode\":" << value.spawnMode << ",\"raw3e\":" << value.raw3e
                << ",\"childDivisorOrParameter\":"; writeFloat(out, value.childDivisorOrParameter);
            out << ",\"childParameters44\":"; writeFloatArray(out, value.childParameters44);
            out << ",\"vectorMultipliers\":"; writeFloatArray(out, value.vectorMultipliers);
            out << ",\"secondaryVector\":"; writeFloatArray(out, value.secondaryVector);
            out << ",\"choices\":[";
            for (std::size_t index = 0U; index < value.choices.size(); ++index) {
                if (index != 0U) out << ',';
                out << "{\"value\":" << value.choices[index].value
                    << ",\"weight\":" << value.choices[index].weight << '}';
            }
            out << "]}";
        } else if constexpr (std::is_same_v<Payload, StdPutModelPayload>) {
            out << "{\"kind\":\"putModel\","; writePrefix(out, value.common);
            out << ",\"mldResourceId\":" << value.mldResourceId
                << ",\"reserved0cHex\":\"" << bytesHex(value.reserved0c)
                << "\",\"modelFlags\":" << value.modelFlags
                << ",\"activationFrame\":" << value.activationFrame
                << ",\"fadeOutStartFrame\":" << value.fadeOutStartFrame
                << ",\"fadeInStartFrame\":" << value.fadeInStartFrame
                << ",\"fadeInFrames\":" << value.fadeInFrames
                << ",\"fadeOutFrames\":" << value.fadeOutFrames
                << ",\"anchorNodeIndex\":" << value.anchorNodeIndex << ",\"initialPosition\":";
            writeFloatArray(out, value.initialPosition); out << ",\"initialEulerDegrees\":";
            writeFloatArray(out, value.initialEulerDegrees); out << ",\"modelTimeline\":";
            writeModelTimeline(out, value.modelTimeline);
            out << ",\"timelineRepeatFirst\":" << value.timelineRepeatFirst
                << ",\"timelineRepeatLast\":" << value.timelineRepeatLast
                << ",\"positionDeltaPerTick\":";
            writeFloatArray(out, value.positionDeltaPerTick); out << ",\"eulerDeltaDegreesPerTick\":";
            writeFloatArray(out, value.eulerDeltaDegreesPerTick); out << ",\"scaleDeltaA\":";
            writeFloatArray(out, value.scaleDeltaA);
            out << ",\"scaleDeltaAStartFrame\":" << value.scaleDeltaAStartFrame
                << ",\"scaleDeltaATicks\":" << value.scaleDeltaATicks << ",\"initialScale\":";
            writeFloatArray(out, value.initialScale); out << ",\"scaleDeltaB\":";
            writeFloatArray(out, value.scaleDeltaB);
            out << ",\"scaleDeltaBStartFrame\":" << value.scaleDeltaBStartFrame
                << ",\"scaleDeltaBTicks\":" << value.scaleDeltaBTicks
                << ",\"textureStripSelectorA\":" << value.textureStripSelectorA
                << ",\"textureStripSelectorB\":" << value.textureStripSelectorB
                << ",\"motionFrameDelta\":"; writeFloat(out, value.motionFrameDelta);
            out << ",\"eulerDeltaStartFrame\":" << value.eulerDeltaStartFrame
                << ",\"eulerDeltaEndFrame\":" << value.eulerDeltaEndFrame
                << ",\"fallbackRenderParam\":" << value.fallbackRenderParam
                << ",\"positionDeltaStartFrame\":" << value.positionDeltaStartFrame
                << ",\"positionDeltaEndFrame\":" << value.positionDeltaEndFrame
                << ",\"rawTail192Hex\":\"" << bytesHex(value.rawTail192) << "\"}";
        } else if constexpr (std::is_same_v<Payload, StdSetCommandPayload>) {
            out << "{\"kind\":\"setCommand\","; writePrefix(out, value.common);
            out << ",\"reserved08Hex\":\"" << bytesHex(value.reserved08)
                << "\",\"behaviorFlags\":" << value.behaviorFlags
                << ",\"preApplicationCountdown\":" << value.preApplicationCountdown
                << ",\"pendingTargetCommandOrStateCode\":" << value.pendingTargetCommandOrStateCode
                << ",\"reserved18Hex\":\"" << bytesHex(value.reserved18) << "\"}";
        } else if constexpr (std::is_same_v<Payload, StdMotionPausePayload>) {
            out << "{\"kind\":\"motionPause\","; writePrefix(out, value.common);
            out << ",\"reserved08Hex\":\"" << bytesHex(value.reserved08)
                << "\",\"pauseFlags\":" << value.pauseFlags
                << ",\"pauseStartFrame\":" << value.pauseStartFrame
                << ",\"pauseEndFrame\":" << value.pauseEndFrame << '}';
        } else if constexpr (std::is_same_v<Payload, StdCollisionBoxPayload>) {
            out << "{\"kind\":\"collisionBox\","; writePrefix(out, value.common);
            out << ",\"reserved08Hex\":\"" << bytesHex(value.reserved08)
                << "\",\"modeFlags\":" << value.modeFlags << ",\"startFrame\":" << value.startFrame
                << ",\"endFrameOrLifetime\":" << value.endFrameOrLifetime
                << ",\"modelNodeOrdinal\":" << value.modelNodeOrdinal
                << ",\"reserved1a\":" << value.reserved1a << ",\"positionVector\":";
            writeFloatArray(out, value.positionVector); out << ",\"velocityVector\":";
            writeFloatArray(out, value.velocityVector);
            out << ",\"collisionFlags\":" << value.collisionFlags << '}';
        } else if constexpr (std::is_same_v<Payload, StdMoveModelPayload>) {
            out << "{\"kind\":\"moveModel\","; writePrefix(out, value.common);
            out << ",\"mldResourceId\":" << value.mldResourceId
                << ",\"reserved0cHex\":\"" << bytesHex(value.reserved0c)
                << "\",\"modelFlags\":" << value.modelFlags
                << ",\"conditionFlags\":" << value.conditionFlags
                << ",\"activationFrame\":" << value.activationFrame
                << ",\"fadeOutStartFrame\":" << value.fadeOutStartFrame
                << ",\"fadeInStartFrame\":" << value.fadeInStartFrame
                << ",\"fadeInFrames\":" << value.fadeInFrames
                << ",\"fadeOutFrames\":" << value.fadeOutFrames
                << ",\"anchorNodeIndex\":" << value.anchorNodeIndex
                << ",\"reserved22Hex\":\"" << bytesHex(value.reserved22)
                << "\",\"initialPosition\":";
            writeFloatArray(out, value.initialPosition); out << ",\"initialEulerDegrees\":";
            writeFloatArray(out, value.initialEulerDegrees); out << ",\"modelTimeline\":";
            writeModelTimeline(out, value.modelTimeline);
            out << ",\"timelineRepeatFirst\":" << value.timelineRepeatFirst
                << ",\"timelineRepeatLast\":" << value.timelineRepeatLast
                << ",\"positionDeltaPerTick\":";
            writeFloatArray(out, value.positionDeltaPerTick); out << ",\"eulerDeltaDegreesPerTick\":";
            writeFloatArray(out, value.eulerDeltaDegreesPerTick); out << ",\"scaleDelta\":";
            writeFloatArray(out, value.scaleDelta);
            out << ",\"scaleDeltaStartFrame\":" << value.scaleDeltaStartFrame
                << ",\"scaleDeltaTicks\":" << value.scaleDeltaTicks << ",\"initialScale\":";
            writeFloatArray(out, value.initialScale);
            out << ",\"textureStripSelectorA\":" << value.textureStripSelectorA
                << ",\"textureStripSelectorB\":" << value.textureStripSelectorB
                << ",\"motionFrameDelta\":"; writeFloat(out, value.motionFrameDelta);
            out << ",\"eulerDeltaStartFrame\":" << value.eulerDeltaStartFrame
                << ",\"eulerDeltaEndFrame\":" << value.eulerDeltaEndFrame
                << ",\"fallbackRenderParam\":" << value.fallbackRenderParam
                << ",\"reserved182Hex\":\"" << bytesHex(value.reserved182)
                << "\",\"alternateChildVector\":";
            writeFloatArray(out, value.alternateChildVector);
            out << ",\"alternateChildYAcceleration\":"; writeFloat(out, value.alternateChildYAcceleration);
            out << ",\"alternateChildFadeFrames\":" << value.alternateChildFadeFrames
                << ",\"alternateChildFadeStartFrame\":" << value.alternateChildFadeStartFrame
                << ",\"conditionalEffectStartFrame\":" << value.conditionalEffectStartFrame
                << ",\"conditionalEffectEndFrame\":" << value.conditionalEffectEndFrame
                << ",\"conditionalEffectInterval\":" << value.conditionalEffectInterval
                << ",\"conditionalChildFadeFrames\":" << value.conditionalChildFadeFrames << '}';
        } else if constexpr (std::is_same_v<Payload, StdHitWeaponPayload>) {
            out << "{\"kind\":\"hitWeapon\","; writePrefix(out, value.common);
            out << ",\"raw08\":" << value.raw08
                << ",\"reserved0cHex\":\"" << bytesHex(value.reserved0c)
                << "\",\"modelFlags\":" << value.modelFlags
                << ",\"activationFrame\":" << value.activationFrame
                << ",\"pathStartFrame\":" << value.pathStartFrame
                << ",\"pathDurationFrames\":" << value.pathDurationFrames
                << ",\"fadeInStartFrame\":" << value.fadeInStartFrame
                << ",\"fadeInFrames\":" << value.fadeInFrames
                << ",\"fadeOutFrames\":" << value.fadeOutFrames
                << ",\"anchorNodeIndex\":" << value.anchorNodeIndex
                << ",\"reserved22Hex\":\"" << bytesHex(value.reserved22)
                << "\",\"pathEndpointOffset\":";
            writeFloatArray(out, value.pathEndpointOffset); out << ",\"initialEulerDegrees\":";
            writeFloatArray(out, value.initialEulerDegrees); out << ",\"initialScale\":";
            writeFloatArray(out, value.initialScale); out << ",\"modelTimeline\":";
            writeModelTimeline(out, value.modelTimeline);
            out << ",\"timelineRepeatFirst\":" << value.timelineRepeatFirst
                << ",\"timelineRepeatLast\":" << value.timelineRepeatLast
                << ",\"pathAngleDeltaDegreesPerTick\":";
            writeFloat(out, value.pathAngleDeltaDegreesPerTick);
            out << ",\"eulerDeltaDegreesPerTick\":"; writeFloatArray(out, value.eulerDeltaDegreesPerTick);
            out << ",\"raw15c\":"; writeFloat(out, value.raw15c);
            out << ",\"textureStripSelectorA\":" << value.textureStripSelectorA
                << ",\"textureStripSelectorB\":" << value.textureStripSelectorB
                << ",\"motionFrameDelta\":"; writeFloat(out, value.motionFrameDelta);
            out << ",\"eulerDeltaStartFrame\":" << value.eulerDeltaStartFrame
                << ",\"eulerDeltaEndFrame\":" << value.eulerDeltaEndFrame
                << ",\"childEffectInterval\":" << value.childEffectInterval
                << ",\"childEffectFadeFrames\":" << value.childEffectFadeFrames
                << ",\"fallbackRenderParam\":" << value.fallbackRenderParam
                << ",\"targetStateFlagMode\":" << value.targetStateFlagMode << '}';
        } else if constexpr (std::is_same_v<Payload, StdPointLightPayload>) {
            out << "{\"kind\":\"pointLight\","; writePrefix(out, value.common);
            out << ",\"raw08\":" << value.raw08
                << ",\"reserved0cHex\":\"" << bytesHex(value.reserved0c)
                << "\",\"lightFlags\":" << value.lightFlags
                << ",\"activationFrame\":" << value.activationFrame
                << ",\"fadeOutStartFrame\":" << value.fadeOutStartFrame
                << ",\"fadeInFrames\":" << value.fadeInFrames
                << ",\"fadeOutFrames\":" << value.fadeOutFrames
                << ",\"anchorNodeIndex\":" << value.anchorNodeIndex
                << ",\"reserved1eHex\":\"" << bytesHex(value.reserved1e)
                << "\",\"position\":";
            writeFloatArray(out, value.position);
            out << ",\"lightSlot\":" << value.lightSlot
                << ",\"reserved2eHex\":\"" << bytesHex(value.reserved2e)
                << "\",\"rgb\":";
            writeFloatArray(out, value.rgb);
            out << ",\"attenuationParameterA\":"; writeFloat(out, value.attenuationParameterA);
            out << ",\"attenuationParameterB\":"; writeFloat(out, value.attenuationParameterB);
            out << ",\"rgbWave\":"; writePointLightWave(out, value.rgbWave);
            out << ",\"attenuationWave\":"; writePointLightWave(out, value.attenuationWave);
            out << ",\"enablePulseInterval\":" << value.enablePulseInterval
                << ",\"reserved5eHex\":\"" << bytesHex(value.reserved5e)
                << "\",\"attenuationARamp\":";
            writePointLightRamp(out, value.attenuationARamp);
            out << ",\"attenuationBRamp\":"; writePointLightRamp(out, value.attenuationBRamp);
            out << '}';
        } else if constexpr (std::is_same_v<Payload, StdSystemCameraPayload>) {
            out << "{\"kind\":\"systemCamera\","; writePrefix(out, value.common);
            out << ",\"reserved08Hex\":\"" << bytesHex(value.reserved08)
                << "\",\"cameraBehaviorFlags\":" << value.cameraBehaviorFlags
                << ",\"cameraModeParameter\":"; writeFloat(out, value.cameraModeParameter);
            out << ",\"startFrame\":" << value.startFrame << ",\"reserved1a\":" << value.reserved1a
                << ",\"endFrame\":" << value.endFrame << ",\"holdFrameCount\":" << value.holdFrameCount
                << ",\"stepFrameCount\":" << value.stepFrameCount
                << ",\"requestedMode\":" << value.requestedMode << '}';
        } else if constexpr (std::is_same_v<Payload, StdEffectWaitPayload>) {
            out << "{\"kind\":\"effectWait\",\"primaryActionKey\":" << value.primaryActionKey
                << ",\"routeOrScopeRaw\":" << value.routeOrScopeRaw
                << ",\"secondaryActionKey\":" << value.secondaryActionKey
                << ",\"flagsRaw\":" << value.flagsRaw << ",\"reserved08Hex\":\"" << bytesHex(value.reserved08)
                << "\",\"waitValueRaw\":" << value.waitValueRaw << ",\"reserved12\":" << value.reserved12 << '}';
        } else if constexpr (std::is_same_v<Payload, StdSeRequestPayload>) {
            out << "{\"kind\":\"seRequest\","; writePrefix(out, value.common);
            out << ",\"reserved08Hex\":\"" << bytesHex(value.reserved08)
                << "\",\"requestFlags\":" << value.requestFlags
                << ",\"reserved14Hex\":\"" << bytesHex(value.reserved14)
                << "\",\"requestMode\":" << value.requestMode
                << ",\"playAtFrameOrDelay\":" << value.playAtFrameOrDelay
                << ",\"mode4Timeout\":" << value.mode4Timeout
                << ",\"soundBankGroupRaw\":" << value.soundBankGroupRaw
                << ",\"soundBankGroupSelector\":" << static_cast<int>(value.soundBankGroupSelector())
                << ",\"primaryCueSelector\":" << value.primaryCueSelector
                << ",\"outputSlotAgeOrLifetime\":" << value.outputSlotAgeOrLifetime
                << ",\"alternateCueSelector1\":" << value.alternateCueSelector1
                << ",\"alternateCueSelector2\":" << value.alternateCueSelector2
                << ",\"trailingRaw28\":" << value.trailingRaw28 << '}';
        }
    }, content);
}

void writeActionRow(std::ostringstream& out, const StdActionRow& row) {
    out << "{\"id\":" << row.id.value << ",\"actionId\":" << row.actionId
        << ",\"rowType\":" << row.rowType()
        << ",\"selectorCallbackIndex\":" << row.selectorCallbackIndex
        << ",\"secondaryActionKey\":" << row.secondaryActionKey;
    std::visit([&](const auto& fields) {
        using Fields = std::decay_t<decltype(fields)>;
        if constexpr (std::is_same_v<Fields, StdType0ActionRowFields>) {
            out << ",\"fields\":{\"kind\":\"type0\",\"reserved06\":" << fields.reserved06
                << ",\"raw08\":" << fields.raw08
                << ",\"verticalExtentOverrideCode\":" << fields.verticalExtentOverrideCode
                << ",\"verticalExtentOverrideWorldUnits\":";
            if (const auto extent = fields.verticalExtentOverrideWorldUnits()) out << *extent;
            else out << "null";
            out << ",\"defaultPlanarMovementStep\":";
            writeFloat(out, fields.defaultPlanarMovementStep);
            out << ",\"turningStepOrThreshold\":";
            writeFloat(out, fields.turningStepOrThreshold); out << "}}";
        } else if constexpr (std::is_same_v<Fields, StdMotionActionRowFields>) {
            out << ",\"fields\":{\"kind\":\"motion\",\"motionResourceSelector\":" << fields.motionResourceSelector
                << ",\"motionFlags\":" << fields.motionFlags
                << ",\"actionParameterS16\":" << fields.actionParameterS16
                << ",\"timingOrTransitionScalar\":";
            writeFloat(out, fields.timingOrTransitionScalar);
            out << ",\"motionFrameIncrement\":";
            writeFloat(out, fields.motionFrameIncrement); out << "}}";
        } else {
            out << ",\"fields\":{\"kind\":\"unrecognized\",\"raw06\":" << fields.raw06
                << ",\"raw08\":" << fields.raw08 << ",\"raw0e\":" << fields.raw0e
                << ",\"raw10\":";
            writeFloat(out, fields.raw10);
            out << ",\"raw14\":";
            writeFloat(out, fields.raw14); out << "}}";
        }
    }, row.fields);
}

void writeDocument(std::ostringstream& out, const StdDocument& document) {
    std::visit([&](const auto& content) {
        using Content = std::decay_t<decltype(content)>;
        if constexpr (std::is_same_v<Content, StdActionRowsContent>) {
            out << "{\"kind\":\"actionRows\",\"rawCommandLow\":" << content.rawCommandLow
                << ",\"rawCommandHigh\":" << content.rawCommandHigh
                << ",\"rawLoaderContextWord\":" << content.rawLoaderContextWord
                << ",\"rawRowTablePointerWord\":" << content.rawRowTablePointerWord << ",\"rows\":[";
            for (std::size_t index = 0U; index < content.rows.size(); ++index) {
                if (index != 0U) out << ',';
                writeActionRow(out, content.rows[index]);
            }
            out << "]}";
        } else if constexpr (std::is_same_v<Content, StdEntryTableContent>) {
            out << "{\"kind\":\"entryTable\",\"tableKind\":" << content.kind
                << ",\"rawHeader04\":" << content.rawHeader04 << ",\"rawHeader08\":" << content.rawHeader08
                << ",\"records\":[";
            for (std::size_t index = 0U; index < content.records.size(); ++index) {
                if (index != 0U) out << ',';
                const auto& record = content.records[index];
                out << "{\"id\":" << record.id.value << ",\"location\":" << record.locationCode
                    << ",\"opcode\":" << record.opcode << ",\"raw04\":" << record.raw04 << ",\"commandName\":";
                if (const auto* descriptor = findStdCommandDescriptor(record.combinedType())) {
                    out << '"' << descriptor->binaryName << '"';
                } else out << "null";
                out << ",\"payloadId\":";
                if (record.payload.has_value()) out << record.payload->value;
                else out << "null";
                out << '}';
            }
            const auto& terminal = content.terminator;
            out << "],\"terminator\":{\"id\":" << terminal.id.value
                << ",\"negativeLocation\":" << terminal.negativeLocation
                << ",\"raw02\":" << terminal.raw02 << ",\"raw04\":" << terminal.raw04
                << ",\"raw08\":" << terminal.raw08 << ",\"raw0c\":" << terminal.raw0c
                << "},\"payloads\":[";
            for (std::size_t index = 0U; index < content.payloads.size(); ++index) {
                if (index != 0U) out << ',';
                out << "{\"id\":" << content.payloads[index].id.value << ",\"content\":";
                writePayload(out, content.payloads[index].content);
                out << '}';
            }
            out << "],\"opaqueFragments\":[";
            for (std::size_t index = 0U; index < content.opaqueFragments.size(); ++index) {
                if (index != 0U) out << ',';
                const auto& fragment = content.opaqueFragments[index];
                out << "{\"id\":" << fragment.id.value << ",\"bytesHex\":\"" << bytesHex(fragment.bytes) << "\"}";
            }
            out << "],\"payloadLayout\":[";
            for (std::size_t index = 0U; index < content.payloadLayout.size(); ++index) {
                if (index != 0U) out << ',';
                std::visit([&](const auto id) {
                    using Id = std::remove_cv_t<decltype(id)>;
                    out << "{\"kind\":\"" << (std::is_same_v<Id, StdEntryPayloadId> ? "payload" : "opaqueFragment")
                        << "\",\"id\":" << id.value << '}';
                }, content.payloadLayout[index]);
            }
            out << "],\"fileTrailer\":";
            if (content.fileTrailer.has_value()) {
                out << "{\"id\":" << content.fileTrailer->id.value
                    << ",\"bytesHex\":\"" << bytesHex(content.fileTrailer->bytes) << "\"}";
            } else out << "null";
            out << '}';
        } else {
            out << "{\"kind\":\"opaque\",\"decodedBytesHex\":\"" << bytesHex(content.decodedBytes) << "\"}";
        }
    }, document.content);
}

} // namespace

std::string StdJsonExporter::toJson(const StdDocumentImportResult& imported) const {
    std::ostringstream out;
    out << "{\n  \"schema\": \"spice_std_json_export\",\n  \"schemaVersion\": 5,\n  \"ok\": "
        << (imported.ok() ? "true" : "false")
        << ",\n  \"receipt\": ";
    writeReceipt(out, imported.receipt);
    out << ",\n  \"diagnostics\": ";
    writeDiagnostics(out, imported.diagnostics);
    out << ",\n  \"document\": ";
    if (imported.document.has_value()) writeDocument(out, *imported.document);
    else out << "null";
    out << "\n}\n";
    return out.str();
}

std::string StdJsonExporter::toJson(const StdDocument& document) const {
    std::ostringstream out;
    out << "{\n  \"schema\": \"spice_std_json_export\",\n  \"schemaVersion\": 5,\n  \"document\": ";
    writeDocument(out, document);
    out << "\n}\n";
    return out.str();
}

} // namespace spice::stdfile
