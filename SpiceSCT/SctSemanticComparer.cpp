#include "SctSemanticComparer.h"

#include "SctIrBuilder.h"
#include "SctOpcodeMetadata.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace spice::sct {
namespace {

struct NormalizedInstruction {
    std::uint16_t opcode = 0;
    std::vector<std::string> operands;
};

struct NormalizedSection {
    std::string name;
    SctSectionKind kind = SctSectionKind::Unknown;
    std::vector<NormalizedInstruction> instructions;
    std::vector<std::uint8_t> rawBytes;
};

std::vector<const SctInstruction*> sortedInstructions(const SctSection& section) {
    std::vector<const SctInstruction*> result{};
    result.reserve(section.instructions.size());
    for (const auto& instruction : section.instructions) {
        result.push_back(&instruction);
    }
    std::sort(result.begin(), result.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->offset < rhs->offset;
    });
    return result;
}

std::unordered_map<std::uint32_t, std::size_t> instructionIndexes(const std::vector<const SctInstruction*>& instructions) {
    std::unordered_map<std::uint32_t, std::size_t> result{};
    for (std::size_t i = 0; i < instructions.size(); ++i) {
        result.emplace(instructions[i]->offset, i);
    }
    return result;
}

const SctEdge* findEdge(
    const SctSection& section,
    std::uint32_t fromOffset,
    SctEdgeType type,
    std::size_t ordinal = 0) {
    std::size_t seen = 0;
    for (const auto& edge : section.edges) {
        if (!edge.fromOffset.has_value() || *edge.fromOffset != fromOffset || edge.type != type || !edge.toOffset.has_value()) {
            continue;
        }
        if (seen++ == ordinal) {
            return &edge;
        }
    }
    return nullptr;
}

std::string targetToken(
    const std::unordered_map<std::uint32_t, std::size_t>& indexes,
    const SctEdge* edge) {
    if (edge == nullptr || !edge->toOffset.has_value()) {
        return "target:<missing>";
    }
    const auto it = indexes.find(*edge->toOffset);
    if (it == indexes.end()) {
        return "target:end";
    }
    return "target:" + std::to_string(it->second);
}

std::vector<std::string> normalizedOperands(
    const SctSection& section,
    const SctInstruction& instruction,
    const std::unordered_map<std::uint32_t, std::size_t>& indexes) {
    std::vector<std::string> result{};
    result.reserve(instruction.operands.size());

    if (instruction.opcode == 0) {
        for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
            if (i + 1u == instruction.operands.size()) {
                result.push_back(targetToken(indexes, findEdge(section, instruction.offset, SctEdgeType::BranchFalse)));
            } else {
                result.push_back(std::to_string(instruction.operands[i]));
            }
        }
        return result;
    }

    if (instruction.opcode == 10) {
        result.push_back(targetToken(indexes, findEdge(section, instruction.offset, SctEdgeType::Jump)));
        return result;
    }

    if (instruction.opcode != 3) {
        for (const auto operand : instruction.operands) {
            result.push_back(std::to_string(operand));
        }
        return result;
    }

    const auto& pattern = kSalsaOpcodeParamPatterns[instruction.opcode];
    std::size_t edgeOrdinal = 0;
    const auto loopWidth = pattern.loopStartParam >= 0 && pattern.loopEndParam >= pattern.loopStartParam
        ? static_cast<std::size_t>(pattern.loopEndParam - pattern.loopStartParam + 1)
        : 0u;
    for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
        const bool isSwitchTarget = pattern.switchJumpParam >= 0
            && loopWidth > 0u
            && i >= static_cast<std::size_t>(pattern.switchJumpParam)
            && ((i - static_cast<std::size_t>(pattern.switchJumpParam)) % loopWidth) == 0u;
        if (isSwitchTarget) {
            result.push_back(targetToken(indexes, findEdge(section, instruction.offset, SctEdgeType::SwitchCase, edgeOrdinal++)));
        } else {
            result.push_back(std::to_string(instruction.operands[i]));
        }
    }
    return result;
}

std::vector<std::uint8_t> rawSectionBytes(const SctSection& section) {
    std::vector<std::uint8_t> bytes{};
    for (const auto& span : section.rawSpans) {
        if (span.rawBytes.empty()) {
            continue;
        }
        if (bytes.size() < span.startOffset) {
            bytes.resize(span.startOffset, 0u);
        }
        if (bytes.size() < span.startOffset + span.rawBytes.size()) {
            bytes.resize(span.startOffset + span.rawBytes.size(), 0u);
        }
        std::copy(span.rawBytes.begin(), span.rawBytes.end(), bytes.begin() + static_cast<std::ptrdiff_t>(span.startOffset));
    }
    return bytes;
}

NormalizedSection normalizeSection(const SctSection& section) {
    NormalizedSection result{};
    result.name = section.id.name;
    result.kind = section.kind;

    if (section.kind != SctSectionKind::Script) {
        result.rawBytes = rawSectionBytes(section);
        return result;
    }

    const auto instructions = sortedInstructions(section);
    const auto indexes = instructionIndexes(instructions);
    result.instructions.reserve(instructions.size());
    for (const auto* instruction : instructions) {
        result.instructions.push_back({
            instruction->opcode,
            normalizedOperands(section, *instruction, indexes),
        });
    }
    return result;
}

std::vector<NormalizedSection> normalize(const SctParseResult& parseResult) {
    const auto ir = SctIrBuilder{}.build(parseResult);
    std::vector<NormalizedSection> sections{};
    sections.reserve(ir.file.sections.size());
    for (const auto& section : ir.file.sections) {
        sections.push_back(normalizeSection(section));
    }
    return sections;
}

void addDifference(SctSemanticCompareResult& result, std::string difference) {
    result.equivalent = false;
    result.differences.push_back(std::move(difference));
}

} // namespace

SctSemanticCompareResult SctSemanticComparer::compare(
    const SctParseResult& lhs,
    const SctParseResult& rhs) const {
    SctSemanticCompareResult result{};
    const auto lhsSections = normalize(lhs);
    const auto rhsSections = normalize(rhs);

    if (lhsSections.size() != rhsSections.size()) {
        addDifference(result, "section count differs");
        return result;
    }

    for (std::size_t i = 0; i < lhsSections.size(); ++i) {
        const auto& left = lhsSections[i];
        const auto& right = rhsSections[i];
        const auto prefix = "section " + std::to_string(i) + " (" + left.name + "): ";

        if (left.name != right.name) {
            addDifference(result, prefix + "name differs");
        }
        if (left.kind != right.kind) {
            addDifference(result, prefix + "kind differs");
            continue;
        }
        if (left.kind != SctSectionKind::Script) {
            if (left.rawBytes != right.rawBytes) {
                addDifference(result, prefix + "raw payload differs");
            }
            continue;
        }
        if (left.instructions.size() != right.instructions.size()) {
            addDifference(result, prefix + "instruction count differs");
            continue;
        }
        for (std::size_t instIndex = 0; instIndex < left.instructions.size(); ++instIndex) {
            const auto& leftInst = left.instructions[instIndex];
            const auto& rightInst = right.instructions[instIndex];
            const auto instPrefix = prefix + "instruction " + std::to_string(instIndex) + ": ";
            if (leftInst.opcode != rightInst.opcode) {
                addDifference(result, instPrefix + "opcode differs");
                continue;
            }
            if (leftInst.operands != rightInst.operands) {
                addDifference(result, instPrefix + "operands differ");
            }
        }
    }

    return result;
}

} // namespace spice::sct
