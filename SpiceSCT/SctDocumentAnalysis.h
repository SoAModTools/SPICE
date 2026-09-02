#pragma once

#include "SctDocumentImporter.h"
#include "SctDocumentIndex.h"
#include "SctStructuredControlFlow.h"

#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace spice::sct {

struct SctInstructionSemanticContribution;

struct SctControlFlowEdge {
    SctInstructionId sourceInstruction;
    SctControlFlowKind kind = SctControlFlowKind::Fallthrough;
    SctSemanticConfidence confidence = SctSemanticConfidence::Unknown;
    std::optional<SctParameterSite> origin;
    std::optional<SctInstructionId> targetInstruction;
    std::optional<std::uint32_t> unresolvedTargetPayloadOffset;
    std::vector<SctOpaqueAttachmentId> crossedOpaqueAttachments;
};

class SctControlFlowIndex {
public:
    [[nodiscard]] static SctControlFlowIndex build(const SctDocument& document,
        const SctBoundImportEvidence* evidence = nullptr);

    [[nodiscard]] std::span<const SctControlFlowEdge> currentEdges() const noexcept {
        return currentEdges_;
    }
    [[nodiscard]] std::span<const SctControlFlowEdge> importedEdges() const noexcept {
        return importedEdges_;
    }
    [[nodiscard]] std::vector<SctControlFlowEdge> currentOutbound(
        SctInstructionId source) const;
    [[nodiscard]] std::vector<SctControlFlowEdge> currentInbound(
        SctInstructionId target) const;

private:
    std::vector<SctControlFlowEdge> currentEdges_;
    std::vector<SctControlFlowEdge> importedEdges_;
};

struct SctOpcodeUsage {
    std::uint16_t opcode = 0;
    SctInstructionId instruction;
};

struct SctReferenceUsage {
    SctParameterSite source;
    SctDocumentReferenceTarget target;
};

enum class SctVariableKind { Integer, Float, Bit, Byte };

struct SctVariableIdentity {
    SctVariableKind kind = SctVariableKind::Integer;
    std::uint32_t index = 0;
    auto operator<=>(const SctVariableIdentity&) const = default;
};

struct SctVariableUsage {
    SctVariableIdentity variable;
    SctScptValueKind encodedForm = SctScptValueKind::DirectIntVariable;
    SctExpressionOperationSite source;
};

struct SctUnresolvedReferenceUsage {
    SctParameterSite source;
    SctExpectedReferenceTarget expectedTarget;
    std::size_t encodedWordCount = 0;
};

struct SctOpaqueParameterUsage {
    SctParameterSite source;
    std::size_t wordCount = 0;
};

struct SctOpaqueExpressionUsage {
    SctExpressionSite source;
    std::size_t wordCount = 0;
};

class SctSemanticUsageIndex {
public:
    [[nodiscard]] static SctSemanticUsageIndex build(const SctDocument& document);
    // Contributions must follow physical document order: SctDocument::sections order,
    // then instruction order within each script section. Occurrence collections and
    // filtered queries preserve the supplied order; this overload does not sort or
    // validate it.
    [[nodiscard]] static SctSemanticUsageIndex build(
        std::span<const SctInstructionSemanticContribution> contributions);

    [[nodiscard]] std::span<const SctOpcodeUsage> opcodeUsages() const noexcept { return opcodeUsages_; }
    [[nodiscard]] std::span<const SctReferenceUsage> referenceUsages() const noexcept { return referenceUsages_; }
    [[nodiscard]] std::span<const SctVariableUsage> variableUsages() const noexcept { return variableUsages_; }
    [[nodiscard]] std::span<const SctUnresolvedReferenceUsage> unresolvedReferences() const noexcept {
        return unresolvedReferences_;
    }
    [[nodiscard]] std::span<const SctOpaqueParameterUsage> opaqueParameters() const noexcept {
        return opaqueParameters_;
    }
    [[nodiscard]] std::span<const SctOpaqueExpressionUsage> opaqueExpressions() const noexcept {
        return opaqueExpressions_;
    }
    [[nodiscard]] std::vector<SctOpcodeUsage> usagesForOpcode(std::uint16_t opcode) const;
    [[nodiscard]] std::vector<SctReferenceUsage> outboundReferences(SctInstructionId source) const;
    [[nodiscard]] std::vector<SctReferenceUsage> inboundReferences(
        const SctDocumentReferenceTarget& target) const;
    [[nodiscard]] std::vector<SctVariableUsage> usagesForVariable(SctVariableIdentity variable) const;

private:
    std::vector<SctOpcodeUsage> opcodeUsages_;
    std::vector<SctReferenceUsage> referenceUsages_;
    std::vector<SctVariableUsage> variableUsages_;
    std::vector<SctUnresolvedReferenceUsage> unresolvedReferences_;
    std::vector<SctOpaqueParameterUsage> opaqueParameters_;
    std::vector<SctOpaqueExpressionUsage> opaqueExpressions_;
};

enum class SctOpaqueInterpretationKind { ControlFlowGap, SwitchDispatchGap };

struct SctOpaqueInterpretation {
    SctOpaqueInterpretationKind kind = SctOpaqueInterpretationKind::ControlFlowGap;
    SctSemanticConfidence confidence = SctSemanticConfidence::Unknown;
};

struct SctControlFlowEdgeKey {
    SctInstructionId sourceInstruction;
    SctControlFlowKind kind = SctControlFlowKind::Fallthrough;
    std::optional<SctParameterSite> origin;
    std::optional<SctInstructionId> targetInstruction;
};

struct SctOpaqueContextRecord {
    SctOpaqueAttachmentId attachment;
    SctImportedByteSpan sourceSpan;
    SctSourceRegion region = SctSourceRegion::SectionPayload;
    std::optional<SctSectionId> containingSection;
    std::optional<SctDocumentEntityId> previousSemanticEntity;
    std::optional<SctDocumentEntityId> nextSemanticEntity;
    SctSourceRecordNeighborhood sourceNeighborhood;
    std::vector<SctControlFlowEdgeKey> crossingImportedEdges;
    std::vector<SctOpaqueInterpretation> interpretations;
};

class SctOpaqueContextIndex {
public:
    [[nodiscard]] static SctOpaqueContextIndex build(const SctDocument& document,
        const SctBoundImportEvidence* evidence, const SctControlFlowIndex& controlFlow);
    [[nodiscard]] std::span<const SctOpaqueContextRecord> records() const noexcept { return records_; }
    [[nodiscard]] const SctOpaqueContextRecord* find(SctOpaqueAttachmentId id) const noexcept;

private:
    std::vector<SctOpaqueContextRecord> records_;
};

enum class SctResourceKind { Script, Mld };

struct SctResourceLoadEffect {
    SctInstructionId sourceInstruction;
    SctResourceKind resource = SctResourceKind::Script;
    SctParameterSite resourceParameter;
    SctSemanticConfidence confidence = SctSemanticConfidence::Unknown;
};

struct SctGroundVariantSelectionEffect {
    SctInstructionId sourceInstruction;
    SctParameterSite tableIdParameter;
    SctParameterSite variantParameter;
    SctSemanticConfidence confidence = SctSemanticConfidence::Unknown;
};

using SctOpcodeEffect = std::variant<SctResourceLoadEffect, SctGroundVariantSelectionEffect>;

enum class SctOpcodeEffectUsability {
    Usable,
    UnresolvedInput,
    OpaqueInput,
    MissingInput,
    IncompatibleInput,
};

struct SctOpcodeEffectOccurrence {
    SctOpcodeEffect effect;
    SctOpcodeEffectUsability usability = SctOpcodeEffectUsability::Usable;
};

struct SctInstructionSemanticContribution {
    SctOpcodeUsage opcode;
    std::vector<SctReferenceUsage> references;
    std::vector<SctVariableUsage> variables;
    std::vector<SctUnresolvedReferenceUsage> unresolvedReferences;
    std::vector<SctOpaqueParameterUsage> opaqueParameters;
    std::vector<SctOpaqueExpressionUsage> opaqueExpressions;
    std::vector<SctOpcodeEffectOccurrence> effects;
};

class SctInstructionSemanticAnalyzer {
public:
    [[nodiscard]] static SctInstructionSemanticContribution build(
        const SctDocumentInstruction& instruction);
};

class SctOpcodeEffectIndex {
public:
    [[nodiscard]] static SctOpcodeEffectIndex build(const SctDocument& document);
    // Contributions must follow physical document order: SctDocument::sections order,
    // then instruction order within each script section. Effect collections and
    // filtered queries preserve the supplied order; this overload does not sort or
    // validate it.
    [[nodiscard]] static SctOpcodeEffectIndex build(
        std::span<const SctInstructionSemanticContribution> contributions);
    [[nodiscard]] std::span<const SctOpcodeEffectOccurrence> effects() const noexcept { return effects_; }
    [[nodiscard]] std::vector<SctOpcodeEffectOccurrence> effectsForInstruction(SctInstructionId id) const;
    [[nodiscard]] std::vector<SctOpcodeEffectOccurrence> usableEffects() const;

private:
    std::vector<SctOpcodeEffectOccurrence> effects_;
};

enum class SctImportedSiteAddressability {
    ExactSite,
    ParentSiteOnly,
    OwningEntityOnly,
    MissingEntity,
};

enum class SctImportedAddressabilitySummary {
    NoSites,
    FullyAddressable,
    PartiallyAddressable,
    NoLongerAddressable,
};

struct SctImportedSiteAddressabilityRecord {
    SctImportedSourceTarget importedTarget;
    SctImportedSiteAddressability addressability =
        SctImportedSiteAddressability::MissingEntity;
};

class SctImportedSiteAddressabilityIndex {
public:
    [[nodiscard]] static SctImportedSiteAddressabilityIndex build(
        const SctDocument& document, const SctDocumentIndex& entities,
        const SctImportedSourceMap& sourceMap);

    [[nodiscard]] std::span<const SctImportedSiteAddressabilityRecord> records() const noexcept {
        return records_;
    }
    [[nodiscard]] const SctImportedSiteAddressabilityRecord* find(
        const SctImportedSourceTarget& target) const noexcept;
    [[nodiscard]] SctImportedAddressabilitySummary summary() const noexcept {
        return summary_;
    }

private:
    std::vector<SctImportedSiteAddressabilityRecord> records_;
    std::map<SctImportedSourceTarget, std::size_t> recordIndex_;
    SctImportedAddressabilitySummary summary_ =
        SctImportedAddressabilitySummary::NoSites;
};

// A revision-scoped collection of derived views. Rebuild after mutating document.
struct SctDocumentAnalysis {
    SctDocumentIndex entities;
    SctControlFlowIndex controlFlow;
    SctSemanticUsageIndex usage;
    SctOpaqueContextIndex opaqueContext;
    SctStructuredControlFlowAnalysis structuredControlFlow;
    SctOpcodeEffectIndex effects;
    std::optional<SctImportedSiteAddressabilityIndex> importedSites;

    [[nodiscard]] static SctDocumentAnalysis build(const SctDocument& document,
        const SctBoundImportEvidence* evidence = nullptr);
};

} // namespace spice::sct
