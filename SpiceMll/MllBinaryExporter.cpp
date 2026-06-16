#include "MllBinaryExporter.h"

#include "../Compression/Aklz.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace spice::mll {
namespace {

constexpr std::uint32_t kMllHeaderWord0 = 0x0000ffffU;
constexpr std::uint32_t kMllCountLowWord = 0xffffU;
constexpr std::uint32_t kMllRecordsOffset = 0x08U;
constexpr std::uint32_t kMllRecordStride = 0x20U;
constexpr std::uint32_t kMllRawWord1cSentinel = 0xffffffffU;

[[nodiscard]] bool canReadRange(const std::size_t size, const std::uint32_t offset, const std::uint32_t length) {
    return offset <= size && length <= size - offset;
}

void writeU32Be(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint32_t value) {
    if (offset > bytes.size() || 4U > bytes.size() - offset) {
        throw std::runtime_error("MLL export failed: write offset is out of bounds");
    }
    bytes[offset + 0U] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value & 0xffU);
}

[[nodiscard]] std::uint32_t checkedU32Size(const std::size_t size, const char* what) {
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(std::string("MLL export failed: ") + what + " exceeds 32-bit range");
    }
    return static_cast<std::uint32_t>(size);
}

void validateSupportedSource(const MllFile& file, std::span<const std::uint8_t> originalDecodedBytes, const MllExportOptions& options) {
    if (!file.supported) {
        throw std::runtime_error("MLL export failed: parsed MLL is not in the supported normal table shape");
    }
    if (originalDecodedBytes.empty()) {
        throw std::runtime_error("MLL export failed: original decoded bytes are required");
    }
    if (file.decodedSize != originalDecodedBytes.size()) {
        throw std::runtime_error("MLL export failed: decoded byte size does not match parsed metadata");
    }
    if (file.headerWord0 != kMllHeaderWord0) {
        throw std::runtime_error("MLL export failed: unsupported MLL header word");
    }
    if ((file.countWord & 0xffffU) != kMllCountLowWord ||
        (file.countWord >> 16U) != file.selectedMemberCount) {
        throw std::runtime_error("MLL export failed: unsupported MLL count word");
    }
    if (file.memberCountSource != MllMemberCountSource::HeaderU16At04 ||
        file.tableShape != MllTableShape::Normal ||
        file.recordsOffset != kMllRecordsOffset ||
        file.recordStride != kMllRecordStride ||
        file.members.empty()) {
        throw std::runtime_error("MLL export failed: unsupported MLL table metadata");
    }
    if (!canReadRange(originalDecodedBytes.size(), file.recordsOffset, file.memberTableEndOffset - file.recordsOffset)) {
        throw std::runtime_error("MLL export failed: member table is out of bounds");
    }

    std::uint32_t expectedPayloadOffset = file.memberTableEndOffset;
    for (const auto& member : file.members) {
        if (!member.payloadInBounds ||
            !canReadRange(originalDecodedBytes.size(), member.payloadOffset, member.payloadSize)) {
            throw std::runtime_error("MLL export failed: member payload span is invalid");
        }
        if (member.payloadOverlapsMemberTable) {
            throw std::runtime_error("MLL export failed: member payload overlaps the member table");
        }
        if (options.requireRawWord1cSentinel && member.rawWord1c != kMllRawWord1cSentinel) {
            throw std::runtime_error("MLL export failed: member raw word 0x1c is not the known sentinel value");
        }
        if (member.payloadOffset != expectedPayloadOffset) {
            throw std::runtime_error("MLL export failed: source member payloads are not tightly packed");
        }
        const auto nextPayloadOffset = static_cast<std::uint64_t>(expectedPayloadOffset) + member.payloadSize;
        if (nextPayloadOffset > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("MLL export failed: member payload layout exceeds 32-bit range");
        }
        expectedPayloadOffset = static_cast<std::uint32_t>(nextPayloadOffset);
    }
    if (expectedPayloadOffset != originalDecodedBytes.size()) {
        throw std::runtime_error("MLL export failed: source MLL has trailing bytes after the last member payload");
    }
}

[[nodiscard]] std::vector<std::optional<std::span<const std::uint8_t>>> buildReplacementMap(
    const MllFile& file,
    const MllExportOptions& options) {
    std::vector<std::optional<std::span<const std::uint8_t>>> replacements(file.members.size());
    for (const auto& replacement : options.payloadReplacements) {
        if (replacement.memberIndex >= file.members.size()) {
            throw std::runtime_error("MLL export failed: replacement member index is out of range");
        }
        if (replacement.payload.empty() && file.members[replacement.memberIndex].payloadSize != 0U) {
            throw std::runtime_error("MLL export failed: replacement payload is empty");
        }
        if (replacements[replacement.memberIndex].has_value()) {
            throw std::runtime_error("MLL export failed: duplicate payload replacement for member");
        }
        if (!options.allowPayloadResize &&
            replacement.payload.size() != file.members[replacement.memberIndex].payloadSize) {
            throw std::runtime_error("MLL export failed: payload replacement changes size without explicit resize permission");
        }
        replacements[replacement.memberIndex] = std::span<const std::uint8_t>(replacement.payload);
    }
    return replacements;
}

} // namespace

std::vector<std::uint8_t> MllBinaryExporter::exportFile(
    const MllFile& file,
    const MllExportOptions& options) const {
    return exportDecoded(file, file.originalDecodedBytes, options);
}

std::vector<std::uint8_t> MllBinaryExporter::exportDecoded(
    const MllFile& file,
    std::span<const std::uint8_t> originalDecodedBytes,
    const MllExportOptions& options) const {
    validateSupportedSource(file, originalDecodedBytes, options);
    const auto replacements = buildReplacementMap(file, options);

    std::vector<std::span<const std::uint8_t>> payloads{};
    payloads.reserve(file.members.size());

    std::size_t totalPayloadSize = 0U;
    for (std::size_t i = 0; i < file.members.size(); ++i) {
        const auto& member = file.members[i];
        if (replacements[i].has_value()) {
            payloads.push_back(*replacements[i]);
        } else {
            payloads.push_back(originalDecodedBytes.subspan(member.payloadOffset, member.payloadSize));
        }
        if (payloads.back().size() > std::numeric_limits<std::size_t>::max() - totalPayloadSize) {
            throw std::runtime_error("MLL export failed: rebuilt payload size exceeds addressable range");
        }
        totalPayloadSize += payloads.back().size();
    }

    if (totalPayloadSize > std::numeric_limits<std::size_t>::max() - file.memberTableEndOffset) {
        throw std::runtime_error("MLL export failed: rebuilt decoded size exceeds addressable range");
    }
    const auto outputSize = static_cast<std::size_t>(file.memberTableEndOffset) + totalPayloadSize;
    const auto checkedOutputSize = checkedU32Size(outputSize, "rebuilt decoded size");

    std::vector<std::uint8_t> out{};
    out.reserve(checkedOutputSize);
    out.insert(out.end(),
        originalDecodedBytes.begin(),
        originalDecodedBytes.begin() + static_cast<std::ptrdiff_t>(file.memberTableEndOffset));

    std::uint32_t payloadCursor = file.memberTableEndOffset;
    for (std::size_t i = 0; i < file.members.size(); ++i) {
        const auto& member = file.members[i];
        const auto payloadSize = checkedU32Size(payloads[i].size(), "member payload");
        writeU32Be(out, static_cast<std::size_t>(member.recordOffset) + 0x14U, payloadCursor);
        writeU32Be(out, static_cast<std::size_t>(member.recordOffset) + 0x18U, payloadSize);
        writeU32Be(out, static_cast<std::size_t>(member.recordOffset) + 0x1CU, member.rawWord1c);
        out.insert(out.end(), payloads[i].begin(), payloads[i].end());
        payloadCursor = checkedU32Size(out.size(), "rebuilt decoded size");
    }

    if (!options.compressAklz) {
        return out;
    }

    auto compressed = spice::compression::aklz::compress(out);
    if (!compressed.ok()) {
        throw std::runtime_error("MLL AKLZ compression failed: " +
            std::string(spice::compression::aklz::errorToString(compressed.error)));
    }
    return std::move(compressed.bytes);
}

} // namespace spice::mll
