#include "SctIrBuilder.h"

#include "SctOpcodeMetadata.h"

#include <optional>
#include <string>
#include <utility>

namespace spice::sct {
namespace {

std::string fallbackMnemonic(std::uint16_t opcode) {
    return "op_" + std::to_string(opcode);
}

SctSectionKind inferSectionKind(const SctSection& section) {
    if (section.kind != SctSectionKind::Unknown) {
        return section.kind;
    }
    if (section.isStringSection) {
        return SctSectionKind::String;
    }
    return section.instructions.empty() ? SctSectionKind::Unknown : SctSectionKind::Script;
}

SctParameter makeFallbackParameter(const SctInstruction& instruction, std::size_t operandIndex) {
    const auto metadata = sctOpcodeMetadata(instruction.opcode);
    SctParameter parameter{};
    parameter.index = static_cast<std::uint32_t>(operandIndex);
    if (operandIndex < metadata.parameterRoles.size()) {
        parameter.role = std::string(metadata.parameterRoles[operandIndex]);
    }
    parameter.confidence = metadata.confidence;
    parameter.valueKind = SctParameterValueKind::Integer;
    if (metadata.resourceRole != SctOpcodeResourceRole::None) {
        parameter.valueKind = SctParameterValueKind::ResourceRef;
    } else if (parameter.role.find("offset") != std::string::npos || parameter.role.find("Offset") != std::string::npos) {
        parameter.valueKind = SctParameterValueKind::Link;
    }
    parameter.rawWords.push_back(instruction.operands[operandIndex]);
    parameter.displayValue = std::to_string(instruction.operands[operandIndex]);
    return parameter;
}

std::optional<std::uint32_t> firstOperand(const SctInstruction& instruction) {
    if (instruction.operands.empty()) {
        return std::nullopt;
    }
    return instruction.operands.front();
}

SctEdgeType semanticEdgeType(const SctOpcodeSemanticMetadata& metadata) {
    switch (metadata.controlRole) {
    case SctOpcodeControlRole::CallSubscript:
        return SctEdgeType::CallSubscript;
    case SctOpcodeControlRole::Return:
        return SctEdgeType::Return;
    default:
        break;
    }
    switch (metadata.resourceRole) {
    case SctOpcodeResourceRole::LoadsMld:
        return SctEdgeType::LoadsMld;
    case SctOpcodeResourceRole::LoadsScript:
        return SctEdgeType::LoadsScript;
    default:
        return SctEdgeType::Fallthrough;
    }
}

void addInstructionSemanticEdges(SctSection& section, const SctInstruction& instruction) {
    const auto metadata = sctOpcodeMetadata(instruction.opcode);
    const auto type = semanticEdgeType(metadata);
    if (type == SctEdgeType::Fallthrough) {
        return;
    }

    SctEdge edge{};
    edge.type = type;
    edge.confidence = metadata.confidence;
    edge.fromOffset = instruction.offset;
    edge.fromPayloadOffset = section.startOffset + instruction.offset;
    edge.opcode = instruction.opcode;
    edge.detail = instruction.mnemonic.empty() ? fallbackMnemonic(instruction.opcode) : instruction.mnemonic;
    if (const auto operand = firstOperand(instruction); operand.has_value()) {
        edge.attributes.emplace("operand0", std::to_string(*operand));
        if (type == SctEdgeType::CallSubscript) {
            edge.attributes.emplace("signed_offset_operand", std::to_string(static_cast<std::int32_t>(*operand)));
            if (const auto targetPayloadOffset = resolveRelativeTargetPayloadOffset(
                    *edge.fromPayloadOffset,
                    instruction.sizeBytes,
                    *operand);
                targetPayloadOffset.has_value()) {
                edge.toPayloadOffset = *targetPayloadOffset;
                edge.toOffset = *targetPayloadOffset >= section.startOffset && *targetPayloadOffset < section.endOffset
                    ? *targetPayloadOffset - section.startOffset
                    : *targetPayloadOffset;
                edge.attributes.emplace("target_payload_offset", std::to_string(*targetPayloadOffset));
            } else {
                edge.attributes.emplace("target_resolution", "out_of_range");
            }
        }
    }
    section.edges.push_back(std::move(edge));
}

SctEdgeType edgeTypeForSuccessor(
    const SctInstruction* lastInstruction,
    std::size_t successorIndex,
    std::uint32_t successorOffset,
    std::uint32_t blockEndOffset) {
    if (lastInstruction == nullptr) {
        return successorOffset == blockEndOffset ? SctEdgeType::Fallthrough : SctEdgeType::Jump;
    }
    switch (lastInstruction->opcode) {
    case 0:
        return successorIndex == 0 ? SctEdgeType::BranchFalse : SctEdgeType::BranchTrue;
    case 3:
        return SctEdgeType::SwitchCase;
    case 10:
        return SctEdgeType::Jump;
    default:
        return successorOffset == blockEndOffset ? SctEdgeType::Fallthrough : SctEdgeType::Jump;
    }
}

const SctInstruction* instructionAt(const SctSection& section, std::uint32_t offset) {
    for (const auto& instruction : section.instructions) {
        if (instruction.offset == offset) {
            return &instruction;
        }
    }
    return nullptr;
}

void addBlockSuccessorEdges(SctSection& section) {
    for (const auto& block : section.blocks) {
        const auto* lastInstruction = block.instructionOffsets.empty()
            ? nullptr
            : instructionAt(section, block.instructionOffsets.back());
        for (std::size_t i = 0; i < block.successorOffsets.size(); ++i) {
            const auto successorOffset = block.successorOffsets[i];
            SctEdge edge{};
            edge.type = edgeTypeForSuccessor(lastInstruction, i, successorOffset, block.endOffset);
            edge.confidence = SctSemanticConfidence::Known;
            edge.fromOffset = lastInstruction == nullptr ? block.startOffset : lastInstruction->offset;
            edge.toOffset = successorOffset;
            edge.opcode = lastInstruction == nullptr ? 0 : lastInstruction->opcode;
            edge.detail = "basic-block successor";
            edge.attributes.emplace("target_offset", std::to_string(successorOffset));
            section.edges.push_back(std::move(edge));
        }
    }
}

} // namespace

SctParseResult SctIrBuilder::build(const SctParseResult& parseResult) const {
    auto result = parseResult;

    for (auto& section : result.file.sections) {
        section.kind = inferSectionKind(section);

        if (section.rawSpans.empty()) {
            for (const auto& region : section.unknownRegions) {
                SctRawSpan span{};
                span.startOffset = region.startOffset;
                span.endOffset = region.endOffset;
                span.reason = SctRawSpanReason::Unreached;
                span.rawBytes = region.rawBytes;
                span.detail = region.reason;
                section.rawSpans.push_back(std::move(span));
            }
        }

        for (auto& instruction : section.instructions) {
            const auto metadata = sctOpcodeMetadata(instruction.opcode);
            if (instruction.mnemonic.empty()) {
                instruction.mnemonic = metadata.mnemonic.empty() ? fallbackMnemonic(instruction.opcode) : std::string(metadata.mnemonic);
            }
            if (instruction.semanticConfidence == SctSemanticConfidence::Unknown) {
                instruction.semanticConfidence = metadata.confidence;
            }
            if (instruction.rawWords.empty()) {
                if (instruction.skipRefresh) {
                    instruction.rawWords.push_back(13u);
                }
                if (instruction.scheduled.present) {
                    if (!instruction.scheduled.rawWords.empty()) {
                        instruction.rawWords.insert(
                            instruction.rawWords.end(),
                            instruction.scheduled.rawWords.begin(),
                            instruction.scheduled.rawWords.end());
                    } else {
                        instruction.rawWords.push_back(129u);
                        instruction.rawWords.insert(
                            instruction.rawWords.end(),
                            instruction.scheduled.frameDelay.rawWords.begin(),
                            instruction.scheduled.frameDelay.rawWords.end());
                        instruction.rawWords.push_back(instruction.scheduled.instructionByteLength);
                    }
                }
                instruction.opcodeWordIndex = static_cast<std::uint32_t>(instruction.rawWords.size());
                instruction.rawWords.push_back(instruction.opcode);
                instruction.rawWords.insert(instruction.rawWords.end(), instruction.operands.begin(), instruction.operands.end());
            }
            if (instruction.parameters.empty()) {
                for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
                    instruction.parameters.push_back(makeFallbackParameter(instruction, i));
                }
            }
        }

        if (section.edges.empty()) {
            for (const auto& instruction : section.instructions) {
                addInstructionSemanticEdges(section, instruction);
            }
            addBlockSuccessorEdges(section);
        }
    }

    return result;
}

} // namespace spice::sct
