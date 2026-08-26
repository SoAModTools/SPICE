#include "PvrDecoder.h"

#include <algorithm>
#include <array>
#include <limits>

namespace spice::pvm::decoding {
namespace {

using model::DataLayout;
using model::DiagnosticSeverity;
using model::ParseStatus;
using model::PixelFormat;

struct PhysicalLevel {
    std::uint32_t size = 0;
    std::size_t offset = 0;
    std::size_t byteSize = 0;
};

bool checkedAdd(const std::size_t left, const std::size_t right, std::size_t& result)
{
    if (right > std::numeric_limits<std::size_t>::max() - left)
        return false;
    result = left + right;
    return true;
}

bool checkedMul(const std::size_t left, const std::size_t right, std::size_t& result)
{
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

bool isPowerOfTwo(const std::uint32_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

void error(model::DecodeResult& result, const std::size_t offset, std::string message)
{
    result.diagnostics.push_back({DiagnosticSeverity::Error, offset, std::move(message)});
}

bool validatePayloadSize(
    const model::PvrTexture& texture,
    const std::size_t localDataOffset,
    const std::size_t expectedPayload,
    model::DecodeResult& result)
{
    if (texture.textureDataRange.size == expectedPayload)
        return true;

    std::size_t alignedPayload = 0;
    if (!checkedAdd(expectedPayload, 31, alignedPayload))
        return false;
    alignedPayload &= ~std::size_t{31};
    if (texture.textureDataRange.size != alignedPayload || alignedPayload == expectedPayload)
        return false;

    const std::size_t paddingSize = alignedPayload - expectedPayload;
    const auto paddingBegin = texture.sourceBytes.begin() +
        static_cast<std::ptrdiff_t>(localDataOffset + expectedPayload);
    if (!std::all_of(paddingBegin, paddingBegin + static_cast<std::ptrdiff_t>(paddingSize),
            [](const std::uint8_t value) { return value == 0; }))
        return false;

    result.trailingPaddingRange = model::ByteRange{
        texture.textureDataRange.offset + expectedPayload, paddingSize};
    return true;
}

std::size_t twiddle(const std::uint32_t value)
{
    std::size_t result = 0;
    for (unsigned bit = 0; bit < 16; ++bit)
        result |= static_cast<std::size_t>((value >> bit) & 1U) << (bit * 2U);
    return result;
}

std::array<std::uint8_t, 4> decodeColor(const std::uint16_t value, const PixelFormat format)
{
    switch (format) {
    case PixelFormat::Argb1555:
        return {
            static_cast<std::uint8_t>(((value >> 10) & 0x1F) * 255 / 31),
            static_cast<std::uint8_t>(((value >> 5) & 0x1F) * 255 / 31),
            static_cast<std::uint8_t>((value & 0x1F) * 255 / 31),
            static_cast<std::uint8_t>((value & 0x8000) != 0 ? 255 : 0),
        };
    case PixelFormat::Rgb565:
        return {
            static_cast<std::uint8_t>(((value >> 11) & 0x1F) * 255 / 31),
            static_cast<std::uint8_t>(((value >> 5) & 0x3F) * 255 / 63),
            static_cast<std::uint8_t>((value & 0x1F) * 255 / 31),
            255,
        };
    case PixelFormat::Argb4444:
        return {
            static_cast<std::uint8_t>(((value >> 8) & 0x0F) * 17),
            static_cast<std::uint8_t>(((value >> 4) & 0x0F) * 17),
            static_cast<std::uint8_t>((value & 0x0F) * 17),
            static_cast<std::uint8_t>(((value >> 12) & 0x0F) * 17),
        };
    default:
        return {};
    }
}

std::uint16_t readU16(const std::vector<std::uint8_t>& bytes, const std::size_t offset)
{
    return static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
}

void writePixel(model::RgbaImage& image, const std::uint32_t x, const std::uint32_t y,
    const std::array<std::uint8_t, 4>& color)
{
    const std::size_t destination =
        (static_cast<std::size_t>(y) * image.width + x) * 4;
    std::copy(color.begin(), color.end(), image.pixels.begin() + static_cast<std::ptrdiff_t>(destination));
}

std::size_t smallVqCodebookEntries(const std::uint32_t size, const bool mipmapped)
{
    if (mipmapped) {
        if (size <= 16) return 16;
        if (size <= 32) return 64;
        if (size <= 64) return 256;
        return 0;
    }
    if (size <= 16) return 16;
    if (size <= 32) return 32;
    if (size <= 64) return 128;
    return 0;
}

bool isMipmapped(const DataLayout layout)
{
    return layout == DataLayout::TwiddledMipmaps ||
        layout == DataLayout::VqMipmaps ||
        layout == DataLayout::SmallVqMipmaps ||
        layout == DataLayout::TwiddledMipmapsDma;
}

bool isVq(const DataLayout layout)
{
    return layout == DataLayout::Vq || layout == DataLayout::VqMipmaps ||
        layout == DataLayout::SmallVq || layout == DataLayout::SmallVqMipmaps;
}

bool isTwiddled(const DataLayout layout)
{
    return layout == DataLayout::Twiddled || layout == DataLayout::TwiddledMipmaps ||
        layout == DataLayout::TwiddledMipmapsDma;
}

bool levelByteSize(const DataLayout layout, const std::uint32_t size, std::size_t& bytes)
{
    std::size_t pixels = 0;
    if (!checkedMul(size, size, pixels))
        return false;
    if (isVq(layout)) {
        bytes = std::max<std::size_t>(pixels / 4, 1);
        return true;
    }
    return checkedMul(pixels, 2, bytes);
}

bool buildPhysicalLevels(
    const DataLayout layout,
    const std::uint32_t baseSize,
    const std::size_t prefixSize,
    std::vector<PhysicalLevel>& levels,
    std::size_t& totalSize)
{
    totalSize = prefixSize;
    if (!isMipmapped(layout)) {
        std::size_t byteSize = 0;
        if (!levelByteSize(layout, baseSize, byteSize))
            return false;
        levels.push_back({baseSize, 0, byteSize});
        totalSize = byteSize;
        return true;
    }

    for (std::uint32_t size = 1;; size <<= 1) {
        std::size_t byteSize = 0;
        if (!levelByteSize(layout, size, byteSize))
            return false;
        levels.push_back({size, totalSize, byteSize});
        if (!checkedAdd(totalSize, byteSize, totalSize))
            return false;
        if (size == baseSize)
            break;
        if (size > baseSize / 2)
            return false;
    }
    return true;
}

model::RgbaImage decodeDirectLevel(
    const model::PvrTexture& texture,
    const std::size_t localDataOffset,
    const PhysicalLevel& level)
{
    model::RgbaImage image;
    image.width = level.size;
    image.height = level.size;
    image.pixels.resize(static_cast<std::size_t>(level.size) * level.size * 4);

    for (std::uint32_t y = 0; y < level.size; ++y) {
        for (std::uint32_t x = 0; x < level.size; ++x) {
            const std::size_t sourcePixel = isTwiddled(texture.dataLayout)
                ? ((twiddle(x) << 1) | twiddle(y))
                : (static_cast<std::size_t>(y) * level.size + x);
            const std::size_t source = localDataOffset + level.offset + sourcePixel * 2;
            writePixel(image, x, y, decodeColor(readU16(texture.sourceBytes, source), texture.pixelFormat));
        }
    }
    return image;
}

bool decodeVqLevel(
    const model::PvrTexture& texture,
    const std::size_t localCodebookOffset,
    const std::size_t localIndexOffset,
    const std::size_t codebookEntries,
    const PhysicalLevel& level,
    model::RgbaImage& image,
    model::DecodeResult& result)
{
    image.width = level.size;
    image.height = level.size;
    image.pixels.resize(static_cast<std::size_t>(level.size) * level.size * 4);

    if (level.size == 1) {
        const std::uint8_t index = texture.sourceBytes[localIndexOffset + level.offset];
        if (index >= codebookEntries) {
            error(result, texture.textureDataRange.offset +
                    (localIndexOffset - (texture.textureDataRange.offset - texture.sourceRange.offset)) + level.offset,
                "VQ index is outside the available codebook");
            return false;
        }
        writePixel(image, 0, 0,
            decodeColor(readU16(texture.sourceBytes, localCodebookOffset + index * 8), texture.pixelFormat));
        return true;
    }

    for (std::uint32_t y = 0; y < level.size; y += 2) {
        for (std::uint32_t x = 0; x < level.size; x += 2) {
            const std::size_t indexPosition = (twiddle(x >> 1) << 1) | twiddle(y >> 1);
            const std::uint8_t index = texture.sourceBytes[localIndexOffset + level.offset + indexPosition];
            if (index >= codebookEntries) {
                error(result, texture.textureDataRange.offset +
                        (localIndexOffset - (texture.textureDataRange.offset - texture.sourceRange.offset)) +
                        level.offset + indexPosition,
                    "VQ index is outside the available codebook");
                return false;
            }
            const std::size_t vectorOffset = localCodebookOffset + index * 8;
            for (std::uint32_t blockX = 0; blockX < 2; ++blockX) {
                for (std::uint32_t blockY = 0; blockY < 2; ++blockY) {
                    const std::size_t colorIndex = blockX * 2 + blockY;
                    writePixel(image, x + blockX, y + blockY,
                        decodeColor(readU16(texture.sourceBytes, vectorOffset + colorIndex * 2),
                            texture.pixelFormat));
                }
            }
        }
    }
    return true;
}

} // namespace

model::DecodeResult decodePvrTexture(const model::PvrTexture& texture)
{
    model::DecodeResult result;
    if (texture.status == ParseStatus::Failed) {
        error(result, texture.sourceRange.offset, "Cannot decode a PVR texture whose parse failed");
        return result;
    }
    if (texture.pixelFormat == PixelFormat::Unknown) {
        error(result, texture.pvrtRange.offset + 8, "Unsupported PVR pixel format");
        return result;
    }
    if (texture.dataLayout == DataLayout::Unknown) {
        error(result, texture.pvrtRange.offset + 9, "Unsupported PVR data layout");
        return result;
    }
    if (texture.width == 0 || texture.height == 0) {
        error(result, texture.pvrtRange.offset + 12, "PVR dimensions must be non-zero");
        return result;
    }

    if (texture.dataLayout != DataLayout::Rectangle) {
        if (texture.width != texture.height) {
            error(result, texture.pvrtRange.offset + 12,
                "Twiddled and VQ PVR textures must be square");
            return result;
        }
        if (!isPowerOfTwo(texture.width)) {
            error(result, texture.pvrtRange.offset + 12,
                "Twiddled and VQ PVR dimensions must be powers of two");
            return result;
        }
    }
    if (isVq(texture.dataLayout) && texture.width < 2 && !isMipmapped(texture.dataLayout)) {
        error(result, texture.pvrtRange.offset + 12, "A non-mipmapped VQ texture must be at least 2x2");
        return result;
    }

    const std::size_t localDataOffset = texture.textureDataRange.offset - texture.sourceRange.offset;
    if (localDataOffset > texture.sourceBytes.size() ||
        texture.textureDataRange.size > texture.sourceBytes.size() - localDataOffset) {
        error(result, texture.textureDataRange.offset,
            "Retained PVR source bytes do not cover the declared texture payload");
        return result;
    }

    if (texture.dataLayout == DataLayout::Rectangle) {
        std::size_t pixels = 0;
        std::size_t expected = 0;
        if (!checkedMul(texture.width, texture.height, pixels) || !checkedMul(pixels, 2, expected)) {
            error(result, texture.pvrtRange.offset + 12, "Rectangle PVR dimensions overflow decoded size");
            return result;
        }
        if (!validatePayloadSize(texture, localDataOffset, expected, result)) {
            error(result, texture.textureDataRange.offset,
                "Rectangle PVR payload size is inconsistent with its dimensions");
            return result;
        }
        model::PvrMipLevel level;
        level.width = texture.width;
        level.height = texture.height;
        level.sourceRange = texture.textureDataRange;
        level.image.width = texture.width;
        level.image.height = texture.height;
        level.image.pixels.resize(pixels * 4);
        for (std::uint32_t y = 0; y < texture.height; ++y) {
            for (std::uint32_t x = 0; x < texture.width; ++x) {
                const std::size_t source = localDataOffset +
                    (static_cast<std::size_t>(y) * texture.width + x) * 2;
                writePixel(level.image, x, y,
                    decodeColor(readU16(texture.sourceBytes, source), texture.pixelFormat));
            }
        }
        result.codebookRange = {texture.textureDataRange.offset, 0};
        result.mipLevels.push_back(std::move(level));
        result.status = ParseStatus::Complete;
        return result;
    }

    std::size_t codebookEntries = 0;
    std::size_t codebookBytes = 0;
    if (texture.dataLayout == DataLayout::Vq || texture.dataLayout == DataLayout::VqMipmaps)
        codebookEntries = 256;
    else if (texture.dataLayout == DataLayout::SmallVq ||
             texture.dataLayout == DataLayout::SmallVqMipmaps)
        codebookEntries = smallVqCodebookEntries(texture.width, isMipmapped(texture.dataLayout));
    if (isVq(texture.dataLayout)) {
        if (codebookEntries == 0) {
            error(result, texture.pvrtRange.offset + 12,
                "Small VQ dimensions are outside the Dreamcast SDK codebook rules");
            return result;
        }
        codebookBytes = codebookEntries * 8;
        if (texture.textureDataRange.size < codebookBytes) {
            error(result, texture.textureDataRange.offset, "PVR VQ codebook is truncated");
            return result;
        }
    }

    std::size_t prefixSize = 0;
    if (texture.dataLayout == DataLayout::TwiddledMipmaps)
        prefixSize = 2;
    else if (texture.dataLayout == DataLayout::TwiddledMipmapsDma)
        prefixSize = 6;

    std::vector<PhysicalLevel> physicalLevels;
    std::size_t streamSize = 0;
    if (!buildPhysicalLevels(texture.dataLayout, texture.width, prefixSize, physicalLevels, streamSize)) {
        error(result, texture.pvrtRange.offset + 12, "PVR mip-chain size overflows addressable input");
        return result;
    }
    std::size_t expectedPayload = 0;
    if (!checkedAdd(codebookBytes, streamSize, expectedPayload) ||
        !validatePayloadSize(texture, localDataOffset, expectedPayload, result)) {
        error(result, texture.textureDataRange.offset,
            "PVR payload size is inconsistent with the declared layout, dimensions, and mip chain");
        return result;
    }

    result.codebookRange = {texture.textureDataRange.offset, codebookBytes};
    const std::size_t localIndexOffset = localDataOffset + codebookBytes;
    const std::size_t globalIndexOffset = texture.textureDataRange.offset + codebookBytes;

    result.mipLevels.reserve(physicalLevels.size());
    for (auto iterator = physicalLevels.rbegin(); iterator != physicalLevels.rend(); ++iterator) {
        model::PvrMipLevel level;
        level.width = iterator->size;
        level.height = iterator->size;
        level.sourceRange = {globalIndexOffset + iterator->offset, iterator->byteSize};
        if (isVq(texture.dataLayout)) {
            if (!decodeVqLevel(texture, localDataOffset, localIndexOffset,
                    codebookEntries, *iterator, level.image, result)) {
                result.mipLevels.clear();
                return result;
            }
        } else {
            level.image = decodeDirectLevel(texture, localDataOffset, *iterator);
        }
        result.mipLevels.push_back(std::move(level));
    }
    result.status = ParseStatus::Complete;
    return result;
}

} // namespace spice::pvm::decoding
