#pragma once

#include "SctOpcodeMetadata.h"

#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace spice::sct {

template <typename Tag>
class SctEntityId {
public:
    constexpr SctEntityId() noexcept = default;
    explicit constexpr SctEntityId(std::uint64_t value) noexcept : value_(value) {}
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return value_ != 0; }
    [[nodiscard]] explicit constexpr operator std::uint64_t() const noexcept { return value_; }
    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
    auto operator<=>(const SctEntityId&) const = default;
private:
    std::uint64_t value_ = 0;
};

struct SctSectionIdTag;
struct SctInstructionIdTag;
struct SctStringIdTag;
struct SctFooterEntryIdTag;
struct SctOpaqueAttachmentIdTag;
using SctSectionId = SctEntityId<SctSectionIdTag>;
using SctInstructionId = SctEntityId<SctInstructionIdTag>;
using SctStringId = SctEntityId<SctStringIdTag>;
using SctFooterEntryId = SctEntityId<SctFooterEntryIdTag>;
using SctOpaqueAttachmentId = SctEntityId<SctOpaqueAttachmentIdTag>;

struct SctEntityIdHash {
    template <typename Tag>
    [[nodiscard]] std::size_t operator()(SctEntityId<Tag> id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value());
    }
};

using SctDocumentEntityId = std::variant<std::monostate, SctSectionId, SctInstructionId,
    SctStringId, SctFooterEntryId, SctOpaqueAttachmentId>;

enum class SctDiagnosticSeverity { Info, Warning, Error };
enum class SctDiagnosticCode {
    ParseFailed,
    UnsafePhysicalStructure,
    OverlappingSourceClaims,
    UnresolvedReference,
    RepeatedCountMismatch,
    AmbiguousExpression,
    AmbiguousString,
    InvalidId,
    DuplicateId,
    AllocatorDiscontinuity,
    InvalidName,
    InvalidContent,
    OpcodeUnavailable,
    ParameterMismatch,
    ExpressionInvalid,
    AttachmentInvalid,
};

struct SctDocumentDiagnostic {
    SctDiagnosticSeverity severity = SctDiagnosticSeverity::Error;
    SctDiagnosticCode code = SctDiagnosticCode::InvalidContent;
    std::optional<SctDocumentEntityId> entity;
    std::string message;
};

enum class SctExpressionTermination { InlineValue, StopCode };
enum class SctCanonicalExpressionNodeKind {
    NoLoopValue, RawValue, FloatLiteral, DecimalLiteral, IntVariable, FloatVariable,
    BitVariable, ByteVariable, SecondaryValue, CompareOperator, ArithmeticOperator,
    AssignmentOperator, Stop,
};

struct SctCanonicalExpressionNode {
    SctCanonicalExpressionNodeKind kind = SctCanonicalExpressionNodeKind::RawValue;
    std::uint32_t encodingCode = 0;
    std::vector<std::uint32_t> payloadWords;
    std::vector<SctCanonicalExpressionNode> children;
};

struct SctOpaqueExpression { std::vector<std::uint32_t> words; };
struct SctCanonicalExpression {
    std::variant<SctCanonicalExpressionNode, SctOpaqueExpression> root;
    SctExpressionTermination termination = SctExpressionTermination::InlineValue;
};

struct SctEncodedWordValue { std::uint32_t value = 0; };
struct SctInstructionReference { SctInstructionId target; };
struct SctFooterEntryReference { SctFooterEntryId target; };
struct SctOpaqueParameterValue { std::vector<std::uint32_t> words; };
using SctDocumentParameterValue = std::variant<SctEncodedWordValue, SctCanonicalExpression,
    SctInstructionReference, SctFooterEntryReference, SctOpaqueParameterValue>;

struct SctDocumentParameter {
    std::uint32_t schemaIndex = 0;
    SctDocumentParameterValue value;
};

struct SctDocumentRepeatedParameterGroup {
    std::vector<SctDocumentParameter> parameters;
};

struct SctDocumentInstruction {
    SctInstructionId id;
    std::uint16_t opcode = 0;
    bool skipRefresh = false;
    std::optional<SctCanonicalExpression> scheduledExpression;
    std::vector<SctDocumentParameter> fixedParameters;
    std::vector<SctDocumentRepeatedParameterGroup> repeatedParameterGroups;
};

struct SctEditableText { std::string bytes; };
struct SctOpaqueText { std::vector<std::uint8_t> bytes; };
using SctTextValue = std::variant<SctEditableText, SctOpaqueText>;

struct SctDocumentString {
    SctStringId id;
    SctTextValue value;
};

struct SctDocumentFooterEntry {
    SctFooterEntryId id;
    SctFooterEntryKind kind = SctFooterEntryKind::String;
    SctTextValue value;
};

struct SctScriptSectionContent { std::vector<SctDocumentInstruction> instructions; };
struct SctStringSectionContent { SctStringId stringId; };
struct SctLabelSectionContent {};
struct SctOpaqueSectionContent {};
using SctDocumentSectionContent = std::variant<SctScriptSectionContent, SctStringSectionContent,
    SctLabelSectionContent, SctOpaqueSectionContent>;

struct SctDocumentSection {
    SctSectionId id;
    std::string nameBytes;
    SctDocumentSectionContent content;
    std::vector<SctOpaqueAttachmentId> opaqueAttachments;
};

struct SctDocumentAnchor {};
using SctOpaqueAnchor = std::variant<SctDocumentAnchor, SctSectionId, SctInstructionId,
    SctStringId, SctFooterEntryId>;
enum class SctOpaquePlacement { Before, After, FixedOffset };
enum class SctOpaqueRelocationSupport { Relocatable, FixedOnly };
enum class SctOpaqueReason { Header, Preamble, Padding, Gap, Unreached, UnknownEncoding, ContradictoryEvidence };

struct SctOpaqueAttachment {
    SctOpaqueAttachmentId id;
    std::vector<std::uint8_t> bytes;
    SctOpaqueAnchor anchor;
    SctOpaquePlacement placement = SctOpaquePlacement::FixedOffset;
    std::optional<std::uint32_t> fixedOffset;
    std::uint32_t alignment = 1;
    SctOpaqueRelocationSupport relocation = SctOpaqueRelocationSupport::FixedOnly;
    SctOpaqueReason reason = SctOpaqueReason::UnknownEncoding;
};

class SctDocument {
public:
    std::vector<SctDocumentSection> sections;
    std::vector<SctDocumentString> strings;
    std::vector<SctDocumentFooterEntry> footerEntries;
    std::vector<SctOpaqueAttachment> opaqueAttachments;

    [[nodiscard]] SctSectionId allocateSectionId() noexcept { return SctSectionId(nextSectionId_++); }
    [[nodiscard]] SctInstructionId allocateInstructionId() noexcept { return SctInstructionId(nextInstructionId_++); }
    [[nodiscard]] SctStringId allocateStringId() noexcept { return SctStringId(nextStringId_++); }
    [[nodiscard]] SctFooterEntryId allocateFooterEntryId() noexcept { return SctFooterEntryId(nextFooterEntryId_++); }
    [[nodiscard]] SctOpaqueAttachmentId allocateOpaqueAttachmentId() noexcept { return SctOpaqueAttachmentId(nextOpaqueAttachmentId_++); }

    [[nodiscard]] std::uint64_t nextSectionIdValue() const noexcept { return nextSectionId_; }
    [[nodiscard]] std::uint64_t nextInstructionIdValue() const noexcept { return nextInstructionId_; }
    [[nodiscard]] std::uint64_t nextStringIdValue() const noexcept { return nextStringId_; }
    [[nodiscard]] std::uint64_t nextFooterEntryIdValue() const noexcept { return nextFooterEntryId_; }
    [[nodiscard]] std::uint64_t nextOpaqueAttachmentIdValue() const noexcept { return nextOpaqueAttachmentId_; }

private:
    std::uint64_t nextSectionId_ = 1;
    std::uint64_t nextInstructionId_ = 1;
    std::uint64_t nextStringId_ = 1;
    std::uint64_t nextFooterEntryId_ = 1;
    std::uint64_t nextOpaqueAttachmentId_ = 1;
};

} // namespace spice::sct

namespace std {
template <> struct hash<spice::sct::SctSectionId> : spice::sct::SctEntityIdHash {};
template <> struct hash<spice::sct::SctInstructionId> : spice::sct::SctEntityIdHash {};
template <> struct hash<spice::sct::SctStringId> : spice::sct::SctEntityIdHash {};
template <> struct hash<spice::sct::SctFooterEntryId> : spice::sct::SctEntityIdHash {};
template <> struct hash<spice::sct::SctOpaqueAttachmentId> : spice::sct::SctEntityIdHash {};
}
