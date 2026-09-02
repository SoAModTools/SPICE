#include "SctOpcodeParameterFacts.h"

#include <algorithm>
#include <array>
#include <limits>

namespace spice::sct {
namespace {

using Profile = SctRuntimeProfileId;
using Payload = SctRuntimeFactPayload;

constexpr std::array<SctRuntimeProfile, 7> kProfiles{{
    {Profile::GameCubeUsRetail20021219, SctPlatform::GameCube,
        "GameCube US retail 2002-12-19",
        "9A549F1424BBD7D4D22491ED26CA0A7E47F4B064B9C9FC39B6D60D1124EF37B8",
        "US retail Start.dol used by the GameCube SCT handler research"},
    {Profile::GameCubeJpRetail20021112, SctPlatform::GameCube,
        "GameCube JP retail 2002-11-12",
        "6A8212FEA4D21E40BB294D4563A2B4A339D50506A14D87566CB239A1547953C6",
        "Retail JP Start.dol inspected for SCT dispatch and parameter-parser compatibility"},
    {Profile::GameCubeEuRetail20030305, SctPlatform::GameCube,
        "GameCube EU retail 2003-03-05",
        "32B83047D12FEC618A435A8015AD5F72DF0B354A8850A88EE759B266D1D044EB",
        "Retail EU Start.dol inspected for SCT dispatch and parameter-parser compatibility"},
    {Profile::DreamcastUsRetailDisc1, SctPlatform::Dreamcast,
        "Dreamcast US retail Disc 1",
        "4218C07829D63080BE2DA86413EC023E6D77001C13A1494648DB6E2E5CB73611",
        "Retail US 1ST_READ.BIN inspected for SCT dispatch and parameter dataflow"},
    {Profile::DreamcastJpRetailDisc1, SctPlatform::Dreamcast,
        "Dreamcast JP retail Disc 1",
        "CC9F585DB9121D9DCFFFC77471A6BDF472CFC60D03ED9310A744264D6EAF2AD1",
        "Retail Japanese 1ST_READ.BIN inspected for SCT dispatch and parameter dataflow"},
    {Profile::DreamcastEuRetailDisc1, SctPlatform::Dreamcast,
        "Dreamcast EU retail Disc 1",
        "F09FA463E8BCB50196056D0BEE4331E94A3770158C39FAFE2BE97BE3045BBB68",
        "Retail EU 1ST_READ.BIN used for resolved downstream SCT parameter tracing"},
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
    return {Profile::GameCubeUsRetail20021219, Profile::GameCubeJpRetail20021112,
        Profile::GameCubeEuRetail20030305};
}

std::vector<Profile> dreamcastProfiles() {
    return {Profile::DreamcastUsRetailDisc1, Profile::DreamcastJpRetailDisc1,
        Profile::DreamcastEuRetailDisc1, Profile::DreamcastCustomEuDerived2026};
}

std::vector<Profile> initialResearchProfiles() {
    return {Profile::GameCubeUsRetail20021219, Profile::DreamcastUsRetailDisc1,
        Profile::DreamcastJpRetailDisc1, Profile::DreamcastCustomEuDerived2026};
}

std::vector<Profile> resolvedV3Profiles() {
    return {Profile::GameCubeUsRetail20021219, Profile::DreamcastEuRetailDisc1};
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
    return behavior(initialResearchProfiles(), stage, std::move(facts), gameCubeConfidence,
        dreamcastConfidence, method);
}

struct ResolvedV3EvidenceAddresses {
    std::uint16_t opcode;
    std::uint32_t schemaIndex;
    std::uint32_t gameCubeHandler;
    std::uint32_t gameCubeParserCall;
    std::uint32_t dreamcastEuHandler;
    std::uint32_t dreamcastEuParserCall;
};

constexpr std::array<ResolvedV3EvidenceAddresses, 84> kResolvedV3Evidence{{
    {16,0,0x801f570c,0x801f5768,0x8c04b0c8,0x8c04b102}, {17,0,0x801f5b14,0x801f5b60,0x8c04acde,0x8c04ad12},
    {18,0,0x801f5a74,0x801f5ac0,0x8c04ad74,0x8c04adb2}, {19,0,0x801f59c0,0x801f5a0c,0x8c04ae10,0x8c04ae4e},
    {20,0,0x801f593c,0x801f5988,0x8c04aee4,0x8c04af18}, {21,0,0x801f58b8,0x801f5904,0x8c04af68,0x8c04afa6},
    {26,0,0x802088d8,0x80208938,0x8c04d31c,0x8c04d362}, {30,1,0x802085cc,0x80208698,0x8c04d5d6,0x8c04d6aa},
    {31,7,0x80208234,0x80208348,0x8c04d974,0x8c04dab6}, {32,7,0x80208044,0x80208158,0x8c04dbb0,0x8c04dcf2},
    {40,2,0x80206ea8,0x80206f7c,0x8c04edb8,0x8c04ee58}, {41,0,0x80207a40,0x80207aa0,0x8c04e21c,0x8c04e25a},
    {46,0,0x8020549c,0x802054d8,0x8c050560,0x8c050592}, {46,1,0x8020549c,0x802054f4,0x8c050560,0x8c0505a8},
    {46,2,0x8020549c,0x80205510,0x8c050560,0x8c0505be}, {48,0,0x802051d0,0x8020522c,0x8c0509a4,0x8c0509f0},
    {50,3,0x80204cd0,0x80204d5c,0x8c050de8,0x8c050e40}, {50,4,0x80204cd0,0x80204d68,0x8c050de8,0x8c050e4e},
    {51,3,0x801fa060,0x801fa168,0x8c067954,0x8c067a5a}, {53,19,0x801fa630,0x801faa50,0x8c066e62,0x8c06731a},
    {54,0,0x801fa4c0,0x801fa514,0x8c0674ca,0x8c067512}, {55,2,0x801fa398,0x801fa470,0x8c06766c,0x8c067726},
    {72,8,0x80206cbc,0x80206d94,0x8c04eeec,0x8c04efd4}, {73,9,0x80206ab0,0x80206c18,0x8c04f148,0x8c04f29a},
    {75,7,0x8020660c,0x802066fc,0x8c04f4a6,0x8c04f546}, {79,6,0x80201228,0x80201394,0x8c054c4e,0x8c054dba},
    {94,0,0x801ffdb4,0x801ffe04,0x8c056688,0x8c0566c0}, {95,5,0x80206424,0x802064d4,0x8c04f72c,0x8c04f7a4},
    {96,5,0x8020623c,0x802062ec,0x8c04f8f8,0x8c04f970}, {97,2,0x80208a20,0x80208b2c,0x8c04d110,0x8c04d222},
    {114,0,0x801fef80,0x801fefd4,0x8c057550,0x8c057592}, {120,6,0x801f8608,0x801f87ac,0x8c06936e,0x8c06952a},
    {121,6,0x802034b0,0x8020382c,0x8c051ce8,0x8c05208a}, {122,2,0x80205f14,0x80205fa4,0x8c04fbf8,0x8c04fc5e},
    {122,3,0x80205f14,0x80205fc0,0x8c04fbf8,0x8c04fc7c}, {126,1,0x80207554,0x802075d4,0x8c04e810,0x8c04e86a},
    {127,1,0x80207630,0x802076b0,0x8c04e728,0x8c04e782}, {135,2,0x80201458,0x80201514,0x8c054b00,0x8c054ba6},
    {136,4,0x80209b94,0x80209c80,0x8c04be8c,0x8c04bf92}, {139,6,0x80202dec,0x80203168,0x8c052930,0x8c052ce2},
    {142,0,0x801fe560,0x801fe5b4,0x8c057e62,0x8c057ea6}, {142,1,0x801fe560,0x801fe5f0,0x8c057e62,0x8c057ede},
    {142,2,0x801fe560,0x801fe62c,0x8c057e62,0x8c057f26}, {146,0,0x801fe06c,0x801fe0c4,0x8c0583c8,0x8c058404},
    {147,0,0x801f9c18,0x801f9c8c,0x8c067c90,0x8c067cd6}, {147,9,0x801f9c18,0x801f9dfc,0x8c067c90,0x8c067e4c},
    {149,0,0x801ff604,0x801ff658,0x8c056e62,0x8c056eaa}, {150,0,0x801ff470,0x801ff4c4,0x8c056f88,0x8c056fd2},
    {152,1,0x8020ceb4,0x8020cf38,0x8c05d834,0x8c05d892}, {154,0,0x8020c390,0x8020c3ec,0x8c05de84,0x8c05ded6},
    {159,6,0x802023bc,0x80202640,0x8c053110,0x8c0533d2}, {161,10,0x801f8158,0x801f8354,0x8c069672,0x8c0698fa},
    {162,0,0x80205e48,0x80205ea8,0x8c04fda0,0x8c04fdde}, {163,6,0x80205c24,0x80205d14,0x8c04fe68,0x8c04ff20},
    {163,7,0x80205c24,0x80205d20,0x8c04fe68,0x8c04ff2e}, {163,8,0x80205c24,0x80205d54,0x8c04fe68,0x8c04ff66},
    {164,1,0x8020184c,0x8020191c,0x8c054696,0x8c05476e}, {165,0,0x80215590,0x802155e4,0x8c07bb70,0x8c07bbc4},
    {165,8,0x80215590,0x802157e8,0x8c07bb70,0x8c07bdf8}, {176,5,0x80209974,0x80209aa0,0x8c04c0a4,0x8c04c1ea},
    {177,0,0x8020dd1c,0x8020ddd8,0x8c05c134,0x8c05c19a}, {178,7,0x802049bc,0x80204ae4,0x8c050ff4,0x8c0510ea},
    {183,0,0x801fdfb8,0x801fe010,0x8c058560,0x8c05859a}, {195,1,0x801fdab8,0x801fdb80,0x8c058990,0x8c058a48},
    {204,0,0x80214b28,0x80214b80,0x8c07ca34,0x8c07ca70}, {207,1,0x8020bdb0,0x8020be50,0x8c05eaa0,0x8c05eb22},
    {207,2,0x8020bdb0,0x8020be8c,0x8c05eaa0,0x8c05eb5a}, {216,0,0x801fedd0,0x801fee20,0x8c057784,0x8c0577bc},
    {221,0,0x801fd584,0x801fd5d0,0x8c059028,0x8c059060}, {230,5,0x80205a3c,0x80205aec,0x8c050034,0x8c0500ac},
    {233,1,0x8020cba4,0x8020cc40,0x8c05dba8,0x8c05dc2a}, {242,1,0x801fcff0,0x801fd07c,0x8c0594b0,0x8c059536},
    {242,2,0x801fcff0,0x801fd0b8,0x8c0594b0,0x8c05956e}, {243,0,0x801f9988,0x801f99fc,0x8c067eec,0x8c067f32},
    {243,9,0x801f9988,0x801f9b6c,0x8c067eec,0x8c0680a8}, {244,0,0x8020bee8,0x8020bf44,0x8c05e5cc,0x8c05e614},
    {244,1,0x8020bee8,0x8020bf84,0x8c05e5cc,0x8c05e65e}, {249,1,0x80208424,0x802084f0,0x8c04d7a4,0x8c04d87a},
    {256,0,0x801fdc70,0x801fdcb8,0x8c0588cc,0x8c05890e}, {258,1,0x801fd44c,0x801fd4cc,0x8c059168,0x8c0591c2},
    {262,0,0x801fe310,0x801fe364,0x8c058096,0x8c0580ea}, {262,1,0x801fe310,0x801fe3a0,0x8c058096,0x8c058122},
    {262,2,0x801fe310,0x801fe3dc,0x8c058096,0x8c05816a}, {264,7,0x801f7f98,0x801f8098,0x8c069c6a,0x8c069daa},
}};

const ResolvedV3EvidenceAddresses& resolvedV3Evidence(std::uint16_t opcode,
    std::uint32_t schemaIndex) {
    const auto found = std::find_if(kResolvedV3Evidence.begin(), kResolvedV3Evidence.end(),
        [=](const auto& row) { return row.opcode == opcode && row.schemaIndex == schemaIndex; });
    return *found;
}

SctProfiledRuntimeBehavior resolvedV3Behavior(std::uint16_t opcode,
    std::uint32_t schemaIndex, SctRuntimeFactStage stage, std::vector<Payload> facts,
    SctRuntimeFactConfidencePair factConfidence = confidence(
        SctRuntimeFactConfidence::High, SctRuntimeFactConfidence::High)) {
    const auto& addresses = resolvedV3Evidence(opcode, schemaIndex);
    SctProfiledRuntimeBehavior result;
    result.profiles = resolvedV3Profiles();
    result.stage = stage;
    result.facts = std::move(facts);
    result.evidence = {
        {Profile::GameCubeUsRetail20021219, factConfidence, addresses.gameCubeHandler,
            addresses.gameCubeParserCall, "Resolved-v3 handler and downstream dataflow review"},
        {Profile::DreamcastEuRetailDisc1, factConfidence, addresses.dreamcastEuHandler,
            addresses.dreamcastEuParserCall, "Resolved-v3 handler and downstream dataflow review"},
    };
    return result;
}

SctProfiledRuntimeBehavior resolvedV3ProfileBehavior(std::uint16_t opcode,
    std::uint32_t schemaIndex, Profile profile, SctRuntimeFactStage stage,
    std::vector<Payload> facts) {
    const auto& addresses = resolvedV3Evidence(opcode, schemaIndex);
    const bool gameCube = profile == Profile::GameCubeUsRetail20021219;
    SctProfiledRuntimeBehavior result;
    result.profiles = {profile};
    result.stage = stage;
    result.facts = std::move(facts);
    result.evidence = {{profile,
        confidence(SctRuntimeFactConfidence::High, SctRuntimeFactConfidence::High),
        gameCube ? addresses.gameCubeHandler : addresses.dreamcastEuHandler,
        gameCube ? addresses.gameCubeParserCall : addresses.dreamcastEuParserCall,
        "Resolved-v3 handler and downstream dataflow review"}};
    return result;
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

SctRuntimeValueFlowFact flow(std::initializer_list<SctRuntimeValueFlowStep> steps) {
    return {{steps.begin(), steps.end()}};
}

SctRuntimeValueFlowFact low16Flow() {
    return flow({conversion(SctRuntimeValueRepresentation::ScptFloat32,
                     SctRuntimeValueRepresentation::Int32,
                     SctRuntimeConversionKind::ConvertTowardZero),
        SctBitMaskFact{0x0000ffffu, 0u, SctBitMaskPurpose::RuntimeValueMask}});
}

SctRuntimeValueFlowFact bitWordIndexFlow() {
    return flow({conversion(SctRuntimeValueRepresentation::ScptFloat32,
                     SctRuntimeValueRepresentation::Int32,
                     SctRuntimeConversionKind::ConvertTowardZero),
        SctRuntimeArithmeticFact{SctRuntimeArithmeticOperation::ShiftRight,
            SctRuntimeScalarValue{std::int64_t{5}}}});
}

SctRuntimeValueFlowFact bitWithinWordFlow() {
    return flow({conversion(SctRuntimeValueRepresentation::ScptFloat32,
                     SctRuntimeValueRepresentation::Int32,
                     SctRuntimeConversionKind::ConvertTowardZero),
        SctBitMaskFact{0x0000001fu, 0u, SctBitMaskPurpose::RuntimeValueMask}});
}

SctRuntimeValueFlowFact degreeAngleFlow() {
    return flow({conversion(SctRuntimeValueRepresentation::ScptFloat32,
                     SctRuntimeValueRepresentation::Float32),
        SctRuntimeArithmeticFact{SctRuntimeArithmeticOperation::Multiply,
            SctRuntimeScalarValue{double{65536.0 / 360.0}}},
        conversion(SctRuntimeValueRepresentation::Float32,
            SctRuntimeValueRepresentation::Int32,
            SctRuntimeConversionKind::ConvertTowardZero)});
}

SctRuntimeValueFlowFact booleanFlow() {
    return flow({conversion(SctRuntimeValueRepresentation::ScptFloat32,
                     SctRuntimeValueRepresentation::Int32,
                     SctRuntimeConversionKind::ConvertTowardZero),
        SctRuntimeComparisonFact{SctRuntimeComparisonKind::Nonzero, std::nullopt}});
}

SctRuntimeValueFlowFact low8Flow(std::int64_t add = 0) {
    std::vector<SctRuntimeValueFlowStep> steps;
    steps.push_back(conversion(SctRuntimeValueRepresentation::ScptFloat32,
        SctRuntimeValueRepresentation::Int32,
        SctRuntimeConversionKind::ConvertTowardZero));
    if (add != 0) {
        steps.push_back(SctRuntimeArithmeticFact{SctRuntimeArithmeticOperation::Add,
            SctRuntimeScalarValue{add}});
    }
    steps.push_back(SctBitMaskFact{0x000000ffu, 0u,
        SctBitMaskPurpose::RuntimeValueMask});
    return {std::move(steps)};
}

SctOpcodeParameterFactRecord parameterRecord(std::string_view id, std::uint16_t opcode,
    std::uint32_t schemaIndex, std::optional<SctRuntimeFactCondition> recordCondition,
    std::vector<SctProfiledRuntimeBehavior> behaviors, std::string_view summary,
    std::string_view outOfDomain,
    SctRuntimeFactResolution resolution = SctRuntimeFactResolution::Confirmed) {
    SctOpcodeParameterFactRecord result;
    result.sourceRecordId = id;
    result.opcode = opcode;
    result.schemaIndex = schemaIndex;
    result.resolution = resolution;
    result.condition = std::move(recordCondition);
    result.behaviors = std::move(behaviors);
    result.summary = summary;
    result.outOfDomainSummary = outOfDomain;
    return result;
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
        {resolvedV3Behavior(16, 0, Stage::Handler, {
            conversion(Representation::ScptFloat32, Representation::Int32,
                Conversion::ConvertTowardZero),
            signedDomain(Authority::LosslessRepresentation,
                std::numeric_limits<std::int32_t>::min(),
                std::numeric_limits<std::int32_t>::max()),
            SctSpecialEncodedValueFact{0x7f7fffffu,
                SctSpecialEncodedValueKind::ReplacementRuntimeValue,
                SctRuntimeScalarValue{std::int64_t{std::numeric_limits<std::int32_t>::max()}}},
            SctRuntimeComparisonFact{SctRuntimeComparisonKind::LessOrEqual,
                SctRuntimeScalarValue{std::int64_t{0}}},
            SctRuntimeOutcomeFact{Outcome::ImmediateCompletion}})},
        "Both evidenced handlers use a signed int32 frame duration; the default becomes INT32_MAX.",
        "Nonpositive durations complete immediately; positive values have no upper clamp."));

    for (const auto opcode : {17u, 18u, 19u}) {
        const auto id = opcode == 17u ? "sct-param-v2:017:0"
            : opcode == 18u ? "sct-param-v2:018:0" : "sct-param-v2:019:0";
        records.push_back(parameterRecord(id, static_cast<std::uint16_t>(opcode), 0,
            std::nullopt, {resolvedV3Behavior(static_cast<std::uint16_t>(opcode), 0,
                Stage::Handler, {bitWordIndexFlow(), bitWithinWordFlow(),
                SctExternalDomainFact{External::FlagTable},
                SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::UsedForIndexedAccess},
                SctRuntimeOutcomeFact{Outcome::UncheckedRead}})},
            "The int32 index selects word index>>5 and bit index&31 in a flag array.",
            "No bounds check protects the selected word."));
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

    auto op114p0 = resolvedV3Behavior(114, 0, Stage::Callee, {
        conversion(Representation::ScptFloat32, Representation::UInt16,
            Conversion::NarrowLowBits),
        unsignedDomain(Authority::LosslessRepresentation, 0u, 65535u),
        SctSpecialEncodedValueFact{0x7f7fffffu,
            SctSpecialEncodedValueKind::ReplacementRuntimeValue, SctRuntimeScalarValue{std::uint64_t{65535}}},
        SctExternalDomainFact{External::EventEntryTable},
        SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::UsedForLookup},
        SctRuntimeOutcomeFact{Outcome::LookupFailure}});
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
        {resolvedV3Behavior(221, 0, Stage::Handler, {
            SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::EvaluatedAndDiscarded}})},
        "The expression is evaluated and its result is discarded on both evidenced binaries.",
        "All values have the same observed downstream disposition apart from expression side effects."));
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
        {behavior({Profile::GameCubeUsRetail20021219}, Stage::Handler, {
            conversion(Representation::ScptFloat32, Representation::Int32,
                Conversion::ConvertTowardZero),
            signedDomain(Authority::LosslessRepresentation,
                std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max())})},
        "The displayed value is converted to int32 by the GameCube implementation.",
        "The Dreamcast dispatch slot is unavailable."));
    records.push_back(parameterRecord("sct-param-v2:265:1", 265, 1, std::nullopt,
        {behavior({Profile::GameCubeUsRetail20021219}, Stage::ControlFlow, {
            SctParameterRelationshipFact{SctParameterRelationshipKind::TargetIsIndexedSctString,
                std::nullopt, {}}})},
        "The GameCube operand targets an indexed SCT string section.",
        "The Dreamcast dispatch slot is unavailable."));

    const auto addV3 = [&](std::string_view id, std::uint16_t opcode,
        std::uint32_t schemaIndex, Stage stage, std::vector<Payload> facts,
        std::string_view summary, std::string_view outOfDomain,
        SctRuntimeFactResolution resolution = SctRuntimeFactResolution::Confirmed,
        SctRuntimeFactConfidencePair factConfidence = confidence(
            SctRuntimeFactConfidence::High, SctRuntimeFactConfidence::High)) {
        records.push_back(parameterRecord(id, opcode, schemaIndex, std::nullopt,
            {resolvedV3Behavior(opcode, schemaIndex, stage, std::move(facts), factConfidence)},
            summary, outOfDomain, resolution));
    };

    struct V3Site { std::string_view id; std::uint16_t opcode; std::uint32_t index; };

    for (const auto& site : std::array<V3Site, 3>{{
             {"sct-param-v3:020:0",20,0}, {"sct-param-v3:021:0",21,0},
             {"sct-param-v3:183:0",183,0}}}) {
        addV3(site.id, site.opcode, site.index, Stage::Callee, {
            conversion(Representation::ScptFloat32, Representation::Int16,
                Conversion::NarrowLowBits),
            signedDomain(Authority::LosslessRepresentation, -32768, 32767),
            SctExternalDomainFact{External::ItemTable},
            SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::UsedForLookup}},
            "The evaluated value reaches the item system as a signed 16-bit identifier.",
            "Wider values narrow before category-specific lookup behavior.");
    }

    for (const auto& site : std::array<V3Site, 13>{{
             {"sct-param-v3:026:0",26,0}, {"sct-param-v3:126:1",126,1},
             {"sct-param-v3:127:1",127,1}, {"sct-param-v3:135:2",135,2},
             {"sct-param-v3:146:0",146,0}, {"sct-param-v3:147:0",147,0},
             {"sct-param-v3:149:0",149,0}, {"sct-param-v3:150:0",150,0},
             {"sct-param-v3:152:1",152,1}, {"sct-param-v3:154:0",154,0},
             {"sct-param-v3:165:8",165,8}, {"sct-param-v3:243:0",243,0},
             {"sct-param-v3:258:1",258,1}}}) {
        addV3(site.id, site.opcode, site.index, Stage::Callee, {low16Flow(),
            unsignedDomain(Authority::LosslessRepresentation, 0u, 65535u),
            SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::UsedForLookup}},
            "The value narrows to a low-16 target or object identifier before use.",
            "High bits are discarded; lookup failure follows the target subsystem.");
    }

    for (const auto& site : std::array<V3Site, 6>{{
             {"sct-param-v3:040:2",40,2}, {"sct-param-v3:072:8",72,8},
             {"sct-param-v3:162:0",162,0}, {"sct-param-v3:163:8",163,8},
             {"sct-param-v3:164:1",164,1}, {"sct-param-v3:178:7",178,7}}}) {
        addV3(site.id, site.opcode, site.index, Stage::ControlFlow, {booleanFlow(),
            SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::UsedForControlDecision}},
            "The retained integer value is tested as zero versus nonzero.",
            "Values other than zero and one remain meaningful as nonzero flags.");
    }

    for (const auto& site : std::array<V3Site, 13>{{
             {"sct-param-v3:046:0",46,0}, {"sct-param-v3:046:1",46,1},
             {"sct-param-v3:046:2",46,2}, {"sct-param-v3:050:3",50,3},
             {"sct-param-v3:050:4",50,4}, {"sct-param-v3:075:7",75,7},
             {"sct-param-v3:121:6",121,6}, {"sct-param-v3:122:2",122,2},
             {"sct-param-v3:122:3",122,3}, {"sct-param-v3:139:6",139,6},
             {"sct-param-v3:159:6",159,6}, {"sct-param-v3:163:6",163,6},
             {"sct-param-v3:163:7",163,7}}}) {
        addV3(site.id, site.opcode, site.index, Stage::DownstreamConsumer,
            {degreeAngleFlow(), SctRuntimeValueDispositionFact{
                SctRuntimeValueDispositionKind::Stored}},
            "The float degree value is scaled to the engine's signed int32 angle unit.",
            "No degree clamp is applied; later short-angle operations may wrap.");
    }

    for (const auto& site : std::array<V3Site, 2>{{
             {"sct-param-v3:048:0",48,0}, {"sct-param-v3:195:1",195,1}}}) {
        addV3(site.id, site.opcode, site.index, Stage::Handler, {
            conversion(Representation::ScptFloat32, Representation::Int32,
                Conversion::ConvertTowardZero),
            signedDomain(Authority::LosslessRepresentation,
                std::numeric_limits<std::int32_t>::min(),
                std::numeric_limits<std::int32_t>::max()),
            SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::Stored}},
            "The value is retained as signed int32 state.",
            "Normal signed-int32 behavior applies without a narrower proven domain.");
    }

    addV3("sct-param-v3:051:3", 51, 3, Stage::Handler,
        {SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::EvaluatedAndDiscarded}},
        "The expression is evaluated and its result is discarded.",
        "Only expression-evaluation side effects can vary with the value.");

    for (const auto& site : std::array<V3Site, 5>{{
             {"sct-param-v3:073:9",73,9}, {"sct-param-v3:094:0",94,0},
             {"sct-param-v3:095:5",95,5}, {"sct-param-v3:096:5",96,5},
             {"sct-param-v3:230:5",230,5}}}) {
        addV3(site.id, site.opcode, site.index, Stage::Callee, {low16Flow(),
            unsignedDomain(Authority::LosslessRepresentation, 0u, 65535u),
            SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::Stored}},
            "The value narrows to a low-16 helper argument or stored field.",
            "High bits are discarded at the 16-bit sink.");
    }

    addV3("sct-param-v3:079:6", 79, 6, Stage::Handler, {
        conversion(Representation::ScptFloat32, Representation::Int32,
            Conversion::ConvertTowardZero),
        SctSpecialEncodedValueFact{0x7f7fffffu,
            SctSpecialEncodedValueKind::ReplacementRuntimeValue,
            SctRuntimeScalarValue{std::int64_t{1}}},
        SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::Stored}},
        "The frame count is signed int32 and the default value becomes one.",
        "No int8 restriction or upper clamp was found.");

    addV3("sct-param-v3:120:6", 120, 6, Stage::Handler, {
        flow({conversion(Representation::ScptFloat32, Representation::Int32,
                  Conversion::ConvertTowardZero),
            SctBitMaskFact{0x0000ffffu, 0u, SctBitMaskPurpose::RuntimeValueMask},
            SctRuntimeArithmeticFact{SctRuntimeArithmeticOperation::ShiftLeft,
                SctRuntimeScalarValue{std::int64_t{16}}}}),
        unsignedDomain(Authority::LosslessRepresentation, 0u, 65535u),
        SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::Stored}},
        "The low 16 bits are packed into the upper halfword.",
        "High bits are discarded before packing.");

    for (const auto& site : std::array<V3Site, 6>{{
             {"sct-param-v3:142:0",142,0}, {"sct-param-v3:142:1",142,1},
             {"sct-param-v3:142:2",142,2}, {"sct-param-v3:262:0",262,0},
             {"sct-param-v3:262:1",262,1}, {"sct-param-v3:262:2",262,2}}}) {
        addV3(site.id, site.opcode, site.index, Stage::Handler, {low8Flow(),
            unsignedDomain(Authority::LosslessRepresentation, 0u, 255u),
            SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::Stored},
            SctRuntimeOutcomeFact{Outcome::TruncateModulo}},
            "The RGB component is stored as its low eight bits.",
            "Values outside 0..255 wrap modulo 256.");
    }

    for (const auto& site : std::array<V3Site, 2>{{
             {"sct-param-v3:147:9",147,9}, {"sct-param-v3:243:9",243,9}}}) {
        addV3(site.id, site.opcode, site.index, Stage::Handler, {low8Flow(1),
            unsignedDomain(Authority::LosslessRepresentation, 0u, 255u),
            SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::Stored},
            SctRuntimeOutcomeFact{Outcome::TruncateModulo}},
            "One is added before the result is narrowed to a low-eight-bit slot.",
            "The transformed value wraps modulo 256.");
    }

    addV3("sct-param-v3:041:0", 41, 0, Stage::ControlFlow, {
        flow({conversion(Representation::ScptFloat32, Representation::Int32,
                  Conversion::ConvertTowardZero),
            SctRuntimeComparisonFact{SctRuntimeComparisonKind::Equal,
                SctRuntimeScalarValue{std::int64_t{0x10000}}},
            SctBitMaskFact{0x0000ffffu, 0u, SctBitMaskPurpose::RuntimeValueMask}}),
        SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::UsedForControlDecision},
        SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::UsedForLookup}},
        "The full int32 is compared with 0x10000 before non-special values narrow to low16.",
        "Non-special high bits are discarded before lookup.");

    addV3("sct-param-v3:165:0", 165, 0, Stage::ControlFlow, {low16Flow(),
        discreteDomain(Authority::RuntimeEnforced, {0, 1, 2, 3}),
        SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::UsedForControlDecision}},
        "The low-16 phase selector recognizes values zero through three.",
        "Other values miss the recognized phase cases.");

    addV3("sct-param-v3:177:0", 177, 0, Stage::Handler, {
        conversion(Representation::ScptFloat32, Representation::Int32,
            Conversion::ConvertTowardZero),
        SctRuntimeComparisonFact{SctRuntimeComparisonKind::GreaterOrEqual,
            SctRuntimeScalarValue{std::int64_t{0x1194}}},
        SctExternalDomainFact{External::FlagTable}, SctExternalDomainFact{External::ObjectTable},
        SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::UsedForControlDecision},
        SctRuntimeOutcomeFact{Outcome::UncheckedRead}},
        "The int32 selects discovery-bit access below 0x1194 or low-16 object lookup at and above it.",
        "Discovery indexes are unchecked; object identifiers narrow before lookup.");

    addV3("sct-param-v3:207:1", 207, 1, Stage::Callee, {
        flow({conversion(Representation::ScptFloat32, Representation::Int8,
                  Conversion::NarrowLowBits),
            SctRuntimeArithmeticFact{SctRuntimeArithmeticOperation::Add,
                SctRuntimeScalarValue{std::int64_t{128}}},
            SctRuntimeArithmeticFact{SctRuntimeArithmeticOperation::ShiftRight,
                SctRuntimeScalarValue{std::int64_t{1}}}}),
        signedDomain(Authority::LosslessRepresentation, -128, 127),
        SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::Stored}},
        "The signed-eight-bit value is transformed as (value + 128) >> 1.",
        "Wider inputs first narrow to signed eight bits.");
    addV3("sct-param-v3:207:2", 207, 2, Stage::Callee, {
        flow({conversion(Representation::ScptFloat32, Representation::Int16,
                  Conversion::NarrowLowBits),
            SctRuntimeComparisonFact{SctRuntimeComparisonKind::Greater,
                SctRuntimeScalarValue{std::int64_t{500}}}}),
        SctNumericDomainFact{Authority::RuntimeEnforced, SctNumericDomainKind::SignedInteger,
            std::nullopt, SctRuntimeScalarValue{std::int64_t{500}}, {}, false},
        SctRuntimeOutcomeFact{Outcome::Clamp}},
        "The signed-16 duration is capped above at 500.",
        "Values above 500 become 500; negative values are not clamped upward.");

    for (const auto& site : std::array<V3Site, 3>{{
             {"sct-param-v3:233:1",233,1}, {"sct-param-v3:242:2",242,2},
             {"sct-param-v3:244:1",244,1}}}) {
        addV3(site.id, site.opcode, site.index, Stage::Handler,
            {bitWordIndexFlow(), bitWithinWordFlow(),
            SctExternalDomainFact{External::FlagTable},
            SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::UsedForIndexedAccess},
            SctRuntimeOutcomeFact{Outcome::UncheckedRead}},
            "The int32 index selects word index>>5 and bit index&31.",
            "No bounds check protects the selected word.");
    }

    addV3("sct-param-v3:242:1", 242, 1, Stage::Callee, {
        conversion(Representation::ScptFloat32, Representation::Int8,
            Conversion::NarrowLowBits),
        signedDomain(Authority::LosslessRepresentation, -128, 127),
        SctExternalDomainFact{External::AbilityTable},
        SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::UsedForLookup}},
        "The ability index narrows to signed eight bits before lookup.",
        "Wider values wrap to signed eight bits.");

    addV3("sct-param-v3:244:0", 244, 0, Stage::ControlFlow, {
        flow({conversion(Representation::ScptFloat32, Representation::Int32,
                  Conversion::ConvertTowardZero),
            SctBitMaskFact{0x0000ffffu, 0u, SctBitMaskPurpose::RuntimeValueMask},
            SctRuntimeComparisonFact{SctRuntimeComparisonKind::GreaterOrEqual,
                SctRuntimeScalarValue{std::int64_t{0x200}}}}),
        SctExternalDomainFact{External::ItemTable},
        SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::UsedForControlDecision}},
        "The low-16 reward selector chooses item handling below 0x200 and gold handling otherwise.",
        "High bits are discarded before the branch.");

    const auto addUnresolved = [&](std::string_view id, std::uint16_t opcode,
        std::uint32_t index, bool signed16Producer = false) {
        std::vector<Payload> facts;
        facts.push_back(conversion(Representation::ScptFloat32,
            signed16Producer ? Representation::Int16 : Representation::Int32,
            signed16Producer ? Conversion::NarrowLowBits : Conversion::ConvertTowardZero));
        facts.push_back(SctRuntimeValueDispositionFact{
            SctRuntimeValueDispositionKind::Stored});
        facts.push_back(SctRuntimeValueDispositionFact{
            SctRuntimeValueDispositionKind::ForwardedToCallee});
        facts.push_back(SctExternalDomainFact{External::UnknownExternalConsumer});
        addV3(id, opcode, index, Stage::Handler, std::move(facts),
            "The producer representation and queued field are confirmed; the final consumer is unresolved.",
            "Behavior outside the eventual consumer domain remains unresolved.",
            SctRuntimeFactResolution::ProducerConfirmedFinalConsumerUnresolved,
            confidence(SctRuntimeFactConfidence::High, SctRuntimeFactConfidence::Low));
    };
    addUnresolved("sct-param-v3:030:1",30,1);
    addUnresolved("sct-param-v3:031:7",31,7);
    addUnresolved("sct-param-v3:032:7",32,7);
    addUnresolved("sct-param-v3:053:19",53,19);
    addUnresolved("sct-param-v3:097:2",97,2);
    addUnresolved("sct-param-v3:136:4",136,4);
    addUnresolved("sct-param-v3:161:10",161,10);
    addUnresolved("sct-param-v3:176:5",176,5);
    addUnresolved("sct-param-v3:249:1",249,1,true);
    addUnresolved("sct-param-v3:264:7",264,7);

    const auto addDivergent = [&](std::string_view id, std::uint16_t opcode,
        std::uint32_t index, SctRuntimeValueRepresentation dreamcastRepresentation,
        bool audio, bool continuation) {
        std::vector<Payload> gameCubeFacts{
            SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::EvaluatedAndDiscarded}};
        std::vector<Payload> dreamcastFacts{
            conversion(Representation::ScptFloat32, dreamcastRepresentation,
                dreamcastRepresentation == Representation::Int32
                    ? Conversion::ConvertTowardZero : Conversion::NarrowLowBits),
            SctRuntimeValueDispositionFact{SctRuntimeValueDispositionKind::ForwardedToCallee}};
        if (audio) dreamcastFacts.push_back(SctExternalDomainFact{External::AudioResourceTable});
        if (continuation) dreamcastFacts.push_back(SctRuntimeValueDispositionFact{
            SctRuntimeValueDispositionKind::UsedForControlDecision});
        records.push_back(parameterRecord(id, opcode, index, std::nullopt,
            {resolvedV3ProfileBehavior(opcode, index, Profile::GameCubeUsRetail20021219,
                 Stage::Handler, std::move(gameCubeFacts)),
             resolvedV3ProfileBehavior(opcode, index, Profile::DreamcastEuRetailDisc1,
                 Stage::Callee, std::move(dreamcastFacts))},
            "The evidenced GameCube handler discards this value while Dreamcast forwards an active argument.",
            "The Dreamcast value has no equivalent downstream effect in the evidenced GameCube binary."));
    };
    addDivergent("sct-param-v3:054:0",54,0,Representation::Int32,true,false);
    addDivergent("sct-param-v3:055:2",55,2,Representation::Int16,true,false);
    addDivergent("sct-param-v3:204:0",204,0,Representation::Int16,false,false);
    addDivergent("sct-param-v3:216:0",216,0,Representation::Int32,true,false);
    addDivergent("sct-param-v3:256:0",256,0,Representation::Int16,false,true);

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
SctRuntimeFactAgreement calculateAgreement(std::span<const Profile> evidenced,
    std::span<const Record> records) {
    if (records.empty()) return SctRuntimeFactAgreement::NoEvidence;
    if (evidenced.size() == 1u) return SctRuntimeFactAgreement::SingleProfile;
    for (std::size_t index = 1; index < evidenced.size(); ++index) {
        if (!profileBehaviorsEqual(records, evidenced.front(), evidenced[index])) {
            return SctRuntimeFactAgreement::Divergent;
        }
    }
    return SctRuntimeFactAgreement::Uniform;
}

template <typename Record>
std::vector<Profile> evidencedProfiles(std::span<const Profile> available,
    std::span<const Record> records) {
    std::vector<Profile> result;
    for (const auto profile : available) {
        const bool evidenced = std::any_of(records.begin(), records.end(), [profile](const auto& record) {
            return std::any_of(record.behaviors.begin(), record.behaviors.end(),
                [profile](const auto& behavior) { return containsProfile(behavior.profiles, profile); });
        });
        if (evidenced) result.push_back(profile);
    }
    return result;
}

SctRuntimeFactCoverage calculateCoverage(std::span<const Profile> available,
    std::span<const Profile> evidenced) {
    if (evidenced.empty()) return SctRuntimeFactCoverage::None;
    return evidenced.size() == available.size() ? SctRuntimeFactCoverage::Complete
                                                : SctRuntimeFactCoverage::Partial;
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
        if (isAvailable(*schema, profile)) result.opcodeAvailableProfiles.push_back(profile);
    }
    if (result.opcodeAvailableProfiles.empty()) {
        result.status = SctRuntimeFactQueryStatus::UnavailableForTarget;
        return result;
    }
    std::vector<SctOpcodeParameterFactRecord> matching;
    for (const auto& record : kParameterRecords) {
        if (record.opcode == opcode && record.schemaIndex == schemaIndex) matching.push_back(record);
    }
    result.records = filterRecords<SctOpcodeParameterFactRecord>(matching,
        result.opcodeAvailableProfiles);
    result.evidencedProfiles = evidencedProfiles(
        result.opcodeAvailableProfiles,
        std::span<const SctOpcodeParameterFactRecord>{result.records});
    result.coverage = calculateCoverage(result.opcodeAvailableProfiles,
        result.evidencedProfiles);
    result.status = result.records.empty() ? SctRuntimeFactQueryStatus::NoConfirmedFacts
                                           : SctRuntimeFactQueryStatus::Available;
    result.agreement = calculateAgreement(result.evidencedProfiles,
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
        if (isAvailable(*schema, profile)) result.opcodeAvailableProfiles.push_back(profile);
    }
    if (result.opcodeAvailableProfiles.empty()) {
        result.status = SctRuntimeFactQueryStatus::UnavailableForTarget;
        return result;
    }
    std::vector<SctOpcodeRuntimeFactRecord> matching;
    for (const auto& record : kOpcodeRecords) {
        if (record.opcode == opcode) matching.push_back(record);
    }
    result.records = filterRecords<SctOpcodeRuntimeFactRecord>(matching,
        result.opcodeAvailableProfiles);
    result.evidencedProfiles = evidencedProfiles(result.opcodeAvailableProfiles,
        std::span<const SctOpcodeRuntimeFactRecord>{result.records});
    result.coverage = calculateCoverage(result.opcodeAvailableProfiles,
        result.evidencedProfiles);
    result.status = result.records.empty() ? SctRuntimeFactQueryStatus::NoConfirmedFacts
                                           : SctRuntimeFactQueryStatus::Available;
    result.agreement = calculateAgreement(result.evidencedProfiles,
        std::span<const SctOpcodeRuntimeFactRecord>{result.records});
    return result;
}

} // namespace spice::sct
