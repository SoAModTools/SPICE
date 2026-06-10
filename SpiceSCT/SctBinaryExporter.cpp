#include "SctBinaryExporter.h"

#include "SctIrBuilder.h"
#include "SctOpcodeMetadata.h"

#include "../Compression/Aklz.h"
#include "../SpiceCore/Binary/EndianReader.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace spice::sct {
namespace {

constexpr std::size_t kHeaderSize = 12;
constexpr std::size_t kIndexEntrySize = 0x14;
constexpr std::size_t kIndexNameOffset = 4;
constexpr std::size_t kIndexNameMaxLen = 0x10;

using Endian = spice::core::Endian;

Endian resolveEndian(const SctFile& file, SctExportEndianPolicy policy) {
    switch (policy) {
    case SctExportEndianPolicy::Big:
        return Endian::Big;
    case SctExportEndianPolicy::Little:
        return Endian::Little;
    case SctExportEndianPolicy::PreserveParsed:
        return file.detectedEndian == "little" ? Endian::Little : Endian::Big;
    }
    return Endian::Big;
}

void writeU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value, Endian endian) {
    if (offset + 4u > bytes.size()) {
        bytes.resize(offset + 4u);
    }
    if (endian == Endian::Big) {
        bytes[offset + 0u] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
        bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
        bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
        bytes[offset + 3u] = static_cast<std::uint8_t>(value & 0xffu);
    } else {
        bytes[offset + 0u] = static_cast<std::uint8_t>(value & 0xffu);
        bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
        bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
        bytes[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
    }
}

std::uint32_t readU32(std::span<const std::uint8_t> bytes, std::size_t offset, Endian endian) {
    return spice::core::EndianReader(bytes, endian).try_read_u32(offset).value_or(0u);
}

void appendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value, Endian endian) {
    const auto offset = bytes.size();
    bytes.resize(offset + 4u);
    writeU32(bytes, offset, value, endian);
}

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

std::uint32_t instructionSizeWords(const SctInstruction& instruction) {
    if (!instruction.rawWords.empty()) {
        return static_cast<std::uint32_t>(instruction.rawWords.size());
    }
    return static_cast<std::uint32_t>(1u + instruction.operands.size());
}

std::vector<std::uint32_t> writableWords(const SctInstruction& instruction) {
    if (!instruction.rawWords.empty()) {
        return instruction.rawWords;
    }
    std::vector<std::uint32_t> words{};
    words.reserve(1u + instruction.operands.size());
    words.push_back(instruction.opcode);
    words.insert(words.end(), instruction.operands.begin(), instruction.operands.end());
    return words;
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

std::uint32_t remapTarget(
    const std::unordered_map<std::uint32_t, std::uint32_t>& offsetMap,
    std::uint32_t oldTarget,
    std::uint32_t fallback) {
    const auto it = offsetMap.find(oldTarget);
    return it == offsetMap.end() ? fallback : it->second;
}

std::uint32_t branchRelative(std::uint32_t instructionOffset, std::uint32_t instructionSize, std::uint32_t targetOffset) {
    return static_cast<std::uint32_t>(
        static_cast<std::int32_t>(targetOffset) - static_cast<std::int32_t>(instructionOffset + instructionSize) + 4);
}

std::uint32_t switchRelative(std::uint32_t instructionOffset, std::uint32_t instructionSize, std::uint32_t targetOffset) {
    return static_cast<std::uint32_t>(
        static_cast<std::int32_t>(targetOffset) - static_cast<std::int32_t>(instructionOffset + instructionSize));
}

void patchControlFlowWords(
    const SctSection& section,
    const SctInstruction& instruction,
    std::uint32_t newOffset,
    std::uint32_t newSize,
    const std::unordered_map<std::uint32_t, std::uint32_t>& offsetMap,
    std::uint32_t newSectionEnd,
    std::vector<std::uint32_t>& words) {
    if (words.empty()) {
        throw std::runtime_error("SCT export failed: instruction has no words.");
    }

    if (instruction.opcode == 0) {
        if (words.size() < 2u) {
            throw std::runtime_error("SCT export failed: branch instruction is missing its offset operand.");
        }
        if (const auto* edge = findEdge(section, instruction.offset, SctEdgeType::BranchFalse); edge != nullptr) {
            const auto target = remapTarget(offsetMap, *edge->toOffset, newSectionEnd);
            words.back() = branchRelative(newOffset, newSize, target);
        }
        return;
    }

    if (instruction.opcode == 10) {
        if (words.size() < 2u) {
            throw std::runtime_error("SCT export failed: jump instruction is missing its offset operand.");
        }
        if (const auto* edge = findEdge(section, instruction.offset, SctEdgeType::Jump); edge != nullptr) {
            const auto target = remapTarget(offsetMap, *edge->toOffset, newSectionEnd);
            words.back() = branchRelative(newOffset, newSize, target);
        }
        return;
    }

    if (instruction.opcode != 3) {
        return;
    }

    const auto& pattern = kSalsaOpcodeParamPatterns[instruction.opcode];
    if (pattern.switchJumpParam < 0 || pattern.loopStartParam < 0 || pattern.loopEndParam < pattern.loopStartParam) {
        return;
    }
    const auto loopWidth = static_cast<std::size_t>(pattern.loopEndParam - pattern.loopStartParam + 1);
    std::size_t edgeOrdinal = 0;
    for (std::size_t operandIndex = static_cast<std::size_t>(pattern.switchJumpParam);
         operandIndex + 1u < words.size();
         operandIndex += loopWidth) {
        const auto* edge = findEdge(section, instruction.offset, SctEdgeType::SwitchCase, edgeOrdinal++);
        if (edge == nullptr) {
            break;
        }
        const auto target = remapTarget(offsetMap, *edge->toOffset, newSectionEnd);
        words[operandIndex + 1u] = switchRelative(newOffset, newSize, target);
    }
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

std::vector<std::uint8_t> exportScriptSection(const SctSection& section, Endian endian) {
    const auto instructions = sortedInstructions(section);
    std::unordered_map<std::uint32_t, std::uint32_t> offsetMap{};
    std::uint32_t cursor = 0;
    for (const auto* instruction : instructions) {
        offsetMap.emplace(instruction->offset, cursor);
        cursor += instructionSizeWords(*instruction) * 4u;
    }
    offsetMap.emplace(section.endOffset > section.startOffset ? section.endOffset - section.startOffset : cursor, cursor);
    const auto newSectionEnd = cursor;

    std::vector<std::uint8_t> bytes{};
    for (const auto* instruction : instructions) {
        auto words = writableWords(*instruction);
        const auto newOffset = offsetMap.at(instruction->offset);
        const auto newSize = static_cast<std::uint32_t>(words.size() * 4u);
        patchControlFlowWords(section, *instruction, newOffset, newSize, offsetMap, newSectionEnd, words);
        for (const auto word : words) {
            appendU32(bytes, word, endian);
        }
    }
    return bytes;
}

std::vector<std::uint8_t> exportSection(const SctSection& section, Endian endian) {
    if (section.kind == SctSectionKind::Script) {
        return exportScriptSection(section, endian);
    }
    return rawSectionBytes(section);
}

void writeIndexName(std::vector<std::uint8_t>& bytes, std::size_t offset, const std::string& name) {
    for (std::size_t i = 0; i < kIndexNameMaxLen; ++i) {
        bytes[offset + i] = 0u;
    }
    const auto count = std::min<std::size_t>(name.size(), kIndexNameMaxLen);
    for (std::size_t i = 0; i < count; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>(name[i]);
    }
}

std::vector<std::uint8_t> exportCanonicalPayload(const SctParseResult& parseResult, Endian endian) {
    const auto ir = SctIrBuilder{}.build(parseResult);
    const auto sectionCount = static_cast<std::uint32_t>(ir.file.sections.size());
    const auto indexSize = static_cast<std::size_t>(sectionCount) * kIndexEntrySize;
    const auto dataStart = kHeaderSize + indexSize;

    std::vector<std::vector<std::uint8_t>> sectionBytes{};
    sectionBytes.reserve(ir.file.sections.size());
    for (const auto& section : ir.file.sections) {
        sectionBytes.push_back(exportSection(section, endian));
    }

    std::vector<std::uint8_t> out{};
    out.resize(dataStart, 0u);
    if (ir.file.headerBytes.size() >= 8u) {
        std::copy(ir.file.headerBytes.begin(), ir.file.headerBytes.begin() + 8, out.begin());
    }
    writeU32(out, 8, sectionCount, endian);

    std::uint32_t sectionStart = 0;
    for (std::size_t i = 0; i < ir.file.sections.size(); ++i) {
        const auto rowOffset = kHeaderSize + (i * kIndexEntrySize);
        writeU32(out, rowOffset, sectionStart, endian);
        writeIndexName(out, rowOffset + kIndexNameOffset, ir.file.sections[i].id.name);
        sectionStart += static_cast<std::uint32_t>(sectionBytes[i].size());
    }

    for (const auto& bytes : sectionBytes) {
        out.insert(out.end(), bytes.begin(), bytes.end());
    }
    return out;
}

} // namespace

std::vector<std::uint8_t> SctBinaryExporter::exportFile(
    const SctParseResult& parseResult,
    const SctExportOptions& options) const {
    if (options.mode == SctExportMode::PreserveBytesForTest && !parseResult.file.originalBytes.empty()) {
        return parseResult.file.originalBytes;
    }

    auto payload = exportCanonicalPayload(parseResult, resolveEndian(parseResult.file, options.endianPolicy));
    if (!options.compressAklz) {
        return payload;
    }

    auto compressed = spice::compression::aklz::compress(payload);
    if (!compressed.ok()) {
        throw std::runtime_error("SCT AKLZ compression failed: " + std::string(spice::compression::aklz::errorToString(compressed.error)));
    }
    return std::move(compressed.bytes);
}

} // namespace spice::sct
