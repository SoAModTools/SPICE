#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace spice::pvm::model {

enum class ParseStatus {
    Complete,
    Partial,
    Failed,
};

enum class DiagnosticSeverity {
    Information,
    Warning,
    Error,
};

struct ByteRange {
    std::size_t offset = 0;
    std::size_t size = 0;

    [[nodiscard]] std::size_t end() const noexcept { return offset + size; }
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::size_t offset = 0;
    std::string message;
};

enum class PixelFormat : std::uint8_t {
    Argb1555 = 0x00,
    Rgb565 = 0x01,
    Argb4444 = 0x02,
    Unknown = 0xFF,
};

enum class DataLayout : std::uint8_t {
    Twiddled = 0x01,
    TwiddledMipmaps = 0x02,
    Vq = 0x03,
    VqMipmaps = 0x04,
    Rectangle = 0x09,
    SmallVq = 0x10,
    SmallVqMipmaps = 0x11,
    TwiddledMipmapsDma = 0x12,
    Unknown = 0xFF,
};

struct RgbaImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;
};

struct PvrMipLevel {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ByteRange sourceRange;
    RgbaImage image;
};

struct PvrTexture {
    ParseStatus status = ParseStatus::Failed;
    ByteRange sourceRange;
    std::optional<ByteRange> gbixRange;
    ByteRange pvrtRange;
    ByteRange textureDataRange;
    std::optional<std::uint32_t> globalIndex;
    std::uint8_t rawPixelFormat = 0;
    std::uint8_t rawDataLayout = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    PixelFormat pixelFormat = PixelFormat::Unknown;
    DataLayout dataLayout = DataLayout::Unknown;
    std::vector<std::uint8_t> pvrtUnknownHeader;
    std::vector<std::uint8_t> sourceBytes;
    std::vector<Diagnostic> diagnostics;
};

struct PvrScanResult {
    ParseStatus status = ParseStatus::Failed;
    std::vector<PvrTexture> textures;
    std::vector<Diagnostic> diagnostics;
};

struct PvmEntry {
    std::uint16_t archiveIndex = 0;
    std::string name;
    std::vector<std::uint8_t> rawName;
    std::optional<std::uint8_t> rawPixelFormat;
    std::optional<std::uint8_t> rawDataLayout;
    std::optional<std::uint16_t> rawDimensions;
    std::optional<std::uint32_t> globalIndex;
    std::optional<std::uint16_t> declaredWidth;
    std::optional<std::uint16_t> declaredHeight;
    ByteRange metadataRange;
    std::vector<std::uint8_t> unknownMetadata;
    std::optional<PvrTexture> texture;
    std::vector<Diagnostic> diagnostics;
};

struct PvmArchive {
    ParseStatus status = ParseStatus::Failed;
    ByteRange sourceRange;
    ByteRange pvmhRange;
    std::uint16_t flags = 0;
    std::uint16_t declaredTextureCount = 0;
    std::vector<std::uint8_t> headerPadding;
    std::vector<std::uint8_t> interstitialMetadata;
    std::vector<std::uint8_t> sourceBytes;
    std::vector<PvmEntry> entries;
    std::vector<PvrTexture> unpairedTextures;
    std::vector<Diagnostic> diagnostics;
};

struct DecodeResult {
    ParseStatus status = ParseStatus::Failed;
    ByteRange codebookRange;
    std::optional<ByteRange> trailingPaddingRange;
    std::vector<PvrMipLevel> mipLevels;
    std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] PixelFormat pixelFormatFromRaw(std::uint8_t raw) noexcept;
[[nodiscard]] DataLayout dataLayoutFromRaw(std::uint8_t raw) noexcept;

} // namespace spice::pvm::model
