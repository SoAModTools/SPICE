#include "SctParser.h"
#include "SctOpcodeMetadata.h"
#include "SctScptDecodeHelpers.h"

#include "../SpiceCore/Binary/EndianReader.h"
#include "../Compression/Aklz.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

namespace spice::sct {
namespace {

constexpr std::size_t kHeaderSize = 12;
constexpr std::size_t kIndexEntrySize = 0x14;
constexpr std::size_t kIndexNameOffset = 4;
constexpr std::size_t kIndexNameMaxLen = 0x10;
constexpr std::uint32_t kMaxOpcodeProbe = 265;
constexpr std::uint32_t kScptStopCode = 0x0000001d;

using Endian = spice::core::Endian;

[[nodiscard]] std::uint32_t readU32(std::span<const std::uint8_t> bytes, std::size_t offset, Endian endian) {
    return spice::core::EndianReader(bytes, endian).try_read_u32(offset).value_or(0U);
}

[[nodiscard]] std::string readIndexName(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset >= bytes.size()) {
        return {};
    }

    const std::size_t limit = std::min<std::size_t>(bytes.size(), offset + kIndexNameMaxLen);
    std::string name;
    for (std::size_t i = offset; i < limit; ++i) {
        if (bytes[i] == 0) {
            break;
        }
        name.push_back(static_cast<char>(bytes[i]));
    }
    return name;
}

[[nodiscard]] Endian detectIndexEndian(std::span<const std::uint8_t> bytes) {
    const auto big = readU32(bytes, 8, Endian::Big);
    const auto little = readU32(bytes, 8, Endian::Little);
    return big <= little ? Endian::Big : Endian::Little;
}

struct DecodedInstruction {
    SctInstruction inst;
    std::vector<std::uint32_t> successors;
    bool blockTerminator = false;
    bool touchesFlag = false;
    bool writesFlag = false;
    bool testedFlag = false;
    bool isSwitch = false;
};

struct GlobalDecodedInstruction {
    DecodedInstruction decoded;
    std::uint32_t entryPayloadOffset = 0;
};

struct SectionRow {
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    std::string name;
    bool isString = false;
    bool isCode = false;
    bool isValid = true;
};

[[nodiscard]] std::string fallbackMnemonic(std::uint16_t opcode) {
    return "op_" + std::to_string(opcode);
}

[[nodiscard]] bool isOffsetInBounds(std::uint32_t offset, std::uint32_t sectionSize) {
    return offset < sectionSize && (offset % 4u == 0u);
}

[[nodiscard]] bool isScptNoLoopValue(std::uint32_t value) {
    return value == 0x7f7fffff || value == 0x00800000 || value == 0x7fffffff || value == kScptStopCode;
}

[[nodiscard]] bool isStringSection(std::span<const std::uint8_t> sectionBytes, Endian baseEndian) {
    if (sectionBytes.size() < 12u) {
        return false;
    }

    const auto otherEndian = baseEndian == Endian::Big ? Endian::Little : Endian::Big;
    const auto firstWordBase = readU32(sectionBytes, 0, baseEndian);
    const auto firstWordOther = readU32(sectionBytes, 0, otherEndian);

    Endian chosenEndian = baseEndian;
    if (firstWordBase != 0x00000009u) {
        if (firstWordOther != 0x00000009u) {
            return false;
        }
        chosenEndian = otherEndian;
    }

    std::uint32_t cursor = 0;
    while (cursor + 4u <= sectionBytes.size()) {
        const auto currentWord = readU32(sectionBytes, cursor, chosenEndian);
        if (currentWord == kScptStopCode) {
            break;
        }
        cursor += 4u;
    }

    if (cursor + 8u > sectionBytes.size()) {
        return false;
    }

    const auto nextWord = readU32(sectionBytes, cursor + 4u, chosenEndian);
    return nextWord > kMaxOpcodeProbe;
}

[[nodiscard]] std::uint32_t scptInputActionPrefix(std::uint32_t value) {
    if (value >= 0x50000000) {
        return 0x50000000;
    }
    if (value >= 0x40000000) {
        return 0x40000000;
    }
    if (value >= 0x20000000) {
        return 0x20000000;
    }
    if (value >= 0x10000000) {
        return 0x10000000;
    }
    if (value >= 0x08000000) {
        return 0x08000000;
    }
    if (value >= 0x04000000) {
        return 0x04000000;
    }
    return 0;
}

[[nodiscard]] bool isScptCompareCode(std::uint32_t value) {
    switch (value) {
    case 0x00000000:
    case 0x00000001:
    case 0x00000002:
    case 0x00000003:
    case 0x00000004:
    case 0x00000005:
    case 0x00000006:
    case 0x00000007:
    case 0x00000008:
    case 0x00000009:
    case 0x0000000a:
    case 0x00000010:
    case 0x00000011:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool isScptArithmeticCode(std::uint32_t value) {
    switch (value) {
    case 0x0000000b:
    case 0x0000000c:
    case 0x0000000d:
    case 0x0000000e:
    case 0x0000000f:
    case 0x00000012:
    case 0x00000013:
    case 0x00000014:
    case 0x00000015:
    case 0x00000016:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] SctScptAstNode makeScptNode(
    SctScptAstNodeKind kind,
    std::string display,
    std::uint32_t rawWord,
    std::string op = {})
{
    SctScptAstNode node{};
    node.kind = kind;
    node.display = std::move(display);
    node.op = std::move(op);
    node.rawWords.push_back(rawWord);
    return node;
}

[[nodiscard]] SctScptAstNode makeUnknownScptNode() {
    SctScptAstNode node{};
    node.kind = SctScptAstNodeKind::Unknown;
    node.display = "?";
    return node;
}

[[nodiscard]] const SctScptAstNode* stackAstValue(
    const std::array<std::optional<SctScptAstNode>, 20>& stack,
    std::int32_t index)
{
    if (index < 0 || index >= static_cast<std::int32_t>(stack.size()) || !stack[index].has_value()) {
        return nullptr;
    }
    return &*stack[index];
}

[[nodiscard]] SctScptAstNodeKind inputNodeKind(std::uint32_t action) {
    switch (action) {
    case 0x50000000:
        return SctScptAstNodeKind::IntVariable;
    case 0x40000000:
        return SctScptAstNodeKind::FloatVariable;
    case 0x20000000:
        return SctScptAstNodeKind::BitVariable;
    case 0x10000000:
        return SctScptAstNodeKind::ByteVariable;
    case 0x08000000:
        return SctScptAstNodeKind::DecimalLiteral;
    case 0x04000000:
        return SctScptAstNodeKind::FloatLiteral;
    default:
        return SctScptAstNodeKind::RawValue;
    }
}

void setScptStackNode(
    std::array<std::optional<SctScptAstNode>, 20>& stack,
    std::int32_t index,
    SctScptAstNode node)
{
    stack[std::clamp<std::int32_t>(index, 0, 19)] = std::move(node);
}

[[nodiscard]] std::uint32_t consumeScptParameterWords(
    std::span<const std::uint8_t> sectionBytes,
    std::uint32_t wordOffset,
    Endian endian,
    std::uint32_t instructionOffset,
    std::vector<SctDiagnostic>& diagnostics,
    SctInstruction::ScptParameterValueRecord* record) {

    if (wordOffset + 4u > sectionBytes.size()) {
        diagnostics.push_back({"SCPT parameter decode failed: out-of-bounds read.", instructionOffset});
        return 0;
    }

    const auto firstWord = readU32(sectionBytes, wordOffset, endian);
    if (isScptNoLoopValue(firstWord)) {
        if (record != nullptr) {
            record->resolvedValue = detail::toHexWord(firstWord);
            record->evaluationTrace.push_back({firstWord, record->resolvedValue});
            record->hitStopCode = firstWord == kScptStopCode;
            record->ast = makeScptNode(
                record->hitStopCode ? SctScptAstNodeKind::Stop : SctScptAstNodeKind::NoLoopValue,
                record->resolvedValue,
                firstWord);
        }
        return 1;
    }

    std::uint32_t consumedWords = 0;
    std::uint32_t cursor = wordOffset;
    std::array<std::string, 20> resultStack{};
    std::array<std::optional<SctScptAstNode>, 20> astStack{};

    // Mirror SALSA's _SCPT_analyze stack-driven loop semantics.
    // In Python this starts as `stack_index = 0`, `max_index = 18` and bails
    // if stack_index >= max_index before processing the current word.
    std::int32_t stackIndex = 0;
    constexpr std::int32_t kScptMaxIndex = 18;

    while (cursor + 4u <= sectionBytes.size()) {
        const auto currentWord = readU32(sectionBytes, cursor, endian);

        if (stackIndex >= kScptMaxIndex) {
            diagnostics.push_back({"SCPT parameter decode stopped at stack overflow threshold.", instructionOffset});
            break;
        }

        ++consumedWords;

        if (currentWord == kScptStopCode) {
            if (record != nullptr) {
                record->hitStopCode = true;
                if (!resultStack[2].empty()) {
                    record->resolvedValue = resultStack[2];
                    if (astStack[2].has_value()) {
                        record->ast = astStack[2];
                    }
                } else {
                    record->resolvedValue = "return values (0x1d)";
                    record->ast = makeScptNode(SctScptAstNodeKind::Stop, record->resolvedValue, currentWord);
                }
                record->evaluationTrace.push_back({currentWord, "return values (0x1d)"});
            }
            return consumedWords;
        }

        if (isScptCompareCode(currentWord)) {
            const auto lhs = detail::stackValue(resultStack, stackIndex);
            const auto rhs = detail::stackValue(resultStack, stackIndex + 1);
            const auto expr = "(" + lhs + " " + detail::compareSymbol(currentWord) + " " + rhs + ")";
            auto node = makeScptNode(
                currentWord == 0x0000000au ? SctScptAstNodeKind::AssignmentOp : SctScptAstNodeKind::CompareOp,
                expr,
                currentWord,
                detail::compareSymbol(currentWord));
            if (const auto* lhsNode = stackAstValue(astStack, stackIndex)) {
                node.children.push_back(*lhsNode);
            } else {
                node.children.push_back(makeUnknownScptNode());
            }
            if (const auto* rhsNode = stackAstValue(astStack, stackIndex + 1)) {
                node.children.push_back(*rhsNode);
            } else {
                node.children.push_back(makeUnknownScptNode());
            }
            std::int32_t nones = 0;
            if (lhs == "?") {
                ++nones;
            }
            if (rhs == "?") {
                ++nones;
            }
            resultStack[std::clamp<std::int32_t>(stackIndex + nones, 0, 19)] = expr;
            setScptStackNode(astStack, stackIndex + nones, std::move(node));
            if (record != nullptr) {
                record->evaluationTrace.push_back({currentWord, expr});
            }
            --stackIndex;
            if (currentWord == 0x0000000a) {
                ++stackIndex;
            }
            cursor += 4u;
            continue;
        }

        if (isScptArithmeticCode(currentWord)) {
            const auto lhs = detail::stackValue(resultStack, stackIndex);
            const auto rhs = detail::stackValue(resultStack, stackIndex + 1);
            const auto expr = "(" + lhs + " " + detail::arithmeticSymbol(currentWord) + " " + rhs + ")";
            auto node = makeScptNode(SctScptAstNodeKind::ArithmeticOp, expr, currentWord, detail::arithmeticSymbol(currentWord));
            if (const auto* lhsNode = stackAstValue(astStack, stackIndex)) {
                node.children.push_back(*lhsNode);
            } else {
                node.children.push_back(makeUnknownScptNode());
            }
            if (const auto* rhsNode = stackAstValue(astStack, stackIndex + 1)) {
                node.children.push_back(*rhsNode);
            } else {
                node.children.push_back(makeUnknownScptNode());
            }
            std::int32_t nones = 0;
            if (lhs == "?") {
                ++nones;
            }
            if (rhs == "?") {
                ++nones;
            }
            resultStack[std::clamp<std::int32_t>(stackIndex + nones, 0, 19)] = expr;
            setScptStackNode(astStack, stackIndex + nones, std::move(node));
            if (record != nullptr) {
                record->evaluationTrace.push_back({currentWord, expr});
            }
            --stackIndex;
            cursor += 4u;
            continue;
        }

        const auto action = scptInputActionPrefix(currentWord);
        if (action != 0x50000000u) {
            if (action == 0x04000000u) {
                if (cursor + 8u > sectionBytes.size()) {
                    diagnostics.push_back({"SCPT float literal payload exceeds section bounds.", instructionOffset});
                    return consumedWords;
                }
                const auto floatPayload = readU32(sectionBytes, cursor + 4u, endian);
                const auto floatValue = detail::floatFromWordBits(floatPayload);
                const auto value = std::string{detail::inputPrefix(action)} + std::to_string(floatValue);
                resultStack[std::clamp<std::int32_t>(stackIndex + 2, 0, 19)] = value;
                auto node = makeScptNode(SctScptAstNodeKind::FloatLiteral, value, currentWord);
                node.rawWords.push_back(floatPayload);
                setScptStackNode(astStack, stackIndex + 2, std::move(node));
                if (record != nullptr) {
                    record->evaluationTrace.push_back({currentWord, detail::toHexWord(currentWord)});
                    record->evaluationTrace.push_back({floatPayload, value});
                }
                ++consumedWords;
                cursor += 8u;
            } else {
                std::string value;
                if (action == 0x08000000u) {
                    const auto whole = (currentWord & 0x00ffff00u) >> 8u;
                    const auto frac = currentWord & 0x000000ffu;
                    value = std::string{detail::inputPrefix(action)} + std::to_string(whole) + "+" + std::to_string(frac) + "/256";
                } else {
                    value = std::string{detail::inputPrefix(action)} + std::to_string(currentWord & 0x00ffffffu);
                }
                resultStack[std::clamp<std::int32_t>(stackIndex + 2, 0, 19)] = value;
                setScptStackNode(astStack, stackIndex + 2, makeScptNode(inputNodeKind(action), value, currentWord));
                if (record != nullptr) {
                    record->evaluationTrace.push_back({currentWord, value});
                }
                cursor += 4u;
            }
        } else {
            const auto masked = currentWord & 0x00ffffffu;
            auto value = detail::secondaryLabel(masked);
            if (value.empty()) {
                value = std::string{detail::inputPrefix(action)} + std::to_string(masked);
                setScptStackNode(astStack, stackIndex + 2, makeScptNode(SctScptAstNodeKind::IntVariable, value, currentWord));
            } else {
                setScptStackNode(astStack, stackIndex + 2, makeScptNode(SctScptAstNodeKind::SecondaryValue, value, currentWord));
            }
            resultStack[std::clamp<std::int32_t>(stackIndex + 2, 0, 19)] = value;
            if (record != nullptr) {
                record->evaluationTrace.push_back({currentWord, value});
            }
            cursor += 4u;
        }
        ++stackIndex;
    }

    if (record != nullptr && record->resolvedValue.empty() && !resultStack[2].empty()) {
        record->resolvedValue = resultStack[2];
        if (astStack[2].has_value()) {
            record->ast = astStack[2];
        }
    }
    diagnostics.push_back({"SCPT parameter decode reached section end before stop code (0x1d).", instructionOffset});
    return consumedWords;
}

void populateRawWords(SctInstruction& inst, std::span<const std::uint8_t> sectionBytes, Endian endian) {
    inst.rawWords.clear();
    if (inst.sizeBytes == 0 || inst.offset >= sectionBytes.size()) {
        return;
    }

    const auto end = std::min<std::uint32_t>(static_cast<std::uint32_t>(sectionBytes.size()), inst.offset + inst.sizeBytes);
    for (std::uint32_t cursor = inst.offset; cursor + 4u <= end; cursor += 4u) {
        inst.rawWords.push_back(readU32(sectionBytes, cursor, endian));
    }
}

[[nodiscard]] std::optional<std::uint32_t> sectionIndexForPayloadOffset(
    const std::vector<SectionRow>& rows,
    std::uint32_t payloadOffset) {
    for (std::uint32_t i = 0; i < rows.size(); ++i) {
        if (payloadOffset >= rows[i].start && payloadOffset < rows[i].end) {
            return i;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t> containingInstructionStart(
    const std::map<std::uint32_t, GlobalDecodedInstruction>& instructions,
    std::uint32_t offset) {
    const auto upper = instructions.upper_bound(offset);
    if (upper == instructions.begin()) {
        return std::nullopt;
    }
    const auto it = std::prev(upper);
    const auto start = it->first;
    const auto size = it->second.decoded.inst.sizeBytes;
    if (offset > start && offset < start + size) {
        return start;
    }
    return std::nullopt;
}

[[nodiscard]] std::string decimalString(std::uint32_t value);

[[nodiscard]] SctEdgeType edgeTypeForSuccessor(
    const SctInstruction& instruction,
    std::size_t successorIndex,
    std::uint32_t successorOffset);

[[nodiscard]] SctEdge makeControlFlowEdge(
    const SctInstruction& instruction,
    std::size_t successorIndex,
    std::uint32_t successorPayloadOffset,
    const std::vector<SectionRow>& rows) {
    SctEdge edge{};
    edge.type = edgeTypeForSuccessor(instruction, successorIndex, successorPayloadOffset);
    edge.confidence = SctSemanticConfidence::Known;
    edge.fromOffset = instruction.offset;
    edge.fromPayloadOffset = instruction.payloadOffset;
    edge.toPayloadOffset = successorPayloadOffset;
    edge.opcode = instruction.opcode;
    edge.detail = "Control-flow successor discovered during SCT parse.";
    edge.attributes.emplace("target_payload_offset", decimalString(successorPayloadOffset));
    if (const auto targetSection = sectionIndexForPayloadOffset(rows, successorPayloadOffset); targetSection.has_value()) {
        edge.toOffset = successorPayloadOffset - rows[*targetSection].start;
        edge.attributes.emplace("target_section", rows[*targetSection].name);
        edge.attributes.emplace("target_section_index", decimalString(*targetSection));
        edge.attributes.emplace("target_offset", decimalString(successorPayloadOffset - rows[*targetSection].start));
    } else {
        edge.toOffset = successorPayloadOffset;
        edge.attributes.emplace("target_offset", decimalString(successorPayloadOffset));
    }
    return edge;
}

void addInstructionSemanticEdges(
    std::vector<SctEdge>& edges,
    const SctInstruction& instruction,
    std::uint32_t sourceSectionStart) {
    const auto metadata = sctOpcodeMetadata(instruction.opcode);
    if (metadata.controlRole == SctOpcodeControlRole::CallSubscript) {
        SctEdge edge{};
        edge.type = SctEdgeType::CallSubscript;
        edge.confidence = metadata.confidence;
        edge.fromOffset = instruction.payloadOffset - sourceSectionStart;
        edge.fromPayloadOffset = instruction.payloadOffset;
        edge.opcode = instruction.opcode;
        edge.detail = "Opcode calls a subscript target.";
        if (!instruction.operands.empty()) {
            edge.toOffset = instruction.operands.front();
            edge.toPayloadOffset = instruction.operands.front();
            edge.attributes.emplace("offset_operand", decimalString(instruction.operands.front()));
        }
        edges.push_back(std::move(edge));
    }
    if (metadata.controlRole == SctOpcodeControlRole::Return) {
        SctEdge edge{};
        edge.type = SctEdgeType::Return;
        edge.confidence = metadata.confidence;
        edge.fromOffset = instruction.payloadOffset - sourceSectionStart;
        edge.fromPayloadOffset = instruction.payloadOffset;
        edge.opcode = instruction.opcode;
        edge.detail = "Opcode returns from the current subscript stack.";
        edges.push_back(std::move(edge));
    }
    if (metadata.resourceRole == SctOpcodeResourceRole::LoadsMld || metadata.resourceRole == SctOpcodeResourceRole::LoadsScript) {
        SctEdge edge{};
        edge.type = metadata.resourceRole == SctOpcodeResourceRole::LoadsMld ? SctEdgeType::LoadsMld : SctEdgeType::LoadsScript;
        edge.confidence = metadata.confidence;
        edge.fromOffset = instruction.payloadOffset - sourceSectionStart;
        edge.fromPayloadOffset = instruction.payloadOffset;
        edge.opcode = instruction.opcode;
        edge.detail = "Opcode references an external SCT/MLD resource.";
        if (!instruction.operands.empty()) {
            edge.attributes.emplace("resource_id", decimalString(instruction.operands.front()));
        }
        edges.push_back(std::move(edge));
    }
}

[[nodiscard]] std::string decimalString(std::uint32_t value) {
    return std::to_string(value);
}

[[nodiscard]] bool externalLoopBreakApplies(std::uint16_t opcode, std::uint32_t iterations) {
    return (opcode == 118u || opcode == 119u) && iterations == 0x00010000u;
}

[[nodiscard]] bool parameterMatchesLoopBreakValue(const SctParameter& parameter, std::uint32_t breakValue) {
    if (parameter.rawWords.size() == 1u && parameter.rawWords.front() == breakValue) {
        return true;
    }
    if (breakValue == 0u) {
        if (parameter.rawWords.size() >= 3u && parameter.rawWords[0] == 0x04000000u && parameter.rawWords[1] == 0u
            && parameter.rawWords.back() == kScptStopCode) {
            return true;
        }
        return parameter.displayValue == "0" || parameter.displayValue == "float: 0.000000";
    }
    return false;
}

[[nodiscard]] SctEdgeType edgeTypeForSuccessor(
    const SctInstruction& instruction,
    std::size_t successorIndex,
    std::uint32_t successorOffset) {
    switch (instruction.opcode) {
    case 0:
        return successorIndex == 0 ? SctEdgeType::BranchFalse : SctEdgeType::BranchTrue;
    case 3:
        return SctEdgeType::SwitchCase;
    case 10:
        return SctEdgeType::Jump;
    default:
        return successorOffset == instruction.offset + instruction.sizeBytes ? SctEdgeType::Fallthrough : SctEdgeType::Jump;
    }
}

void addInstructionSemanticEdges(SctSection& section, const SctInstruction& instruction) {
    const auto metadata = sctOpcodeMetadata(instruction.opcode);
    const auto operand = instruction.operands.empty() ? 0u : instruction.operands.front();

    if (metadata.controlRole == SctOpcodeControlRole::CallSubscript) {
        SctEdge edge{};
        edge.type = SctEdgeType::CallSubscript;
        edge.confidence = metadata.confidence;
        edge.fromOffset = instruction.offset;
        edge.toOffset = operand;
        edge.opcode = instruction.opcode;
        edge.detail = "Opcode calls a subscript target.";
        edge.attributes.emplace("offset_operand", decimalString(operand));
        section.edges.push_back(std::move(edge));
    }

    if (metadata.controlRole == SctOpcodeControlRole::Return) {
        SctEdge edge{};
        edge.type = SctEdgeType::Return;
        edge.confidence = metadata.confidence;
        edge.fromOffset = instruction.offset;
        edge.opcode = instruction.opcode;
        edge.detail = "Opcode returns from the current subscript stack.";
        section.edges.push_back(std::move(edge));
    }

    if (metadata.resourceRole == SctOpcodeResourceRole::LoadsMld
        || metadata.resourceRole == SctOpcodeResourceRole::LoadsScript) {
        SctEdge edge{};
        edge.type = metadata.resourceRole == SctOpcodeResourceRole::LoadsMld ? SctEdgeType::LoadsMld : SctEdgeType::LoadsScript;
        edge.confidence = metadata.confidence;
        edge.fromOffset = instruction.offset;
        edge.opcode = instruction.opcode;
        edge.detail = "Opcode references an external resource.";
        edge.attributes.emplace("operand0", decimalString(operand));
        section.edges.push_back(std::move(edge));
    }
}

[[nodiscard]] DecodedInstruction decodeInstruction(
    std::span<const std::uint8_t> sectionBytes,
    std::uint32_t offset,
    Endian baseEndian,
    std::vector<SctDiagnostic>& diagnostics) {

    DecodedInstruction decoded{};
    decoded.inst.offset = offset;

    if (offset + 4u > sectionBytes.size()) {
        decoded.inst.decodeOk = false;
        decoded.inst.sizeBytes = 0;
        diagnostics.push_back({"Instruction decode failed: out-of-bounds read.", offset});
        decoded.blockTerminator = true;
        return decoded;
    }

    const auto wordBase = readU32(sectionBytes, offset, baseEndian);
    const auto otherEndian = baseEndian == Endian::Big ? Endian::Little : Endian::Big;
    const auto wordOther = readU32(sectionBytes, offset, otherEndian);

    const auto baseOpcode = static_cast<std::uint16_t>(wordBase & 0xffffu);
    const auto otherOpcode = static_cast<std::uint16_t>(wordOther & 0xffffu);

    Endian chosenEndian = baseEndian;
    std::uint32_t word = wordBase;

    if (baseOpcode > kMaxOpcodeProbe && otherOpcode <= kMaxOpcodeProbe) {
        chosenEndian = otherEndian;
        word = wordOther;
    }

    std::uint32_t actualOpcodeOffset = offset;
    std::vector<std::uint32_t> prefixWords{};

    auto currentWord = word;
    auto currentOpcode = static_cast<std::uint16_t>(currentWord & 0xffffu);
    if (currentOpcode == 13u && actualOpcodeOffset + 8u <= sectionBytes.size()) {
        decoded.inst.skipRefresh = true;
        prefixWords.push_back(currentWord);
        actualOpcodeOffset += 4u;
        currentWord = readU32(sectionBytes, actualOpcodeOffset, chosenEndian);
        currentOpcode = static_cast<std::uint16_t>(currentWord & 0xffffu);
    }

    if (currentOpcode == 129u && actualOpcodeOffset + 12u <= sectionBytes.size()) {
        SctInstruction::ScptParameterValueRecord delayRecord{};
        delayRecord.parameterIndex = 0;
        delayRecord.operandStartWordIndex = 0;
        const auto delayParamOffset = actualOpcodeOffset + 4u;
        const auto delayWordCount = consumeScptParameterWords(
            sectionBytes, delayParamOffset, chosenEndian, offset, diagnostics, &delayRecord);
        const auto lengthOffset = delayParamOffset + (delayWordCount * 4u);
        if (delayWordCount != 0u && lengthOffset + 8u <= sectionBytes.size()) {
            decoded.inst.scheduled.present = true;
            decoded.inst.scheduled.rawWords.push_back(currentWord);

            SctParameter delayParameter{};
            delayParameter.index = 0;
            delayParameter.role = "delayFrames";
            delayParameter.valueKind = SctParameterValueKind::Expression;
            delayParameter.confidence = SctSemanticConfidence::Known;
            for (std::uint32_t i = 0; i < delayWordCount; ++i) {
                const auto rawWord = readU32(sectionBytes, delayParamOffset + (i * 4u), chosenEndian);
                delayParameter.rawWords.push_back(rawWord);
                decoded.inst.scheduled.rawWords.push_back(rawWord);
            }
            delayParameter.displayValue = delayRecord.resolvedValue;
            SctExpression expression{};
            expression.display = delayRecord.resolvedValue;
            expression.hitStopCode = delayRecord.hitStopCode;
            for (const auto& trace : delayRecord.evaluationTrace) {
                expression.trace.push_back({trace.rawWord, trace.interpretedValue});
            }
            expression.ast = delayRecord.ast;
            delayParameter.expression = std::move(expression);
            decoded.inst.scheduled.frameDelay = std::move(delayParameter);

            decoded.inst.scheduled.instructionByteLength = readU32(sectionBytes, lengthOffset, chosenEndian);
            decoded.inst.scheduled.rawWords.push_back(decoded.inst.scheduled.instructionByteLength);
            prefixWords.insert(
                prefixWords.end(),
                decoded.inst.scheduled.rawWords.begin(),
                decoded.inst.scheduled.rawWords.end());

            actualOpcodeOffset = lengthOffset + 4u;
            currentWord = readU32(sectionBytes, actualOpcodeOffset, chosenEndian);
            currentOpcode = static_cast<std::uint16_t>(currentWord & 0xffffu);
        }
    }

    decoded.inst.opcodeWordIndex = static_cast<std::uint32_t>(prefixWords.size());
    const auto opcode = currentOpcode;
    decoded.inst.opcode = opcode;
    const auto opcodeMetadata = sctOpcodeMetadata(opcode);
    decoded.inst.mnemonic = opcodeMetadata.mnemonic.empty() ? fallbackMnemonic(opcode) : std::string(opcodeMetadata.mnemonic);
    decoded.inst.semanticConfidence = opcodeMetadata.confidence;
    decoded.inst.decodeOk = opcode <= kMaxOpcodeProbe;
    decoded.inst.sizeBytes = (actualOpcodeOffset - offset) + 4u;

    if (opcode < kSalsaOpcodeParamPatterns.size()) {
        const auto& paramPattern = kSalsaOpcodeParamPatterns[opcode];
        std::uint32_t totalParamSlots = paramPattern.paramCount;
        std::uint32_t consumedOperandWords = 0;
        std::uint32_t iterations = 0;
        bool loopBreakReached = false;

        auto consumeParamSlot = [&](std::uint32_t paramIndex) -> bool {
            std::uint32_t baseParamIndex = paramIndex;
            if (paramPattern.loopStartParam >= 0 && paramPattern.loopEndParam >= paramPattern.loopStartParam
                && paramIndex >= paramPattern.paramCount) {
                const auto loopStart = static_cast<std::uint32_t>(paramPattern.loopStartParam);
                const auto loopWidth = static_cast<std::uint32_t>(paramPattern.loopEndParam - paramPattern.loopStartParam + 1);
                baseParamIndex = loopStart + ((paramIndex - paramPattern.paramCount) % loopWidth);
            }

            const auto paramWordOffset = actualOpcodeOffset + 4u + (consumedOperandWords * 4u);
            if (paramWordOffset + 4u > sectionBytes.size()) {
                diagnostics.push_back({"Instruction payload exceeds section bounds.", offset});
                return false;
            }

            const bool isScptParam = baseParamIndex < 64u && ((paramPattern.scptAnalyzeMask >> baseParamIndex) & 1ull) != 0ull;
            std::uint32_t wordsForParam = 1;
            SctInstruction::ScptParameterValueRecord scptRecord{};
            if (isScptParam) {
                scptRecord.parameterIndex = static_cast<std::uint8_t>(paramIndex);
                scptRecord.operandStartWordIndex = consumedOperandWords;
                wordsForParam = consumeScptParameterWords(
                    sectionBytes, paramWordOffset, chosenEndian, offset, diagnostics, &scptRecord);
                if (wordsForParam == 0) {
                    return false;
                }
                decoded.inst.scptAnalyzeOperandIndexes.push_back(static_cast<std::uint8_t>(paramIndex));
                scptRecord.operandWordCount = wordsForParam;
            }

            if (paramPattern.iterationCountParam >= 0
                && baseParamIndex == static_cast<std::uint32_t>(paramPattern.iterationCountParam)) {
                iterations = readU32(sectionBytes, paramWordOffset, chosenEndian);
            }

            SctParameter parameter{};
            parameter.index = paramIndex;
            if (baseParamIndex < opcodeMetadata.parameterRoles.size()) {
                parameter.role = std::string(opcodeMetadata.parameterRoles[baseParamIndex]);
            }
            parameter.confidence = opcodeMetadata.confidence;
            parameter.valueKind = isScptParam ? SctParameterValueKind::Expression : SctParameterValueKind::Integer;

            if (!isScptParam && (parameter.role.find("offset") != std::string::npos
                || parameter.role.find("Offset") != std::string::npos)) {
                parameter.valueKind = SctParameterValueKind::Link;
            }
            if (!isScptParam && (opcodeMetadata.resourceRole == SctOpcodeResourceRole::LoadsMld
                || opcodeMetadata.resourceRole == SctOpcodeResourceRole::LoadsScript)) {
                parameter.valueKind = SctParameterValueKind::ResourceRef;
            }

            for (std::uint32_t i = 0; i < wordsForParam; ++i) {
                const auto operandOffset = paramWordOffset + (i * 4u);
                if (operandOffset + 4u > sectionBytes.size()) {
                    diagnostics.push_back({"Instruction payload exceeds section bounds.", offset});
                    return false;
                }
                const auto operand = readU32(sectionBytes, operandOffset, chosenEndian);
                decoded.inst.operands.push_back(operand);
                parameter.rawWords.push_back(operand);
            }

            consumedOperandWords += wordsForParam;
            if (isScptParam) {
                parameter.displayValue = scptRecord.resolvedValue;
                SctExpression expression{};
                expression.display = scptRecord.resolvedValue;
                expression.hitStopCode = scptRecord.hitStopCode;
                for (const auto& trace : scptRecord.evaluationTrace) {
                    expression.trace.push_back({trace.rawWord, trace.interpretedValue});
                }
                expression.ast = scptRecord.ast;
                parameter.expression = std::move(expression);
                decoded.inst.scptParameterValueRecords.push_back(std::move(scptRecord));
            } else if (!parameter.rawWords.empty()) {
                parameter.displayValue = std::to_string(parameter.rawWords.front());
            }
            if (paramPattern.internalLoopBreakParam >= 0
                && baseParamIndex == static_cast<std::uint32_t>(paramPattern.internalLoopBreakParam)
                && parameterMatchesLoopBreakValue(parameter, paramPattern.internalLoopBreakValue)) {
                loopBreakReached = true;
            }
            decoded.inst.parameters.push_back(std::move(parameter));
            return true;
        };

        for (std::uint32_t paramIndex = 0; paramIndex < paramPattern.paramCount; ++paramIndex) {
            if (!consumeParamSlot(paramIndex)) {
                decoded.blockTerminator = true;
                return decoded;
            }
        }

        if (paramPattern.loopStartParam >= 0 && paramPattern.loopEndParam >= paramPattern.loopStartParam
            && paramPattern.iterationCountParam >= 0 && iterations > 0u && !loopBreakReached
            && !externalLoopBreakApplies(opcode, iterations)) {
            const auto loopWidth = static_cast<std::uint32_t>(paramPattern.loopEndParam - paramPattern.loopStartParam + 1);
            const auto decodedLoopIterations = static_cast<std::uint32_t>(paramPattern.loopStartParam) < paramPattern.paramCount ? 1u : 0u;
            if (iterations > decodedLoopIterations) {
                totalParamSlots += (iterations - decodedLoopIterations) * loopWidth;
            }
            for (std::uint32_t paramIndex = paramPattern.paramCount; paramIndex < totalParamSlots; ++paramIndex) {
                if (!consumeParamSlot(paramIndex)) {
                    decoded.blockTerminator = true;
                    return decoded;
                }
                if (loopBreakReached) {
                    break;
                }
            }
        }

        decoded.inst.sizeBytes = (actualOpcodeOffset - offset) + 4u + (consumedOperandWords * 4u);
        populateRawWords(decoded.inst, sectionBytes, chosenEndian);

        if (opcode == 0 || opcode == 10 || opcode == 3) {
            decoded.blockTerminator = true;

            if (opcode == 3) {
                decoded.isSwitch = true;
                if (paramPattern.switchJumpParam >= 0 && paramPattern.loopStartParam >= 0
                    && paramPattern.loopEndParam >= paramPattern.loopStartParam) {
                    const auto loopWidth = static_cast<std::size_t>(paramPattern.loopEndParam - paramPattern.loopStartParam + 1);
                    std::uint32_t operandWordOffset = 0;
                    for (const auto& parameter : decoded.inst.parameters) {
                        const auto parameterIndex = static_cast<std::size_t>(parameter.index);
                        const bool isSwitchJump = parameterIndex >= static_cast<std::size_t>(paramPattern.switchJumpParam)
                            && ((parameterIndex - static_cast<std::size_t>(paramPattern.switchJumpParam)) % loopWidth) == 0u;
                        if (isSwitchJump && !parameter.rawWords.empty()) {
                            const auto jumpWordOffset = actualOpcodeOffset + 4u + (operandWordOffset * 4u);
                            const auto rel = static_cast<std::int32_t>(parameter.rawWords.front());
                            const auto jumpTarget = static_cast<std::int64_t>(jumpWordOffset) + rel;
                            if (jumpTarget >= 0) {
                                decoded.successors.push_back(static_cast<std::uint32_t>(jumpTarget));
                            }
                        }
                        operandWordOffset += static_cast<std::uint32_t>(parameter.rawWords.size());
                    }
                }
            } else  {
                const auto rel = static_cast<std::int32_t>(decoded.inst.operands.back());
                const auto jumpTarget = static_cast<std::int64_t>(offset + decoded.inst.sizeBytes) + rel + -4;
                if (jumpTarget >= 0) {
                    decoded.successors.push_back(static_cast<std::uint32_t>(jumpTarget));
                }
                if (opcode == 0) {
                    decoded.successors.push_back(offset + decoded.inst.sizeBytes);
                }
            }
        }
    } else {
        populateRawWords(decoded.inst, sectionBytes, chosenEndian);
    }

    if (opcode == 12) {
        decoded.blockTerminator = true;
    }

    // Placeholder flag heuristics for early indexing.
    if (opcode == 52 || opcode == 53 || opcode == 54) {
        decoded.touchesFlag = true;
        decoded.testedFlag = true;
        if (!decoded.inst.operands.empty()) {
            decoded.inst.operands.push_back(decoded.inst.operands.front());
        }
    }

    if (opcode == 55 || opcode == 56) {
        decoded.touchesFlag = true;
        decoded.writesFlag = true;
    }

    return decoded;
}

void fillUnknownRegions(
    std::unordered_map<std::uint32_t, std::uint32_t> visitedRegions, std::span<const std::uint8_t> sectionBytes, SctSection& section) 
{
    std::uint32_t cursor = 0;
    while (cursor < sectionBytes.size()) {
        const bool visited = visitedRegions.contains(cursor);
        if (visited) {
            cursor += visitedRegions[cursor];
            continue;
        }

        const auto start = cursor;
        while (cursor < sectionBytes.size() && !visitedRegions.contains(cursor)) {
            cursor += 4;
        }

        SctUnknownRegion region{};
        region.startOffset = start;
        region.endOffset = cursor;
        region.reason = "Not reached by control-flow-guided pass.";
        region.rawBytes.insert(
            region.rawBytes.end(),
            sectionBytes.begin() + start,
            sectionBytes.begin() + std::min<std::size_t>(cursor, sectionBytes.size()));

        SctRawSpan span{};
        span.startOffset = region.startOffset;
        span.endOffset = region.endOffset;
        span.reason = SctRawSpanReason::Unreached;
        span.rawBytes = region.rawBytes;
        span.detail = region.reason;
        section.rawSpans.push_back(std::move(span));

        section.unknownRegions.push_back(std::move(region));
    }
}

} // namespace

SctParseResult SctParser::parseFile(const std::string& sourcePath) const {
    SctParseResult result{};
    result.file.sourcePath = sourcePath;

    if (sourcePath.empty()) {
        result.diagnostics.push_back({"SCT parse skipped: source path is empty.", 0});
        return result;
    }

    std::ifstream in(sourcePath, std::ios::binary);
    if (!in) {
        result.diagnostics.push_back({"Unable to open SCT file on disk.", 0});
        return result;
    }

    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    in.seekg(0, std::ios::beg);

    if (size <= 0) {
        result.diagnostics.push_back({"SCT parse skipped: file is empty.", 0});
        return result;
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

    return parse(bytes, sourcePath);
}

SctParseResult SctParser::parse(std::span<const std::uint8_t> bytes, std::string sourcePath) const {
    std::cout << "[SpiceSCT] Step 1/5: Starting parse (" << bytes.size() << " bytes).\n";
    SctParseResult result{};
    result.file.sourcePath = std::move(sourcePath);
    result.file.originalBytes.assign(bytes.begin(), bytes.end());

    std::vector<std::uint8_t> decoded;
    std::span<const std::uint8_t> payload = bytes;
    if (spice::compression::aklz::isAklz(bytes)) {
        result.file.originalCompressedAklz = true;
        std::cout << "[SpiceSCT] Step 2/5: Input is AKLZ-compressed, decompressing...\n";
        auto decodedResult = spice::compression::aklz::decompress(bytes);
        if (!decodedResult.ok()) {
            result.diagnostics.push_back({
                "AKLZ decompression failed: " + std::string(spice::compression::aklz::errorToString(decodedResult.error)),
                0
            });
            return result;
        }

        decoded = std::move(decodedResult.bytes);
        payload = std::span<const std::uint8_t>(decoded.data(), decoded.size());
    }
    else {
        std::cout << "[SpiceSCT] Step 2/5: Input is not AKLZ-compressed.\n";
    }

    if (payload.empty()) {
        result.diagnostics.push_back({"SCT parse skipped: input byte buffer is empty.", 0});
        return result;
    }
    result.file.originalPayloadBytes.assign(payload.begin(), payload.end());

    if (payload.size() < kHeaderSize) {
        result.diagnostics.push_back({"SCT parse failed: file too small for header and index count.", 0});
        return result;
    }

    const auto indexEndian = detectIndexEndian(payload);
    const auto sectionCount = readU32(payload, 8, indexEndian);
    const std::size_t indexSize = static_cast<std::size_t>(sectionCount) * kIndexEntrySize;
    result.file.detectedEndian = indexEndian == Endian::Big ? "big" : "little";
    result.file.headerBytes.assign(payload.begin(), payload.begin() + kHeaderSize);

    if (kHeaderSize + indexSize > payload.size()) {
        result.diagnostics.push_back({"SCT parse failed: index table exceeds file bounds.", 8});
        return result;
    }

    std::vector<SectionRow> rows;
    rows.reserve(sectionCount);
    std::cout << "[SpiceSCT] Step 3/5: Reading section index (" << sectionCount << " sections)...\n";

    for (std::uint32_t i = 0; i < sectionCount; ++i) {
        const auto rowOffset = kHeaderSize + (static_cast<std::size_t>(i) * kIndexEntrySize);
        const auto start = readU32(payload, rowOffset, indexEndian);
        auto name = readIndexName(payload, rowOffset + kIndexNameOffset);
        if (name.empty()) {
            name = "section_" + std::to_string(i);
        }
        SectionRow row{};
        row.start = start;
        row.name = std::move(name);
        rows.push_back(std::move(row));
    }

    const auto dataStart = static_cast<std::uint32_t>(kHeaderSize + indexSize);
    const auto dataSize = static_cast<std::uint32_t>(payload.size() - dataStart);
    const auto dataBytes = payload.subspan(dataStart);

    for (std::uint32_t i = 0; i < rows.size(); ++i) {
        rows[i].end = (i + 1u < rows.size()) ? rows[i + 1u].start : dataSize;
        rows[i].isValid = rows[i].start <= dataSize && rows[i].end <= dataSize && rows[i].end >= rows[i].start;
        SctSection section{};
        section.id.index = i;
        section.id.name = rows[i].name;
        section.startOffset = dataStart + std::min(rows[i].start, dataSize);
        section.endOffset = dataStart + std::min(rows[i].end, dataSize);
        if (!rows[i].isValid) {
            section.kind = SctSectionKind::Unknown;
            result.file.sections.push_back(std::move(section));
            result.diagnostics.push_back({"Invalid section bounds in SCT index.", rows[i].start, rows[i].name});
            continue;
        }

        const auto sectionBytes = dataBytes.subspan(rows[i].start, rows[i].end - rows[i].start);
        if (sectionBytes.empty()) {
            section.kind = SctSectionKind::Unknown;
            result.file.sections.push_back(std::move(section));
            continue;
        }

        section.kind = SctSectionKind::Script;
        if (isStringSection(sectionBytes, indexEndian)) {
            rows[i].isString = true;
            section.kind = SctSectionKind::String;
            section.isStringSection = true;
            section.heuristicEvidence.notes.push_back(
                "Detected string section from SALSA-style pattern (9 ... 0x1d followed by non-opcode payload); skipped decode.");
            SctRawSpan span{};
            span.startOffset = 0;
            span.endOffset = static_cast<std::uint32_t>(sectionBytes.size());
            span.reason = SctRawSpanReason::StringPadding;
            span.detail = "Raw string-section bytes preserved by SCT IR.";
            span.rawBytes.insert(span.rawBytes.end(), sectionBytes.begin(), sectionBytes.end());
            section.rawSpans.push_back(std::move(span));
            result.file.sections.push_back(std::move(section));
            continue;
        }

        const auto firstWord = readU32(dataBytes, rows[i].start, indexEndian);
        if (firstWord != 9u && firstWord > kMaxOpcodeProbe) {
            section.kind = SctSectionKind::Unknown;
            SctRawSpan span{};
            span.startOffset = 0;
            span.endOffset = static_cast<std::uint32_t>(sectionBytes.size());
            span.reason = SctRawSpanReason::Unknown;
            span.detail = "Physical index row does not start with a plausible SCT opcode; raw bytes preserved.";
            span.rawBytes.insert(span.rawBytes.end(), sectionBytes.begin(), sectionBytes.end());
            section.rawSpans.push_back(std::move(span));
            result.file.sections.push_back(std::move(section));
            continue;
        }

        rows[i].isCode = true;
        if (firstWord != 9u) {
            result.diagnostics.push_back({
                "Script index entry does not start with label opcode 9.",
                rows[i].start,
                rows[i].name
            });
        }
        result.file.sections.push_back(std::move(section));
    }

    std::cout << "[SpiceSCT] Step 4/5: Walking global script instructions...\n";

    struct WorkItem {
        std::uint32_t payloadOffset = 0;
        std::uint32_t entryPayloadOffset = 0;
    };

    std::deque<WorkItem> worklist;
    std::set<std::uint32_t> enqueued;
    for (const auto& row : rows) {
        if (!row.isValid || !row.isCode || row.isString || row.start >= row.end || !isOffsetInBounds(row.start, dataSize)) {
            continue;
        }
        worklist.push_back({row.start, row.start});
        enqueued.insert(row.start);
    }

    std::map<std::uint32_t, GlobalDecodedInstruction> globalInstructions;

    while (!worklist.empty()) {
        const auto item = worklist.front();
        worklist.pop_front();

        if (!isOffsetInBounds(item.payloadOffset, dataSize)) {
            continue;
        }

        std::uint32_t cursor = item.payloadOffset;
        while (isOffsetInBounds(cursor, dataSize)) {
            if (globalInstructions.contains(cursor)) {
                break;
            }
            if (const auto containing = containingInstructionStart(globalInstructions, cursor); containing.has_value()) {
                result.diagnostics.push_back({
                    "Control-flow target lands inside an already decoded instruction; overlapping decode skipped.",
                    cursor,
                    {}
                });
                break;
            }

            std::vector<SctDiagnostic> inst_diagnostics{};
            auto decoded = decodeInstruction(dataBytes, cursor, indexEndian, inst_diagnostics);
            if (decoded.inst.sizeBytes == 0) {
                break;
            }
            decoded.inst.payloadOffset = cursor;

            const auto sourceSection = sectionIndexForPayloadOffset(rows, cursor);
            for (auto& diag : inst_diagnostics) {
                if (sourceSection.has_value()) {
                    diag.section = rows[*sourceSection].name;
                }
            }

            result.diagnostics.insert(result.diagnostics.end(), inst_diagnostics.begin(), inst_diagnostics.end());

            GlobalDecodedInstruction global{};
            global.decoded = std::move(decoded);
            global.entryPayloadOffset = item.entryPayloadOffset;
            const auto [insertedIt, inserted] = globalInstructions.emplace(cursor, std::move(global));
            const auto& storedDecoded = insertedIt->second.decoded;

            bool hasControlSuccessor = !storedDecoded.successors.empty();
            for (const auto successor : storedDecoded.successors) {
                if (!isOffsetInBounds(successor, dataSize)) {
                    result.diagnostics.push_back({
                        "Control-flow target is outside the SCT payload; target left unresolved.",
                        successor,
                        sourceSection.has_value() ? rows[*sourceSection].name : std::string{}
                    });
                    continue;
                }
                if (const auto containing = containingInstructionStart(globalInstructions, successor); containing.has_value()) {
                    result.diagnostics.push_back({
                        "Control-flow target lands inside an already decoded instruction; overlapping decode skipped.",
                        successor,
                        sourceSection.has_value() ? rows[*sourceSection].name : std::string{}
                    });
                    continue;
                }
                if (!enqueued.contains(successor)) {
                    worklist.push_back({successor, item.entryPayloadOffset});
                    enqueued.insert(successor);
                }
            }

            if (hasControlSuccessor || storedDecoded.blockTerminator) {
                break;
            }

            const auto nextCursor = cursor + storedDecoded.inst.sizeBytes;
            if (!isOffsetInBounds(nextCursor, dataSize)) {
                break;
            }
            if (const auto containing = containingInstructionStart(globalInstructions, nextCursor); containing.has_value()) {
                result.diagnostics.push_back({
                    "Fallthrough lands inside an already decoded instruction; overlapping decode skipped.",
                    nextCursor,
                    sourceSection.has_value() ? rows[*sourceSection].name : std::string{}
                });
                break;
            }
            cursor = nextCursor;
        }
    }

    for (const auto& [payloadOffset, global] : globalInstructions) {
        const auto sectionIndex = sectionIndexForPayloadOffset(rows, payloadOffset);
        if (!sectionIndex.has_value() || *sectionIndex >= result.file.sections.size()) {
            continue;
        }
        auto& section = result.file.sections[*sectionIndex];
        auto instruction = global.decoded.inst;
        instruction.payloadOffset = payloadOffset;
        instruction.offset = payloadOffset - rows[*sectionIndex].start;
        section.instructions.push_back(std::move(instruction));

        if (global.decoded.touchesFlag) {
            section.heuristicEvidence.touchesFlags = true;
        }
        if (global.decoded.testedFlag) {
            section.heuristicEvidence.branchesOnFlags = true;
        }
        if (global.decoded.writesFlag) {
            section.heuristicEvidence.writesFlags = true;
        }
        if (global.decoded.isSwitch) {
            section.heuristicEvidence.hasSwitch = true;
        }
    }

    for (const auto& [payloadOffset, global] : globalInstructions) {
        const auto sectionIndex = sectionIndexForPayloadOffset(rows, payloadOffset);
        if (!sectionIndex.has_value() || *sectionIndex >= result.file.sections.size()) {
            continue;
        }
        auto& section = result.file.sections[*sectionIndex];
        auto localInstruction = global.decoded.inst;
        localInstruction.payloadOffset = payloadOffset;
        localInstruction.offset = payloadOffset - rows[*sectionIndex].start;

        std::size_t successorIndex = 0;
        for (const auto successor : global.decoded.successors) {
            auto edge = makeControlFlowEdge(localInstruction, successorIndex++, successor, rows);
            section.edges.push_back(std::move(edge));
        }
        addInstructionSemanticEdges(section.edges, localInstruction, rows[*sectionIndex].start);
    }

    for (std::uint32_t sectionIndex = 0; sectionIndex < result.file.sections.size(); ++sectionIndex) {
        auto& section = result.file.sections[sectionIndex];
        if (section.kind != SctSectionKind::Script || section.instructions.empty()) {
            continue;
        }
        std::sort(section.instructions.begin(), section.instructions.end(), [](const auto& a, const auto& b) {
            return a.offset < b.offset;
        });

        SctBasicBlock block{};
        block.startOffset = section.instructions.front().offset;
        block.endOffset = section.instructions.back().offset + section.instructions.back().sizeBytes;
        for (const auto& instruction : section.instructions) {
            block.instructionOffsets.push_back(instruction.offset);
        }
        for (const auto& edge : section.edges) {
            if (edge.toOffset.has_value()
                && edge.type != SctEdgeType::CallSubscript
                && edge.type != SctEdgeType::LoadsMld
                && edge.type != SctEdgeType::LoadsScript
                && edge.type != SctEdgeType::Return) {
                block.successorOffsets.push_back(*edge.toOffset);
            }
        }
        section.blocks.push_back(std::move(block));

        std::unordered_map<std::uint32_t, std::uint32_t> localVisited{};
        for (const auto& [payloadOffset, global] : globalInstructions) {
            const auto instStart = payloadOffset;
            const auto instEnd = payloadOffset + global.decoded.inst.sizeBytes;
            const auto rowStart = rows[sectionIndex].start;
            const auto rowEnd = rows[sectionIndex].end;
            if (instEnd <= rowStart || instStart >= rowEnd) {
                continue;
            }
            const auto localStart = std::max(instStart, rowStart) - rowStart;
            const auto localEnd = std::min(instEnd, rowEnd) - rowStart;
            if (localEnd > localStart) {
                localVisited.emplace(localStart, localEnd - localStart);
            }
        }
        const auto sectionBytes = dataBytes.subspan(rows[sectionIndex].start, rows[sectionIndex].end - rows[sectionIndex].start);
        fillUnknownRegions(localVisited, sectionBytes, section);

        if (section.instructions.size() >= 16) {
            section.heuristicEvidence.hasLongLinearSequence = true;
        }

        if (section.heuristicEvidence.hasSwitch || section.heuristicEvidence.branchesOnFlags) {
            section.heuristicEvidence.likelyTrigger = true;
            section.heuristicEvidence.notes.push_back("Contains switch/branch patterns compatible with trigger checks.");
        }

        if (section.heuristicEvidence.hasLongLinearSequence && !section.heuristicEvidence.likelyTrigger) {
            section.heuristicEvidence.likelyCutscene = true;
            section.heuristicEvidence.notes.push_back("Long reachable sequence without strong trigger indicators.");
        }
    }

    for (const auto& row : rows) {
        if (!row.isValid || !row.isCode || row.isString || row.start >= row.end || !isOffsetInBounds(row.start, dataSize)) {
            continue;
        }
        SctCodeRegion region{};
        region.name = row.name;
        region.entryPayloadOffset = row.start;

        std::deque<std::uint32_t> regionWorklist;
        std::set<std::uint32_t> regionVisited;
        regionWorklist.push_back(row.start);
        while (!regionWorklist.empty()) {
            const auto current = regionWorklist.front();
            regionWorklist.pop_front();
            if (regionVisited.contains(current)) {
                continue;
            }
            const auto it = globalInstructions.find(current);
            if (it == globalInstructions.end()) {
                continue;
            }
            regionVisited.insert(current);
            region.instructionPayloadOffsets.push_back(current);
            if (const auto sectionIndex = sectionIndexForPayloadOffset(rows, current); sectionIndex.has_value()) {
                if (std::find(region.coveredSectionIndexes.begin(), region.coveredSectionIndexes.end(), *sectionIndex)
                    == region.coveredSectionIndexes.end()) {
                    region.coveredSectionIndexes.push_back(*sectionIndex);
                }
            }
            if (!it->second.decoded.successors.empty()) {
                for (const auto successor : it->second.decoded.successors) {
                    if (globalInstructions.contains(successor) && !regionVisited.contains(successor)) {
                        regionWorklist.push_back(successor);
                    }
                }
            } else if (!it->second.decoded.blockTerminator) {
                const auto next = current + it->second.decoded.inst.sizeBytes;
                if (globalInstructions.contains(next) && !regionVisited.contains(next)) {
                    regionWorklist.push_back(next);
                }
            }
        }
        std::sort(region.instructionPayloadOffsets.begin(), region.instructionPayloadOffsets.end());
        std::sort(region.coveredSectionIndexes.begin(), region.coveredSectionIndexes.end());
        result.file.codeRegions.push_back(std::move(region));
    }

    if (result.file.sections.empty()) {
        result.diagnostics.push_back({"No sections were parsed from SCT index.", 0});
        return result;
    }

    result.parseOk = true;
    std::cout << "[SpiceSCT] Step 5/5: Parse complete. ParsedSections="
              << result.file.sections.size()
              << ", diagnostics=" << result.diagnostics.size() << ".\n";
    result.diagnostics.push_back(
        {"Initial SCT parser pass completed (index + control-flow-guided section walk with placeholder opcode semantics).", 0});
    return result;
}

} // namespace spice::sct
