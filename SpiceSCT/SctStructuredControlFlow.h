#pragma once

#include "SctDocument.h"
#include "SctSourceMap.h"

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace spice::sct {

class SctBoundImportEvidence;
class SctControlFlowIndex;
class SctOpaqueContextIndex;
struct SctDocumentAnalysis;

struct SctBasicBlockId final {
    SctSectionId section;
    SctInstructionId entryInstruction;
    auto operator<=>(const SctBasicBlockId&) const = default;
};

enum class SctSectionEntryKind {
    PhysicalSectionStart,
    SameSectionCallTarget,
    CrossSectionCallTarget,
    CrossSectionNonCallTarget,
};

struct SctSectionEntryPoint final {
    SctInstructionId instruction;
    SctBasicBlockId block;
    SctSectionEntryKind kind = SctSectionEntryKind::PhysicalSectionStart;
    auto operator<=>(const SctSectionEntryPoint&) const = default;
};

// A source-site control-flow observation. Transfers retain their exact source
// instruction even when that instruction is in the middle of a maximal block.
// targetBlock is present only when the target is in the same section graph.
struct SctStructuredControlTransfer final {
    SctInstructionId sourceInstruction;
    SctControlFlowKind kind = SctControlFlowKind::Fallthrough;
    SctSemanticConfidence confidence = SctSemanticConfidence::Unknown;
    std::optional<SctParameterSite> origin{};
    std::optional<SctInstructionId> targetInstruction{};
    std::optional<SctBasicBlockId> targetBlock{};
    std::optional<std::uint32_t> unresolvedTargetPayloadOffset{};
    std::vector<SctOpaqueAttachmentId> crossedOpaqueAttachments{};
    auto operator<=>(const SctStructuredControlTransfer&) const = default;
};

// A conventional maximal straight-line basic block. Calls are retained in
// transfers but do not end the local block because they have local continuation.
struct SctStructuredBasicBlock final {
    SctBasicBlockId id;
    std::vector<SctInstructionId> instructions{};
    std::vector<SctStructuredControlTransfer> transfers{};
    bool reachable = false;
    auto operator<=>(const SctStructuredBasicBlock&) const = default;
};

enum class SctStructureEvidenceKind {
    CurrentControlFlow,
    ConditionalFalseTarget,
    PreTargetJump,
    BackwardTerminatorJump,
    CommonForwardExit,
    PhysicalCaseBoundary,
    SharedCaseTarget,
    CaseFallthrough,
    ImportedControlFlow,
    ImportedOpaqueControlFlowGap,
};

struct SctStructureEvidence final {
    SctStructureEvidenceKind kind = SctStructureEvidenceKind::CurrentControlFlow;
    SctSemanticConfidence confidence = SctSemanticConfidence::Unknown;
    std::optional<SctControlFlowKind> edgeKind{};
    std::optional<SctParameterSite> origin{};
    std::optional<SctInstructionId> source{};
    std::optional<SctInstructionId> target{};
    std::vector<SctOpaqueAttachmentId> opaqueAttachments{};
    auto operator<=>(const SctStructureEvidence&) const = default;
};

enum class SctStructuredRegionKind {
    If,
    IfElse,
    While,
    NaturalLoop,
    Switch,
};

struct SctStructuredRegionId final {
    SctSectionId section;
    SctInstructionId headerInstruction;
    SctStructuredRegionKind kind = SctStructuredRegionKind::If;
    auto operator<=>(const SctStructuredRegionId&) const = default;
};

enum class SctStructuredArmKind {
    Then,
    Else,
    LoopBody,
    SwitchCase,
};

struct SctSwitchCaseLabel final {
    std::uint32_t repeatedGroupOrdinal = 0;
    std::optional<std::int32_t> value{};
    std::optional<SctParameterSite> valueSite{};
    std::optional<SctParameterSite> targetSite{};
    auto operator<=>(const SctSwitchCaseLabel&) const = default;
};

struct SctStructuredArm final {
    SctStructuredArmKind kind = SctStructuredArmKind::Then;
    std::optional<SctBasicBlockId> entry{};
    std::vector<SctBasicBlockId> blocks{};
    std::vector<SctSwitchCaseLabel> caseLabels{};
    auto operator<=>(const SctStructuredArm&) const = default;
};

enum class SctStructureInterpretationBasis {
    CurrentGraph,
};

struct SctStructuredRegion final {
    SctStructuredRegionId id;
    SctBasicBlockId header;
    std::optional<SctBasicBlockId> join{};
    std::vector<SctBasicBlockId> members{};
    std::vector<SctStructuredArm> arms{};
    std::optional<SctStructuredRegionId> parent{};
    std::optional<SctParameterSite> controllerSite{};
    SctStructureInterpretationBasis basis = SctStructureInterpretationBasis::CurrentGraph;
    SctSemanticConfidence minimumEdgeConfidence = SctSemanticConfidence::Unknown;
    std::vector<SctStructureEvidence> evidence{};
    auto operator<=>(const SctStructuredRegion&) const = default;
};

enum class SctStructuredRejectionReason {
    MissingTarget,
    ExternalEntry,
    UnsupportedExitShape,
    NonContiguousCandidate,
    OverlappingArms,
    AmbiguousSwitchCases,
    ClosedComponentWithoutExit,
    HistoricalConflict,
    UnsupportedHistoricalShape,
};

enum class SctStructureIssueKind {
    UnresolvedControlFlow,
    CrossSectionNonCallControlFlow,
    MissingControlFlow,
    IrreducibleCycle,
    MultipleEntryRegion,
    AmbiguousJoin,
    OverlappingRegions,
    AmbiguousSwitchCases,
    HistoricalEdgeConflict,
    RejectedStructuredCandidate,
};

struct SctStructureIssue final {
    SctStructureIssueKind kind = SctStructureIssueKind::RejectedStructuredCandidate;
    SctSectionId section;
    std::optional<SctInstructionId> instruction{};
    std::vector<SctInstructionId> relatedInstructions{};
    std::optional<SctStructuredRejectionReason> rejectionReason{};
    std::vector<SctStructureEvidence> evidence{};
    auto operator<=>(const SctStructureIssue&) const = default;
};

// Imported topology can suggest a historical shape, but it never contributes
// blocks, reachability, dominance, nesting, or regions to the current graph.
struct SctHistoricalStructureCandidate final {
    SctSectionId section;
    SctInstructionId sourceInstruction;
    std::optional<SctInstructionId> targetInstruction{};
    std::optional<std::uint32_t> unresolvedTargetPayloadOffset{};
    std::optional<SctStructuredRegionKind> suggestedKind{};
    std::optional<SctInstructionId> suggestedController{};
    std::optional<SctInstructionId> suggestedJoin{};
    std::vector<SctInstructionId> involvedInstructions{};
    SctSemanticConfidence evidenceConfidence = SctSemanticConfidence::Heuristic;
    std::optional<SctStructuredRejectionReason> rejectionReason{};
    std::vector<SctStructureEvidence> evidence{};
    auto operator<=>(const SctHistoricalStructureCandidate&) const = default;
};

// A conservative, value-owned structural view for one script section. Issues
// describe rejected or incomplete claims and never alter the source document.
struct SctSectionStructure final {
    SctSectionId section;
    std::vector<SctSectionEntryPoint> entryPoints{};
    std::vector<SctStructuredBasicBlock> blocks{};
    std::vector<SctStructuredRegion> regions{};
    std::vector<SctHistoricalStructureCandidate> historicalCandidates{};
    std::vector<SctStructureIssue> issues{};
    auto operator<=>(const SctSectionStructure&) const = default;
};

// Revision-scoped derived analysis. Rebuild after mutating the document.
// Stored results contain stable IDs and no pointers into the source document.
class SctStructuredControlFlowAnalysis final {
public:
    // Standalone construction builds the supporting indexes from the same
    // document revision, preventing accidental cross-revision composition.
    [[nodiscard]] static SctStructuredControlFlowAnalysis build(
        const SctDocument& document,
        const SctBoundImportEvidence* evidence = nullptr);

    [[nodiscard]] std::span<const SctSectionStructure> sections() const noexcept {
        return sections_;
    }
    [[nodiscard]] const SctSectionStructure* findSection(
        SctSectionId section) const noexcept;
    [[nodiscard]] const SctStructuredBasicBlock* blockContaining(
        SctInstructionId instruction) const noexcept;
    [[nodiscard]] const SctStructuredRegion* findRegion(
        const SctStructuredRegionId& region) const noexcept;
    [[nodiscard]] std::vector<const SctStructuredRegion*> regionsContaining(
        SctInstructionId instruction) const;

private:
    friend struct SctDocumentAnalysis;

    [[nodiscard]] static SctStructuredControlFlowAnalysis buildFromIndexes(
        const SctDocument& document,
        const SctControlFlowIndex& controlFlow,
        const SctOpaqueContextIndex& opaqueContext);
    void buildIndexes();

    std::vector<SctSectionStructure> sections_{};
    std::map<SctSectionId, std::size_t> sectionIndex_{};
    std::map<SctInstructionId, std::pair<std::size_t, std::size_t>> instructionBlockIndex_{};
    std::map<SctStructuredRegionId, std::pair<std::size_t, std::size_t>> regionIndex_{};
    std::map<SctBasicBlockId, std::vector<std::pair<std::size_t, std::size_t>>> regionsByBlock_{};
};

}  // namespace spice::sct
