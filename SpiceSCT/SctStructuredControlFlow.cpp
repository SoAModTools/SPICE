#include "SctStructuredControlFlow.h"

#include "SctDocumentAnalysis.h"
#include "SctOpcodeMetadata.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <ranges>
#include <set>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace spice::sct {
namespace {

struct InstructionLocation final {
    SctSectionId section;
    std::size_t ordinal = 0;
    const SctDocumentInstruction* instruction = nullptr;
};

struct EffectiveEdge final {
    SctControlFlowEdge edge;
    bool historical = false;
    std::vector<SctOpaqueAttachmentId> qualifyingOpaqueAttachments{};
};

class BitSet final {
public:
    explicit BitSet(const std::size_t size = 0, const bool fill = false)
        : size_(size), words_((size + 63u) / 64u, fill ? ~std::uint64_t{} : 0u) {
        trim();
    }

    void set(const std::size_t value) { words_[value / 64u] |= std::uint64_t{1} << (value % 64u); }
    void reset(const std::size_t value) { words_[value / 64u] &= ~(std::uint64_t{1} << (value % 64u)); }
    [[nodiscard]] bool test(const std::size_t value) const {
        return value < size_ && (words_[value / 64u] & (std::uint64_t{1} << (value % 64u))) != 0u;
    }
    void intersect(const BitSet& other) {
        for (std::size_t i = 0; i < words_.size(); ++i) words_[i] &= other.words_[i];
    }
    [[nodiscard]] std::size_t count() const {
        return std::accumulate(words_.begin(), words_.end(), std::size_t{},
            [](const std::size_t total, const std::uint64_t value) {
                return total + static_cast<std::size_t>(std::popcount(value));
            });
    }
    [[nodiscard]] bool operator==(const BitSet&) const = default;

private:
    void trim() {
        if (!words_.empty() && size_ % 64u != 0u)
            words_.back() &= (std::uint64_t{1} << (size_ % 64u)) - 1u;
    }

    std::size_t size_ = 0;
    std::vector<std::uint64_t> words_{};
};

struct SectionGraph final {
    const SctDocumentSection* section = nullptr;
    const SctScriptSectionContent* script = nullptr;
    std::vector<EffectiveEdge> edges{};
    std::vector<SctStructuredBasicBlock> blocks{};
    std::unordered_map<std::uint64_t, std::size_t> instructionBlock{};
    std::unordered_map<std::uint64_t, std::size_t> instructionOrdinal{};
    std::vector<std::vector<std::size_t>> successors{};
    std::vector<std::vector<std::size_t>> predecessors{};
    std::set<std::size_t> entryBlocks{};
    std::vector<bool> exitsSection{};
    std::vector<bool> canReachExit{};
    std::vector<BitSet> dominators{};
    std::vector<BitSet> postDominators{};
    std::size_t syntheticExit = 0;
};

[[nodiscard]] int confidenceRank(const SctSemanticConfidence value) noexcept {
    switch (value) {
    case SctSemanticConfidence::Unknown: return 0;
    case SctSemanticConfidence::Heuristic: return 1;
    case SctSemanticConfidence::Partial: return 2;
    case SctSemanticConfidence::Known: return 3;
    }
    return 0;
}

[[nodiscard]] SctSemanticConfidence weakest(
    const SctSemanticConfidence left, const SctSemanticConfidence right) noexcept {
    return confidenceRank(left) <= confidenceRank(right) ? left : right;
}

[[nodiscard]] bool isLocalSuccessorKind(const SctControlFlowKind kind) noexcept {
    return kind != SctControlFlowKind::Call && kind != SctControlFlowKind::Return;
}

[[nodiscard]] bool terminatesLocalBlock(const SctControlFlowKind kind) noexcept {
    return kind == SctControlFlowKind::BranchTrue
        || kind == SctControlFlowKind::BranchFalse
        || kind == SctControlFlowKind::SwitchCase
        || kind == SctControlFlowKind::Jump
        || kind == SctControlFlowKind::Return;
}

[[nodiscard]] bool sameTarget(const SctControlFlowEdge& left, const SctControlFlowEdge& right) {
    return left.sourceInstruction == right.sourceInstruction && left.kind == right.kind
        && left.origin == right.origin && left.targetInstruction == right.targetInstruction;
}

[[nodiscard]] std::vector<SctOpaqueAttachmentId> qualifyingOpaqueGaps(
    const SctOpaqueContextIndex& opaqueContext, const SctControlFlowEdge& edge) {
    std::vector<SctOpaqueAttachmentId> result;
    for (const auto attachment : edge.crossedOpaqueAttachments) {
        const auto* context = opaqueContext.find(attachment);
        if (context == nullptr) continue;
        if (std::ranges::any_of(context->interpretations, [](const auto& interpretation) {
                return interpretation.kind == SctOpaqueInterpretationKind::ControlFlowGap
                    || interpretation.kind == SctOpaqueInterpretationKind::SwitchDispatchGap;
            })) result.push_back(attachment);
    }
    return result;
}

[[nodiscard]] SctStructureEvidence evidenceFor(
    const SctStructureEvidenceKind kind, const SctStructuredControlTransfer& transfer,
    std::vector<SctOpaqueAttachmentId> attachments = {}) {
    return {kind, transfer.confidence, transfer.kind, transfer.origin,
        transfer.sourceInstruction, transfer.targetInstruction, std::move(attachments)};
}

[[nodiscard]] SctStructureEvidence evidenceFor(
    const SctStructureEvidenceKind kind, const EffectiveEdge& edge) {
    const auto evidenceKind = edge.historical && !edge.qualifyingOpaqueAttachments.empty()
        ? SctStructureEvidenceKind::ImportedOpaqueControlFlowGap : kind;
    return {evidenceKind,
        edge.historical ? SctSemanticConfidence::Heuristic : edge.edge.confidence,
        edge.edge.kind, edge.edge.origin, edge.edge.sourceInstruction,
        edge.edge.targetInstruction, edge.edge.crossedOpaqueAttachments};
}

void addIssue(SctSectionStructure& output, const SctStructureIssueKind kind,
    const SctSectionId section, const std::optional<SctInstructionId> instruction,
    std::vector<SctInstructionId> related = {},
    const std::optional<SctStructuredRejectionReason> rejectionReason = std::nullopt,
    std::vector<SctStructureEvidence> evidence = {}) {
    output.issues.push_back({kind, section, instruction, std::move(related), rejectionReason,
        std::move(evidence)});
}

[[nodiscard]] SectionGraph buildGraph(
    const SctDocumentSection& section, const SctScriptSectionContent& script,
    const std::vector<EffectiveEdge>& allEdges,
    const std::unordered_map<std::uint64_t, InstructionLocation>& locations,
    SctSectionStructure& output) {
    SectionGraph graph{&section, &script};
    if (script.instructions.empty()) return graph;
    for (std::size_t i = 0; i < script.instructions.size(); ++i)
        graph.instructionOrdinal.emplace(script.instructions[i].id.value(), i);
    for (const auto& edge : allEdges) {
        const auto source = locations.find(edge.edge.sourceInstruction.value());
        if (source != locations.end() && source->second.section == section.id)
            graph.edges.push_back(edge);
    }

    std::vector<bool> leaders(script.instructions.size(), false);
    leaders.front() = true;
    std::vector<std::pair<SctInstructionId, SctSectionEntryKind>> pendingEntries{
        {script.instructions.front().id, SctSectionEntryKind::PhysicalSectionStart}};
    std::vector<bool> hasOutgoing(script.instructions.size(), false);

    // Incoming current control transfers establish additional independent
    // entries. Historical observations never change the current entry model.
    for (const auto& effective : allEdges) {
        if (effective.historical || !effective.edge.targetInstruction) continue;
        const auto sourceLocation = locations.find(effective.edge.sourceInstruction.value());
        const auto targetLocation = locations.find(effective.edge.targetInstruction->value());
        if (sourceLocation == locations.end() || targetLocation == locations.end()
            || targetLocation->second.section != section.id) continue;
        if (effective.edge.kind == SctControlFlowKind::Call) {
            const auto kind = sourceLocation->second.section == section.id
                ? SctSectionEntryKind::SameSectionCallTarget
                : SctSectionEntryKind::CrossSectionCallTarget;
            pendingEntries.emplace_back(*effective.edge.targetInstruction, kind);
            leaders[targetLocation->second.ordinal] = true;
        } else if (sourceLocation->second.section != section.id) {
            pendingEntries.emplace_back(*effective.edge.targetInstruction,
                SctSectionEntryKind::CrossSectionNonCallTarget);
            leaders[targetLocation->second.ordinal] = true;
        }
    }

    for (const auto& effective : graph.edges) {
        const auto source = graph.instructionOrdinal.find(effective.edge.sourceInstruction.value());
        if (source == graph.instructionOrdinal.end()) continue;
        hasOutgoing[source->second] = true;
        if (terminatesLocalBlock(effective.edge.kind) && source->second + 1u < leaders.size())
            leaders[source->second + 1u] = true;
        if (!effective.edge.targetInstruction) {
            if (effective.edge.kind != SctControlFlowKind::Return) {
                addIssue(output, SctStructureIssueKind::UnresolvedControlFlow, section.id,
                    effective.edge.sourceInstruction, {}, SctStructuredRejectionReason::MissingTarget,
                    {evidenceFor(SctStructureEvidenceKind::CurrentControlFlow, effective)});
            }
            continue;
        }
        const auto targetLocation = locations.find(effective.edge.targetInstruction->value());
        if (targetLocation == locations.end()) continue;
        if (targetLocation->second.section != section.id) {
            if (!effective.historical && effective.edge.kind != SctControlFlowKind::Call) {
                addIssue(output, SctStructureIssueKind::CrossSectionNonCallControlFlow,
                    section.id, effective.edge.sourceInstruction,
                    {*effective.edge.targetInstruction}, SctStructuredRejectionReason::ExternalEntry,
                    {evidenceFor(SctStructureEvidenceKind::CurrentControlFlow, effective)});
            }
            continue;
        }
        if (effective.edge.kind != SctControlFlowKind::Fallthrough)
            leaders[targetLocation->second.ordinal] = true;
    }
    for (std::size_t i = 0; i + 1u < script.instructions.size(); ++i) {
        if (!hasOutgoing[i]) {
            leaders[i + 1u] = true;
            addIssue(output, SctStructureIssueKind::MissingControlFlow, section.id,
                script.instructions[i].id);
        }
    }

    std::size_t begin = 0;
    while (begin < script.instructions.size()) {
        std::size_t end = begin + 1u;
        while (end < script.instructions.size() && !leaders[end]) ++end;
        SctStructuredBasicBlock block;
        block.id = {section.id, script.instructions[begin].id};
        for (std::size_t i = begin; i < end; ++i) {
            block.instructions.push_back(script.instructions[i].id);
            graph.instructionBlock.emplace(script.instructions[i].id.value(), graph.blocks.size());
        }
        graph.blocks.push_back(std::move(block));
        begin = end;
    }

    std::ranges::sort(pendingEntries, [&](const auto& left, const auto& right) {
        const auto leftOrdinal = graph.instructionOrdinal.at(left.first.value());
        const auto rightOrdinal = graph.instructionOrdinal.at(right.first.value());
        return leftOrdinal != rightOrdinal ? leftOrdinal < rightOrdinal : left.second < right.second;
    });
    pendingEntries.erase(std::unique(pendingEntries.begin(), pendingEntries.end()), pendingEntries.end());
    for (const auto& [instructionId, kind] : pendingEntries) {
        const auto block = graph.instructionBlock.find(instructionId.value());
        if (block == graph.instructionBlock.end()) continue;
        graph.entryBlocks.insert(block->second);
        output.entryPoints.push_back({instructionId, graph.blocks[block->second].id, kind});
    }

    graph.successors.resize(graph.blocks.size());
    graph.predecessors.resize(graph.blocks.size());
    graph.exitsSection.assign(graph.blocks.size(), false);
    for (const auto& effective : graph.edges) {
        const auto source = graph.instructionBlock.find(effective.edge.sourceInstruction.value());
        if (source == graph.instructionBlock.end()) continue;
        std::optional<SctBasicBlockId> targetBlock;
        if (effective.edge.targetInstruction) {
            const auto target = graph.instructionBlock.find(effective.edge.targetInstruction->value());
            if (target != graph.instructionBlock.end()) targetBlock = graph.blocks[target->second].id;
        }
        graph.blocks[source->second].transfers.push_back({effective.edge.sourceInstruction,
            effective.edge.kind, effective.edge.confidence, effective.edge.origin,
            effective.edge.targetInstruction, targetBlock,
            effective.edge.unresolvedTargetPayloadOffset,
            effective.edge.crossedOpaqueAttachments});
        if (effective.edge.kind == SctControlFlowKind::Return
            || (isLocalSuccessorKind(effective.edge.kind) && !targetBlock)) {
            graph.exitsSection[source->second] = true;
        }
        if (!targetBlock || !isLocalSuccessorKind(effective.edge.kind)) continue;
        const auto target = graph.instructionBlock.find(targetBlock->entryInstruction.value());
        if (target == graph.instructionBlock.end()) continue;
        if (target->second == source->second
            && effective.edge.kind == SctControlFlowKind::Fallthrough) continue;
        if (std::ranges::find(graph.successors[source->second], target->second)
            == graph.successors[source->second].end()) {
            graph.successors[source->second].push_back(target->second);
            graph.predecessors[target->second].push_back(source->second);
        }
    }

    for (std::size_t block = 0; block < graph.blocks.size(); ++block) {
        if (graph.successors[block].empty() && !graph.exitsSection[block])
            graph.exitsSection[block] = true;
    }

    std::queue<std::size_t> pending;
    for (const auto entry : graph.entryBlocks) {
        pending.push(entry);
        graph.blocks[entry].reachable = true;
    }
    while (!pending.empty()) {
        const auto current = pending.front();
        pending.pop();
        for (const auto next : graph.successors[current]) {
            if (graph.blocks[next].reachable) continue;
            graph.blocks[next].reachable = true;
            pending.push(next);
        }
    }

    const auto blockCount = graph.blocks.size();
    graph.syntheticExit = blockCount;
    graph.dominators.assign(blockCount, BitSet(blockCount, true));
    for (std::size_t i = 0; i < blockCount; ++i) {
        if (!graph.blocks[i].reachable || graph.entryBlocks.contains(i)) {
            graph.dominators[i] = BitSet(blockCount);
            graph.dominators[i].set(i);
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < blockCount; ++i) {
            if (!graph.blocks[i].reachable || graph.entryBlocks.contains(i)) continue;
            BitSet next(blockCount, true);
            bool found = false;
            for (const auto predecessor : graph.predecessors[i]) {
                if (!graph.blocks[predecessor].reachable) continue;
                if (!found) next = graph.dominators[predecessor];
                else next.intersect(graph.dominators[predecessor]);
                found = true;
            }
            if (!found) next = BitSet(blockCount);
            next.set(i);
            if (!(next == graph.dominators[i])) {
                graph.dominators[i] = std::move(next);
                changed = true;
            }
        }
    }

    const auto postSize = blockCount + 1u;
    graph.canReachExit.assign(blockCount, false);
    std::queue<std::size_t> exitPending;
    for (std::size_t block = 0; block < blockCount; ++block) {
        if (!graph.exitsSection[block]) continue;
        graph.canReachExit[block] = true;
        exitPending.push(block);
    }
    while (!exitPending.empty()) {
        const auto current = exitPending.front();
        exitPending.pop();
        for (const auto predecessor : graph.predecessors[current]) {
            if (graph.canReachExit[predecessor]) continue;
            graph.canReachExit[predecessor] = true;
            exitPending.push(predecessor);
        }
    }

    graph.postDominators.assign(postSize, BitSet(postSize));
    graph.postDominators[graph.syntheticExit] = BitSet(postSize);
    graph.postDominators[graph.syntheticExit].set(graph.syntheticExit);
    for (std::size_t i = 0; i < blockCount; ++i) {
        if (graph.blocks[i].reachable && graph.canReachExit[i]) {
            for (std::size_t candidate = 0; candidate < blockCount; ++candidate)
                if (graph.blocks[candidate].reachable && graph.canReachExit[candidate])
                    graph.postDominators[i].set(candidate);
            graph.postDominators[i].set(graph.syntheticExit);
        } else {
            graph.postDominators[i].set(i);
        }
    }
    changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < blockCount; ++i) {
            if (!graph.blocks[i].reachable || !graph.canReachExit[i]) continue;
            std::vector<std::size_t> successors = graph.successors[i];
            if (graph.exitsSection[i]) successors.push_back(graph.syntheticExit);
            if (successors.empty()) successors.push_back(graph.syntheticExit);
            BitSet next = graph.postDominators[successors.front()];
            for (std::size_t j = 1; j < successors.size(); ++j)
                next.intersect(graph.postDominators[successors[j]]);
            next.set(i);
            if (!(next == graph.postDominators[i])) {
                graph.postDominators[i] = std::move(next);
                changed = true;
            }
        }
    }
    return graph;
}

[[nodiscard]] bool dominates(const SectionGraph& graph,
    const std::size_t dominator, const std::size_t block) {
    return block < graph.dominators.size() && graph.dominators[block].test(dominator);
}

[[nodiscard]] std::optional<std::size_t> immediatePostDominator(
    const SectionGraph& graph, const std::size_t block) {
    if (block >= graph.postDominators.size()
        || !graph.postDominators[block].test(graph.syntheticExit)) return std::nullopt;
    std::optional<std::size_t> result;
    std::size_t bestCount = 0;
    for (std::size_t candidate = 0; candidate < graph.postDominators.size(); ++candidate) {
        if (candidate == block || !graph.postDominators[block].test(candidate)) continue;
        const auto count = graph.postDominators[candidate].count();
        if (!result || count > bestCount) {
            result = candidate;
            bestCount = count;
        }
    }
    return result;
}

[[nodiscard]] std::optional<std::size_t> nearestPostDominatorOutside(
    const SectionGraph& graph, const std::size_t block,
    const std::set<std::size_t>& excluded) {
    auto candidate = immediatePostDominator(graph, block);
    std::set<std::size_t> visited;
    while (candidate && *candidate != graph.syntheticExit
        && excluded.contains(*candidate)) {
        if (!visited.insert(*candidate).second) return std::nullopt;
        candidate = immediatePostDominator(graph, *candidate);
    }
    return candidate;
}

[[nodiscard]] std::set<std::size_t> reachableUntil(
    const SectionGraph& graph, const std::size_t start,
    const std::optional<std::size_t> stop) {
    std::set<std::size_t> result;
    if (stop && start == *stop) return result;
    std::queue<std::size_t> pending;
    pending.push(start);
    while (!pending.empty()) {
        const auto current = pending.front();
        pending.pop();
        if (stop && current == *stop) continue;
        if (!result.insert(current).second) continue;
        for (const auto next : graph.successors[current]) pending.push(next);
    }
    return result;
}

[[nodiscard]] std::vector<SctBasicBlockId> blockIds(
    const SectionGraph& graph, const std::set<std::size_t>& indexes) {
    std::vector<SctBasicBlockId> result;
    for (const auto index : indexes) result.push_back(graph.blocks[index].id);
    return result;
}

[[nodiscard]] bool contiguous(const std::set<std::size_t>& indexes) {
    return indexes.empty() || (*indexes.rbegin() - *indexes.begin() + 1u) == indexes.size();
}

[[nodiscard]] bool hasDefensibleBoundaries(const SectionGraph& graph,
    const std::set<std::size_t>& members, const std::size_t header,
    const std::optional<std::size_t> join) {
    for (const auto block : members) {
        if (block != header && graph.entryBlocks.contains(block)) return false;
        if (block != header) {
            for (const auto predecessor : graph.predecessors[block]) {
                if (!members.contains(predecessor)) return false;
            }
        }
        for (const auto& edge : graph.blocks[block].transfers) {
            if (edge.kind == SctControlFlowKind::Call) continue;
            if (edge.kind == SctControlFlowKind::Return) continue;
            if (!edge.targetBlock) return false;
            const auto target = graph.instructionBlock.find(
                edge.targetBlock->entryInstruction.value());
            if (target == graph.instructionBlock.end()) return false;
            if (members.contains(target->second) || (join && target->second == *join)) continue;
            return false;
        }
    }
    return true;
}

[[nodiscard]] const EffectiveEdge* effectiveEdgeFor(
    const SectionGraph& graph, const SctStructuredControlTransfer& transfer) {
    const auto found = std::ranges::find_if(graph.edges, [&](const auto& candidate) {
        return candidate.edge.sourceInstruction == transfer.sourceInstruction
            && candidate.edge.kind == transfer.kind
            && candidate.edge.origin == transfer.origin
            && candidate.edge.targetInstruction == transfer.targetInstruction;
    });
    return found == graph.edges.end() ? nullptr : &*found;
}

[[nodiscard]] SctStructureEvidence graphEvidenceFor(
    const SectionGraph& graph, const SctStructureEvidenceKind kind,
    const SctStructuredControlTransfer& transfer) {
    const auto* effective = effectiveEdgeFor(graph, transfer);
    return effective != nullptr && effective->historical
        ? evidenceFor(kind, *effective)
        : evidenceFor(kind, transfer, transfer.crossedOpaqueAttachments);
}

[[nodiscard]] SctSemanticConfidence regionConfidence(
    const SectionGraph& graph, const std::set<std::size_t>& members) {
    auto confidence = SctSemanticConfidence::Known;
    bool found = false;
    for (const auto block : members) {
        for (const auto& edge : graph.blocks[block].transfers) {
            if (edge.kind == SctControlFlowKind::Call) continue;
            confidence = weakest(confidence, edge.confidence);
            const auto* effective = effectiveEdgeFor(graph, edge);
            if (effective != nullptr && effective->historical)
                confidence = weakest(confidence, SctSemanticConfidence::Heuristic);
            found = true;
        }
    }
    if (!found) confidence = SctSemanticConfidence::Unknown;
    return confidence;
}

[[nodiscard]] std::vector<SctStructureEvidence> canonicalEvidence(
    const SectionGraph& graph, const std::set<std::size_t>& members) {
    std::vector<SctStructureEvidence> result;
    for (const auto block : members) {
        for (const auto& edge : graph.blocks[block].transfers) {
            if (edge.kind == SctControlFlowKind::Call) continue;
            result.push_back(graphEvidenceFor(
                graph, SctStructureEvidenceKind::CurrentControlFlow, edge));
        }
    }
    return result;
}

[[nodiscard]] const SctDocumentInstruction* instruction(
    const SectionGraph& graph, const SctInstructionId id) {
    const auto found = graph.instructionOrdinal.find(id.value());
    return found == graph.instructionOrdinal.end()
        ? nullptr : &graph.script->instructions[found->second];
}

[[nodiscard]] std::optional<SctParameterSite> parameterSiteForRole(
    const SctDocumentInstruction& instructionValue, const std::string_view role) {
    const auto* schema = findSctOpcodeSchema(instructionValue.opcode);
    if (schema == nullptr) return std::nullopt;
    for (std::uint32_t index = 0; index < schema->semantic.parameterRoles.size(); ++index) {
        if (schema->semantic.parameterRoles[index] != role) continue;
        if (std::ranges::find(instructionValue.fixedParameters, index,
                &SctDocumentParameter::schemaIndex) != instructionValue.fixedParameters.end()) {
            return SctParameterSite{instructionValue.id, {index, std::nullopt}};
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> targetBlock(
    const SectionGraph& graph, const SctStructuredControlTransfer& edge) {
    if (!edge.targetBlock) return std::nullopt;
    const auto found = graph.instructionBlock.find(edge.targetBlock->entryInstruction.value());
    return found == graph.instructionBlock.end() ? std::nullopt : std::optional{found->second};
}

[[nodiscard]] std::vector<const SctStructuredControlTransfer*> edgesOf(
    const SectionGraph& graph, const std::size_t block, const SctControlFlowKind kind) {
    std::vector<const SctStructuredControlTransfer*> result;
    for (const auto& edge : graph.blocks[block].transfers)
        if (edge.kind == kind) result.push_back(&edge);
    return result;
}

void detectLoops(const SectionGraph& graph, SctSectionStructure& output) {
    // Characterize irreducible SCCs before looking for natural back edges. A
    // cyclic component with multiple graph entry nodes cannot be represented
    // as a single-entry structured loop and must stay flat.
    std::vector<int> discovery(graph.blocks.size(), -1);
    std::vector<int> lowLink(graph.blocks.size(), -1);
    std::vector<std::size_t> stack;
    std::vector<bool> onStack(graph.blocks.size(), false);
    int nextDiscovery = 0;
    std::function<void(std::size_t)> visit = [&](const std::size_t node) {
        discovery[node] = lowLink[node] = nextDiscovery++;
        stack.push_back(node);
        onStack[node] = true;
        for (const auto successor : graph.successors[node]) {
            if (!graph.blocks[successor].reachable) continue;
            if (discovery[successor] < 0) {
                visit(successor);
                lowLink[node] = std::min(lowLink[node], lowLink[successor]);
            } else if (onStack[successor]) {
                lowLink[node] = std::min(lowLink[node], discovery[successor]);
            }
        }
        if (lowLink[node] != discovery[node]) return;
        std::set<std::size_t> component;
        while (!stack.empty()) {
            const auto member = stack.back();
            stack.pop_back();
            onStack[member] = false;
            component.insert(member);
            if (member == node) break;
        }
        const bool selfCycle = component.size() == 1u
            && std::ranges::find(graph.successors[*component.begin()], *component.begin())
                != graph.successors[*component.begin()].end();
        if (component.size() < 2u && !selfCycle) return;
        std::set<std::size_t> entries;
        for (const auto member : component) {
            if (graph.entryBlocks.contains(member) || std::ranges::any_of(graph.predecessors[member],
                    [&](const auto predecessor) { return !component.contains(predecessor); })) {
                entries.insert(member);
            }
        }
        if (entries.size() > 1u) {
            addIssue(output, SctStructureIssueKind::IrreducibleCycle,
                graph.section->id, graph.blocks[*entries.begin()].id.entryInstruction,
                [&] {
                    std::vector<SctInstructionId> related;
                    for (const auto entry : entries)
                        related.push_back(graph.blocks[entry].id.entryInstruction);
                    return related;
                }(), SctStructuredRejectionReason::ExternalEntry);
        }
    };
    for (std::size_t node = 0; node < graph.blocks.size(); ++node)
        if (graph.blocks[node].reachable && discovery[node] < 0) visit(node);

    std::map<std::size_t, std::set<std::size_t>> loops;
    for (std::size_t source = 0; source < graph.blocks.size(); ++source) {
        if (!graph.blocks[source].reachable) continue;
        for (const auto target : graph.successors[source]) {
            if (!dominates(graph, target, source)) continue;
            auto& members = loops[target];
            members.insert(target);
            members.insert(source);
            std::vector<std::size_t> pending{source};
            while (!pending.empty()) {
                const auto current = pending.back();
                pending.pop_back();
                for (const auto predecessor : graph.predecessors[current]) {
                    if (members.insert(predecessor).second && predecessor != target)
                        pending.push_back(predecessor);
                }
            }
        }
    }

    for (const auto& [header, members] : loops) {
        if (!contiguous(members)) {
            addIssue(output, SctStructureIssueKind::IrreducibleCycle, graph.section->id,
                graph.blocks[header].id.entryInstruction, {},
                SctStructuredRejectionReason::NonContiguousCandidate);
            continue;
        }
        bool singleEntry = true;
        for (const auto member : members) {
            if (member == header) continue;
            if (graph.entryBlocks.contains(member)
                || std::ranges::any_of(graph.predecessors[member], [&](const auto predecessor) {
                    return !members.contains(predecessor);
                })) {
                singleEntry = false;
                break;
            }
        }
        if (!singleEntry) {
            addIssue(output, SctStructureIssueKind::MultipleEntryRegion, graph.section->id,
                graph.blocks[header].id.entryInstruction, {},
                SctStructuredRejectionReason::ExternalEntry);
            continue;
        }
        const auto controller = graph.blocks[header].instructions.back();
        const auto trueEdges = edgesOf(graph, header, SctControlFlowKind::BranchTrue);
        const auto falseEdges = edgesOf(graph, header, SctControlFlowKind::BranchFalse);
        const bool whileShape = trueEdges.size() == 1u && falseEdges.size() == 1u
            && targetBlock(graph, *trueEdges.front()).has_value()
            && targetBlock(graph, *falseEdges.front()).has_value()
            && (members.contains(*targetBlock(graph, *trueEdges.front()))
                != members.contains(*targetBlock(graph, *falseEdges.front())));
        std::set<std::size_t> body = members;
        body.erase(header);
        std::optional<std::size_t> exit;
        if (whileShape) {
            const auto trueTarget = *targetBlock(graph, *trueEdges.front());
            const auto falseTarget = *targetBlock(graph, *falseEdges.front());
            exit = members.contains(trueTarget) ? falseTarget : trueTarget;
        }
        auto evidence = canonicalEvidence(graph, members);
        for (const auto member : members) {
            for (const auto& edge : graph.blocks[member].transfers) {
                const auto target = targetBlock(graph, edge);
                if (edge.kind == SctControlFlowKind::Jump && target
                    && *target == header && member >= header) {
                    evidence.push_back(graphEvidenceFor(graph,
                        SctStructureEvidenceKind::BackwardTerminatorJump, edge));
                }
            }
        }
        SctStructuredRegion region;
        region.id = {graph.section->id, controller,
            whileShape ? SctStructuredRegionKind::While : SctStructuredRegionKind::NaturalLoop};
        region.header = graph.blocks[header].id;
        if (exit) region.join = graph.blocks[*exit].id;
        region.members = blockIds(graph, members);
        region.arms.push_back({SctStructuredArmKind::LoopBody,
            body.empty() ? std::nullopt : std::optional{graph.blocks[*body.begin()].id},
            blockIds(graph, body), {}});
        const auto* controllerInstruction = instruction(graph, controller);
        if (controllerInstruction != nullptr)
            region.controllerSite = parameterSiteForRole(*controllerInstruction, "condition");
        region.minimumEdgeConfidence = regionConfidence(graph, members);
        region.evidence = std::move(evidence);
        output.regions.push_back(std::move(region));
    }
}

[[nodiscard]] bool isLoopHeader(const SctSectionStructure& output, const SctBasicBlockId& block) {
    return std::ranges::any_of(output.regions, [&](const auto& region) {
        return region.header == block && (region.id.kind == SctStructuredRegionKind::While
            || region.id.kind == SctStructuredRegionKind::NaturalLoop);
    });
}

void detectConditionals(const SectionGraph& graph, SctSectionStructure& output) {
    for (std::size_t header = 0; header < graph.blocks.size(); ++header) {
        if (!graph.blocks[header].reachable) continue;
        const auto trueEdges = edgesOf(graph, header, SctControlFlowKind::BranchTrue);
        const auto falseEdges = edgesOf(graph, header, SctControlFlowKind::BranchFalse);
        if (trueEdges.size() != 1u || falseEdges.size() != 1u
            || isLoopHeader(output, graph.blocks[header].id)) continue;
        const auto trueTarget = targetBlock(graph, *trueEdges.front());
        const auto falseTarget = targetBlock(graph, *falseEdges.front());
        if (!trueTarget || !falseTarget) continue;
        const auto post = immediatePostDominator(graph, header);
        if (!post) {
            addIssue(output, SctStructureIssueKind::AmbiguousJoin, graph.section->id,
                graph.blocks[header].instructions.back(), {},
                graph.postDominators[header].test(graph.syntheticExit)
                    ? SctStructuredRejectionReason::UnsupportedExitShape
                    : SctStructuredRejectionReason::ClosedComponentWithoutExit);
            continue;
        }
        const auto join = *post == graph.syntheticExit ? std::nullopt : std::optional{*post};
        auto thenBlocks = reachableUntil(graph, *trueTarget, join);
        auto elseBlocks = reachableUntil(graph, *falseTarget, join);
        std::vector<std::size_t> overlap;
        std::ranges::set_intersection(thenBlocks, elseBlocks, std::back_inserter(overlap));
        if (!overlap.empty()) {
            addIssue(output, SctStructureIssueKind::RejectedStructuredCandidate,
                graph.section->id, graph.blocks[header].instructions.back(), {},
                SctStructuredRejectionReason::OverlappingArms);
            continue;
        }
        std::set<std::size_t> members{header};
        members.insert(thenBlocks.begin(), thenBlocks.end());
        members.insert(elseBlocks.begin(), elseBlocks.end());
        if (!contiguous(members) || !hasDefensibleBoundaries(graph, members, header, join)) {
            addIssue(output, SctStructureIssueKind::RejectedStructuredCandidate,
                graph.section->id, graph.blocks[header].instructions.back(), {},
                !contiguous(members) ? SctStructuredRejectionReason::NonContiguousCandidate
                    : SctStructuredRejectionReason::ExternalEntry);
            continue;
        }

        auto evidence = canonicalEvidence(graph, members);
        evidence.push_back(graphEvidenceFor(graph,
            SctStructureEvidenceKind::ConditionalFalseTarget, *falseEdges.front()));
        const auto falseOrdinal = graph.instructionOrdinal.find(
            graph.blocks[*falseTarget].id.entryInstruction.value());
        if (falseOrdinal != graph.instructionOrdinal.end() && falseOrdinal->second > 0u) {
            const auto& prior = graph.script->instructions[falseOrdinal->second - 1u];
            const auto jump = std::ranges::find_if(graph.edges, [&](const auto& candidate) {
                return candidate.edge.sourceInstruction == prior.id
                    && candidate.edge.kind == SctControlFlowKind::Jump;
            });
            if (jump != graph.edges.end()) {
                evidence.push_back(evidenceFor(SctStructureEvidenceKind::PreTargetJump, *jump));
            }
        }
        SctStructuredRegion region;
        const bool hasElse = !elseBlocks.empty() && (!join || *falseTarget != *join);
        const auto controller = graph.blocks[header].instructions.back();
        region.id = {graph.section->id, controller,
            hasElse ? SctStructuredRegionKind::IfElse : SctStructuredRegionKind::If};
        region.header = graph.blocks[header].id;
        if (join) region.join = graph.blocks[*join].id;
        region.members = blockIds(graph, members);
        region.arms.push_back({SctStructuredArmKind::Then,
            thenBlocks.empty() ? std::nullopt : std::optional{graph.blocks[*thenBlocks.begin()].id},
            blockIds(graph, thenBlocks), {}});
        if (hasElse) {
            region.arms.push_back({SctStructuredArmKind::Else,
                elseBlocks.empty() ? std::nullopt : std::optional{graph.blocks[*elseBlocks.begin()].id},
                blockIds(graph, elseBlocks), {}});
        }
        const auto* controllerInstruction = instruction(graph, controller);
        if (controllerInstruction != nullptr)
            region.controllerSite = parameterSiteForRole(*controllerInstruction, "condition");
        region.minimumEdgeConfidence = regionConfidence(graph, members);
        region.evidence = std::move(evidence);
        output.regions.push_back(std::move(region));
    }
}

[[nodiscard]] std::optional<std::uint32_t> switchCaseValueParameterIndex(
    const SctDocumentInstruction& instruction) {
    const auto* schema = findSctOpcodeSchema(instruction.opcode);
    if (schema == nullptr || schema->semantic.controlRole != SctOpcodeControlRole::Switch) {
        return std::nullopt;
    }
    const auto repeated = sctOpcodeRepeatedGroup(*schema);
    if (!repeated) return std::nullopt;
    for (auto index = repeated->firstParameter; index <= repeated->lastParameter; ++index) {
        const auto* parameter = sctOpcodeParameterSchema(*schema, index);
        if (parameter != nullptr && parameter->role == "caseValue") return index;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::int32_t> caseValue(
    const SctDocumentInstruction& instruction, const std::uint32_t group,
    const std::optional<std::uint32_t> parameterIndex) {
    if (!parameterIndex) return std::nullopt;
    if (group >= instruction.repeatedParameterGroups.size()) return std::nullopt;
    const auto& parameters = instruction.repeatedParameterGroups[group].parameters;
    const auto found = std::ranges::find(
        parameters, *parameterIndex, &SctDocumentParameter::schemaIndex);
    if (found == parameters.end()) return std::nullopt;
    const auto* encoded = std::get_if<SctEncodedWordValue>(&found->value);
    return encoded == nullptr ? std::nullopt
        : std::optional{static_cast<std::int32_t>(encoded->value)};
}

void detectSwitches(const SectionGraph& graph, SctSectionStructure& output) {
    for (std::size_t header = 0; header < graph.blocks.size(); ++header) {
        if (!graph.blocks[header].reachable) continue;
        const auto caseEdges = edgesOf(graph, header, SctControlFlowKind::SwitchCase);
        if (caseEdges.empty()) continue;
        std::map<std::size_t, std::vector<const SctStructuredControlTransfer*>> byTarget;
        bool incomplete = false;
        for (const auto* edge : caseEdges) {
            const auto target = targetBlock(graph, *edge);
            if (!target) incomplete = true;
            else byTarget[*target].push_back(edge);
        }
        if (incomplete || byTarget.empty()) {
            addIssue(output, SctStructureIssueKind::AmbiguousSwitchCases,
                graph.section->id, graph.blocks[header].instructions.back(), {},
                SctStructuredRejectionReason::MissingTarget);
            continue;
        }
        std::set<std::size_t> caseEntries;
        for (const auto& [entry, edges] : byTarget) caseEntries.insert(entry);
        if (!graph.postDominators[header].test(graph.syntheticExit)) {
            addIssue(output, SctStructureIssueKind::AmbiguousJoin, graph.section->id,
                graph.blocks[header].instructions.back(), {},
                SctStructuredRejectionReason::ClosedComponentWithoutExit);
            continue;
        }
        // A later case can postdominate an earlier fallthrough case without
        // being the switch join. Walk outward until the candidate is no longer
        // itself a case entry.
        const auto post = nearestPostDominatorOutside(graph, header, caseEntries);
        const auto join = post && *post != graph.syntheticExit
            ? std::optional{*post} : std::nullopt;
        std::set<std::size_t> members{header};
        std::vector<SctStructuredArm> arms;
        std::vector<SctStructureEvidence> evidence;
        bool valid = true;
        for (auto current = byTarget.begin(); current != byTarget.end(); ++current) {
            const auto next = std::next(current);
            const auto end = next != byTarget.end() ? next->first
                : join.value_or(graph.blocks.size());
            if (current->first >= end) {
                valid = false;
                break;
            }
            std::set<std::size_t> armBlocks;
            for (auto block = current->first; block < end; ++block) {
                if (!dominates(graph, header, block)
                    || !dominates(graph, current->first, block)) {
                    valid = false;
                    break;
                }
                armBlocks.insert(block);
                members.insert(block);
            }
            if (!valid) break;
            SctStructuredArm arm;
            arm.kind = SctStructuredArmKind::SwitchCase;
            arm.entry = graph.blocks[current->first].id;
            arm.blocks = blockIds(graph, armBlocks);
            for (const auto* edge : current->second) {
                const auto ordinal = edge->origin && edge->origin->parameter.repeatedGroupOrdinal
                    ? *edge->origin->parameter.repeatedGroupOrdinal : 0u;
                const auto* selector = instruction(graph, edge->sourceInstruction);
                const auto valueParameter = selector == nullptr
                    ? std::nullopt : switchCaseValueParameterIndex(*selector);
                arm.caseLabels.push_back({ordinal,
                    selector == nullptr ? std::nullopt
                                        : caseValue(*selector, ordinal, valueParameter),
                    valueParameter ? std::optional{SctParameterSite{
                        edge->sourceInstruction, {*valueParameter, ordinal}}} : std::nullopt,
                    edge->origin});
                evidence.push_back(graphEvidenceFor(graph, current->second.size() > 1u
                    ? SctStructureEvidenceKind::SharedCaseTarget
                    : SctStructureEvidenceKind::PhysicalCaseBoundary, *edge));
            }
            if (next != byTarget.end()) {
                const auto last = *armBlocks.rbegin();
                const bool fallsThrough = std::ranges::any_of(graph.blocks[last].transfers,
                    [&](const auto& edge) {
                        return edge.kind == SctControlFlowKind::Fallthrough
                            && targetBlock(graph, edge) == std::optional{next->first};
                    });
                if (fallsThrough) {
                    evidence.push_back({SctStructureEvidenceKind::CaseFallthrough,
                        SctSemanticConfidence::Known, SctControlFlowKind::Fallthrough,
                        std::nullopt, graph.blocks[last].instructions.back(),
                        graph.blocks[next->first].id.entryInstruction, {}});
                }
            }
            arms.push_back(std::move(arm));
        }
        // Cross-arm flow is only structured switch fallthrough when both the
        // physical boundary and the canonical graph agree. An explicit jump
        // into another case is retained as flat code instead of being
        // over-grouped by the legacy physical-range heuristic.
        if (valid) {
            std::map<std::size_t, std::size_t> armForBlock;
            for (std::size_t armIndex = 0; armIndex < arms.size(); ++armIndex) {
                for (const auto& block : arms[armIndex].blocks) {
                    armForBlock.emplace(
                        graph.instructionBlock.at(block.entryInstruction.value()), armIndex);
                }
            }
            for (std::size_t armIndex = 0; armIndex < arms.size() && valid; ++armIndex) {
                const auto last = graph.instructionBlock.at(
                    arms[armIndex].blocks.back().entryInstruction.value());
                for (const auto& block : arms[armIndex].blocks) {
                    const auto blockIndex = graph.instructionBlock.at(
                        block.entryInstruction.value());
                    for (const auto& edge : graph.blocks[blockIndex].transfers) {
                        const auto target = targetBlock(graph, edge);
                        if (!target || !armForBlock.contains(*target)
                            || armForBlock.at(*target) == armIndex) continue;
                        const bool nextCaseFallthrough = armIndex + 1u < arms.size()
                            && armForBlock.at(*target) == armIndex + 1u
                            && blockIndex == last
                            && arms[armIndex + 1u].entry
                            && *target == graph.instructionBlock.at(
                                arms[armIndex + 1u].entry->entryInstruction.value())
                            && edge.kind == SctControlFlowKind::Fallthrough;
                        if (!nextCaseFallthrough) {
                            valid = false;
                            break;
                        }
                    }
                    if (!valid) break;
                }
            }
        }
        if (!valid || !contiguous(members)
            || !hasDefensibleBoundaries(graph, members, header, join)) {
            addIssue(output, SctStructureIssueKind::AmbiguousSwitchCases,
                graph.section->id, graph.blocks[header].instructions.back(), {},
                !valid ? SctStructuredRejectionReason::AmbiguousSwitchCases
                    : (!contiguous(members)
                        ? SctStructuredRejectionReason::NonContiguousCandidate
                        : SctStructuredRejectionReason::ExternalEntry),
                std::move(evidence));
            continue;
        }
        for (const auto& arm : arms) {
            if (arm.blocks.empty()) continue;
            const auto last = graph.instructionBlock.at(arm.blocks.back().entryInstruction.value());
            for (const auto& edge : graph.blocks[last].transfers) {
                if (edge.kind == SctControlFlowKind::Jump && join
                    && targetBlock(graph, edge) == join) {
                    evidence.push_back(graphEvidenceFor(graph,
                        SctStructureEvidenceKind::CommonForwardExit, edge));
                }
            }
        }
        auto canonical = canonicalEvidence(graph, members);
        evidence.insert(evidence.end(), canonical.begin(), canonical.end());
        SctStructuredRegion region;
        const auto controller = graph.blocks[header].instructions.back();
        region.id = {graph.section->id, controller, SctStructuredRegionKind::Switch};
        region.header = graph.blocks[header].id;
        if (join) region.join = graph.blocks[*join].id;
        region.members = blockIds(graph, members);
        region.arms = std::move(arms);
        const auto* controllerInstruction = instruction(graph, controller);
        if (controllerInstruction != nullptr)
            region.controllerSite = parameterSiteForRole(*controllerInstruction, "choice");
        region.minimumEdgeConfidence = regionConfidence(graph, members);
        region.evidence = std::move(evidence);
        output.regions.push_back(std::move(region));
    }
}

[[nodiscard]] std::set<SctBasicBlockId> memberSet(const SctStructuredRegion& region) {
    return {region.members.begin(), region.members.end()};
}

void resolveNesting(SctSectionStructure& output) {
    std::vector<bool> rejected(output.regions.size(), false);
    std::vector<std::set<SctBasicBlockId>> members;
    members.reserve(output.regions.size());
    for (const auto& region : output.regions) members.push_back(memberSet(region));
    for (std::size_t left = 0; left < output.regions.size(); ++left) {
        for (std::size_t right = left + 1u; right < output.regions.size(); ++right) {
            std::vector<SctBasicBlockId> overlap;
            std::ranges::set_intersection(members[left], members[right], std::back_inserter(overlap));
            if (overlap.empty()) continue;
            const bool leftContains = std::ranges::includes(members[left], members[right]);
            const bool rightContains = std::ranges::includes(members[right], members[left]);
            if (!leftContains && !rightContains) {
                rejected[left] = rejected[right] = true;
                addIssue(output, SctStructureIssueKind::OverlappingRegions, output.section,
                    output.regions[left].id.headerInstruction,
                    {output.regions[right].id.headerInstruction},
                    SctStructuredRejectionReason::OverlappingArms);
            }
        }
    }
    std::vector<SctStructuredRegion> retained;
    for (std::size_t i = 0; i < output.regions.size(); ++i)
        if (!rejected[i]) retained.push_back(std::move(output.regions[i]));
    output.regions = std::move(retained);

    for (auto& child : output.regions) {
        const auto childMembers = memberSet(child);
        const SctStructuredRegion* parent = nullptr;
        std::size_t parentSize = std::numeric_limits<std::size_t>::max();
        for (const auto& candidate : output.regions) {
            if (candidate.id == child.id || candidate.members.size() <= child.members.size()) continue;
            const auto candidateMembers = memberSet(candidate);
            if (std::ranges::includes(candidateMembers, childMembers)
                && candidate.members.size() < parentSize) {
                parent = &candidate;
                parentSize = candidate.members.size();
            }
        }
        if (parent != nullptr) child.parent = parent->id;
    }
    std::ranges::sort(output.regions, [](const auto& left, const auto& right) {
        if (left.header.entryInstruction != right.header.entryInstruction)
            return left.header.entryInstruction < right.header.entryInstruction;
        return left.members.size() > right.members.size();
    });
}

[[nodiscard]] std::vector<SctInstructionId> regionInstructions(
    const SectionGraph& graph, const SctStructuredRegion& region) {
    std::set<std::size_t> blockIndexes;
    for (const auto& block : region.members) {
        const auto found = graph.instructionBlock.find(block.entryInstruction.value());
        if (found != graph.instructionBlock.end()) blockIndexes.insert(found->second);
    }
    std::vector<SctInstructionId> result;
    for (const auto block : blockIndexes) {
        result.insert(result.end(), graph.blocks[block].instructions.begin(),
            graph.blocks[block].instructions.end());
    }
    return result;
}

[[nodiscard]] bool evidenceUsesHistoricalEdge(
    const SctStructureEvidence& evidence, const EffectiveEdge& historical) {
    return (evidence.kind == SctStructureEvidenceKind::ImportedControlFlow
            || evidence.kind == SctStructureEvidenceKind::ImportedOpaqueControlFlowGap)
        && evidence.source == std::optional{historical.edge.sourceInstruction}
        && evidence.target == historical.edge.targetInstruction
        && evidence.edgeKind == std::optional{historical.edge.kind}
        && evidence.origin == historical.edge.origin;
}

}  // namespace

SctStructuredControlFlowAnalysis SctStructuredControlFlowAnalysis::build(
    const SctDocument& document, const SctBoundImportEvidence* evidence) {
    const auto controlFlow = SctControlFlowIndex::build(document, evidence);
    const auto opaqueContext = SctOpaqueContextIndex::build(document, evidence, controlFlow);
    return buildFromIndexes(document, controlFlow, opaqueContext);
}

SctStructuredControlFlowAnalysis SctStructuredControlFlowAnalysis::buildFromIndexes(
    const SctDocument& document, const SctControlFlowIndex& controlFlow,
    const SctOpaqueContextIndex& opaqueContext) {
    SctStructuredControlFlowAnalysis result;
    std::unordered_map<std::uint64_t, InstructionLocation> locations;
    for (const auto& section : document.sections) {
        const auto* script = std::get_if<SctScriptSectionContent>(&section.content);
        if (script == nullptr) continue;
        for (std::size_t ordinal = 0; ordinal < script->instructions.size(); ++ordinal) {
            locations.emplace(script->instructions[ordinal].id.value(),
                InstructionLocation{section.id, ordinal, &script->instructions[ordinal]});
        }
    }
    std::vector<EffectiveEdge> currentEdges;
    currentEdges.reserve(controlFlow.currentEdges().size());
    for (const auto& edge : controlFlow.currentEdges()) currentEdges.push_back({edge});

    for (const auto& section : document.sections) {
        const auto* script = std::get_if<SctScriptSectionContent>(&section.content);
        if (script == nullptr) continue;
        SctSectionStructure output;
        output.section = section.id;
        auto graph = buildGraph(section, *script, currentEdges, locations, output);
        if (!graph.blocks.empty()) {
            detectLoops(graph, output);
            detectConditionals(graph, output);
            detectSwitches(graph, output);
            resolveNesting(output);

            // Historical source topology is evaluated separately. It can suggest
            // a shape, but never changes the current graph or its regions.
            for (const auto& imported : controlFlow.importedEdges()) {
                const auto source = locations.find(imported.sourceInstruction.value());
                if (source == locations.end() || source->second.section != section.id) continue;
                EffectiveEdge hint{imported, true,
                    qualifyingOpaqueGaps(opaqueContext, imported)};
                const auto matchingCurrent = std::ranges::find_if(currentEdges,
                    [&](const auto& candidate) {
                        return candidate.edge.sourceInstruction == imported.sourceInstruction
                            && candidate.edge.kind == imported.kind
                            && candidate.edge.origin == imported.origin;
                    });
                if (matchingCurrent != currentEdges.end()) {
                    if (sameTarget(matchingCurrent->edge, imported)) continue;
                    SctHistoricalStructureCandidate candidate;
                    candidate.section = section.id;
                    candidate.sourceInstruction = imported.sourceInstruction;
                    candidate.targetInstruction = imported.targetInstruction;
                    candidate.unresolvedTargetPayloadOffset = imported.unresolvedTargetPayloadOffset;
                    candidate.rejectionReason = SctStructuredRejectionReason::HistoricalConflict;
                    candidate.evidence.push_back(evidenceFor(
                        SctStructureEvidenceKind::ImportedControlFlow, hint));
                    output.historicalCandidates.push_back(candidate);
                    addIssue(output, SctStructureIssueKind::HistoricalEdgeConflict,
                        section.id, imported.sourceInstruction,
                        imported.targetInstruction
                            ? std::vector<SctInstructionId>{*imported.targetInstruction}
                            : std::vector<SctInstructionId>{},
                        SctStructuredRejectionReason::HistoricalConflict,
                        candidate.evidence);
                    continue;
                }

                auto makeCandidate = [&] {
                    SctHistoricalStructureCandidate candidate;
                    candidate.section = section.id;
                    candidate.sourceInstruction = imported.sourceInstruction;
                    candidate.targetInstruction = imported.targetInstruction;
                    candidate.unresolvedTargetPayloadOffset = imported.unresolvedTargetPayloadOffset;
                    candidate.evidence.push_back(evidenceFor(
                        SctStructureEvidenceKind::ImportedControlFlow, hint));
                    return candidate;
                };

                const auto target = imported.targetInstruction
                    ? locations.find(imported.targetInstruction->value()) : locations.end();
                if (!imported.targetInstruction || target == locations.end()) {
                    auto candidate = makeCandidate();
                    candidate.rejectionReason = SctStructuredRejectionReason::MissingTarget;
                    output.historicalCandidates.push_back(std::move(candidate));
                    continue;
                }
                if (target->second.section != section.id) {
                    auto candidate = makeCandidate();
                    candidate.rejectionReason = SctStructuredRejectionReason::ExternalEntry;
                    output.historicalCandidates.push_back(std::move(candidate));
                    continue;
                }

                auto augmentedEdges = currentEdges;
                augmentedEdges.push_back(hint);
                SctSectionStructure candidateOutput;
                candidateOutput.section = section.id;
                auto candidateGraph = buildGraph(
                    section, *script, augmentedEdges, locations, candidateOutput);
                detectLoops(candidateGraph, candidateOutput);
                detectConditionals(candidateGraph, candidateOutput);
                detectSwitches(candidateGraph, candidateOutput);
                resolveNesting(candidateOutput);
                bool recognized = false;
                for (const auto& region : candidateOutput.regions) {
                    if (!std::ranges::any_of(region.evidence, [&](const auto& evidenceValue) {
                            return evidenceUsesHistoricalEdge(evidenceValue, hint);
                        })) continue;
                    recognized = true;
                    auto candidate = makeCandidate();
                    candidate.suggestedKind = region.id.kind;
                    candidate.suggestedController = region.id.headerInstruction;
                    candidate.suggestedJoin = region.join
                        ? std::optional{region.join->entryInstruction} : std::nullopt;
                    candidate.involvedInstructions = regionInstructions(candidateGraph, region);
                    candidate.evidenceConfidence = region.minimumEdgeConfidence;
                    candidate.evidence = region.evidence;
                    output.historicalCandidates.push_back(std::move(candidate));
                }
                if (!recognized) {
                    auto candidate = makeCandidate();
                    candidate.rejectionReason =
                        SctStructuredRejectionReason::UnsupportedHistoricalShape;
                    output.historicalCandidates.push_back(std::move(candidate));
                }
            }
        }
        output.blocks = std::move(graph.blocks);
        result.sections_.push_back(std::move(output));
    }
    result.buildIndexes();
    return result;
}

const SctSectionStructure* SctStructuredControlFlowAnalysis::findSection(
    const SctSectionId section) const noexcept {
    const auto found = sectionIndex_.find(section);
    return found == sectionIndex_.end() ? nullptr : &sections_[found->second];
}

const SctStructuredBasicBlock* SctStructuredControlFlowAnalysis::blockContaining(
    const SctInstructionId instructionId) const noexcept {
    const auto found = instructionBlockIndex_.find(instructionId);
    return found == instructionBlockIndex_.end() ? nullptr
        : &sections_[found->second.first].blocks[found->second.second];
}

const SctStructuredRegion* SctStructuredControlFlowAnalysis::findRegion(
    const SctStructuredRegionId& regionId) const noexcept {
    const auto found = regionIndex_.find(regionId);
    return found == regionIndex_.end() ? nullptr
        : &sections_[found->second.first].regions[found->second.second];
}

std::vector<const SctStructuredRegion*> SctStructuredControlFlowAnalysis::regionsContaining(
    const SctInstructionId instructionId) const {
    std::vector<const SctStructuredRegion*> result;
    const auto* block = blockContaining(instructionId);
    if (block == nullptr) return result;
    const auto found = regionsByBlock_.find(block->id);
    if (found == regionsByBlock_.end()) return result;
    for (const auto& [section, region] : found->second) {
        result.push_back(&sections_[section].regions[region]);
    }
    std::ranges::sort(result, [](const auto* left, const auto* right) {
        if (left->members.size() != right->members.size()) {
            return left->members.size() < right->members.size();
        }
        return left->id < right->id;
    });
    return result;
}

void SctStructuredControlFlowAnalysis::buildIndexes() {
    sectionIndex_.clear();
    instructionBlockIndex_.clear();
    regionIndex_.clear();
    regionsByBlock_.clear();
    for (std::size_t section = 0; section < sections_.size(); ++section) {
        const auto& value = sections_[section];
        sectionIndex_.emplace(value.section, section);
        for (std::size_t block = 0; block < value.blocks.size(); ++block) {
            for (const auto instruction : value.blocks[block].instructions) {
                instructionBlockIndex_.emplace(instruction, std::pair{section, block});
            }
        }
        for (std::size_t region = 0; region < value.regions.size(); ++region) {
            regionIndex_.emplace(value.regions[region].id, std::pair{section, region});
            for (const auto& block : value.regions[region].members) {
                regionsByBlock_[block].emplace_back(section, region);
            }
        }
    }
}

}  // namespace spice::sct
