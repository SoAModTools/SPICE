#pragma once

#include "SctDocument.h"
#include "SctModel.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace spice::sct {

struct SctDocumentImportReceipt;

enum class SctSourceCoverageKind {
    SemanticEntity,
    DerivedLayout,
    OpaqueAttachment,
    SourceObservation,
};

struct SctImportedByteSpan {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;

    [[nodiscard]] constexpr std::uint64_t endOffset() const noexcept {
        return static_cast<std::uint64_t>(offset) + size;
    }
    auto operator<=>(const SctImportedByteSpan&) const = default;
};

enum class SctSourceSpanLayer { Envelope, Leaf };

enum class SctSourceSpanRole {
    Header,
    SectionCount,
    SectionIndexRow,
    SectionOffsetField,
    SectionName,
    SectionNamePadding,
    SectionPayload,
    Instruction,
    InstructionModifier,
    InstructionOpcode,
    InstructionParameter,
    Expression,
    TextElement,
    TextTerminator,
    IndexedStringPreamble,
    IndexedStringRecord,
    FooterRegion,
    FooterEntry,
    DerivedPadding,
    OpaqueAttachment,
};

enum class SctSourceRegion { Header, SectionIndex, SectionPayload, Footer };

enum class SctControlFlowKind {
    Fallthrough,
    BranchTrue,
    BranchFalse,
    SwitchCase,
    Jump,
    Call,
    Return,
};

struct SctImportedControlFlowObservation {
    SctInstructionId sourceInstruction;
    SctControlFlowKind kind = SctControlFlowKind::Fallthrough;
    SctSemanticConfidence confidence = SctSemanticConfidence::Unknown;
    std::optional<SctParameterSite> origin;
    std::optional<SctInstructionId> targetInstruction;
    std::optional<std::uint32_t> targetPayloadOffset;
};

struct SctSourceSpanRecord {
    SctImportedByteSpan span;
    SctSourceSpanRole role = SctSourceSpanRole::OpaqueAttachment;
    SctSourceSpanLayer layer = SctSourceSpanLayer::Leaf;
    SctSourceCoverageKind coverageKind = SctSourceCoverageKind::SemanticEntity;
    // Historical site in the imported revision. Parameter and expression paths
    // must not be interpreted as current after the document is restructured.
    using Target = std::variant<SctDocumentEntityId, SctParameterSite,
        SctExpressionSite, SctTextSite>;
    std::optional<Target> target;
    std::optional<SctSectionId> containingSection;
    std::optional<std::uint32_t> sectionRelativeOffset;
    SctSourceRegion region = SctSourceRegion::SectionPayload;
    bool primaryEntityLocation = false;
};

using SctImportedSourceTarget = SctSourceSpanRecord::Target;

enum class SctSourceMapIssueCode {
    OutOfBounds,
    LeafGap,
    LeafOverlap,
    ZeroLengthLeaf,
    DuplicatePrimaryLocation,
    MissingPrimaryLocation,
    InvalidContainingSection,
    InvalidSectionRelativeOffset,
    IllegalEnvelopeOverlap,
    InvalidTarget,
};

struct SctSourceMapIssue {
    SctSourceMapIssueCode code = SctSourceMapIssueCode::OutOfBounds;
    std::optional<SctImportedByteSpan> span;
    std::string message;
};

struct SctSourceEntityLocation {
    SctDocumentEntityId entity;
    SctImportedByteSpan primarySpan;
    std::optional<SctSectionId> containingSection;
    std::optional<std::uint32_t> sectionRelativeOffset;
    SctSourceRegion region = SctSourceRegion::SectionPayload;
};

enum class SctSourceRelationship {
    Before,
    After,
    SameSpan,
    Contains,
    ContainedBy,
    Overlaps,
};

// Immutable import provenance. All offsets address the decoded SCT payload.
class SctImportedSourceMap {
public:
    struct BuildResult;
    [[nodiscard]] static BuildResult build(std::uint32_t decodedPayloadSize,
        std::vector<SctSourceSpanRecord> records);

    [[nodiscard]] std::uint32_t decodedPayloadSize() const noexcept { return decodedPayloadSize_; }
    [[nodiscard]] std::span<const SctSourceSpanRecord> records() const noexcept { return records_; }
    [[nodiscard]] std::vector<SctSourceSpanRecord> recordsFor(
        const SctImportedSourceTarget& target) const;
    [[nodiscard]] std::vector<SctSourceSpanRecord> recordsFor(
        const SctDocumentEntityId& entity) const {
        return recordsFor(SctImportedSourceTarget{entity});
    }
    [[nodiscard]] std::vector<SctSourceSpanRecord> recordsAt(std::uint32_t offset) const;
    [[nodiscard]] std::vector<SctSourceSpanRecord> recordsContaining(
        SctImportedByteSpan span) const;
    [[nodiscard]] std::optional<SctSourceEntityLocation> location(
        const SctDocumentEntityId& entity) const;
    [[nodiscard]] std::optional<SctDocumentEntityId> previousSemanticEntity(
        const SctDocumentEntityId& entity) const;
    [[nodiscard]] std::optional<SctDocumentEntityId> nextSemanticEntity(
        const SctDocumentEntityId& entity) const;
    [[nodiscard]] std::vector<SctDocumentEntityId> semanticEntitiesBetween(
        const SctDocumentEntityId& first, const SctDocumentEntityId& second) const;
    [[nodiscard]] std::optional<SctSourceRelationship> relationship(
        const SctDocumentEntityId& first, const SctDocumentEntityId& second) const;
    [[nodiscard]] bool hasCompleteLeafCoverage() const noexcept;

private:
    friend struct SctDocumentImportReceipt;
    SctImportedSourceMap() = default;
    explicit SctImportedSourceMap(std::uint32_t decodedPayloadSize,
        std::vector<SctSourceSpanRecord> records);
    [[nodiscard]] std::vector<SctSourceEntityLocation> semanticLocations() const;

    std::uint32_t decodedPayloadSize_ = 0;
    std::vector<SctSourceSpanRecord> records_;
};

struct SctImportedSourceMap::BuildResult {
    std::optional<SctImportedSourceMap> map;
    std::vector<SctSourceMapIssue> issues;
};

} // namespace spice::sct
