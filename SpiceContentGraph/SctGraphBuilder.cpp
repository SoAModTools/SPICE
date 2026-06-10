#include "SctGraphBuilder.h"

#include "ContentGraphIds.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>

namespace spice::contentgraph {
namespace {

std::string decimalString(std::uint32_t value) {
    return std::to_string(value);
}

std::string hexString(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

ContentEvidence instructionEvidence(
    const std::string& sourcePath,
    const std::string& sectionName,
    const spice::sct::SctInstruction& instruction,
    std::string detail) {
    ContentEvidence evidence{};
    evidence.sourcePath = sourcePath;
    evidence.sectionName = sectionName;
    evidence.instructionOffset = instruction.offset;
    evidence.opcode = instruction.opcode;
    evidence.detail = std::move(detail);
    return evidence;
}

const spice::sct::SctInstruction* instructionAt(
    const spice::sct::SctSection& section,
    std::uint32_t offset) {
    const auto it = std::find_if(section.instructions.begin(), section.instructions.end(), [offset](const auto& instruction) {
        return instruction.offset == offset;
    });
    return it == section.instructions.end() ? nullptr : &*it;
}

ContentEdgeType contentEdgeTypeForSctEdge(spice::sct::SctEdgeType type) {
    switch (type) {
    case spice::sct::SctEdgeType::Fallthrough:
        return ContentEdgeType::Fallthrough;
    case spice::sct::SctEdgeType::BranchTrue:
        return ContentEdgeType::BranchTrue;
    case spice::sct::SctEdgeType::BranchFalse:
        return ContentEdgeType::BranchFalse;
    case spice::sct::SctEdgeType::SwitchCase:
        return ContentEdgeType::SwitchCase;
    case spice::sct::SctEdgeType::Jump:
        return ContentEdgeType::Jump;
    case spice::sct::SctEdgeType::CallSubscript:
        return ContentEdgeType::CallSubscript;
    case spice::sct::SctEdgeType::Return:
        return ContentEdgeType::Return;
    case spice::sct::SctEdgeType::LoadsMld:
        return ContentEdgeType::LoadsMld;
    case spice::sct::SctEdgeType::LoadsScript:
        return ContentEdgeType::LoadsScript;
    case spice::sct::SctEdgeType::ReferencesString:
        return ContentEdgeType::Jump;
    }
    return ContentEdgeType::Jump;
}

ContentConfidence contentConfidenceForSctEdge(spice::sct::SctSemanticConfidence confidence) {
    switch (confidence) {
    case spice::sct::SctSemanticConfidence::Known:
        return ContentConfidence::Known;
    case spice::sct::SctSemanticConfidence::Partial:
        return ContentConfidence::Inferred;
    case spice::sct::SctSemanticConfidence::Heuristic:
        return ContentConfidence::Heuristic;
    case spice::sct::SctSemanticConfidence::Unknown:
        return ContentConfidence::Unresolved;
    }
    return ContentConfidence::Unresolved;
}

const spice::sct::SctEdge* findSctEdgeForSuccessor(
    const spice::sct::SctSection& section,
    const spice::sct::SctInstruction* lastInstruction,
    std::uint32_t blockStartOffset,
    std::uint32_t successorOffset) {
    const auto fromOffset = lastInstruction == nullptr ? blockStartOffset : lastInstruction->offset;
    const auto it = std::find_if(section.edges.begin(), section.edges.end(), [&](const auto& edge) {
        return edge.fromOffset.has_value()
            && *edge.fromOffset == fromOffset
            && edge.toOffset.has_value()
            && *edge.toOffset == successorOffset;
    });
    return it == section.edges.end() ? nullptr : &*it;
}

std::uint32_t firstOperandFromEdgeOrInstruction(
    const spice::sct::SctEdge& edge,
    const spice::sct::SctInstruction& instruction) {
    const auto edgeOperand = edge.attributes.find("operand0");
    if (edgeOperand != edge.attributes.end()) {
        try {
            return static_cast<std::uint32_t>(std::stoul(edgeOperand->second));
        } catch (...) {
        }
    }
    return instruction.operands.empty() ? 0u : instruction.operands.front();
}

std::string resourceKind(spice::sct::SctEdgeType type) {
    return type == spice::sct::SctEdgeType::LoadsMld ? "mld" : "script";
}

void addInstructionEdgeTarget(
    ContentGraph& graph,
    const std::string& sourcePath,
    const std::string& sectionName,
    const spice::sct::SctInstruction& instruction,
    const spice::sct::SctEdge& sctEdge,
    const std::string& sourceNodeId) {
    const auto contentType = contentEdgeTypeForSctEdge(sctEdge.type);
    const auto operand = firstOperandFromEdgeOrInstruction(sctEdge, instruction);
    std::string targetId{};
    ContentNodeType targetType = ContentNodeType::UnknownTarget;
    std::string targetLabel{};

    if (sctEdge.type == spice::sct::SctEdgeType::LoadsMld || sctEdge.type == spice::sct::SctEdgeType::LoadsScript) {
        const auto kind = resourceKind(sctEdge.type);
        targetId = resourceRefNodeId(kind, sourcePath + ":" + sectionName + ":" + decimalString(instruction.offset)
            + ":opcode:" + decimalString(instruction.opcode));
        targetType = ContentNodeType::ResourceRef;
        targetLabel = std::string("unresolved ") + kind + " load";
    } else if (sctEdge.type == spice::sct::SctEdgeType::CallSubscript) {
        targetId = unknownTargetNodeId(sourcePath, sectionName, instruction.offset, instruction.opcode);
        targetLabel = "subscript target";
    } else if (sctEdge.type == spice::sct::SctEdgeType::Return) {
        targetId = unknownTargetNodeId(sourcePath, sectionName, instruction.offset, instruction.opcode);
        targetLabel = "return target";
    } else {
        return;
    }

    ContentNode target{};
    target.id = targetId;
    target.type = targetType;
    target.label = std::move(targetLabel);
    target.sourcePath = sourcePath;
    if (targetType == ContentNodeType::ResourceRef) {
        target.attributes.emplace("resource_kind", resourceKind(sctEdge.type));
    }
    target.attributes.emplace("opcode", decimalString(instruction.opcode));
    target.attributes.emplace(sctEdge.type == spice::sct::SctEdgeType::CallSubscript ? "offset_operand" : "operand0",
        decimalString(operand));
    graph.addNode(std::move(target));

    ContentEdge edge{};
    edge.from = sourceNodeId;
    edge.to = targetId;
    edge.type = contentType;
    edge.confidence = sctEdge.type == spice::sct::SctEdgeType::LoadsMld || sctEdge.type == spice::sct::SctEdgeType::LoadsScript
        ? ContentConfidence::Unresolved
        : contentConfidenceForSctEdge(sctEdge.confidence);
    edge.attributes.emplace("opcode", decimalString(instruction.opcode));
    edge.attributes.emplace(sctEdge.type == spice::sct::SctEdgeType::CallSubscript ? "offset_operand" : "operand0",
        decimalString(operand));
    edge.evidence.push_back(instructionEvidence(sourcePath, sectionName, instruction,
        sctEdge.detail.empty() ? "SCT IR semantic edge." : sctEdge.detail));
    graph.addEdge(std::move(edge));
}

void addFlagEdges(
    ContentGraph& graph,
    const std::string& sourcePath,
    const std::string& sectionName,
    const std::string& sectionNodeId,
    const spice::sct::FlagAccessSummary& flags) {
    auto addFlag = [&](std::uint32_t flag, ContentEdgeType edgeType) {
        const auto flagId = flagRefNodeId(flag);
        ContentNode node{};
        node.id = flagId;
        node.type = ContentNodeType::FlagRef;
        node.label = "BitVar " + std::to_string(flag);
        node.attributes.emplace("flag", std::to_string(flag));
        graph.addNode(std::move(node));

        ContentEdge edge{};
        edge.from = sectionNodeId;
        edge.to = flagId;
        edge.type = edgeType;
        edge.confidence = ContentConfidence::Known;
        edge.evidence.push_back(ContentEvidence{sourcePath, sectionName, std::nullopt, std::nullopt, "SCT flag summary.", {}});
        graph.addEdge(std::move(edge));
    };

    for (const auto flag : flags.flagsRead) {
        addFlag(flag, ContentEdgeType::ReferencesFlag);
    }
    for (const auto flag : flags.flagsTested) {
        addFlag(flag, ContentEdgeType::ReferencesFlag);
    }
    for (const auto flag : flags.flagsWritten) {
        addFlag(flag, ContentEdgeType::WritesFlag);
    }
}

} // namespace

void SctGraphBuilder::addToGraph(ContentGraph& graph, const spice::sct::SctParseResult& parseResult,
    const SctGraphBuildOptions& options) const {
    const auto sourcePath = parseResult.file.sourcePath;
    const auto scriptFileId = scriptFileNodeId(sourcePath);

    ContentNode scriptFile{};
    scriptFile.id = scriptFileId;
    scriptFile.type = ContentNodeType::ScriptFile;
    scriptFile.label = filenameLabel(sourcePath);
    scriptFile.sourcePath = sourcePath;
    scriptFile.attributes.emplace("section_count", std::to_string(parseResult.file.sections.size()));
    graph.addNode(std::move(scriptFile));

    for (const auto& section : parseResult.file.sections) {
        const auto sectionId = scriptSectionNodeId(sourcePath, section.id.name);
        ContentNode sectionNode{};
        sectionNode.id = sectionId;
        sectionNode.type = ContentNodeType::ScriptSection;
        sectionNode.label = section.id.name;
        sectionNode.sourcePath = sourcePath;
        sectionNode.offset = section.startOffset;
        sectionNode.size = section.endOffset > section.startOffset ? section.endOffset - section.startOffset : 0u;
        sectionNode.attributes.emplace("section_index", std::to_string(section.id.index));
        sectionNode.attributes.emplace("is_string_section", section.isStringSection ? "true" : "false");
        graph.addNode(std::move(sectionNode));

        ContentEdge containsSection{};
        containsSection.from = scriptFileId;
        containsSection.to = sectionId;
        containsSection.type = ContentEdgeType::Contains;
        graph.addEdge(std::move(containsSection));

        if (options.includeFlagEdges) {
            addFlagEdges(graph, sourcePath, section.id.name, sectionId, section.flagSummary);
        }

        std::unordered_map<std::uint32_t, std::string> blockIdsByOffset{};
        if (options.detailLevel != ContentGraphDetailLevel::Sections) {
            for (const auto& block : section.blocks) {
                const auto blockId = basicBlockNodeId(sourcePath, section.id.name, block.startOffset);
                blockIdsByOffset.emplace(block.startOffset, blockId);
                ContentNode blockNode{};
                blockNode.id = blockId;
                blockNode.type = ContentNodeType::BasicBlock;
                blockNode.label = section.id.name + " block " + hexString(block.startOffset);
                blockNode.sourcePath = sourcePath;
                blockNode.offset = block.startOffset;
                blockNode.size = block.endOffset > block.startOffset ? block.endOffset - block.startOffset : 0u;
                graph.addNode(std::move(blockNode));

                ContentEdge containsBlock{};
                containsBlock.from = sectionId;
                containsBlock.to = blockId;
                containsBlock.type = ContentEdgeType::Contains;
                graph.addEdge(std::move(containsBlock));
            }
        }

        for (const auto& instruction : section.instructions) {
            const auto sourceNodeId = options.detailLevel == ContentGraphDetailLevel::Instructions
                ? instructionNodeId(sourcePath, section.id.name, instruction.offset)
                : sectionId;

            if (options.detailLevel == ContentGraphDetailLevel::Instructions) {
                ContentNode instructionNode{};
                instructionNode.id = sourceNodeId;
                instructionNode.type = ContentNodeType::Instruction;
                const auto instructionName = instruction.mnemonic.empty()
                    ? std::string("opcode ") + std::to_string(instruction.opcode)
                    : instruction.mnemonic;
                instructionNode.label = section.id.name + " " + instructionName
                    + " @" + hexString(instruction.offset);
                instructionNode.sourcePath = sourcePath;
                instructionNode.offset = instruction.offset;
                instructionNode.size = instruction.sizeBytes;
                instructionNode.attributes.emplace("opcode", std::to_string(instruction.opcode));
                if (!instruction.mnemonic.empty()) {
                    instructionNode.attributes.emplace("mnemonic", instruction.mnemonic);
                }
                instructionNode.attributes.emplace("decode_ok", instruction.decodeOk ? "true" : "false");
                graph.addNode(std::move(instructionNode));

                const auto containingBlock = std::find_if(section.blocks.begin(), section.blocks.end(), [&instruction](const auto& block) {
                    return instruction.offset >= block.startOffset && instruction.offset < block.endOffset;
                });
                if (containingBlock != section.blocks.end()) {
                    ContentEdge containsInstruction{};
                    containsInstruction.from = basicBlockNodeId(sourcePath, section.id.name, containingBlock->startOffset);
                    containsInstruction.to = sourceNodeId;
                    containsInstruction.type = ContentEdgeType::Contains;
                    graph.addEdge(std::move(containsInstruction));
                }
            }

            for (const auto& sctEdge : section.edges) {
                if (!sctEdge.fromOffset.has_value() || *sctEdge.fromOffset != instruction.offset) {
                    continue;
                }
                if (!options.includeResourceEdges
                    && (sctEdge.type == spice::sct::SctEdgeType::LoadsMld || sctEdge.type == spice::sct::SctEdgeType::LoadsScript)) {
                    continue;
                }
                addInstructionEdgeTarget(graph, sourcePath, section.id.name, instruction, sctEdge, sourceNodeId);
            }
        }

        if (options.detailLevel == ContentGraphDetailLevel::Sections) {
            continue;
        }

        for (const auto& block : section.blocks) {
            const auto blockId = basicBlockNodeId(sourcePath, section.id.name, block.startOffset);
            const auto lastInstruction = block.instructionOffsets.empty()
                ? nullptr
                : instructionAt(section, block.instructionOffsets.back());
            for (std::size_t i = 0; i < block.successorOffsets.size(); ++i) {
                const auto successorOffset = block.successorOffsets[i];
                const auto targetIt = blockIdsByOffset.find(successorOffset);
                if (targetIt == blockIdsByOffset.end()) {
                    continue;
                }
                const auto* sctEdge = findSctEdgeForSuccessor(section, lastInstruction, block.startOffset, successorOffset);
                ContentEdge edge{};
                edge.from = blockId;
                edge.to = targetIt->second;
                edge.type = sctEdge == nullptr ? ContentEdgeType::Jump : contentEdgeTypeForSctEdge(sctEdge->type);
                edge.confidence = sctEdge == nullptr ? ContentConfidence::Known : contentConfidenceForSctEdge(sctEdge->confidence);
                edge.attributes.emplace("target_offset", decimalString(successorOffset));
                if (lastInstruction != nullptr) {
                    edge.evidence.push_back(instructionEvidence(sourcePath, section.id.name, *lastInstruction,
                        sctEdge == nullptr || sctEdge->detail.empty() ? "SCT basic-block successor." : sctEdge->detail));
                }
                graph.addEdge(std::move(edge));
            }
        }
    }
}

} // namespace spice::contentgraph
