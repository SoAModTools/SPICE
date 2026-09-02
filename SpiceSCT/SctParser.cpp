#include "SctParser.h"
#include "SctOpcodeMetadata.h"
#include "SctScptDecodeHelpers.h"
#include "SctScptEncoding.h"

#include "../SpiceRoot/Binary/EndianReader.h"
#include "../Compression/Aklz.h"

#include <algorithm>
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

using Endian = spice::root::Endian;

[[nodiscard]] std::uint32_t readU32(std::span<const std::uint8_t> bytes, std::size_t offset, Endian endian) {
    return spice::root::EndianReader(bytes, endian).try_read_u32(offset).value_or(0U);
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

struct FooterReferenceCandidate {
    SctFooterReference reference;
    SctFooterEntryKind kind = SctFooterEntryKind::PlainString;
    bool valid = false;
};

struct FooterBoundaryResult {
    bool detected = false;
    std::uint32_t footerStart = 0;
    SctSemanticConfidence confidence = SctSemanticConfidence::Unknown;
    std::string diagnostic;
};

struct SectionRow {
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    std::string name;
    bool isString = false;
    bool isCode = false;
    bool isLabelOnly = false;
    bool isGroupLabel = false;
    bool isValid = true;
};

struct LabelPreambleProbe {
    bool present = false;
    Endian endian = Endian::Big;
    std::uint32_t endOffset = 0;
    std::vector<std::uint32_t> rawWords;
};

struct OpcodeBoundaryProbe {
    bool plausible = false;
    bool swapped = false;
    bool suspiciousOpcode4 = false;
    std::uint16_t opcode = 0;
};

[[nodiscard]] std::string fallbackMnemonic(std::uint16_t opcode) {
    return "op_" + std::to_string(opcode);
}

[[nodiscard]] bool isOffsetInBounds(std::uint32_t offset, std::uint32_t sectionSize) {
    return offset < sectionSize && (offset % 4u == 0u);
}

[[nodiscard]] LabelPreambleProbe parseLabelPreamble(std::span<const std::uint8_t> sectionBytes, Endian baseEndian) {
    LabelPreambleProbe probe{};
    if (sectionBytes.size() < 8u) {
        return probe;
    }

    const auto otherEndian = baseEndian == Endian::Big ? Endian::Little : Endian::Big;
    const auto firstWordBase = readU32(sectionBytes, 0, baseEndian);
    const auto firstWordOther = readU32(sectionBytes, 0, otherEndian);

    Endian chosenEndian = baseEndian;
    if (firstWordBase != 9u) {
        if (firstWordOther != 9u) {
            return probe;
        }
        chosenEndian = otherEndian;
    }

    for (std::uint32_t cursor = 0; cursor + 4u <= sectionBytes.size(); cursor += 4u) {
        const auto word = readU32(sectionBytes, cursor, chosenEndian);
        probe.rawWords.push_back(word);
        if (word == kSctScptStopCode) {
            probe.present = true;
            probe.endian = chosenEndian;
            probe.endOffset = cursor + 4u;
            return probe;
        }
    }

    probe.rawWords.clear();
    return probe;
}

[[nodiscard]] OpcodeBoundaryProbe probeOpcodeBoundary(
    std::span<const std::uint8_t> sectionBytes,
    std::uint32_t offset,
    Endian baseEndian) {
    OpcodeBoundaryProbe probe{};
    if (offset + 4u > sectionBytes.size()) {
        return probe;
    }

    const auto baseWord = readU32(sectionBytes, offset, baseEndian);
    if (baseWord <= kMaxOpcodeProbe) {
        probe.plausible = true;
        probe.opcode = static_cast<std::uint16_t>(baseWord);
        return probe;
    }

    const auto otherEndian = baseEndian == Endian::Big ? Endian::Little : Endian::Big;
    const auto otherWord = readU32(sectionBytes, offset, otherEndian);
    if (otherWord <= kMaxOpcodeProbe) {
        probe.swapped = true;
        probe.suspiciousOpcode4 = otherWord == 4u;
        if (!probe.suspiciousOpcode4) {
            probe.plausible = true;
            probe.opcode = static_cast<std::uint16_t>(otherWord);
        }
    }
    return probe;
}

[[nodiscard]] std::string decodeStringBytes(std::span<const std::uint8_t> bytes) {
    std::string decoded{};
    decoded.reserve(bytes.size());
    for (const auto byte : bytes) {
        if (byte == 0u) {
            continue;
        }
        if (byte == '\n' || byte == '\r' || byte == '\t' || (byte >= 0x20u && byte <= 0x7eu)) {
            decoded.push_back(static_cast<char>(byte));
        } else {
            decoded.push_back('?');
        }
    }
    return decoded;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> readNullTerminatedBytes(
    std::span<const std::uint8_t> bytes,
    std::uint32_t offset) {
    if (offset >= bytes.size()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> result{};
    for (std::uint32_t cursor = offset; cursor < bytes.size(); ++cursor) {
        const auto byte = bytes[cursor];
        result.push_back(byte);
        if (byte == 0u) {
            return result;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool hasPlausibleStringBytes(const std::vector<std::uint8_t>& bytes) {
    return !bytes.empty() && bytes.back() == 0u;
}

[[nodiscard]] std::string numberedId(std::string prefix, std::uint32_t value) {
    auto digits = std::to_string(value);
    while (digits.size() < 3u) {
        digits.insert(digits.begin(), '0');
    }
    return prefix + digits;
}

[[nodiscard]] SctRawSpan makeRawSpan(
    std::span<const std::uint8_t> sectionBytes,
    SctRawSpanReason reason,
    std::string detail) {
    SctRawSpan span{};
    span.startOffset = 0;
    span.endOffset = static_cast<std::uint32_t>(sectionBytes.size());
    span.reason = reason;
    span.detail = std::move(detail);
    span.rawBytes.insert(span.rawBytes.end(), sectionBytes.begin(), sectionBytes.end());
    return span;
}

[[nodiscard]] SctStringEntry makeStringEntry(
    std::span<const std::uint8_t> sectionBytes,
    const LabelPreambleProbe& preamble) {
    SctStringEntry entry{};
    entry.hasPreamble = preamble.present;
    entry.preambleEndOffset = preamble.endOffset;
    entry.textStartOffset = preamble.endOffset;
    entry.preambleWords = preamble.rawWords;
    if (preamble.endOffset < sectionBytes.size()) {
        const auto textBytes = sectionBytes.subspan(preamble.endOffset);
        const auto terminator = std::find(textBytes.begin(), textBytes.end(), 0u);
        const auto boundedEnd = terminator == textBytes.end() ? textBytes.end() : terminator + 1;
        entry.rawTextBytes.insert(entry.rawTextBytes.end(), textBytes.begin(), boundedEnd);
        entry.decodedText = decodeStringBytes(
            textBytes.first(static_cast<std::size_t>(boundedEnd - textBytes.begin())));
        entry.decodeOk = terminator != textBytes.end();
    }
    return entry;
}

[[nodiscard]] std::optional<std::uint32_t> firstStringEndOffset(
    std::span<const std::uint8_t> sectionBytes,
    const LabelPreambleProbe& preamble) {
    if (!preamble.present || preamble.endOffset >= sectionBytes.size()) {
        return std::nullopt;
    }
    for (std::uint32_t cursor = preamble.endOffset; cursor < sectionBytes.size(); ++cursor) {
        if (sectionBytes[cursor] == 0u) {
            return cursor + 1u;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool looksLikeLocalizedStringId(const std::string& name) {
    if (name.size() < 2u || name.front() != 'M') {
        return false;
    }
    const auto second = name[1];
    return (second >= '0' && second <= '9')
        || second == 'F'
        || second == 'G'
        || second == 'S'
        || second == 'p'
        || second == 'f'
        || second == 'i';
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

[[nodiscard]] std::vector<std::uint32_t> encodeParsedScptNode(const SctScptAstNode& root,
    bool appendStop) {
    std::vector<std::uint32_t> words;
    const auto append = [&](const auto& self, const SctScptAstNode& node) -> void {
        for (const auto& child : node.children) self(self, child);
        words.insert(words.end(), node.rawWords.begin(), node.rawWords.end());
    };
    append(append, root);
    if (appendStop && root.kind != SctScptAstNodeKind::Stop) words.push_back(kSctScptStopCode);
    return words;
}

[[nodiscard]] SctScptAstNodeKind parsedKind(SctScptWordKind kind) {
    switch (kind) {
    case SctScptWordKind::FloatLiteral: return SctScptAstNodeKind::FloatLiteral;
    case SctScptWordKind::DecimalLiteral: return SctScptAstNodeKind::DecimalLiteral;
    case SctScptWordKind::ByteVariable: return SctScptAstNodeKind::ByteVariable;
    case SctScptWordKind::BitVariable: return SctScptAstNodeKind::BitVariable;
    case SctScptWordKind::FloatVariable: return SctScptAstNodeKind::FloatVariable;
    case SctScptWordKind::DirectIntVariable: return SctScptAstNodeKind::IntVariable;
    case SctScptWordKind::NegatedIntVariable: return SctScptAstNodeKind::NegatedIntVariable;
    case SctScptWordKind::NegatedIntVariableLow16Comparison:
        return SctScptAstNodeKind::NegatedIntVariableLow16Comparison;
    case SctScptWordKind::SecondaryValue: return SctScptAstNodeKind::SecondaryValue;
    case SctScptWordKind::CompareOperator: return SctScptAstNodeKind::CompareOp;
    case SctScptWordKind::ArithmeticOperator: return SctScptAstNodeKind::ArithmeticOp;
    case SctScptWordKind::AssignmentOperator: return SctScptAstNodeKind::AssignmentOp;
    case SctScptWordKind::Stop: return SctScptAstNodeKind::Stop;
    case SctScptWordKind::Inert: break;
    }
    return SctScptAstNodeKind::Unknown;
}

[[nodiscard]] std::string scptLeafDisplay(std::uint32_t word, SctScptWordKind kind,
    std::optional<std::uint32_t> payload = std::nullopt) {
    const auto index = word & 0x00ffffffu;
    switch (kind) {
    case SctScptWordKind::FloatLiteral:
        return "float: " + std::to_string(detail::floatFromWordBits(payload.value_or(0u)));
    case SctScptWordKind::DecimalLiteral: {
        const auto whole = static_cast<std::int16_t>((word >> 8u) & 0xffffu);
        return "decimal: " + std::to_string(whole) + "+" + std::to_string(word & 0xffu) + "/256";
    }
    case SctScptWordKind::ByteVariable: return "ByteVar: " + std::to_string(index);
    case SctScptWordKind::BitVariable: return "BitVar: " + std::to_string(index);
    case SctScptWordKind::FloatVariable: return "FloatVar: " + std::to_string(index);
    case SctScptWordKind::DirectIntVariable: return "IntVar: " + std::to_string(index);
    case SctScptWordKind::NegatedIntVariable: return "NegatedIntVar: " + std::to_string(index);
    case SctScptWordKind::NegatedIntVariableLow16Comparison:
        return "NegatedIntVarLow16: " + std::to_string(index);
    case SctScptWordKind::SecondaryValue: {
        const auto name = sctScptSecondaryValueName(index);
        return name.empty() ? "Secondary: " + std::to_string(index) : std::string{name};
    }
    default: return detail::toHexWord(word);
    }
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

    std::vector<std::uint32_t> availableWords;
    SctScptScanner scanner;
    for (std::uint32_t cursor = wordOffset; cursor + 4u <= sectionBytes.size(); cursor += 4u) {
        const auto word = readU32(sectionBytes, cursor, endian);
        availableWords.push_back(word);
        if (!scanner.consume(word)) break;
    }
    const auto scan = scanner.result();
    if (!scan.complete) {
        diagnostics.push_back({scan.error == SctScptScanError::TruncatedFloatPayload
            ? "SCPT float literal payload exceeds section bounds."
            : "SCPT parameter decode reached section end before stop code (0x1d).", instructionOffset});
        return static_cast<std::uint32_t>(scan.wordCount);
    }

    const std::vector<std::uint32_t> scannedWords(
        availableWords.begin(), availableWords.begin() + scan.wordCount);
    if (record == nullptr) return static_cast<std::uint32_t>(scan.wordCount);

    record->hitStopCode = !scan.inlineValue;
    if (scan.inlineValue) {
        record->resolvedValue = detail::toHexWord(scannedWords.front());
        record->evaluationTrace.push_back({scannedWords.front(), record->resolvedValue});
        record->ast = makeScptNode(SctScptAstNodeKind::NoLoopValue,
            record->resolvedValue, scannedWords.front());
        return 1u;
    }
    if (scannedWords.size() == 1u) {
        record->resolvedValue = "return values (0x1d)";
        record->evaluationTrace.push_back({kSctScptStopCode, record->resolvedValue});
        record->ast = makeScptNode(SctScptAstNodeKind::Stop,
            record->resolvedValue, kSctScptStopCode);
        return 1u;
    }

    bool typed = true;
    bool assignmentSeen = false;
    std::uint32_t maximumDepth = 0u;
    std::vector<SctScptAstNode> stack;
    for (std::size_t cursor = 0; cursor + 1u < scannedWords.size(); ++cursor) {
        const auto word = scannedWords[cursor];
        const auto classification = classifySctScptWord(word);
        if (assignmentSeen) typed = false;
        if (classification.kind == SctScptWordKind::Inert) {
            typed = false;
            record->evaluationTrace.push_back({word, "runtime-inert"});
            continue;
        }
        if (classification.kind == SctScptWordKind::CompareOperator
            || classification.kind == SctScptWordKind::ArithmeticOperator
            || classification.kind == SctScptWordKind::AssignmentOperator) {
            const auto symbol = std::string{sctScptOperatorSymbol(word)};
            if (stack.size() < 2u) {
                typed = false;
                record->evaluationTrace.push_back({word, "operator stack underflow"});
                continue;
            }
            auto right = std::move(stack.back()); stack.pop_back();
            auto left = std::move(stack.back()); stack.pop_back();
            const auto display = "(" + left.display + " " + symbol + " " + right.display + ")";
            auto node = makeScptNode(parsedKind(classification.kind), display, word, symbol);
            node.children.push_back(std::move(left));
            node.children.push_back(std::move(right));
            stack.push_back(std::move(node));
            assignmentSeen = classification.kind == SctScptWordKind::AssignmentOperator;
            record->evaluationTrace.push_back({word, display});
            continue;
        }
        if (classification.kind == SctScptWordKind::Stop) {
            typed = false;
            continue;
        }

        std::optional<std::uint32_t> payload;
        if (classification.payloadWordCount != 0u) {
            payload = scannedWords[++cursor];
            record->evaluationTrace.push_back({word, detail::toHexWord(word)});
        }
        const auto display = scptLeafDisplay(word, classification.kind, payload);
        auto node = makeScptNode(parsedKind(classification.kind), display, word);
        if (payload) node.rawWords.push_back(*payload);
        stack.push_back(std::move(node));
        maximumDepth = std::max(maximumDepth, static_cast<std::uint32_t>(stack.size()));
        record->evaluationTrace.push_back({payload.value_or(word), display});
    }
    record->evaluationTrace.push_back({kSctScptStopCode, "return values (0x1d)"});
    if (maximumDepth > kSctScptRuntimeStackWarningThreshold) {
        diagnostics.push_back({"SCPT expression exceeds the observed runtime stack-depth warning threshold.",
            instructionOffset});
    }
    if (stack.size() != 1u) typed = false;
    if (typed) {
        auto candidate = std::move(stack.front());
        if (encodeParsedScptNode(candidate, true) == scannedWords) {
            record->resolvedValue = candidate.display;
            record->ast = std::move(candidate);
        }
    }
    if (!record->ast) record->resolvedValue = "opaque SCPT expression";
    return static_cast<std::uint32_t>(scan.wordCount);
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

[[nodiscard]] DecodedInstruction decodeInstruction(
    std::span<const std::uint8_t> sectionBytes,
    std::uint32_t offset,
    Endian baseEndian,
    std::vector<SctDiagnostic>& diagnostics);

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
    std::uint32_t sourceSectionStart,
    const std::vector<SectionRow>& rows) {
    const auto* schema = findSctOpcodeSchema(instruction.opcode);
    if (schema == nullptr) {
        return;
    }
    const auto& metadata = schema->semantic;
    if (metadata.controlRole == SctOpcodeControlRole::CallSubscript) {
        SctEdge edge{};
        edge.type = SctEdgeType::CallSubscript;
        edge.confidence = metadata.confidence;
        edge.fromOffset = instruction.payloadOffset - sourceSectionStart;
        edge.fromPayloadOffset = instruction.payloadOffset;
        edge.opcode = instruction.opcode;
        edge.detail = "Opcode calls a subscript target using a signed relative offset.";
        if (!instruction.operands.empty()) {
            const auto encodedOffset = instruction.operands.front();
            edge.attributes.emplace("offset_operand", decimalString(encodedOffset));
            edge.attributes.emplace("signed_offset_operand", std::to_string(static_cast<std::int32_t>(encodedOffset)));
            if (const auto targetPayloadOffset = resolveRelativeTargetPayloadOffset(
                    instruction.payloadOffset,
                    instruction.sizeBytes,
                    encodedOffset);
                targetPayloadOffset.has_value()) {
                edge.toPayloadOffset = *targetPayloadOffset;
                edge.attributes.emplace("target_payload_offset", decimalString(*targetPayloadOffset));
                if (const auto targetSection = sectionIndexForPayloadOffset(rows, *targetPayloadOffset); targetSection.has_value()) {
                    edge.toOffset = *targetPayloadOffset - rows[*targetSection].start;
                    edge.attributes.emplace("target_section", rows[*targetSection].name);
                    edge.attributes.emplace("target_section_index", decimalString(*targetSection));
                    edge.attributes.emplace("target_offset", decimalString(*edge.toOffset));
                } else {
                    edge.toOffset = *targetPayloadOffset;
                    edge.attributes.emplace("target_offset", decimalString(*targetPayloadOffset));
                }
            } else {
                edge.attributes.emplace("target_resolution", "out_of_range");
            }
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
    if (metadata.effect.kind == SctOpcodeEffectKind::LoadMld
        || metadata.effect.kind == SctOpcodeEffectKind::LoadScript) {
        SctEdge edge{};
        edge.type = metadata.effect.kind == SctOpcodeEffectKind::LoadMld
            ? SctEdgeType::LoadsMld : SctEdgeType::LoadsScript;
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

    std::uint32_t operandWordIndex = instruction.opcodeWordIndex + 1u;
    for (const auto& parameter : instruction.parameters) {
        const auto reference = sctOpcodeTextReference(*schema, parameter.index);
        if (reference.has_value() && reference->storage == SctTextStorage::IndexedSection
            && !parameter.rawWords.empty()) {
            const auto operandPayloadOffset = instruction.payloadOffset + operandWordIndex * 4u;
            const auto masked = parameter.rawWords.front() & reference->encodedValueMask;
            const auto relative = reference->signedRelative
                ? static_cast<std::int64_t>(static_cast<std::int32_t>(masked))
                : static_cast<std::int64_t>(masked);
            const auto target = static_cast<std::int64_t>(operandPayloadOffset) + relative;
            SctEdge edge{};
            edge.type = SctEdgeType::ReferencesString;
            edge.confidence = SctSemanticConfidence::Known;
            edge.fromOffset = instruction.payloadOffset - sourceSectionStart;
            edge.fromPayloadOffset = instruction.payloadOffset;
            edge.opcode = instruction.opcode;
            edge.detail = "Instruction references an indexed SCT string section.";
            edge.attributes.emplace("parameter_index", decimalString(parameter.index));
            edge.attributes.emplace("operand_payload_offset", decimalString(operandPayloadOffset));
            if (target >= 0 && target <= std::numeric_limits<std::uint32_t>::max()) {
                edge.toPayloadOffset = static_cast<std::uint32_t>(target);
                if (const auto targetSection = sectionIndexForPayloadOffset(rows, *edge.toPayloadOffset);
                    targetSection.has_value() && rows[*targetSection].start == *edge.toPayloadOffset) {
                    edge.toOffset = 0u;
                    edge.attributes.emplace("target_section", rows[*targetSection].name);
                    edge.attributes.emplace("target_section_index", decimalString(*targetSection));
                } else {
                    edge.attributes.emplace("target_resolution", "not_indexed_section_start");
                }
            } else {
                edge.attributes.emplace("target_resolution", "out_of_range");
            }
            edges.push_back(std::move(edge));
        }
        operandWordIndex += static_cast<std::uint32_t>(parameter.rawWords.size());
    }
}

[[nodiscard]] std::string decimalString(std::uint32_t value) {
    return std::to_string(value);
}

[[nodiscard]] std::optional<std::uint32_t> parameterOperandPayloadOffset(
    const SctInstruction& instruction,
    std::uint32_t parameterIndex) {
    std::uint32_t operandWordIndex = 0;
    for (const auto& parameter : instruction.parameters) {
        if (parameter.index == parameterIndex) {
            return instruction.payloadOffset + ((instruction.opcodeWordIndex + 1u + operandWordIndex) * 4u);
        }
        operandWordIndex += static_cast<std::uint32_t>(parameter.rawWords.size());
    }
    return std::nullopt;
}

[[nodiscard]] std::int64_t footerRelativeValue(const SctParameter& parameter, bool signedRelative) {
    if (parameter.rawWords.empty()) {
        return 0;
    }
    const auto raw = parameter.rawWords.front();
    if (signedRelative) {
        return static_cast<std::int32_t>(raw);
    }
    return raw;
}

[[nodiscard]] std::vector<FooterReferenceCandidate> collectFooterReferenceCandidates(
    const std::map<std::uint32_t, GlobalDecodedInstruction>& globalInstructions,
    std::span<const std::uint8_t> dataBytes,
    std::uint32_t lastRowStart) {
    std::vector<FooterReferenceCandidate> candidates{};
    for (const auto& [payloadOffset, global] : globalInstructions) {
        const auto& instruction = global.decoded.inst;
        const auto* schema = findSctOpcodeSchema(instruction.opcode);
        if (schema == nullptr) {
            continue;
        }
        for (const auto& parameter : instruction.parameters) {
            const auto metadata = sctOpcodeTextReference(*schema, parameter.index);
            if (!metadata.has_value() || metadata->storage != SctTextStorage::Footer) {
                continue;
            }
            const auto operandPayloadOffset = parameterOperandPayloadOffset(instruction, parameter.index);
            if (!operandPayloadOffset.has_value()) {
                continue;
            }
            const auto target = static_cast<std::int64_t>(*operandPayloadOffset)
                + footerRelativeValue(parameter, metadata->signedRelative);

            FooterReferenceCandidate candidate{};
            candidate.reference.instructionPayloadOffset = payloadOffset;
            candidate.reference.parameterIndex = parameter.index;
            candidate.reference.operandPayloadOffset = *operandPayloadOffset;
            candidate.reference.opcode = instruction.opcode;
            candidate.kind = metadata->kind == SctTextKind::SctString
                ? SctFooterEntryKind::SctString
                : SctFooterEntryKind::PlainString;

            if (target >= 0 && target < static_cast<std::int64_t>(dataBytes.size())) {
                candidate.reference.targetPayloadOffset = static_cast<std::uint32_t>(target);
                const auto rawString = readNullTerminatedBytes(dataBytes, candidate.reference.targetPayloadOffset);
                candidate.valid = candidate.reference.targetPayloadOffset > lastRowStart
                    && rawString.has_value()
                    && hasPlausibleStringBytes(*rawString);
            }
            candidates.push_back(std::move(candidate));
        }
    }
    return candidates;
}

[[nodiscard]] std::optional<std::uint32_t> terminatorEndFromDecodedInstruction(
    const DecodedInstruction& decoded,
    std::uint32_t offset) {
    if (!decoded.inst.decodeOk) {
        return std::nullopt;
    }
    if (decoded.inst.opcode == 12u) {
        return offset + 4u;
    }
    if (decoded.inst.opcode == 10u && decoded.inst.sizeBytes >= 8u && !decoded.inst.operands.empty()) {
        if (static_cast<std::int32_t>(decoded.inst.operands.back()) < 0) {
            return offset + decoded.inst.sizeBytes;
        }
    }
    return std::nullopt;
}

[[nodiscard]] FooterBoundaryResult inferFooterBoundary(
    const std::vector<FooterReferenceCandidate>& candidates,
    const std::map<std::uint32_t, GlobalDecodedInstruction>& globalInstructions,
    std::span<const std::uint8_t> dataBytes,
    std::uint32_t lastRowStart,
    Endian indexEndian) {
    FooterBoundaryResult result{};
    std::optional<std::uint32_t> earliestFooterTarget{};
    for (const auto& candidate : candidates) {
        if (!candidate.valid) {
            continue;
        }
        if (!earliestFooterTarget.has_value() || candidate.reference.targetPayloadOffset < *earliestFooterTarget) {
            earliestFooterTarget = candidate.reference.targetPayloadOffset;
        }
    }

    if (!earliestFooterTarget.has_value()) {
        result.diagnostic = "No valid footer string references were found.";
        return result;
    }

    if (*earliestFooterTarget <= lastRowStart) {
        result.diagnostic = "Earliest footer reference does not land after the final index row start.";
        return result;
    }

    auto cursor = ((*earliestFooterTarget - 1u) / 4u) * 4u;
    while (cursor > lastRowStart) {
        if (const auto it = globalInstructions.find(cursor); it != globalInstructions.end()) {
            if (const auto terminatorEnd = terminatorEndFromDecodedInstruction(it->second.decoded, cursor);
                terminatorEnd.has_value() && *terminatorEnd <= *earliestFooterTarget) {
                result.detected = true;
                result.footerStart = *terminatorEnd;
                result.confidence = SctSemanticConfidence::Known;
                return result;
            }
        }
        if (cursor < 4u) {
            break;
        }
        cursor -= 4u;
    }

    cursor = ((*earliestFooterTarget - 1u) / 4u) * 4u;
    while (cursor > lastRowStart) {
        std::vector<SctDiagnostic> diagnostics{};
        const auto decoded = decodeInstruction(dataBytes, cursor, indexEndian, diagnostics);
        if (const auto terminatorEnd = terminatorEndFromDecodedInstruction(decoded, cursor);
            terminatorEnd.has_value() && *terminatorEnd <= *earliestFooterTarget) {
            result.detected = true;
            result.footerStart = *terminatorEnd;
            result.confidence = SctSemanticConfidence::Heuristic;
            result.diagnostic = "Footer boundary inferred using aligned SALSA-compatible fallback scan.";
            return result;
        }
        if (cursor < 4u) {
            break;
        }
        cursor -= 4u;
    }

    result.diagnostic = "Footer references were found, but no aligned final-section terminator was found before the footer.";
    return result;
}

void populateFooterEntriesAndGroups(
    SctParseResult& result,
    SctFooter& footer,
    const std::vector<FooterReferenceCandidate>& footerCandidates,
    std::span<const std::uint8_t> dataBytes) {
    std::map<std::uint32_t, std::size_t> entryIndexByTarget{};
    std::uint32_t plainCount = 0;
    std::uint32_t sctStringCount = 0;
    for (const auto& candidate : footerCandidates) {
        if (!candidate.valid || candidate.reference.targetPayloadOffset < footer.payloadStartOffset) {
            continue;
        }
        const auto rawString = readNullTerminatedBytes(dataBytes, candidate.reference.targetPayloadOffset);
        if (!rawString.has_value()) {
            footer.diagnostics.push_back({
                "Footer reference target is not null-terminated.",
                candidate.reference.targetPayloadOffset
            });
            continue;
        }

        auto entryIt = entryIndexByTarget.find(candidate.reference.targetPayloadOffset);
        if (entryIt == entryIndexByTarget.end()) {
            SctFooterEntry entry{};
            entry.kind = candidate.kind;
            entry.payloadOffset = candidate.reference.targetPayloadOffset;
            entry.rawBytes = *rawString;
            entry.decodedText = decodeStringBytes(entry.rawBytes);
            entry.decodeOk = true;
            if (entry.kind == SctFooterEntryKind::SctString) {
                entry.id = numberedId("FOOTER", sctStringCount++);
            } else {
                entry.id = numberedId("STRING", plainCount++);
            }
            footer.entries.push_back(std::move(entry));
            entryIt = entryIndexByTarget.emplace(candidate.reference.targetPayloadOffset, footer.entries.size() - 1u).first;
        } else if (footer.entries[entryIt->second].kind == SctFooterEntryKind::String
            && candidate.kind == SctFooterEntryKind::SctString) {
            footer.entries[entryIt->second].kind = SctFooterEntryKind::SctString;
            footer.entries[entryIt->second].id = numberedId("FOOTER", sctStringCount++);
        }
        footer.entries[entryIt->second].references.push_back(candidate.reference);
    }

    SctStringGroup footerGroup{};
    footerGroup.name = "_Footer_";
    footerGroup.synthetic = true;
    footerGroup.notes.push_back("Synthetic footer string group created from footer string references.");
    for (const auto& entry : footer.entries) {
        if (entry.kind == SctFooterEntryKind::SctString) {
            footerGroup.footerEntryIds.push_back(entry.id);
        }
    }
    if (!footerGroup.footerEntryIds.empty()) {
        result.file.stringGroups.push_back(std::move(footerGroup));
    }
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
            && parameter.rawWords.back() == kSctScptStopCode) {
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

    Endian chosenEndian = baseEndian;
    std::uint32_t word = wordBase;

    if (wordBase > kMaxOpcodeProbe && wordOther <= kMaxOpcodeProbe && wordOther != 4u) {
        chosenEndian = otherEndian;
        word = wordOther;
    }
    if (wordBase > kMaxOpcodeProbe && wordOther == 4u) {
        diagnostics.push_back({
            "Instruction boundary probe rejected swapped opcode 4 because it aliases the SCPT float preamble.",
            offset
        });
    }

    std::uint32_t actualOpcodeOffset = offset;
    std::vector<std::uint32_t> prefixWords{};

    auto currentWord = word;
    auto currentOpcode = currentWord <= kMaxOpcodeProbe ? static_cast<std::uint16_t>(currentWord) : kMaxOpcodeProbe + 1u;
    if (currentOpcode == 13u && actualOpcodeOffset + 8u <= sectionBytes.size()) {
        decoded.inst.skipRefresh = true;
        prefixWords.push_back(currentWord);
        actualOpcodeOffset += 4u;
        currentWord = readU32(sectionBytes, actualOpcodeOffset, chosenEndian);
        currentOpcode = currentWord <= kMaxOpcodeProbe ? static_cast<std::uint16_t>(currentWord) : kMaxOpcodeProbe + 1u;
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
            currentOpcode = currentWord <= kMaxOpcodeProbe ? static_cast<std::uint16_t>(currentWord) : kMaxOpcodeProbe + 1u;
        }
    }

    decoded.inst.opcodeWordIndex = static_cast<std::uint32_t>(prefixWords.size());
    decoded.inst.endian = chosenEndian == baseEndian ? SctInstructionEndian::Native : SctInstructionEndian::Swapped;
    const auto opcode = currentOpcode;
    decoded.inst.opcode = opcode;
    const auto* opcodeSchema = findSctOpcodeSchema(opcode);
    SctOpcodeSemanticMetadata unknownOpcodeMetadata{};
    unknownOpcodeMetadata.opcode = opcode;
    const auto& opcodeMetadata = opcodeSchema != nullptr ? opcodeSchema->semantic : unknownOpcodeMetadata;
    decoded.inst.mnemonic = opcodeMetadata.mnemonic.empty() ? fallbackMnemonic(opcode) : std::string(opcodeMetadata.mnemonic);
    decoded.inst.semanticConfidence = opcodeMetadata.confidence;
    decoded.inst.decodeOk = opcode <= kMaxOpcodeProbe;
    decoded.inst.sizeBytes = (actualOpcodeOffset - offset) + 4u;

    if (opcodeSchema != nullptr) {
        const auto& paramPattern = opcodeSchema->parameters;
        std::uint32_t totalParamSlots = paramPattern.paramCount;
        std::uint32_t consumedOperandWords = 0;
        std::uint32_t iterations = 0;
        bool loopBreakReached = false;

        auto consumeParamSlot = [&](std::uint32_t paramIndex) -> bool {
            const auto baseParamIndex = sctOpcodeBaseParameterIndex(*opcodeSchema, paramIndex);

            const auto paramWordOffset = actualOpcodeOffset + 4u + (consumedOperandWords * 4u);
            if (paramWordOffset + 4u > sectionBytes.size()) {
                diagnostics.push_back({"Instruction payload exceeds section bounds.", offset});
                return false;
            }

            const auto parameterEncoding = sctOpcodeParameterEncoding(*opcodeSchema, paramIndex);
            const bool isScptParam = parameterEncoding == SctOpcodeParameterEncoding::ScptExpression;
            const bool isRawSentinelSequence = parameterEncoding == SctOpcodeParameterEncoding::RawWordsUntilSentinel;
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
            } else if (isRawSentinelSequence) {
                wordsForParam = 0;
                const auto* parameterSchema = sctOpcodeParameterSchema(*opcodeSchema, paramIndex);
                const auto sentinel = parameterSchema != nullptr && parameterSchema->hasConfirmedSentinel
                    ? parameterSchema->sentinelEncodedWord : 0x0000001du;
                auto rawCursor = paramWordOffset;
                while (rawCursor + 4u <= sectionBytes.size()) {
                    ++wordsForParam;
                    if (readU32(sectionBytes, rawCursor, chosenEndian) == sentinel) {
                        break;
                    }
                    rawCursor += 4u;
                }
                if (wordsForParam == 0u
                    || readU32(sectionBytes, paramWordOffset + (wordsForParam - 1u) * 4u, chosenEndian) != sentinel) {
                    diagnostics.push_back({"Raw sentinel-terminated parameter reached section end before 0x1d.", offset});
                    return false;
                }
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
            parameter.valueKind = isScptParam ? SctParameterValueKind::Expression
                : isRawSentinelSequence ? SctParameterValueKind::Raw : SctParameterValueKind::Integer;

            if (!isScptParam && (parameter.role.find("offset") != std::string::npos
                || parameter.role.find("Offset") != std::string::npos)) {
                parameter.valueKind = SctParameterValueKind::Link;
            }
            if (!isScptParam && (opcodeMetadata.effect.kind == SctOpcodeEffectKind::LoadMld
                || opcodeMetadata.effect.kind == SctOpcodeEffectKind::LoadScript)) {
                parameter.valueKind = SctParameterValueKind::ResourceRef;
            }
            if (!isScptParam && sctOpcodeTextReference(*opcodeSchema, paramIndex).has_value()) {
                parameter.valueKind = SctParameterValueKind::StringRef;
                if (parameter.role.empty()) {
                    parameter.role = "textRef";
                }
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

void decodeUnreachedCodeBlocks(
    SctSection& section,
    std::span<const std::uint8_t> sectionBytes,
    std::uint32_t sectionPayloadStart,
    Endian indexEndian) {

    section.unreachedCode.clear();
    for (const auto& span : section.rawSpans) {
        if (span.reason != SctRawSpanReason::Unreached || span.startOffset >= span.endOffset) {
            continue;
        }

        SctUnreachedCodeBlock block{};
        block.startOffset = span.startOffset;
        block.endOffset = span.endOffset;
        block.payloadStartOffset = sectionPayloadStart + span.startOffset;
        block.payloadEndOffset = sectionPayloadStart + span.endOffset;
        block.rawBytes = span.rawBytes;
        block.confidence = SctSemanticConfidence::Heuristic;
        block.stopReason = "span_end";

        auto cursor = span.startOffset;
        while (cursor < span.endOffset) {
            std::vector<SctDiagnostic> diagnostics{};
            auto decoded = decodeInstruction(sectionBytes, cursor, indexEndian, diagnostics);
            for (const auto& diagnostic : diagnostics) {
                block.diagnostics.push_back({diagnostic.message, diagnostic.offset});
            }

            if (decoded.inst.sizeBytes == 0u) {
                block.stopReason = "zero_length_decode";
                break;
            }
            if (!decoded.inst.decodeOk) {
                const auto rejectedOpcode4 = std::any_of(
                    diagnostics.begin(),
                    diagnostics.end(),
                    [](const SctDiagnostic& diagnostic) {
                        return diagnostic.message.find("swapped opcode 4") != std::string::npos;
                    });
                block.stopReason = rejectedOpcode4 ? "opcode4_swapped_rejected" : "non_opcode_word";
                break;
            }
            if (cursor + decoded.inst.sizeBytes > span.endOffset) {
                block.stopReason = "truncated_instruction";
                block.diagnostics.push_back({
                    "Speculative unreached-code decode stopped because the instruction exceeds the raw span.",
                    cursor
                });
                break;
            }

            decoded.inst.offset = cursor;
            decoded.inst.payloadOffset = sectionPayloadStart + cursor;
            block.instructions.push_back(std::move(decoded.inst));
            cursor += block.instructions.back().sizeBytes;
        }

        if (block.instructions.empty() && block.diagnostics.empty()) {
            block.diagnostics.push_back({
                "No plausible instruction boundary found in unreached raw span.",
                span.startOffset
            });
        }
        section.unreachedCode.push_back(std::move(block));
    }
}

} // namespace

SctParseResult SctParser::parseFile(const std::string& sourcePath, SctParseOptions options) const {
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

    return parse(bytes, sourcePath, options);
}

SctParseResult SctParser::parse(
    std::span<const std::uint8_t> bytes,
    std::string sourcePath,
    SctParseOptions options) const {
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

        const auto preamble = parseLabelPreamble(sectionBytes, indexEndian);
        if (preamble.present && preamble.endOffset == sectionBytes.size()) {
            rows[i].isLabelOnly = true;
            section.kind = SctSectionKind::Label;
            section.heuristicEvidence.notes.push_back("Physical row contains only a 9 ... 0x1d label/string preamble.");
            result.file.sections.push_back(std::move(section));
            continue;
        }

        if (preamble.present) {
            const auto opcodeProbe = probeOpcodeBoundary(sectionBytes, preamble.endOffset, indexEndian);
            if (!opcodeProbe.plausible) {
                rows[i].isString = true;
                section.kind = SctSectionKind::String;
                section.isStringSection = true;
                section.stringEntry = makeStringEntry(sectionBytes, preamble);
                section.heuristicEvidence.notes.push_back(
                    opcodeProbe.suspiciousOpcode4
                        ? "Detected string section after rejecting swapped opcode 4 at the string payload boundary."
                        : "Detected string section from 9 ... 0x1d preamble followed by non-opcode payload.");
                section.rawSpans.push_back(makeRawSpan(
                    sectionBytes,
                    SctRawSpanReason::StringPayload,
                    "Raw string-section bytes preserved by SCT IR."));
                result.file.sections.push_back(std::move(section));
                continue;
            }
        }

        section.kind = SctSectionKind::Script;
        const auto firstWordProbe = probeOpcodeBoundary(sectionBytes, 0, indexEndian);
        if (!preamble.present && !firstWordProbe.plausible) {
            section.kind = SctSectionKind::Unknown;
            section.rawSpans.push_back(makeRawSpan(
                sectionBytes,
                SctRawSpanReason::Unknown,
                "Physical index row does not start with a plausible SCT opcode; raw bytes preserved."));
            result.file.sections.push_back(std::move(section));
            continue;
        }

        rows[i].isCode = true;
        const auto firstWord = readU32(dataBytes, rows[i].start, indexEndian);
        if (firstWord != 9u) {
            result.diagnostics.push_back({
                "Script index entry does not start with label opcode 9.",
                rows[i].start,
                rows[i].name
            });
        }
        result.file.sections.push_back(std::move(section));
    }

    auto addLabelRawSpan = [&](std::uint32_t sectionIndex) {
        if (sectionIndex >= result.file.sections.size() || sectionIndex >= rows.size()) {
            return;
        }
        const auto sectionBytes = dataBytes.subspan(rows[sectionIndex].start, rows[sectionIndex].end - rows[sectionIndex].start);
        auto& section = result.file.sections[sectionIndex];
        section.rawSpans.clear();
        section.rawSpans.push_back(makeRawSpan(
            sectionBytes,
            SctRawSpanReason::StringGroupLabel,
            "Raw string-group label preamble preserved by SCT IR."));
    };

    auto finalizeStringGroup = [&](SctStringGroup& group) {
        if (group.stringSectionIndexes.empty()) {
            return;
        }
        result.file.stringGroups.push_back(std::move(group));
        group = {};
    };

    SctStringGroup currentGroup{};
    std::uint32_t syntheticGroupIndex = 0;
    bool hasOpenGroup = false;

    for (std::uint32_t i = 0; i < rows.size(); ++i) {
        if (rows[i].isLabelOnly) {
            const auto sectionBytes = dataBytes.subspan(rows[i].start, rows[i].end - rows[i].start);
            const bool treatAsEmptyString = looksLikeLocalizedStringId(rows[i].name)
                && (hasOpenGroup || ((i + 1u < rows.size()) && rows[i + 1u].isString));
            if (treatAsEmptyString) {
                rows[i].isString = true;
                result.file.sections[i].kind = SctSectionKind::String;
                result.file.sections[i].isStringSection = true;
                result.file.sections[i].stringEntry = makeStringEntry(sectionBytes, parseLabelPreamble(sectionBytes, indexEndian));
                result.file.sections[i].stringEntry->decodeOk = true;
                result.file.sections[i].rawSpans.clear();
                result.file.sections[i].rawSpans.push_back(makeRawSpan(
                    sectionBytes,
                    SctRawSpanReason::StringPayload,
                    "Raw empty string-section bytes preserved by SCT IR."));
                result.file.sections[i].heuristicEvidence.notes.push_back(
                    "Label-only localized string id treated as an empty string entry.");
                if (!hasOpenGroup) {
                    currentGroup.name = "Untitled(" + std::to_string(syntheticGroupIndex++) + ")";
                    currentGroup.labelSectionIndex.reset();
                    currentGroup.synthetic = true;
                    currentGroup.notes.push_back("Synthetic string group created for strings without a preceding physical label.");
                    hasOpenGroup = true;
                }
                currentGroup.stringSectionIndexes.push_back(i);
                continue;
            }

            const bool ownsFollowingStrings = (i + 1u < rows.size()) && rows[i + 1u].isString;
            if (ownsFollowingStrings) {
                if (hasOpenGroup) {
                    finalizeStringGroup(currentGroup);
                    hasOpenGroup = false;
                }
                rows[i].isGroupLabel = true;
                result.file.sections[i].kind = SctSectionKind::Label;
                addLabelRawSpan(i);
                currentGroup.name = rows[i].name;
                currentGroup.labelSectionIndex = i;
                currentGroup.synthetic = false;
                currentGroup.notes.push_back("Physical label row used as a string group label.");
                hasOpenGroup = true;
                continue;
            }

            result.file.sections[i].kind = SctSectionKind::Script;
            rows[i].isCode = true;
            result.file.sections[i].rawSpans.clear();
            continue;
        }

        if (rows[i].isString) {
            if (!hasOpenGroup) {
                currentGroup.name = "Untitled(" + std::to_string(syntheticGroupIndex++) + ")";
                currentGroup.labelSectionIndex.reset();
                currentGroup.synthetic = true;
                currentGroup.notes.push_back("Synthetic string group created for strings without a preceding physical label.");
                hasOpenGroup = true;
            }
            currentGroup.stringSectionIndexes.push_back(i);
            continue;
        }

        if (hasOpenGroup) {
            finalizeStringGroup(currentGroup);
            hasOpenGroup = false;
        }
    }
    if (hasOpenGroup) {
        finalizeStringGroup(currentGroup);
    }

    for (std::uint32_t i = 0; i < rows.size(); ++i) {
        if (!rows[i].isLabelOnly || rows[i].isGroupLabel) {
            continue;
        }
        const auto firstWord = readU32(dataBytes, rows[i].start, indexEndian);
        if (firstWord != 9u) {
            result.diagnostics.push_back({
                "Script index entry does not start with label opcode 9.",
                rows[i].start,
                rows[i].name
            });
        }
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
            const auto cursorSection = sectionIndexForPayloadOffset(rows, cursor);
            if (cursorSection.has_value() && !rows[*cursorSection].isCode) {
                break;
            }
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

    // Short indexed strings can look like valid byte-swapped opcodes. A confirmed
    // indexed-string relocation is stronger evidence than that boundary heuristic,
    // so promote exact opcode-9 row targets before assigning decoded instructions.
    std::set<std::uint32_t> referencedIndexedStringRows;
    for (const auto& [payloadOffset, global] : globalInstructions) {
        const auto& instruction = global.decoded.inst;
        const auto* schema = findSctOpcodeSchema(instruction.opcode);
        if (schema == nullptr) continue;
        std::uint32_t operandWordIndex = instruction.opcodeWordIndex + 1u;
        for (const auto& parameter : instruction.parameters) {
            const auto rule = sctOpcodeTextReference(*schema, parameter.index);
            if (rule.has_value() && rule->storage == SctTextStorage::IndexedSection
                && !parameter.rawWords.empty()) {
                const auto operandPayloadOffset = instruction.payloadOffset + operandWordIndex * 4u;
                const auto base = rule->relativeBase == SctRelativeReferenceBase::OperandWord
                    ? static_cast<std::int64_t>(operandPayloadOffset)
                    : static_cast<std::int64_t>(instruction.payloadOffset + instruction.sizeBytes - 4u);
                const auto masked = parameter.rawWords.front() & rule->encodedValueMask;
                const auto relative = rule->signedRelative
                    ? static_cast<std::int64_t>(static_cast<std::int32_t>(masked))
                    : static_cast<std::int64_t>(masked);
                const auto target = base + relative;
                if (target >= 0 && target <= std::numeric_limits<std::uint32_t>::max()
                    && static_cast<std::uint32_t>(target) % rule->targetAlignment == 0u) {
                    const auto sectionIndex = sectionIndexForPayloadOffset(
                        rows, static_cast<std::uint32_t>(target));
                    if (sectionIndex.has_value()
                        && rows[*sectionIndex].start == static_cast<std::uint32_t>(target)) {
                        referencedIndexedStringRows.insert(*sectionIndex);
                    }
                }
            }
            operandWordIndex += static_cast<std::uint32_t>(parameter.rawWords.size());
        }
    }
    for (const auto rowIndex : referencedIndexedStringRows) {
        auto& row = rows[rowIndex];
        const auto sectionBytes = dataBytes.subspan(row.start, row.end - row.start);
        const auto preamble = parseLabelPreamble(sectionBytes, indexEndian);
        if (!preamble.present || !firstStringEndOffset(sectionBytes, preamble).has_value()) {
            result.diagnostics.push_back({
                "Indexed text reference targets a row without a bounded opcode-9 string payload.",
                row.start,
                row.name
            });
            continue;
        }
        row.isCode = false;
        row.isString = true;
        row.isLabelOnly = false;
        auto& section = result.file.sections[rowIndex];
        section.kind = SctSectionKind::String;
        section.isStringSection = true;
        section.instructions.clear();
        section.edges.clear();
        section.stringEntry = makeStringEntry(sectionBytes, preamble);
        section.rawSpans.clear();
        section.rawSpans.push_back(makeRawSpan(
            sectionBytes,
            SctRawSpanReason::StringPayload,
            "Raw indexed-string bytes preserved after typed relocation evidence."));
        section.heuristicEvidence.notes.push_back(
            "Classified as an indexed SCT string from a confirmed typed relocation to its opcode-9 row start.");

        for (auto it = globalInstructions.begin(); it != globalInstructions.end();) {
            if (it->first >= row.start && it->first < row.end) it = globalInstructions.erase(it);
            else ++it;
        }
        const auto grouped = std::any_of(result.file.stringGroups.begin(), result.file.stringGroups.end(),
            [&](const auto& group) {
                return std::find(group.stringSectionIndexes.begin(), group.stringSectionIndexes.end(), rowIndex)
                    != group.stringSectionIndexes.end();
            });
        if (!grouped) {
            SctStringGroup group{};
            group.name = "Untitled(" + std::to_string(syntheticGroupIndex++) + ")";
            group.stringSectionIndexes.push_back(rowIndex);
            group.synthetic = true;
            group.notes.push_back("Synthetic string group created from indexed-reference evidence.");
            result.file.stringGroups.push_back(std::move(group));
        }
    }

    SctFooter footer{};
    footer.present = true;
    footer.payloadStartOffset = dataSize;
    footer.payloadEndOffset = dataSize;
    footer.confidence = SctSemanticConfidence::Known;

    if (!rows.empty()) {
        const auto lastRowIndex = static_cast<std::uint32_t>(rows.size() - 1u);
        const auto lastRowStart = rows[lastRowIndex].start;
        auto footerCandidates = collectFooterReferenceCandidates(globalInstructions, dataBytes, lastRowStart);
        bool footerBoundaryApplied = false;

        if (lastRowIndex < result.file.sections.size()
            && result.file.sections[lastRowIndex].kind == SctSectionKind::String) {
            const auto originalLastRowEnd = rows[lastRowIndex].end;
            const auto sectionBytes = dataBytes.subspan(
                rows[lastRowIndex].start,
                originalLastRowEnd - rows[lastRowIndex].start);
            const auto preamble = parseLabelPreamble(sectionBytes, indexEndian);
            if (const auto stringEnd = firstStringEndOffset(sectionBytes, preamble); stringEnd.has_value()) {
                footer.payloadStartOffset = rows[lastRowIndex].start + *stringEnd;
                footer.confidence = SctSemanticConfidence::Known;
                rows[lastRowIndex].end = footer.payloadStartOffset;
                auto& finalSection = result.file.sections[lastRowIndex];
                finalSection.endOffset = dataStart + footer.payloadStartOffset;
                const auto truncatedSectionBytes = dataBytes.subspan(
                    rows[lastRowIndex].start,
                    rows[lastRowIndex].end - rows[lastRowIndex].start);
                finalSection.stringEntry = makeStringEntry(truncatedSectionBytes, preamble);
                finalSection.rawSpans.clear();
                finalSection.rawSpans.push_back(makeRawSpan(
                    truncatedSectionBytes,
                    SctRawSpanReason::StringPayload,
                    "Raw string-section bytes preserved by SCT IR."));
                footerBoundaryApplied = true;
            } else {
                const std::string message = "Final string section did not contain a null terminator; footer left empty.";
                footer.diagnostics.push_back({message, lastRowStart});
                result.diagnostics.push_back({message, lastRowStart, rows[lastRowIndex].name});
                footer.confidence = SctSemanticConfidence::Unknown;
            }
        }

        if (!footerBoundaryApplied
            && lastRowIndex < result.file.sections.size()
            && result.file.sections[lastRowIndex].kind != SctSectionKind::String) {
            auto footerBoundary = inferFooterBoundary(footerCandidates, globalInstructions, dataBytes, lastRowStart, indexEndian);
            if (footerBoundary.detected
                && footerBoundary.footerStart > lastRowStart
                && footerBoundary.footerStart <= dataSize
                && footerBoundary.footerStart < rows[lastRowIndex].end) {
                rows[lastRowIndex].end = footerBoundary.footerStart;
                if (lastRowIndex < result.file.sections.size()) {
                    result.file.sections[lastRowIndex].endOffset = dataStart + footerBoundary.footerStart;
                }
                footer.payloadStartOffset = footerBoundary.footerStart;
                footer.confidence = footerBoundary.confidence;
                if (!footerBoundary.diagnostic.empty()) {
                    footer.diagnostics.push_back({footerBoundary.diagnostic, footerBoundary.footerStart});
                    result.diagnostics.push_back({footerBoundary.diagnostic, footerBoundary.footerStart});
                }
                footerBoundaryApplied = true;
            } else if (!footerBoundary.diagnostic.empty() && std::any_of(
                footerCandidates.begin(),
                footerCandidates.end(),
                [](const auto& candidate) { return candidate.valid; })) {
                footer.diagnostics.push_back({footerBoundary.diagnostic, lastRowStart});
                result.diagnostics.push_back({footerBoundary.diagnostic, lastRowStart});
                footer.confidence = SctSemanticConfidence::Unknown;
            }
        }

        footer.payloadStartOffset = std::min(footer.payloadStartOffset, dataSize);
        footer.rawBytes.insert(
            footer.rawBytes.end(),
            dataBytes.begin() + footer.payloadStartOffset,
            dataBytes.end());
        populateFooterEntriesAndGroups(result, footer, footerCandidates, dataBytes);
    }

    result.file.footer = std::move(footer);

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
        addInstructionSemanticEdges(section.edges, localInstruction, rows[*sectionIndex].start, rows);
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
        if (options.decodeUnreachedCode) {
            decodeUnreachedCodeBlocks(section, sectionBytes, rows[sectionIndex].start, indexEndian);
        }

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
