#pragma once

#include "SctScptProgram.h"
#include "SctTextContract.h"

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

struct SctParameterAddress {
    std::uint32_t schemaIndex = 0;
    std::optional<std::uint32_t> repeatedGroupOrdinal;
    auto operator<=>(const SctParameterAddress&) const = default;
};

struct SctParameterSite {
    SctInstructionId instruction;
    SctParameterAddress parameter;
    auto operator<=>(const SctParameterSite&) const = default;
};

struct SctScheduledExpressionSite {
    auto operator<=>(const SctScheduledExpressionSite&) const = default;
};

using SctExpressionOwner = std::variant<SctScheduledExpressionSite, SctParameterAddress>;

struct SctExpressionSite {
    SctInstructionId instruction;
    SctExpressionOwner owner;
    auto operator<=>(const SctExpressionSite&) const = default;
};

struct SctExpressionOperationSite {
    SctExpressionSite expression;
    std::uint32_t operationOrdinal = 0;
    auto operator<=>(const SctExpressionOperationSite&) const = default;
};

using SctTextEntityId = std::variant<SctStringId, SctFooterEntryId>;
enum class SctTextRegion { Header, Body };
struct SctUtf8ByteRange {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    auto operator<=>(const SctUtf8ByteRange&) const = default;
};
struct SctTextSite {
    SctTextEntityId text;
    SctTextRegion region = SctTextRegion::Body;
    std::optional<std::uint32_t> elementOrdinal;
    SctUtf8ByteRange utf8Range;
    auto operator<=>(const SctTextSite&) const = default;
};

struct SctDraftParameterSite {
    std::uint16_t opcode = 0;
    SctParameterAddress parameter;
    auto operator<=>(const SctDraftParameterSite&) const = default;
};

struct SctDraftExpressionSite {
    std::optional<std::uint16_t> opcode;
    std::optional<SctExpressionOwner> owner;
    auto operator<=>(const SctDraftExpressionSite&) const = default;
};

struct SctDraftExpressionOperationSite {
    SctDraftExpressionSite expression;
    std::uint32_t operationOrdinal = 0;
    auto operator<=>(const SctDraftExpressionOperationSite&) const = default;
};

using SctDiagnosticLocation = std::variant<SctDocumentEntityId, SctParameterSite,
    SctExpressionSite, SctExpressionOperationSite, SctTextSite, SctDraftParameterSite,
    SctDraftExpressionSite, SctDraftExpressionOperationSite>;

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
    OpaquePlatformUnverified,
    LayoutOverflow,
    EncodingUnsupported,
    RelocationOutOfRange,
    OpaquePlacementUnsatisfied,
    CompressionFailed,
    ProvisionalAuthoringDefault,
    ProvisionalOpcodeConstraint,
    TextInvalid,
    HeaderUnavailable,
    ExpressionRuntimeStackDepth,
    ExpressionLogicalStackUnderflow,
    ExpressionUndefinedResult,
    ExpressionResidualStackValues,
};

struct SctDocumentDiagnostic {
    SctDiagnosticSeverity severity = SctDiagnosticSeverity::Error;
    SctDiagnosticCode code = SctDiagnosticCode::InvalidContent;
    std::string message;
    std::optional<SctDiagnosticLocation> primaryLocation;
    std::vector<SctDiagnosticLocation> relatedLocations;
};

struct SctEncodedWordValue { std::uint32_t value = 0; };
struct SctTerminatedWordSequenceValue { std::vector<std::uint32_t> words; };
struct SctInstructionReference { SctInstructionId target; };
struct SctStringReference { SctStringId target; };
struct SctFooterEntryReference { SctFooterEntryId target; };
using SctDocumentReferenceTarget = std::variant<SctInstructionId, SctStringId, SctFooterEntryId>;
enum class SctReferenceTargetStorage { Instruction, IndexedString, FooterEntry };
struct SctExpectedReferenceTarget {
    SctReferenceTargetStorage storage = SctReferenceTargetStorage::Instruction;
    std::optional<SctTextKind> textKind;
    auto operator<=>(const SctExpectedReferenceTarget&) const = default;
};
struct SctUnresolvedReferenceValue {
    SctExpectedReferenceTarget expectedTarget;
    std::vector<std::uint32_t> encodedWords;
};
struct SctOpaqueParameterValue { std::vector<std::uint32_t> words; };
using SctDocumentParameterValue = std::variant<SctEncodedWordValue, SctCanonicalExpression,
    SctTerminatedWordSequenceValue, SctInstructionReference, SctStringReference,
    SctFooterEntryReference, SctUnresolvedReferenceValue, SctOpaqueParameterValue>;

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

struct SctPlainText { std::string utf8; };

enum class SctMessageCommandCode { A, B, C, D, E, P, R, S, U, X, Wc, Wo };
struct SctNoCommandArgument { auto operator<=>(const SctNoCommandArgument&) const = default; };
struct SctDecimalCommandArgument {
    std::optional<std::uint32_t> value;
    auto operator<=>(const SctDecimalCommandArgument&) const = default;
};
struct SctByteListCommandArgument {
    std::vector<std::uint8_t> values;
    auto operator<=>(const SctByteListCommandArgument&) const = default;
};
using SctMessageCommandArgument = std::variant<SctNoCommandArgument,
    SctDecimalCommandArgument, SctByteListCommandArgument>;
struct SctInlineCommand {
    SctMessageCommandCode code = SctMessageCommandCode::C;
    SctMessageCommandArgument argument = SctNoCommandArgument{};
};
struct SctTextChunk { std::string utf8; };
using SctFormattedTextElement = std::variant<SctTextChunk, SctInlineCommand>;
struct SctFormattedText { std::vector<SctFormattedTextElement> elements; };
struct SctMessage {
    std::optional<std::string> headerUtf8;
    SctFormattedText body;
};
struct SctOpaqueText { std::vector<std::uint8_t> bytes; };
struct SctEmptyIndexedText { auto operator<=>(const SctEmptyIndexedText&) const = default; };
using SctTextValue = std::variant<SctPlainText, SctMessage, SctOpaqueText, SctEmptyIndexedText>;

struct SctDocumentString {
    SctStringId id;
    SctTextValue value;
    SctTextKind kind = SctTextKind::SctString;
};

using SctDocumentFooterEntryKind = SctTextKind;

struct SctDocumentFooterEntry {
    SctFooterEntryId id;
    SctTextKind kind = SctTextKind::PlainString;
    SctTextValue value;
};

struct SctScriptSectionContent { std::vector<SctDocumentInstruction> instructions; };
struct SctStringSectionContent {
    SctDocumentString string;
    std::vector<std::uint32_t> preambleWords{9u, 0x0000001du};
};
struct SctLabelSectionContent {};
struct SctOpaqueSectionContent {};
using SctDocumentSectionContent = std::variant<SctScriptSectionContent, SctStringSectionContent,
    SctLabelSectionContent, SctOpaqueSectionContent>;

struct SctDocumentSection {
    SctSectionId id;
    std::string nameBytes;
    SctDocumentSectionContent content;
};

struct SctDocumentAnchor { auto operator<=>(const SctDocumentAnchor&) const = default; };
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
    friend class SctDocumentBuilder;

    void restoreAllocatorState(std::uint64_t nextSectionId, std::uint64_t nextInstructionId,
        std::uint64_t nextStringId, std::uint64_t nextFooterEntryId,
        std::uint64_t nextOpaqueAttachmentId) noexcept {
        nextSectionId_ = nextSectionId;
        nextInstructionId_ = nextInstructionId;
        nextStringId_ = nextStringId;
        nextFooterEntryId_ = nextFooterEntryId;
        nextOpaqueAttachmentId_ = nextOpaqueAttachmentId;
    }

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
