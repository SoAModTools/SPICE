#include "MllArchiveIr.h"

#include <algorithm>
#include <stdexcept>

namespace spice::mll {
namespace {

constexpr std::uint32_t kMllNameLength = 0x14U;

[[nodiscard]] bool canReadRange(const std::size_t size, const std::uint32_t offset, const std::uint32_t length) {
    return offset <= size && length <= size - offset;
}

[[nodiscard]] MllMemberRecordIr buildRecordIr(const MllMember& member, std::span<const std::uint8_t> decodedBytes) {
    MllMemberRecordIr record{};
    record.recordOffset = member.recordOffset;
    record.payloadOffset = member.payloadOffset;
    record.payloadSize = member.payloadSize;
    record.rawWord1c = member.rawWord1c;

    if (canReadRange(decodedBytes.size(), member.recordOffset, kMllNameLength)) {
        std::copy_n(decodedBytes.begin() + static_cast<std::ptrdiff_t>(member.recordOffset),
            record.rawName.size(),
            record.rawName.begin());
    }
    return record;
}

[[nodiscard]] MllMemberProbeSummaryIr buildProbeSummaryIr(const MllMember& member) {
    MllMemberProbeSummaryIr summary{};
    summary.payloadSignature = member.payloadSignature;
    summary.mldHeaderPlausible = member.embeddedMldHeader.plausible;
    summary.indexedBinTablePresent = member.indexedBinTableProbe.present;
    summary.textureTablePresent = member.textureTableProbe.hasTextures;
    summary.textureCount = member.textureTableProbe.textureCount;
    summary.allTexturesParsed = member.textureTableProbe.allTexturesParsed;
    summary.allTexturesDecoded = member.textureTableProbe.allTexturesDecoded;
    summary.allTexturesHaveGlobalIndex = member.textureTableProbe.allTexturesHaveGlobalIndex;
    summary.preTextureTablePresent = member.preTextureTableProbe.present;
    return summary;
}

} // namespace

bool MllArchiveIr::ok() const {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const MllDiagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

std::span<const std::uint8_t> MllArchiveIr::payloadBytes(const std::size_t memberIndex) const {
    if (memberIndex >= members.size()) {
        throw std::out_of_range("MLL archive IR payload member index is out of range");
    }
    const auto& payload = members[memberIndex].payload;
    if (!payload.inBounds || !canReadRange(decodedBytes.size(), payload.offset, payload.size)) {
        throw std::out_of_range("MLL archive IR payload span is out of bounds");
    }
    return std::span<const std::uint8_t>(decodedBytes).subspan(payload.offset, payload.size);
}

MllArchiveIr MllArchiveIrBuilder::build(const MllFile& file) const {
    MllArchiveIr ir{};
    ir.sourcePath = file.sourcePath;
    ir.sourceWasCompressedAklz = file.sourceWasCompressedAklz;
    ir.rawSize = file.rawSize;
    ir.decodedSize = file.decodedSize;
    ir.decodedBytes = file.originalDecodedBytes;
    ir.diagnostics = file.diagnostics;

    ir.header.headerWord0 = file.headerWord0;
    ir.header.countWord = file.countWord;
    ir.header.memberCount = file.selectedMemberCount;
    ir.header.memberCountSource = file.memberCountSource;
    ir.header.recordsOffset = file.recordsOffset;
    ir.header.recordStride = file.recordStride;
    ir.header.memberTableEndOffset = file.memberTableEndOffset;
    ir.header.tableShape = file.tableShape;
    ir.header.supported = file.supported;

    ir.members.reserve(file.members.size());
    for (const auto& member : file.members) {
        MllMemberIr memberIr{};
        memberIr.index = member.index;
        memberIr.displayName = member.name;
        memberIr.record = buildRecordIr(member, ir.decodedBytes);
        memberIr.payload = MllPayloadSpanIr{
            member.payloadOffset,
            member.payloadSize,
            member.payloadInBounds,
        };
        memberIr.payloadKind = member.payloadKind;
        memberIr.probeSummary = buildProbeSummaryIr(member);
        ir.members.push_back(std::move(memberIr));
    }
    return ir;
}

} // namespace spice::mll
