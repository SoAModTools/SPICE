#include "SctOpcodeParameterFacts.h"

#include <algorithm>
#include <array>
#include <limits>

namespace spice::sct {
namespace {

using Profile = SctRuntimeProfileId;
using Payload = SctRuntimeFactPayload;

constexpr std::array<SctRuntimeProfile, 4> kProfiles{{
    {Profile::GameCubeUsRetail20021219, SctPlatform::GameCube,
        "GameCube US retail 2002-12-19",
        "9A549F1424BBD7D4D22491ED26CA0A7E47F4B064B9C9FC39B6D60D1124EF37B8",
        "US retail Start.dol used by the GameCube SCT handler research"},
    {Profile::DreamcastUsRetailDisc1, SctPlatform::Dreamcast,
        "Dreamcast US retail Disc 1",
        "4218C07829D63080BE2DA86413EC023E6D77001C13A1494648DB6E2E5CB73611",
        "Retail US 1ST_READ.BIN inspected for SCT dispatch and parameter dataflow"},
    {Profile::DreamcastJpRetailDisc1, SctPlatform::Dreamcast,
        "Dreamcast JP retail Disc 1",
        "CC9F585DB9121D9DCFFFC77471A6BDF472CFC60D03ED9310A744264D6EAF2AD1",
        "Retail Japanese 1ST_READ.BIN inspected for SCT dispatch and parameter dataflow"},
    {Profile::DreamcastCustomEuDerived2026, SctPlatform::Dreamcast,
        "Dreamcast custom EU-derived 2026 noDebug",
        "5C1F4E79CC96ED0B4C78C69CBAA990F0A69C5B32617891A6E747CC5DD1BDA76A",
        "Custom EU-derived executable with project-specific patches; not a retail EU binary"},
}};

std::vector<Profile> allProfiles() {
    std::vector<Profile> result;
    result.reserve(kProfiles.size());
    for (const auto& profile : kProfiles) result.push_back(profile.id);
    return result;
}

std::vector<Profile> gameCubeProfiles() {
    return {Profile::GameCubeUsRetail20021219};
}

std::vector<Profile> dreamcastProfiles() {
    return {Profile::DreamcastUsRetailDisc1, Profile::DreamcastJpRetailDisc1,
        Profile::DreamcastCustomEuDerived2026};
}

SctRuntimeFactConfidencePair confidence(SctRuntimeFactConfidence structural,
    SctRuntimeFactConfidence semantic) {
    return {structural, semantic};
}

SctProfiledRuntimeBehavior behavior(std::vector<Profile> profiles,
    SctRuntimeFactStage stage, std::vector<Payload> facts,
    SctRuntimeFactConfidencePair gameCubeConfidence = confidence(
        SctRuntimeFactConfidence::High, SctRuntimeFactConfidence::High),
    SctRuntimeFactConfidencePair dreamcastConfidence = confidence(
        SctRuntimeFactConfidence::High, SctRuntimeFactConfidence::High),
    std::string_view method = "Static handler and parameter-dataflow analysis") {
    SctProfiledRuntimeBehavior result;
    result.profiles = std::move(profiles);
    result.stage = stage;
    result.facts = std::move(facts);
    for (const auto profile : result.profiles) {
        const auto* descriptor = findSctRuntimeProfile(profile);
        const auto profileConfidence = descriptor != nullptr
            && descriptor->platform == SctPlatform::GameCube
            ? gameCubeConfidence : dreamcastConfidence;
        result.evidence.push_back({profile, profileConfidence, std::nullopt,
            std::nullopt, method});
    }
    return result;
}

SctProfiledRuntimeBehavior sharedBehavior(SctRuntimeFactStage stage,
    std::vector<Payload> facts,
    SctRuntimeFactConfidencePair gameCubeConfidence = confidence(
        SctRuntimeFactConfidence::High, SctRuntimeFactConfidence::High),
    SctRuntimeFactConfidencePair dreamcastConfidence = confidence(
        SctRuntimeFactConfidence::High, SctRuntimeFactConfidence::High),
    std::string_view method = "Static handler and parameter-dataflow analysis") {
    return behavior(allProfiles(), stage, std::move(facts), gameCubeConfidence,
        dreamcastConfidence, method);
}

SctValueConversionFact conversion(SctRuntimeValueRepresentation input,
    SctRuntimeValueRepresentation output,
    SctRuntimeConversionKind kind = SctRuntimeConversionKind::Preserve) {
    return {input, output, kind};
}

SctNumericDomainFact signedDomain(SctNumericDomainAuthority authority,
    std::int64_t minimum, std::int64_t maximum) {
    return {authority, SctNumericDomainKind::SignedInteger,
        SctRuntimeScalarValue{minimum}, SctRuntimeScalarValue{maximum}, {}, false};
}

SctNumericDomainFact unsignedDomain(SctNumericDomainAuthority authority,
    std::uint64_t minimum, std::uint64_t maximum) {
    return {authority, SctNumericDomainKind::UnsignedInteger,
        SctRuntimeScalarValue{minimum}, SctRuntimeScalarValue{maximum}, {}, false};
}

SctNumericDomainFact finiteFloatDomain(SctNumericDomainAuthority authority) {
    SctNumericDomainFact result;
    result.authority = authority;
    result.kind = SctNumericDomainKind::Float32;
    result.finiteOnly = true;
    return result;
}

SctNumericDomainFact discreteDomain(SctNumericDomainAuthority authority,
    std::initializer_list<std::int64_t> values) {
    SctNumericDomainFact result;
    result.authority = authority;
    result.kind = SctNumericDomainKind::SignedInteger;
    for (const auto value : values) result.discreteValues.emplace_back(value);
    return result;
}

SctRuntimeFactCondition condition(std::uint32_t parameterIndex,
    SctRuntimeConditionProjection projection, SctRuntimeConditionComparison comparison,
    std::initializer_list<std::int64_t> values) {
    return {parameterIndex, projection, comparison, {values.begin(), values.end()}};
}

SctOpcodeParameterFactRecord parameterRecord(std::string_view id, std::uint16_t opcode,
    std::uint32_t schemaIndex, std::optional<SctRuntimeFactCondition> recordCondition,
    std::vector<SctProfiledRuntimeBehavior> behaviors, std::string_view summary,
    std::string_view outOfDomain) {
    return {id, opcode, schemaIndex, std::move(recordCondition), std::move(behaviors),
        summary, outOfDomain};
}

SctProfiledRuntimeBehavior alignedControlBehavior() {
    return sharedBehavior(SctRuntimeFactStage::ControlFlow, {
        SctBitMaskFact{0xfffffffcu, 0u, SctBitMaskPurpose::ClearAlignmentBits},
        SctParameterRelationshipFact{SctParameterRelationshipKind::TargetIsAlignedInstruction,
            std::nullopt, {}},
        SctRuntimeOutcomeFact{SctRuntimeOutcomeKind::CursorRedirect},
    });
}

std::vector<SctOpcodeParameterFactRecord> makeParameterRecords() {
    using Stage = SctRuntimeFactStage;
    using Outcome = SctRuntimeOutcomeKind;
    using Authority = SctNumericDomainAuthority;
    using Representation = SctRuntimeValueRepresentation;
    using Conversion = SctRuntimeConversionKind;
    using External = SctExternalDomainKind;

    std::vector<SctOpcodeParameterFactRecord> records;
    records.reserve(48);
    const auto highStructural = confidence(SctRuntimeFactConfidence::High,
        SctRuntimeFactConfidence::Unknown);
    const auto mediumHighSemantic = confidence(SctRuntimeFactConfidence::High,
        SctRuntimeFactConfidence::MediumHigh);

    records.push_back(parameterRecord("sct-param-v2:000:0", 0, 0, std::nullopt,
        {sharedBehavior(Stage::Handler, {conversion(Representation::ScptFloat32,
            Representation::Boolean), finiteFloatDomain(Authority::ProvenSafe)})},
        "SCPT float32 is compared with zero; zero is false and nonzero is true.",
        "NaN comparison behavior is CPU-specific and no finite-value check is applied."));
    records.push_back(parameterRecord("sct-param-v2:000:1", 0, 1, std::nullopt,
        {alignedControlBehavior()}, "Low two displacement bits are cleared before branching.",
        "No target-region bounds check protects an invalid destination."));

    records.push_back(parameterRecord("sct-param-v2:003:0", 3, 0, std::nullopt,
        {sharedBehavior(Stage::Handler, {conversion(Representation::ScptFloat32,
            Representation::Int32, Conversion::ConvertTowardZero),
            signedDomain(Authority::LosslessRepresentation,
                std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()),
            SctRuntimeOutcomeFact{Outcome::TruncateTowardZero}})},
        "The selector is converted to int32 and compared with raw case values.",
        "Nonintegral values truncate; the handler does not reject them."));
    records.push_back(parameterRecord("sct-param-v2:003:1", 3, 1, std::nullopt,
        {sharedBehavior(Stage::Handler, {
            SctParameterRelationshipFact{SctParameterRelationshipKind::CountMatchesRepeatedGroups,
                std::nullopt, {}}, SctRuntimeOutcomeFact{Outcome::Overread}})},
        "The raw count controls the physically present case-pair loop.",
        "An oversized count can overread the case array."));
    records.push_back(parameterRecord("sct-param-v2:003:2", 3, 2, std::nullopt,
        {sharedBehavior(Stage::Handler, {
            unsignedDomain(Authority::LosslessRepresentation, 0u, 0xffffffffu),
            SctSpecialEncodedValueFact{0xffffffffu, SctSpecialEncodedValueKind::DefaultCase,
                std::nullopt}})},
        "Raw case values use 0xffffffff as the confirmed default-case marker.",
        "Multiple defaults and unmatched selectors follow handler ordering."));
    records.push_back(parameterRecord("sct-param-v2:003:3", 3, 3, std::nullopt,
        {alignedControlBehavior()}, "Each case target clears its low two displacement bits.",
        "No target-region bounds check protects an invalid case destination."));

    records.push_back(parameterRecord("sct-param-v2:005:0", 5, 0, std::nullopt,
        {sharedBehavior(Stage::EncodedOperand, {
            SctBitMaskFact{0x1000ffffu, 0x10000000u, SctBitMaskPurpose::StructuralTag},
            SctExternalDomainFact{External::ByteVariableTable},
            SctRuntimeOutcomeFact{Outcome::UncheckedWrite}})},
        "The structural tag is followed by a signed low-16 byte-variable index.",
        "The handler does not bounds-check the variable table index."));
    records.push_back(parameterRecord("sct-param-v2:005:1", 5, 1, std::nullopt,
        {sharedBehavior(Stage::Handler, {
            conversion(Representation::ScptFloat32, Representation::UInt8, Conversion::NarrowLowBits),
            unsignedDomain(Authority::LosslessRepresentation, 0u, 255u),
            signedDomain(Authority::Conventional, -128, 127),
            SctRuntimeOutcomeFact{Outcome::TruncateModulo}})},
        "The evaluated integer is narrowed to eight bits.",
        "Values outside the selected signed or unsigned interpretation truncate modulo 256."));

    records.push_back(parameterRecord("sct-param-v2:006:0", 6, 0, std::nullopt,
        {sharedBehavior(Stage::EncodedOperand, {
            SctBitMaskFact{0x5000ffffu, 0x50000000u, SctBitMaskPurpose::StructuralTag},
            SctExternalDomainFact{External::IntegerVariableTable},
            SctRuntimeOutcomeFact{Outcome::UncheckedWrite}})},
        "The structural tag is followed by a signed low-16 integer-variable index.",
        "No general variable-table bounds check is applied."));
    records.push_back(parameterRecord("sct-param-v2:006:1:index0", 6, 1,
        condition(0, SctRuntimeConditionProjection::VariableReferenceIndex,
            SctRuntimeConditionComparison::Equals, {0}),
        {sharedBehavior(Stage::Handler, {
            conversion(Representation::ScptFloat32, Representation::Int32,
                Conversion::ConvertTowardZero),
            signedDomain(Authority::RuntimeEnforced, 0, 99999999),
            SctRuntimeOutcomeFact{Outcome::Clamp}},
            confidence(SctRuntimeFactConfidence::High, SctRuntimeFactConfidence::High),
            confidence(SctRuntimeFactConfidence::High, SctRuntimeFactConfidence::MediumHigh))},
        "Assignments to integer variable 0 are clamped to 0..99999999.",
        "Negative values become zero and larger values become 99999999."));
    records.push_back(parameterRecord("sct-param-v2:006:1:index1", 6, 1,
        condition(0, SctRuntimeConditionProjection::VariableReferenceIndex,
            SctRuntimeConditionComparison::Equals, {1}),
        {sharedBehavior(Stage::Handler, {
            conversion(Representation::ScptFloat32, Representation::Int32,
                Conversion::ConvertTowardZero),
            signedDomain(Authority::RuntimeEnforced, 0, 255),
            SctRuntimeOutcomeFact{Outcome::Clamp}},
            confidence(SctRuntimeFactConfidence::High, SctRuntimeFactConfidence::High),
            confidence(SctRuntimeFactConfidence::High, SctRuntimeFactConfidence::MediumHigh))},
        "Assignments to integer variable 1 are clamped to 0..255.",
        "Negative values become zero and larger values become 255."));
    records.push_back(parameterRecord("sct-param-v2:006:1:other", 6, 1,
        condition(0, SctRuntimeConditionProjection::VariableReferenceIndex,
            SctRuntimeConditionComparison::NotInSet, {0, 1}),
        {sharedBehavior(Stage::Handler, {
            conversion(Representation::ScptFloat32, Representation::Int32,
                Conversion::ConvertTowardZero),
            signedDomain(Authority::LosslessRepresentation,
                std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max())})},
        "Other integer variables receive an int32 conversion without a value clamp.",
        "Nonintegral inputs truncate; no narrower handler range is enforced."));

    records.push_back(parameterRecord("sct-param-v2:007:0", 7, 0, std::nullopt,
        {sharedBehavior(Stage::EncodedOperand, {
            SctBitMaskFact{0x4000ffffu, 0x40000000u, SctBitMaskPurpose::StructuralTag},
            SctExternalDomainFact{External::FloatVariableTable},
            SctRuntimeOutcomeFact{Outcome::UncheckedWrite}})},
        "The structural tag is followed by a signed low-16 float-variable index.",
        "The handler does not bounds-check the variable table index."));
    records.push_back(parameterRecord("sct-param-v2:007:1", 7, 1, std::nullopt,
        {sharedBehavior(Stage::Handler, {conversion(Representation::ScptFloat32,
            Representation::Float32), finiteFloatDomain(Authority::ProvenSafe)})},
        "The SCPT result is stored as float32 without narrowing.",
        "No finite-value check or handler clamp is applied."));

    records.push_back(parameterRecord("sct-param-v2:009:0", 9, 0, std::nullopt,
        {sharedBehavior(Stage::EncodedOperand, {
            SctSpecialEncodedValueFact{0x0000001du, SctSpecialEncodedValueKind::Terminator,
                std::nullopt}, SctRuntimeOutcomeFact{Outcome::Overread}})},
        "Raw words are scanned through the exact 0x0000001d terminator.",
        "A missing terminator scans beyond the intended payload."));
    records.push_back(parameterRecord("sct-param-v2:010:0", 10, 0, std::nullopt,
        {alignedControlBehavior()}, "The jump displacement clears its low two bits.",
        "No target-region bounds check protects an invalid destination."));
    records.push_back(parameterRecord("sct-param-v2:011:0", 11, 0, std::nullopt,
        {alignedControlBehavior()}, "The call displacement clears its low two bits.",
        "No target-region bounds check protects an invalid destination."));

    records.push_back(parameterRecord("sct-param-v2:016:0", 16, 0, std::nullopt,
        {behavior(gameCubeProfiles(), Stage::Handler, {
            conversion(Representation::ScptFloat32, Representation::Int32,
                Conversion::ConvertTowardZero),
            signedDomain(Authority::ProvenSafe, 0, std::numeric_limits<std::int32_t>::max()),
            SctRuntimeOutcomeFact{Outcome::ImmediateCompletion}}),
         behavior(dreamcastProfiles(), Stage::Handler, {
            conversion(Representation::ScptFloat32, Representation::Int16,
                Conversion::NarrowLowBits),
            signedDomain(Authority::ProvenSafe, 0, std::numeric_limits<std::int16_t>::max()),
            SctRuntimeOutcomeFact{Outcome::ImmediateCompletion}})},
        "Nonpositive durations complete immediately; positive duration width differs by platform.",
        "There is no positive upper clamp before the platform-specific representation."));

    for (const auto opcode : {17u, 18u, 19u}) {
        const auto id = opcode == 17u ? "sct-param-v2:017:0"
            : opcode == 18u ? "sct-param-v2:018:0" : "sct-param-v2:019:0";
        records.push_back(parameterRecord(id, static_cast<std::uint16_t>(opcode), 0,
            std::nullopt, {sharedBehavior(Stage::Handler, {
                conversion(Representation::ScptFloat32, Representation::Int32,
                    Conversion::ConvertTowardZero),
                SctExternalDomainFact{External::FlagTable},
                SctRuntimeOutcomeFact{Outcome::UncheckedRead}}, highStructural, highStructural)},
            "The evaluated int32 is used as a flag or bitset index.",
            "The complete selected flag-table capacity is external and not locally checked."));
    }

    records.push_back(parameterRecord("sct-param-v2:084:0", 84, 0, std::nullopt,
        {sharedBehavior(Stage::DownstreamConsumer, {
            conversion(Representation::ScptFloat32, Representation::Int32,
                Conversion::ConvertTowardZero), signedDomain(Authority::ProvenSafe, 1, 3),
            SctRuntimeOutcomeFact{Outcome::UncheckedWrite}},
            confidence(SctRuntimeFactConfidence::High, SctRuntimeFactConfidence::High),
            mediumHighSemantic)},
        "Indexed-light selection has three proven slots numbered 1..3.",
        "Other values index adjacent state without a handler clamp."));

    auto op114p0 = sharedBehavior(Stage::Callee, {
        conversion(Representation::ScptFloat32, Representation::UInt16,
            Conversion::NarrowLowBits),
        unsignedDomain(Authority::LosslessRepresentation, 0u, 65535u),
        SctSpecialEncodedValueFact{0x7f7fffffu,
            SctSpecialEncodedValueKind::ReplacementRuntimeValue, SctRuntimeScalarValue{std::uint64_t{65535}}},
        SctExternalDomainFact{External::EventEntryTable},
        SctRuntimeOutcomeFact{Outcome::LookupFailure}}, highStructural, highStructural,
        "Handler and helper ABI review");
    records.push_back(parameterRecord("sct-param-v2:114:0", 114, 0, std::nullopt,
        {std::move(op114p0)}, "The evaluated identifier is reduced to an unsigned 16-bit lane.",
        "There is no numeric clamp; an unknown identifier can make lookup fail."));
    records.push_back(parameterRecord("sct-param-v2:114:1", 114, 1, std::nullopt,
        {sharedBehavior(Stage::Callee, {
            conversion(Representation::ScptFloat32, Representation::Int16,
                Conversion::NarrowLowBits), signedDomain(Authority::LosslessRepresentation, -32768, 32767),
            SctSpecialEncodedValueFact{0x7f7fffffu,
                SctSpecialEncodedValueKind::ReplacementRuntimeValue, SctRuntimeScalarValue{std::int64_t{-1}}}},
            highStructural, highStructural, "Handler and helper ABI review")},
        "The effective helper input is signed 16-bit on both platforms.",
        "Negative values clear; nonnegative values are checked only after addition to the current index."));

    records.push_back(parameterRecord("sct-param-v2:133:0", 133, 0, std::nullopt,
        {sharedBehavior(Stage::Handler, {
            SctParameterRelationshipFact{SctParameterRelationshipKind::CountMatchesRepeatedGroups,
                std::nullopt, {}}, SctExternalDomainFact{External::DestinationCapacity},
            SctRuntimeOutcomeFact{Outcome::Overread}, SctRuntimeOutcomeFact{Outcome::Overwrite}})},
        "The raw count controls repeated keyframe decoding and must match present records.",
        "No source or destination-capacity guard prevents overread or overwrite."));
    records.push_back(parameterRecord("sct-param-v2:133:1", 133, 1, std::nullopt,
        {sharedBehavior(Stage::DownstreamConsumer, {
            conversion(Representation::ScptFloat32, Representation::Int32,
                Conversion::ConvertTowardZero), signedDomain(Authority::ProvenSafe, 1, 3),
            SctRuntimeOutcomeFact{Outcome::UncheckedWrite}},
            confidence(SctRuntimeFactConfidence::High, SctRuntimeFactConfidence::High),
            mediumHighSemantic)},
        "The indexed-light array selector has three proven slots numbered 1..3.",
        "Other values scale unchecked indexes into adjacent state."));
    records.push_back(parameterRecord("sct-param-v2:145:0", 145, 0, std::nullopt,
        {sharedBehavior(Stage::Handler, {
            conversion(Representation::ScptFloat32, Representation::Int32,
                Conversion::ConvertTowardZero),
            SctExternalDomainFact{External::IntegerVariableTable},
            SctRuntimeOutcomeFact{Outcome::UncheckedWrite}},
            highStructural, confidence(SctRuntimeFactConfidence::High,
                SctRuntimeFactConfidence::Medium))},
        "The value selects IntVars[16 + slot] without a local bounds check.",
        "The known authored 0..2 observation is not promoted to a format or runtime-enforced range."));

    const std::array<std::string_view, 9> op220Ids{{
        "sct-param-v2:220:0", "sct-param-v2:220:1", "sct-param-v2:220:2",
        "sct-param-v2:220:3", "sct-param-v2:220:4", "sct-param-v2:220:5",
        "sct-param-v2:220:6", "sct-param-v2:220:7", "sct-param-v2:220:8"}};
    for (std::uint32_t index = 0; index < op220Ids.size(); ++index) {
        std::vector<Payload> facts;
        if (index == 0u || index == 2u) {
            facts = {conversion(Representation::ScptFloat32, Representation::UInt16,
                         Conversion::NarrowLowBits),
                unsignedDomain(Authority::LosslessRepresentation, 0u, 65535u),
                SctSpecialEncodedValueFact{0x7f7fffffu,
                    SctSpecialEncodedValueKind::ReplacementRuntimeValue,
                    SctRuntimeScalarValue{std::uint64_t{65535}}},
                SctExternalDomainFact{index == 2u ? External::ObjectTable
                                                  : External::UnknownExternalConsumer}};
        } else if (index == 1u) {
            facts = {conversion(Representation::ScptFloat32, Representation::Int32,
                         Conversion::ConvertTowardZero),
                signedDomain(Authority::LosslessRepresentation,
                    std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()),
                SctSpecialEncodedValueFact{0x7f7fffffu,
                    SctSpecialEncodedValueKind::ReplacementRuntimeValue,
                    SctRuntimeScalarValue{std::int64_t{std::numeric_limits<std::int32_t>::max()}}}};
        } else if (index <= 5u) {
            facts = {conversion(Representation::ScptFloat32, Representation::Float32),
                finiteFloatDomain(Authority::LosslessRepresentation),
                SctExternalDomainFact{External::UnknownExternalConsumer}};
        } else {
            facts = {conversion(Representation::ScptFloat32, Representation::Int32,
                         Conversion::ScaleThenConvertTowardZero),
                SctExternalDomainFact{External::UnknownExternalConsumer}};
        }
        records.push_back(parameterRecord(op220Ids[index], 220, index, std::nullopt,
            {sharedBehavior(index >= 6u ? Stage::DownstreamConsumer : Stage::Handler,
                std::move(facts), highStructural, highStructural,
                "Nine ordered parser calls plus handler/callee dataflow")},
            index == 0u ? "Reference target identifier reaches an unsigned 16-bit lane."
                : index == 1u ? "Transform selector reaches an int32 lane."
                : index == 2u ? "Owner identifier reaches an unsigned 16-bit lane."
                : index <= 5u ? "Local offset remains a float32 lane."
                : "Local angle is scaled and converted to int32 for the sink.",
            index == 2u ? "A failed owner lookup is diagnosed before later use."
                : "No generic numeric clamp or complete external sink domain is established."));
    }

    records.push_back(parameterRecord("sct-param-v2:221:0", 221, 0, std::nullopt,
        {sharedBehavior(Stage::Handler, {conversion(Representation::ScptFloat32,
            Representation::Float32)}, highStructural,
            confidence(SctRuntimeFactConfidence::High, SctRuntimeFactConfidence::Low))},
        "The expression is evaluated but no independent selector meaning is confirmed.",
        "No relevant range guard or meaningful authored domain is established."));
    records.push_back(parameterRecord("sct-param-v2:221:1", 221, 1, std::nullopt,
        {sharedBehavior(Stage::Handler, {
            conversion(Representation::ScptFloat32, Representation::Int32,
                Conversion::ConvertTowardZero), SctExternalDomainFact{External::ObjectTable},
            SctRuntimeOutcomeFact{Outcome::LookupFailure}})},
        "The int32 result is resolved through an object lookup.",
        "Lookup failure prints a diagnostic and is followed by use of the null result."));

    for (std::uint32_t index = 0; index < 3u; ++index) {
        const auto id = index == 0u ? "sct-param-v2:238:0"
            : index == 1u ? "sct-param-v2:238:1" : "sct-param-v2:238:2";
        records.push_back(parameterRecord(id, 238, index, std::nullopt,
            {sharedBehavior(Stage::DownstreamConsumer, {
                conversion(Representation::ScptFloat32, Representation::Float32),
                finiteFloatDomain(Authority::ProvenSafe),
                SctExternalDomainFact{External::WorldCoordinates}}, highStructural, highStructural)},
            "The coordinate remains float32 and its useful domain belongs to the map system.",
            "The opcode handler applies no coordinate bounds check."));
    }

    records.push_back(parameterRecord("sct-param-v2:253:0", 253, 0, std::nullopt,
        {sharedBehavior(Stage::DownstreamConsumer, {
            conversion(Representation::ScptFloat32, Representation::Int32,
                Conversion::ConvertTowardZero), signedDomain(Authority::ProvenSafe, 1, 3),
            SctRuntimeOutcomeFact{Outcome::UncheckedWrite}},
            confidence(SctRuntimeFactConfidence::High, SctRuntimeFactConfidence::High),
            mediumHighSemantic)},
        "The indexed-light selector has three proven slots numbered 1..3.",
        "Other values write adjacent state without a handler clamp."));
    records.push_back(parameterRecord("sct-param-v2:253:1", 253, 1, std::nullopt,
        {sharedBehavior(Stage::Handler, {
            conversion(Representation::ScptFloat32, Representation::Int32,
                Conversion::ConvertTowardZero), discreteDomain(Authority::Conventional, {0, 1})})},
        "Enable is stored as int32; zero and one are the conventional values.",
        "Other int32 values are stored without a boolean clamp."));
    records.push_back(parameterRecord("sct-param-v2:253:2", 253, 2, std::nullopt,
        {sharedBehavior(Stage::Handler, {
            conversion(Representation::ScptFloat32, Representation::UInt8,
                Conversion::NarrowLowBits), unsignedDomain(Authority::LosslessRepresentation, 0u, 255u),
            unsignedDomain(Authority::Conventional, 0u, 100u),
            SctRuntimeOutcomeFact{Outcome::TruncateModulo}}, highStructural,
            confidence(SctRuntimeFactConfidence::High, SctRuntimeFactConfidence::Medium))},
        "Stability is stored as eight bits; 0..100 is the semantic percentage convention.",
        "There is no percentage clamp and larger values truncate modulo 256."));

    records.push_back(parameterRecord("sct-param-v2:263:0", 263, 0, std::nullopt,
        {sharedBehavior(Stage::Handler, {
            SctParameterRelationshipFact{SctParameterRelationshipKind::CallbackCountShape, 1u,
                {{0, 1}, {3, 1}, {1, 6}, {2, 8}}},
            SctRuntimeOutcomeFact{Outcome::Overread}, SctRuntimeOutcomeFact{Outcome::Overwrite}},
            highStructural, mediumHighSemantic)},
        "Expression count is tied to callback selector shapes 0/3->1, 1->6, and 2->8.",
        "No staging-capacity guard prevents overrun or callback starvation."));
    records.push_back(parameterRecord("sct-param-v2:263:1", 263, 1, std::nullopt,
        {sharedBehavior(Stage::Handler, {
            conversion(Representation::ScptFloat32, Representation::Int32,
                Conversion::ConvertTowardZero), signedDomain(Authority::ProvenSafe, 0, 3),
            SctRuntimeOutcomeFact{Outcome::UncheckedRead}})},
        "The callback selector indexes the confirmed four-entry table.",
        "No callback-table bounds check protects values outside 0..3."));

    records.push_back(parameterRecord("sct-param-v2:265:0", 265, 0, std::nullopt,
        {behavior(gameCubeProfiles(), Stage::Handler, {
            conversion(Representation::ScptFloat32, Representation::Int32,
                Conversion::ConvertTowardZero),
            signedDomain(Authority::LosslessRepresentation,
                std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max())})},
        "The displayed value is converted to int32 by the GameCube implementation.",
        "The Dreamcast dispatch slot is unavailable."));
    records.push_back(parameterRecord("sct-param-v2:265:1", 265, 1, std::nullopt,
        {behavior(gameCubeProfiles(), Stage::ControlFlow, {
            SctParameterRelationshipFact{SctParameterRelationshipKind::TargetIsIndexedSctString,
                std::nullopt, {}}})},
        "The GameCube operand targets an indexed SCT string section.",
        "The Dreamcast dispatch slot is unavailable."));

    return records;
}

std::vector<SctOpcodeRuntimeFactRecord> makeOpcodeRecords() {
    using Stage = SctRuntimeFactStage;
    using Outcome = SctRuntimeOutcomeKind;
    return {{"sct-opcode-v2:011:call-depth", 11, "callDepth",
        {sharedBehavior(Stage::ImplicitOpcodeState, {
            SctRuntimeCapacityFact{32u}, SctRuntimeOutcomeFact{Outcome::DiagnosticThenContinue},
            SctRuntimeOutcomeFact{Outcome::Overwrite}},
            confidence(SctRuntimeFactConfidence::High, SctRuntimeFactConfidence::High),
            confidence(SctRuntimeFactConfidence::High, SctRuntimeFactConfidence::High),
            "Handler families and adjacent GameCube return-address storage")},
        "The return stack safely stores 32 frames at indices 0..31.",
        "The next push diagnoses overflow but continues and writes index 32."}};
}

const std::vector<SctOpcodeParameterFactRecord> kParameterRecords = makeParameterRecords();
const std::vector<SctOpcodeRuntimeFactRecord> kOpcodeRecords = makeOpcodeRecords();

std::vector<Profile> selectProfiles(const SctRuntimeFactTarget& target) {
    if (const auto* profile = std::get_if<Profile>(&target)) return {*profile};
    if (const auto* platform = std::get_if<SctPlatform>(&target)) {
        return *platform == SctPlatform::GameCube ? gameCubeProfiles() : dreamcastProfiles();
    }
    return allProfiles();
}

bool isAvailable(const SctOpcodeSchema& schema, Profile profile) {
    const auto* descriptor = findSctRuntimeProfile(profile);
    if (descriptor == nullptr) return false;
    const auto availability = descriptor->platform == SctPlatform::GameCube
        ? schema.gameCubeAvailability : schema.dreamcastAvailability;
    return availability == SctOpcodeAvailability::Available;
}

bool containsProfile(std::span<const Profile> profiles, Profile profile) {
    return std::find(profiles.begin(), profiles.end(), profile) != profiles.end();
}

template <typename Record>
std::vector<Record> filterRecords(std::span<const Record> records,
    std::span<const Profile> availableProfiles) {
    std::vector<Record> result;
    for (const auto& source : records) {
        Record filtered = source;
        filtered.behaviors.clear();
        for (const auto& behaviorSource : source.behaviors) {
            auto behavior = behaviorSource;
            behavior.profiles.clear();
            behavior.evidence.clear();
            for (const auto profile : behaviorSource.profiles) {
                if (containsProfile(availableProfiles, profile)) behavior.profiles.push_back(profile);
            }
            for (const auto& evidence : behaviorSource.evidence) {
                if (containsProfile(behavior.profiles, evidence.profile)) {
                    behavior.evidence.push_back(evidence);
                }
            }
            if (!behavior.profiles.empty()) filtered.behaviors.push_back(std::move(behavior));
        }
        if (!filtered.behaviors.empty()) result.push_back(std::move(filtered));
    }
    return result;
}

template <typename Record>
bool profileBehaviorsEqual(std::span<const Record> records, Profile left, Profile right) {
    for (const auto& record : records) {
        const SctProfiledRuntimeBehavior* leftBehavior = nullptr;
        const SctProfiledRuntimeBehavior* rightBehavior = nullptr;
        for (const auto& behavior : record.behaviors) {
            if (containsProfile(behavior.profiles, left)) leftBehavior = &behavior;
            if (containsProfile(behavior.profiles, right)) rightBehavior = &behavior;
        }
        if ((leftBehavior == nullptr) != (rightBehavior == nullptr)) return false;
        if (leftBehavior != nullptr && (leftBehavior->stage != rightBehavior->stage
            || leftBehavior->facts != rightBehavior->facts)) return false;
    }
    return true;
}

template <typename Record>
SctRuntimeFactAgreement calculateAgreement(std::span<const Profile> selected,
    std::span<const Profile> available, std::span<const Record> records) {
    if (records.empty()) return SctRuntimeFactAgreement::NoEvidence;
    if (available.size() < selected.size()) return SctRuntimeFactAgreement::PartialAvailability;
    if (available.size() == 1u) return SctRuntimeFactAgreement::SingleProfile;
    for (std::size_t index = 1; index < available.size(); ++index) {
        if (!profileBehaviorsEqual(records, available.front(), available[index])) {
            return SctRuntimeFactAgreement::Divergent;
        }
    }
    return SctRuntimeFactAgreement::Uniform;
}

} // namespace

std::span<const SctRuntimeProfile> sctRuntimeProfiles() noexcept {
    return kProfiles;
}

const SctRuntimeProfile* findSctRuntimeProfile(SctRuntimeProfileId id) noexcept {
    const auto found = std::find_if(kProfiles.begin(), kProfiles.end(),
        [id](const auto& profile) { return profile.id == id; });
    return found == kProfiles.end() ? nullptr : &*found;
}

std::span<const SctOpcodeParameterFactRecord> sctOpcodeParameterFactCatalog() noexcept {
    return kParameterRecords;
}

std::span<const SctOpcodeRuntimeFactRecord> sctOpcodeRuntimeFactCatalog() noexcept {
    return kOpcodeRecords;
}

SctOpcodeParameterFactQueryResult SctOpcodeParameterFacts::query(
    std::uint16_t opcode, std::uint32_t schemaIndex, const SctRuntimeFactTarget& target) {
    SctOpcodeParameterFactQueryResult result;
    const auto* schema = findSctOpcodeSchema(opcode);
    if (schema == nullptr) {
        result.status = SctRuntimeFactQueryStatus::UnknownOpcode;
        return result;
    }
    if (sctOpcodeParameterSchema(*schema, schemaIndex) == nullptr) {
        result.status = SctRuntimeFactQueryStatus::UnknownParameter;
        return result;
    }
    result.selectedProfiles = selectProfiles(target);
    for (const auto profile : result.selectedProfiles) {
        if (isAvailable(*schema, profile)) result.availableProfiles.push_back(profile);
    }
    if (result.availableProfiles.empty()) {
        result.status = SctRuntimeFactQueryStatus::UnavailableForTarget;
        return result;
    }
    std::vector<SctOpcodeParameterFactRecord> matching;
    for (const auto& record : kParameterRecords) {
        if (record.opcode == opcode && record.schemaIndex == schemaIndex) matching.push_back(record);
    }
    result.records = filterRecords<SctOpcodeParameterFactRecord>(matching, result.availableProfiles);
    result.status = result.records.empty() ? SctRuntimeFactQueryStatus::NoConfirmedFacts
                                           : SctRuntimeFactQueryStatus::Available;
    result.agreement = calculateAgreement(result.selectedProfiles, result.availableProfiles,
        std::span<const SctOpcodeParameterFactRecord>{result.records});
    return result;
}

SctOpcodeRuntimeFactQueryResult SctOpcodeRuntimeFacts::query(
    std::uint16_t opcode, const SctRuntimeFactTarget& target) {
    SctOpcodeRuntimeFactQueryResult result;
    const auto* schema = findSctOpcodeSchema(opcode);
    if (schema == nullptr) {
        result.status = SctRuntimeFactQueryStatus::UnknownOpcode;
        return result;
    }
    result.selectedProfiles = selectProfiles(target);
    for (const auto profile : result.selectedProfiles) {
        if (isAvailable(*schema, profile)) result.availableProfiles.push_back(profile);
    }
    if (result.availableProfiles.empty()) {
        result.status = SctRuntimeFactQueryStatus::UnavailableForTarget;
        return result;
    }
    std::vector<SctOpcodeRuntimeFactRecord> matching;
    for (const auto& record : kOpcodeRecords) {
        if (record.opcode == opcode) matching.push_back(record);
    }
    result.records = filterRecords<SctOpcodeRuntimeFactRecord>(matching, result.availableProfiles);
    result.status = result.records.empty() ? SctRuntimeFactQueryStatus::NoConfirmedFacts
                                           : SctRuntimeFactQueryStatus::Available;
    result.agreement = calculateAgreement(result.selectedProfiles, result.availableProfiles,
        std::span<const SctOpcodeRuntimeFactRecord>{result.records});
    return result;
}

} // namespace spice::sct
