#pragma once

#include "MlkModel.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace spice::mlk {

struct MlkRecord {
    std::size_t index{ 0U };
    std::uint32_t recordOffset{ 0U };
    std::uint32_t key{ 0U };
    std::uint32_t payloadOffset{ 0U };
    std::uint32_t payloadSize{ 0U };
    std::uint32_t rawWord12{ 0U };
    bool payloadInBounds{ false };
    bool payloadOverlapsRecordTable{ false };
    bool duplicateKey{ false };
    MlkPayloadKind payloadKind{ MlkPayloadKind::Unknown };
    std::string payloadSignature{};
    MlkEmbeddedMldHeaderProbe embeddedMldHeader{};
};

struct MlkFile {
    std::string sourcePath{};
    bool sourceWasCompressedAklz{ false };
    std::uint32_t rawSize{ 0U };
    std::uint32_t decodedSize{ 0U };
    std::array<std::uint32_t, 4> headerWords{};
    std::int16_t runtimeRecordCount{ 0 };
    std::uint16_t rawRecordCountCandidate{ 0U };
    std::uint16_t selectedRecordCount{ 0U };
    MlkRecordCountSource recordCountSource{ MlkRecordCountSource::Unresolved };
    MlkTableShape tableShape{ MlkTableShape::Normal };
    std::uint32_t recordsOffset{ 0x08U };
    std::uint32_t recordStride{ 0x10U };
    std::uint32_t recordTableEndOffset{ 0U };
    std::uint32_t firstPayloadOffset{ 0U };
    std::uint16_t recordCountInferredFromFirstPayloadOffset{ 0U };
    bool recordCountMatchesFirstPayloadOffset{ false };
    bool recordTableInBounds{ false };
    bool supported{ false };
    std::vector<MlkRecord> records{};
    std::vector<MlkDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const;
};

class MlkParser {
public:
    [[nodiscard]] static MlkFile parse(std::span<const std::uint8_t> bytes,
        std::string sourcePath = {});

    [[nodiscard]] static MlkFile parseFile(const std::filesystem::path& path);
};

[[nodiscard]] MlkTableShape classifyMlkTableShape(const MlkScanResult& scan);

} // namespace spice::mlk
