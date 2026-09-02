#pragma once

#include "SctOpcodeMetadata.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace spice::sct {

// Runtime facts are profile-specific research data, not SCT format contracts.
// Individual records, coverage, confidence, and conclusions may be corrected or
// extended as research improves. The public contract is the typed vocabulary and
// query behavior, not catalogue completeness or any particular fact value. These
// facts are informational and must not become document-validation, construction,
// layout, or export rules.

enum class SctRuntimeProfileId {
    GameCubeUsRetail20021219,
    GameCubeJpRetail20021112,
    GameCubeEuRetail20030305,
    DreamcastUsRetailDisc1,
    DreamcastJpRetailDisc1,
    DreamcastEuRetailDisc1,
    DreamcastCustomEuDerived2026,
};

struct SctRuntimeProfile {
    SctRuntimeProfileId id;
    SctPlatform platform;
    std::string_view label;
    std::string_view executableSha256;
    std::string_view evidenceScope;
};

[[nodiscard]] std::span<const SctRuntimeProfile> sctRuntimeProfiles() noexcept;
[[nodiscard]] const SctRuntimeProfile* findSctRuntimeProfile(SctRuntimeProfileId id) noexcept;

struct SctAllRegisteredRuntimeProfiles {
    auto operator<=>(const SctAllRegisteredRuntimeProfiles&) const = default;
};

using SctRuntimeFactTarget = std::variant<SctRuntimeProfileId, SctPlatform,
    SctAllRegisteredRuntimeProfiles>;

enum class SctRuntimeFactStage {
    EncodedOperand,
    BeforeScptEvaluation,
    AfterScptEvaluation,
    Handler,
    Callee,
    DownstreamConsumer,
    ControlFlow,
    ImplicitOpcodeState,
};

enum class SctRuntimeFactConfidence {
    Unknown,
    Low,
    Medium,
    MediumHigh,
    High,
};

struct SctRuntimeFactConfidencePair {
    SctRuntimeFactConfidence structural = SctRuntimeFactConfidence::Unknown;
    SctRuntimeFactConfidence semantic = SctRuntimeFactConfidence::Unknown;
    auto operator<=>(const SctRuntimeFactConfidencePair&) const = default;
};

enum class SctRuntimeValueRepresentation {
    RawWord32,
    ScptFloat32,
    Float32,
    Int32,
    UInt32,
    Int16,
    UInt16,
    Int8,
    UInt8,
    Boolean,
    RelativeDisplacement,
    TableIndex,
};

enum class SctRuntimeConversionKind {
    Preserve,
    ConvertTowardZero,
    NarrowLowBits,
    ScaleThenConvertTowardZero,
};

struct SctValueConversionFact {
    SctRuntimeValueRepresentation input = SctRuntimeValueRepresentation::ScptFloat32;
    SctRuntimeValueRepresentation output = SctRuntimeValueRepresentation::Float32;
    SctRuntimeConversionKind conversion = SctRuntimeConversionKind::Preserve;
    auto operator<=>(const SctValueConversionFact&) const = default;
};

enum class SctBitMaskPurpose {
    StructuralTag,
    ClearAlignmentBits,
    RuntimeValueMask,
};

struct SctBitMaskFact {
    std::uint32_t mask = 0xffffffffu;
    std::uint32_t requiredValue = 0u;
    SctBitMaskPurpose purpose = SctBitMaskPurpose::RuntimeValueMask;
    auto operator<=>(const SctBitMaskFact&) const = default;
};

using SctRuntimeScalarValue = std::variant<std::int64_t, std::uint64_t, double>;

enum class SctSpecialEncodedValueKind {
    Terminator,
    DefaultCase,
    ReplacementRuntimeValue,
};

struct SctSpecialEncodedValueFact {
    std::uint32_t encodedWord = 0;
    SctSpecialEncodedValueKind kind = SctSpecialEncodedValueKind::ReplacementRuntimeValue;
    std::optional<SctRuntimeScalarValue> replacement;
    auto operator<=>(const SctSpecialEncodedValueFact&) const = default;
};

enum class SctNumericDomainAuthority {
    LosslessRepresentation,
    RuntimeEnforced,
    ProvenSafe,
    Conventional,
};

enum class SctNumericDomainKind {
    SignedInteger,
    UnsignedInteger,
    Float32,
};

struct SctNumericDomainFact {
    SctNumericDomainAuthority authority = SctNumericDomainAuthority::LosslessRepresentation;
    SctNumericDomainKind kind = SctNumericDomainKind::SignedInteger;
    std::optional<SctRuntimeScalarValue> minimum;
    std::optional<SctRuntimeScalarValue> maximum;
    std::vector<SctRuntimeScalarValue> discreteValues;
    bool finiteOnly = false;
    auto operator<=>(const SctNumericDomainFact&) const = default;
};

enum class SctExternalDomainKind {
    ByteVariableTable,
    IntegerVariableTable,
    FloatVariableTable,
    FlagTable,
    EventEntryTable,
    ObjectTable,
    WorldCoordinates,
    DestinationCapacity,
    ItemTable,
    AbilityTable,
    AudioResourceTable,
    UnknownExternalConsumer,
};

enum class SctRuntimeValueDispositionKind {
    EvaluatedAndDiscarded,
    Stored,
    ForwardedToCallee,
    UsedForLookup,
    UsedForIndexedAccess,
    UsedForControlDecision,
};

struct SctRuntimeValueDispositionFact {
    SctRuntimeValueDispositionKind disposition = SctRuntimeValueDispositionKind::Stored;
    auto operator<=>(const SctRuntimeValueDispositionFact&) const = default;
};

enum class SctRuntimeArithmeticOperation {
    Add,
    Multiply,
    ShiftLeft,
    ShiftRight,
};

struct SctRuntimeArithmeticFact {
    SctRuntimeArithmeticOperation operation = SctRuntimeArithmeticOperation::Add;
    SctRuntimeScalarValue scalar = std::int64_t{0};
    auto operator<=>(const SctRuntimeArithmeticFact&) const = default;
};

enum class SctRuntimeComparisonKind {
    Equal,
    NotEqual,
    Less,
    LessOrEqual,
    Greater,
    GreaterOrEqual,
    Zero,
    Nonzero,
};

struct SctRuntimeComparisonFact {
    SctRuntimeComparisonKind comparison = SctRuntimeComparisonKind::Equal;
    std::optional<SctRuntimeScalarValue> operand;
    auto operator<=>(const SctRuntimeComparisonFact&) const = default;
};

using SctRuntimeValueFlowStep = std::variant<SctValueConversionFact, SctBitMaskFact,
    SctRuntimeArithmeticFact, SctRuntimeComparisonFact>;

struct SctRuntimeValueFlowFact {
    std::vector<SctRuntimeValueFlowStep> steps;
    auto operator<=>(const SctRuntimeValueFlowFact&) const = default;
};

struct SctExternalDomainFact {
    SctExternalDomainKind domain = SctExternalDomainKind::UnknownExternalConsumer;
    auto operator<=>(const SctExternalDomainFact&) const = default;
};

enum class SctParameterRelationshipKind {
    CountMatchesRepeatedGroups,
    TargetIsAlignedInstruction,
    TargetIsIndexedSctString,
    CallbackCountShape,
};

struct SctParameterRelationshipFact {
    SctParameterRelationshipKind relationship = SctParameterRelationshipKind::CountMatchesRepeatedGroups;
    std::optional<std::uint32_t> relatedParameterIndex;
    std::vector<std::pair<std::int64_t, std::int64_t>> acceptedPairs;
    auto operator<=>(const SctParameterRelationshipFact&) const = default;
};

enum class SctRuntimeOutcomeKind {
    NoneKnown,
    Clamp,
    TruncateTowardZero,
    TruncateModulo,
    ImmediateCompletion,
    CursorRedirect,
    UncheckedRead,
    UncheckedWrite,
    LookupFailure,
    DiagnosticThenContinue,
    Overread,
    Overwrite,
    UnknownBehavior,
};

struct SctRuntimeOutcomeFact {
    SctRuntimeOutcomeKind outcome = SctRuntimeOutcomeKind::NoneKnown;
    auto operator<=>(const SctRuntimeOutcomeFact&) const = default;
};

struct SctRuntimeCapacityFact {
    std::uint32_t capacity = 0;
    auto operator<=>(const SctRuntimeCapacityFact&) const = default;
};

using SctRuntimeFactPayload = std::variant<SctValueConversionFact, SctBitMaskFact,
    SctSpecialEncodedValueFact, SctNumericDomainFact, SctExternalDomainFact,
    SctParameterRelationshipFact, SctRuntimeOutcomeFact, SctRuntimeCapacityFact,
    SctRuntimeValueDispositionFact, SctRuntimeArithmeticFact,
    SctRuntimeComparisonFact, SctRuntimeValueFlowFact>;

enum class SctRuntimeFactResolution {
    Confirmed,
    ProducerConfirmedFinalConsumerUnresolved,
};

enum class SctRuntimeConditionProjection {
    EncodedWord,
    VariableReferenceIndex,
    EvaluatedIntegralValue,
};

enum class SctRuntimeConditionComparison {
    Equals,
    InSet,
    NotInSet,
};

struct SctRuntimeFactCondition {
    std::uint32_t parameterIndex = 0;
    SctRuntimeConditionProjection projection = SctRuntimeConditionProjection::EvaluatedIntegralValue;
    SctRuntimeConditionComparison comparison = SctRuntimeConditionComparison::Equals;
    std::vector<std::int64_t> values;
    auto operator<=>(const SctRuntimeFactCondition&) const = default;
};

struct SctRuntimeEvidence {
    SctRuntimeProfileId profile;
    SctRuntimeFactConfidencePair confidence;
    std::optional<std::uint32_t> handlerAddress;
    std::optional<std::uint32_t> parserCallAddress;
    std::string_view method;
};

struct SctProfiledRuntimeBehavior {
    std::vector<SctRuntimeProfileId> profiles;
    SctRuntimeFactStage stage = SctRuntimeFactStage::Handler;
    std::vector<SctRuntimeFactPayload> facts;
    std::vector<SctRuntimeEvidence> evidence;
};

struct SctOpcodeParameterFactRecord {
    std::string_view sourceRecordId;
    std::uint16_t opcode = 0;
    std::uint32_t schemaIndex = 0;
    SctRuntimeFactResolution resolution = SctRuntimeFactResolution::Confirmed;
    std::optional<SctRuntimeFactCondition> condition;
    std::vector<SctProfiledRuntimeBehavior> behaviors;
    std::string_view summary;
    std::string_view outOfDomainSummary;
};

struct SctOpcodeRuntimeFactRecord {
    std::string_view sourceRecordId;
    std::uint16_t opcode = 0;
    std::string_view stateKey;
    std::vector<SctProfiledRuntimeBehavior> behaviors;
    std::string_view summary;
    std::string_view outOfDomainSummary;
};

enum class SctRuntimeFactQueryStatus {
    Available,
    UnknownOpcode,
    UnknownParameter,
    UnavailableForTarget,
    NoConfirmedFacts,
};

enum class SctRuntimeFactAgreement {
    NoEvidence,
    SingleProfile,
    Uniform,
    Divergent,
};

enum class SctRuntimeFactCoverage {
    None,
    Partial,
    Complete,
};

struct SctOpcodeParameterFactQueryResult {
    SctRuntimeFactQueryStatus status = SctRuntimeFactQueryStatus::NoConfirmedFacts;
    SctRuntimeFactAgreement agreement = SctRuntimeFactAgreement::NoEvidence;
    SctRuntimeFactCoverage coverage = SctRuntimeFactCoverage::None;
    std::vector<SctRuntimeProfileId> selectedProfiles;
    std::vector<SctRuntimeProfileId> opcodeAvailableProfiles;
    std::vector<SctRuntimeProfileId> evidencedProfiles;
    std::vector<SctOpcodeParameterFactRecord> records;
};

struct SctOpcodeRuntimeFactQueryResult {
    SctRuntimeFactQueryStatus status = SctRuntimeFactQueryStatus::NoConfirmedFacts;
    SctRuntimeFactAgreement agreement = SctRuntimeFactAgreement::NoEvidence;
    SctRuntimeFactCoverage coverage = SctRuntimeFactCoverage::None;
    std::vector<SctRuntimeProfileId> selectedProfiles;
    std::vector<SctRuntimeProfileId> opcodeAvailableProfiles;
    std::vector<SctRuntimeProfileId> evidencedProfiles;
    std::vector<SctOpcodeRuntimeFactRecord> records;
};

[[nodiscard]] std::span<const SctOpcodeParameterFactRecord> sctOpcodeParameterFactCatalog() noexcept;
[[nodiscard]] std::span<const SctOpcodeRuntimeFactRecord> sctOpcodeRuntimeFactCatalog() noexcept;

class SctOpcodeParameterFacts {
public:
    [[nodiscard]] static SctOpcodeParameterFactQueryResult query(
        std::uint16_t opcode, std::uint32_t schemaIndex, const SctRuntimeFactTarget& target);
};

class SctOpcodeRuntimeFacts {
public:
    [[nodiscard]] static SctOpcodeRuntimeFactQueryResult query(
        std::uint16_t opcode, const SctRuntimeFactTarget& target);
};

} // namespace spice::sct
