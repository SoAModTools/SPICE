#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace spice::mlk {

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error,
};

struct MlkDiagnostic {
    DiagnosticSeverity severity{ DiagnosticSeverity::Info };
    std::string message{};
    std::uint32_t offset{ 0U };
};

enum class MlkPayloadKind {
    Empty,
    Unknown,
    AklzCompressed,
    MldFile,
    NinjaChunk,
    Pof0,
};

enum class MlkRecordCountSource {
    HeaderU16At04,
    FirstPayloadOffset,
    Unresolved,
};

struct MlkEmbeddedMldHeaderProbe {
    bool plausible{ false };
    std::uint32_t entryCount{ 0U };
    std::uint32_t indexTableOffset{ 0U };
    std::uint32_t functionParametersOffset{ 0U };
    std::uint32_t realDataOffset{ 0U };
    std::uint32_t textureTableOffset{ 0U };
};

struct MlkRecordProbe {
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

struct MlkScanResult {
    std::string sourcePath{};
    bool sourceWasCompressedAklz{ false };
    std::uint32_t rawSize{ 0U };
    std::uint32_t decodedSize{ 0U };
    std::array<std::uint32_t, 4> headerWords{};
    std::int16_t signedRecordCountCandidate{ 0 };
    std::uint16_t recordCountCandidate{ 0U };
    std::uint16_t selectedRecordCount{ 0U };
    MlkRecordCountSource recordCountSource{ MlkRecordCountSource::Unresolved };
    std::uint32_t recordsOffset{ 0x08U };
    std::uint32_t recordStride{ 0x10U };
    std::uint32_t recordTableEndOffset{ 0U };
    std::uint32_t firstPayloadOffset{ 0U };
    std::uint16_t recordCountInferredFromFirstPayloadOffset{ 0U };
    bool recordCountMatchesFirstPayloadOffset{ false };
    bool recordTableInBounds{ false };
    std::vector<MlkRecordProbe> records{};
    std::vector<MlkDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const;
};

[[nodiscard]] const char* toString(DiagnosticSeverity severity);
[[nodiscard]] const char* toString(MlkPayloadKind kind);
[[nodiscard]] const char* toString(MlkRecordCountSource source);

} // namespace spice::mlk
