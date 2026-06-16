#pragma once

#include "MllModel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace spice::mll {

struct MllPayloadSpanIr {
    std::uint32_t offset{ 0U };
    std::uint32_t size{ 0U };
    bool inBounds{ false };
};

struct MllContainerHeaderIr {
    std::uint32_t headerWord0{ 0U };
    std::uint32_t countWord{ 0U };
    std::uint16_t memberCount{ 0U };
    MllMemberCountSource memberCountSource{ MllMemberCountSource::Unresolved };
    std::uint32_t recordsOffset{ 0x08U };
    std::uint32_t recordStride{ 0x20U };
    std::uint32_t memberTableEndOffset{ 0U };
    MllTableShape tableShape{ MllTableShape::Normal };
    bool supported{ false };
};

struct MllMemberRecordIr {
    std::uint32_t recordOffset{ 0U };
    std::array<std::uint8_t, 0x14U> rawName{};
    std::uint32_t payloadOffset{ 0U };
    std::uint32_t payloadSize{ 0U };
    std::uint32_t rawWord1c{ 0U };
};

struct MllMemberProbeSummaryIr {
    std::string payloadSignature{};
    bool mldHeaderPlausible{ false };
    bool indexedBinTablePresent{ false };
    bool textureTablePresent{ false };
    std::uint32_t textureCount{ 0U };
    bool allTexturesParsed{ false };
    bool allTexturesDecoded{ false };
    bool allTexturesHaveGlobalIndex{ false };
    bool preTextureTablePresent{ false };
};

struct MllMemberIr {
    std::size_t index{ 0U };
    std::string displayName{};
    MllMemberRecordIr record{};
    MllPayloadSpanIr payload{};
    MllPayloadKind payloadKind{ MllPayloadKind::Unknown };
    MllMemberProbeSummaryIr probeSummary{};
};

struct MllArchiveIr {
    std::string sourcePath{};
    bool sourceWasCompressedAklz{ false };
    std::uint32_t rawSize{ 0U };
    std::uint32_t decodedSize{ 0U };
    MllContainerHeaderIr header{};
    std::vector<std::uint8_t> decodedBytes{};
    std::vector<MllMemberIr> members{};
    std::vector<MllDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const;
    [[nodiscard]] std::span<const std::uint8_t> payloadBytes(std::size_t memberIndex) const;
};

class MllArchiveIrBuilder {
public:
    [[nodiscard]] MllArchiveIr build(const MllFile& file) const;
};

} // namespace spice::mll
