#include "SpicePvm/SpicePvm.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {

using spice::pvm::model::DataLayout;
using spice::pvm::model::ParseStatus;
using spice::pvm::model::PixelFormat;

void appendU16(std::vector<std::uint8_t>& bytes, const std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}

void appendU32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
}

void writeU16(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint16_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void writeU32(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint32_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

std::size_t twiddle(const std::uint32_t value)
{
    std::size_t result = 0;
    for (unsigned bit = 0; bit < 16; ++bit)
        result |= static_cast<std::size_t>((value >> bit) & 1U) << (bit * 2U);
    return result;
}

std::uint16_t sampleWord(const PixelFormat format, const unsigned sample)
{
    switch (format) {
    case PixelFormat::Argb1555:
        return static_cast<std::uint16_t>(0x8000U |
            ((sample * 3U & 31U) << 10) | ((sample * 5U & 31U) << 5) | (sample * 7U & 31U));
    case PixelFormat::Rgb565:
        return static_cast<std::uint16_t>(((sample * 3U & 31U) << 11) |
            ((sample * 5U & 63U) << 5) | (sample * 7U & 31U));
    case PixelFormat::Argb4444:
        return static_cast<std::uint16_t>(((sample + 1U & 15U) << 12) |
            ((sample * 3U & 15U) << 8) | ((sample * 5U & 15U) << 4) | (sample * 7U & 15U));
    default:
        return 0;
    }
}

void appendWord(std::vector<std::uint8_t>& bytes, const std::uint16_t value)
{
    appendU16(bytes, value);
}

std::vector<std::uint8_t> directLevel(const PixelFormat format, const std::uint32_t size, const bool twiddled)
{
    std::vector<std::uint8_t> result(static_cast<std::size_t>(size) * size * 2);
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const std::size_t index = twiddled
                ? ((twiddle(x) << 1) | twiddle(y))
                : (static_cast<std::size_t>(y) * size + x);
            writeU16(result, index * 2, sampleWord(format, x + y * size + size));
        }
    }
    return result;
}

std::vector<std::uint8_t> rectangleTwiddledLevel(
    const PixelFormat format, const std::uint32_t width, const std::uint32_t height)
{
    const auto tileSize = std::min(width, height);
    const auto tilesPerRow = width / tileSize;
    std::vector<std::uint8_t> result(static_cast<std::size_t>(width) * height * 2U);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto tileIndex = static_cast<std::size_t>(y / tileSize) * tilesPerRow + x / tileSize;
            const auto withinTile = (twiddle(x % tileSize) << 1U) | twiddle(y % tileSize);
            const auto index = tileIndex * static_cast<std::size_t>(tileSize) * tileSize + withinTile;
            writeU16(result, index * 2U, sampleWord(format, x + y * width + 1U));
        }
    }
    return result;
}

std::size_t smallCodebookEntries(const std::uint32_t size, const bool mipmapped)
{
    if (mipmapped) {
        if (size <= 16) return 16;
        if (size <= 32) return 64;
        return 256;
    }
    if (size <= 16) return 16;
    if (size <= 32) return 32;
    return 128;
}

std::vector<std::uint8_t> vqPayload(
    const PixelFormat format,
    const std::uint32_t baseSize,
    const bool mipmapped,
    const bool small)
{
    const std::size_t entries = small ? smallCodebookEntries(baseSize, mipmapped) : 256;
    std::vector<std::uint8_t> result;
    result.reserve(entries * 8 + static_cast<std::size_t>(baseSize) * baseSize / 4 + 16);
    for (std::size_t entry = 0; entry < entries; ++entry) {
        for (unsigned color = 0; color < 4; ++color)
            appendWord(result, sampleWord(format, static_cast<unsigned>(entry * 4 + color + 1)));
    }

    auto appendIndices = [&](const std::uint32_t size) {
        const std::size_t count = std::max<std::size_t>(static_cast<std::size_t>(size) * size / 4, 1);
        const std::size_t offset = result.size();
        result.resize(offset + count);
        if (size == 1) {
            result[offset] = 0;
            return;
        }
        for (std::uint32_t y = 0; y < size; y += 2) {
            for (std::uint32_t x = 0; x < size; x += 2) {
                const std::size_t position = (twiddle(x >> 1) << 1) | twiddle(y >> 1);
                result[offset + position] = static_cast<std::uint8_t>((x / 2 + y / 2 * (size / 2)) % entries);
            }
        }
    };

    if (mipmapped) {
        for (std::uint32_t size = 1;; size <<= 1) {
            appendIndices(size);
            if (size == baseSize) break;
        }
    } else {
        appendIndices(baseSize);
    }
    return result;
}

std::vector<std::uint8_t> payloadFor(
    const PixelFormat format,
    const DataLayout layout,
    const std::uint32_t width,
    const std::uint32_t height)
{
    if (layout == DataLayout::Rectangle) {
        std::vector<std::uint8_t> bytes;
        for (std::uint32_t y = 0; y < height; ++y)
            for (std::uint32_t x = 0; x < width; ++x)
                appendWord(bytes, sampleWord(format, x + y * width + 1));
        return bytes;
    }
    if (layout == DataLayout::RectangleTwiddled)
        return rectangleTwiddledLevel(format, width, height);
    if (layout == DataLayout::Vq || layout == DataLayout::VqMipmaps ||
        layout == DataLayout::SmallVq || layout == DataLayout::SmallVqMipmaps) {
        return vqPayload(format, width,
            layout == DataLayout::VqMipmaps || layout == DataLayout::SmallVqMipmaps,
            layout == DataLayout::SmallVq || layout == DataLayout::SmallVqMipmaps);
    }

    std::vector<std::uint8_t> bytes;
    const bool mipmapped = layout == DataLayout::TwiddledMipmaps ||
        layout == DataLayout::TwiddledMipmapsDma;
    if (mipmapped)
        bytes.resize(layout == DataLayout::TwiddledMipmapsDma ? 6 : 2, 0xDD);
    if (mipmapped) {
        for (std::uint32_t size = 1;; size <<= 1) {
            auto level = directLevel(format, size, true);
            bytes.insert(bytes.end(), level.begin(), level.end());
            if (size == width) break;
        }
    } else {
        bytes = directLevel(format, width, true);
    }
    return bytes;
}

std::vector<std::uint8_t> makePvr(
    const PixelFormat pixelFormat,
    const DataLayout layout,
    const std::uint16_t width,
    const std::uint16_t height,
    const std::vector<std::uint8_t>& payload,
    const std::optional<std::uint32_t> globalIndex = std::nullopt)
{
    std::vector<std::uint8_t> bytes;
    if (globalIndex.has_value()) {
        bytes.insert(bytes.end(), {'G', 'B', 'I', 'X'});
        appendU32(bytes, 8);
        appendU32(bytes, *globalIndex);
        appendU32(bytes, 0);
    }
    bytes.insert(bytes.end(), {'P', 'V', 'R', 'T'});
    appendU32(bytes, static_cast<std::uint32_t>(payload.size() + 8));
    bytes.push_back(static_cast<std::uint8_t>(pixelFormat));
    bytes.push_back(static_cast<std::uint8_t>(layout));
    bytes.push_back(0xA5);
    bytes.push_back(0x5A);
    appendU16(bytes, width);
    appendU16(bytes, height);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

std::array<std::uint8_t, 4> pixelAt(const spice::pvm::model::RgbaImage& image,
    const std::uint32_t x, const std::uint32_t y)
{
    const std::size_t offset = (static_cast<std::size_t>(y) * image.width + x) * 4;
    return {image.pixels[offset], image.pixels[offset + 1], image.pixels[offset + 2], image.pixels[offset + 3]};
}

std::vector<std::uint8_t> makePvm(const std::vector<std::vector<std::uint8_t>>& textures,
    const bool introduceFormatMismatch = false)
{
    constexpr std::uint16_t flags = 0x000F;
    constexpr std::size_t entrySize = 38;
    const std::size_t unpaddedHeaderSize = 12 + entrySize * textures.size();
    const std::size_t paddedHeaderSize = (unpaddedHeaderSize + 15) & ~std::size_t{15};
    std::vector<std::uint8_t> bytes(paddedHeaderSize, 0);
    std::copy_n("PVMH", 4, bytes.begin());
    writeU32(bytes, 4, static_cast<std::uint32_t>(paddedHeaderSize - 8));
    writeU16(bytes, 8, flags);
    writeU16(bytes, 10, static_cast<std::uint16_t>(textures.size()));
    for (std::size_t i = 0; i < textures.size(); ++i) {
        const std::size_t entry = 12 + i * entrySize;
        writeU16(bytes, entry, static_cast<std::uint16_t>(i));
        const std::string name = "texture_" + std::to_string(i);
        std::copy(name.begin(), name.end(), bytes.begin() + static_cast<std::ptrdiff_t>(entry + 2));
        bytes[entry + 30] = static_cast<std::uint8_t>(PixelFormat::Rgb565);
        bytes[entry + 31] = static_cast<std::uint8_t>(DataLayout::Twiddled);
        if (introduceFormatMismatch && i == 0)
            bytes[entry + 31] = static_cast<std::uint8_t>(DataLayout::Rectangle);
        writeU16(bytes, entry + 32, 0x0000); // 4x4
        writeU32(bytes, entry + 34, static_cast<std::uint32_t>(100 + i));
    }
    for (const auto& texture : textures) {
        const std::size_t pvrt = texture[0] == 'G' ? 16 : 0;
        bytes.insert(bytes.end(), texture.begin() + static_cast<std::ptrdiff_t>(pvrt), texture.end());
    }
    return bytes;
}

TEST(SpicePvmParser, ParsesPvrtOnlyAndGbixPvrt)
{
    const auto payload = payloadFor(PixelFormat::Rgb565, DataLayout::Twiddled, 4, 4);
    const auto standalone = makePvr(PixelFormat::Rgb565, DataLayout::Twiddled, 4, 4, payload);
    const auto parsed = spice::pvm::parsing::parsePvrTexture(standalone);
    ASSERT_EQ(parsed.status, ParseStatus::Complete);
    EXPECT_FALSE(parsed.gbixRange.has_value());
    EXPECT_EQ(parsed.pvrtRange.offset, 0U);
    EXPECT_EQ(parsed.textureDataRange.size, 32U);
    EXPECT_EQ(parsed.pvrtUnknownHeader, (std::vector<std::uint8_t>{0xA5, 0x5A}));
    EXPECT_EQ(parsed.sourceBytes, standalone);

    const auto indexed = makePvr(PixelFormat::Rgb565, DataLayout::Twiddled, 4, 4, payload, 0x12345678);
    const auto indexedParsed = spice::pvm::parsing::parsePvrTexture(indexed);
    ASSERT_EQ(indexedParsed.status, ParseStatus::Complete);
    ASSERT_TRUE(indexedParsed.globalIndex.has_value());
    EXPECT_EQ(*indexedParsed.globalIndex, 0x12345678U);
    EXPECT_EQ(indexedParsed.pvrtRange.offset, 16U);
}

TEST(SpicePvmParser, ScansEmbeddedTexturesAndChecksExpectedCount)
{
    std::vector<std::uint8_t> embedded(13, 0xCC);
    const auto one = makePvr(PixelFormat::Rgb565, DataLayout::Twiddled, 4, 4,
        payloadFor(PixelFormat::Rgb565, DataLayout::Twiddled, 4, 4));
    const auto two = makePvr(PixelFormat::Argb4444, DataLayout::Rectangle, 3, 2,
        payloadFor(PixelFormat::Argb4444, DataLayout::Rectangle, 3, 2));
    embedded.insert(embedded.end(), one.begin(), one.end());
    embedded.insert(embedded.end(), 7, 0xEE);
    embedded.insert(embedded.end(), two.begin(), two.end());
    const auto scan = spice::pvm::parsing::scanPvrTextures(embedded, 5, 2);
    ASSERT_EQ(scan.status, ParseStatus::Complete);
    ASSERT_EQ(scan.textures.size(), 2U);
    EXPECT_EQ(scan.textures[0].sourceRange.offset, 13U);
    const auto mismatch = spice::pvm::parsing::scanPvrTextures(embedded, 5, 3);
    EXPECT_EQ(mismatch.status, ParseStatus::Partial);
    EXPECT_EQ(mismatch.textures.size(), 2U);
}

TEST(SpicePvmParser, PreservesUnknownIdentifiersAndRejectsTruncationAndOverflow)
{
    auto unknown = makePvr(PixelFormat::Unknown, DataLayout::Unknown, 4, 4, {});
    unknown[8] = 0x7A;
    unknown[9] = 0x7B;
    const auto parsed = spice::pvm::parsing::parsePvrTexture(unknown);
    EXPECT_EQ(parsed.status, ParseStatus::Partial);
    EXPECT_EQ(parsed.rawPixelFormat, 0x7A);
    EXPECT_EQ(parsed.rawDataLayout, 0x7B);
    EXPECT_EQ(spice::pvm::decoding::decodePvrTexture(parsed).status, ParseStatus::Failed);

    auto truncated = makePvr(PixelFormat::Rgb565, DataLayout::Rectangle, 1, 1, {0, 0});
    truncated.pop_back();
    EXPECT_EQ(spice::pvm::parsing::parsePvrTexture(truncated).status, ParseStatus::Failed);
    std::vector<std::uint8_t> overflow{'P', 'V', 'R', 'T', 0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_EQ(spice::pvm::parsing::parsePvrTexture(overflow).status, ParseStatus::Failed);
}

TEST(SpicePvmParser, ParsesMultiEntryPvmAndDiagnosesCountAndIdentityMismatches)
{
    const auto pvr0 = makePvr(PixelFormat::Rgb565, DataLayout::Twiddled, 4, 4,
        payloadFor(PixelFormat::Rgb565, DataLayout::Twiddled, 4, 4), 100);
    const auto pvr1 = makePvr(PixelFormat::Rgb565, DataLayout::Twiddled, 4, 4,
        payloadFor(PixelFormat::Rgb565, DataLayout::Twiddled, 4, 4), 101);
    const auto bytes = makePvm({pvr0, pvr1});
    const auto archive = spice::pvm::parsing::parsePvmArchive(bytes);
    ASSERT_EQ(archive.status, ParseStatus::Complete);
    ASSERT_EQ(archive.entries.size(), 2U);
    EXPECT_EQ(archive.entries[0].name, "texture_0");
    ASSERT_TRUE(archive.entries[1].texture.has_value());
    EXPECT_EQ(archive.entries[1].texture->width, 4);
    EXPECT_EQ(archive.sourceBytes, bytes);

    auto invalidCount = bytes;
    writeU16(invalidCount, 10, 0xFFFF);
    EXPECT_EQ(spice::pvm::parsing::parsePvmArchive(invalidCount).status, ParseStatus::Failed);
    const auto mismatch = spice::pvm::parsing::parsePvmArchive(makePvm({pvr0}, true));
    EXPECT_EQ(mismatch.status, ParseStatus::Partial);
}

TEST(SpicePvmParser, InterpretsOptionalPvmhFieldsAndPreservesInterstitialMetadata)
{
    const auto pvr = makePvr(PixelFormat::Rgb565, DataLayout::Twiddled, 4, 4,
        payloadFor(PixelFormat::Rgb565, DataLayout::Twiddled, 4, 4));
    std::vector<std::uint8_t> bytes(16, 0);
    std::copy_n("PVMH", 4, bytes.begin());
    writeU32(bytes, 4, 8); // flags/count, one index-only entry, two padding bytes
    writeU16(bytes, 8, 0);
    writeU16(bytes, 10, 1);
    writeU16(bytes, 12, 7);
    bytes.insert(bytes.end(), {'C', 'O', 'M', 'M', 0, 0, 0, 0});
    bytes.insert(bytes.end(), pvr.begin(), pvr.end());

    const auto archive = spice::pvm::parsing::parsePvmArchive(bytes);
    ASSERT_EQ(archive.status, ParseStatus::Partial); // non-sequential retained index
    ASSERT_EQ(archive.entries.size(), 1U);
    EXPECT_TRUE(archive.entries[0].name.empty());
    EXPECT_FALSE(archive.entries[0].rawPixelFormat.has_value());
    EXPECT_FALSE(archive.entries[0].rawDimensions.has_value());
    EXPECT_FALSE(archive.entries[0].globalIndex.has_value());
    EXPECT_EQ(archive.interstitialMetadata,
        (std::vector<std::uint8_t>{'C', 'O', 'M', 'M', 0, 0, 0, 0}));
}

TEST(SpicePvmDecoder, ConvertsAllObservedColorFormatsExactly)
{
    struct Case { PixelFormat format; std::vector<std::uint16_t> words; std::vector<std::array<std::uint8_t, 4>> expected; };
    const std::vector<Case> cases{
        {PixelFormat::Argb1555, {0x0000, 0xFFFF, 0x94E7}, {{0,0,0,0}, {255,255,255,255}, {41,57,57,255}}},
        {PixelFormat::Rgb565, {0x0000, 0xFFFF, 0x7BEF}, {{0,0,0,255}, {255,255,255,255}, {123,125,123,255}}},
        {PixelFormat::Argb4444, {0x0000, 0xFFFF, 0xA53C}, {{0,0,0,0}, {255,255,255,255}, {85,51,204,170}}},
    };
    for (const auto& test : cases) {
        std::vector<std::uint8_t> payload;
        for (const auto word : test.words) appendWord(payload, word);
        const auto decoded = spice::pvm::decoding::decodePvrTexture(
            spice::pvm::parsing::parsePvrTexture(
                makePvr(test.format, DataLayout::Rectangle,
                    static_cast<std::uint16_t>(test.words.size()), 1, payload)));
        ASSERT_EQ(decoded.status, ParseStatus::Complete);
        for (std::size_t i = 0; i < test.expected.size(); ++i)
            EXPECT_EQ(pixelAt(decoded.mipLevels[0].image, static_cast<std::uint32_t>(i), 0), test.expected[i]);
    }
}

TEST(SpicePvmDecoder, DecodesEveryObservedLayoutForEveryObservedPixelFormat)
{
    const std::array formats{PixelFormat::Argb1555, PixelFormat::Rgb565, PixelFormat::Argb4444};
    const std::array layouts{
        DataLayout::Twiddled, DataLayout::TwiddledMipmaps,
        DataLayout::Vq, DataLayout::VqMipmaps, DataLayout::Rectangle,
        DataLayout::RectangleTwiddled,
        DataLayout::SmallVq, DataLayout::SmallVqMipmaps,
        DataLayout::TwiddledMipmapsDma};
    for (const auto format : formats) {
        for (const auto layout : layouts) {
            const bool rectangle = layout == DataLayout::Rectangle ||
                layout == DataLayout::RectangleTwiddled;
            const bool small = layout == DataLayout::SmallVq || layout == DataLayout::SmallVqMipmaps;
            const std::uint16_t width = rectangle
                ? (layout == DataLayout::RectangleTwiddled ? 4 : 3)
                : (small ? 16 : 4);
            const std::uint16_t height = rectangle ? 2 : width;
            const auto parsed = spice::pvm::parsing::parsePvrTexture(
                makePvr(format, layout, width, height, payloadFor(format, layout, width, height)));
            ASSERT_NE(parsed.status, ParseStatus::Failed);
            const auto decoded = spice::pvm::decoding::decodePvrTexture(parsed);
            ASSERT_EQ(decoded.status, ParseStatus::Complete)
                << "format=" << static_cast<int>(format) << " layout=" << static_cast<int>(layout);
            ASSERT_FALSE(decoded.mipLevels.empty());
            EXPECT_EQ(decoded.mipLevels.front().width, width);
            EXPECT_EQ(decoded.mipLevels.front().height, height);
            EXPECT_EQ(decoded.mipLevels.front().image.pixels.size(),
                static_cast<std::size_t>(width) * height * 4);
        }
    }
}

TEST(SpicePvmDecoder, SmallVqUsesSdkCodebookSizesAtAllObservedDimensions)
{
    for (const std::uint16_t size : {16, 32, 64}) {
        for (const auto layout : {DataLayout::SmallVq, DataLayout::SmallVqMipmaps}) {
            const auto texture = spice::pvm::parsing::parsePvrTexture(
                makePvr(PixelFormat::Rgb565, layout, size, size,
                    payloadFor(PixelFormat::Rgb565, layout, size, size)));
            const auto decoded = spice::pvm::decoding::decodePvrTexture(texture);
            ASSERT_EQ(decoded.status, ParseStatus::Complete) << "size=" << size;
            const std::size_t expectedEntries = smallCodebookEntries(size, layout == DataLayout::SmallVqMipmaps);
            EXPECT_EQ(decoded.codebookRange.size, expectedEntries * 8);
        }
    }
}

TEST(SpicePvmDecoder, ReturnsMipmapsLargestFirstWithPhysicalRangesPreserved)
{
    for (const auto layout : {DataLayout::TwiddledMipmaps, DataLayout::VqMipmaps,
             DataLayout::SmallVqMipmaps, DataLayout::TwiddledMipmapsDma}) {
        const std::uint16_t size = layout == DataLayout::SmallVqMipmaps ? 16 : 4;
        const auto texture = spice::pvm::parsing::parsePvrTexture(
            makePvr(PixelFormat::Rgb565, layout, size, size,
                payloadFor(PixelFormat::Rgb565, layout, size, size)));
        const auto decoded = spice::pvm::decoding::decodePvrTexture(texture);
        ASSERT_EQ(decoded.status, ParseStatus::Complete);
        EXPECT_EQ(decoded.mipLevels.front().width, size);
        EXPECT_EQ(decoded.mipLevels.back().width, 1U);
        EXPECT_GT(decoded.mipLevels.front().sourceRange.offset,
            decoded.mipLevels.back().sourceRange.offset);
        for (const auto& level : decoded.mipLevels)
            EXPECT_EQ(level.image.pixels.size(),
                static_cast<std::size_t>(level.width) * level.height * 4);
    }
}

TEST(SpicePvmDecoder, ExposesMortonAndVqVectorOrientation)
{
    const auto twiddledTexture = spice::pvm::parsing::parsePvrTexture(
        makePvr(PixelFormat::Rgb565, DataLayout::Twiddled, 4, 4,
            payloadFor(PixelFormat::Rgb565, DataLayout::Twiddled, 4, 4)));
    const auto twiddled = spice::pvm::decoding::decodePvrTexture(twiddledTexture);
    ASSERT_EQ(twiddled.status, ParseStatus::Complete);
    EXPECT_NE(pixelAt(twiddled.mipLevels[0].image, 1, 0), pixelAt(twiddled.mipLevels[0].image, 0, 1));

    auto payload = vqPayload(PixelFormat::Rgb565, 2, false, false);
    const auto vq = spice::pvm::decoding::decodePvrTexture(
        spice::pvm::parsing::parsePvrTexture(
            makePvr(PixelFormat::Rgb565, DataLayout::Vq, 2, 2, payload)));
    ASSERT_EQ(vq.status, ParseStatus::Complete);
    EXPECT_NE(pixelAt(vq.mipLevels[0].image, 0, 1), pixelAt(vq.mipLevels[0].image, 1, 0));
}

TEST(SpicePvmDecoder, DecodesRectangleTwiddledTilesAlongEitherAxis)
{
    for (const auto format : {PixelFormat::Argb1555, PixelFormat::Rgb565, PixelFormat::Argb4444}) {
        for (const auto [width, height] : std::array<std::pair<std::uint16_t, std::uint16_t>, 5>{
                 std::pair{4U, 8U}, std::pair{8U, 4U}, std::pair{4U, 4U},
                 std::pair{128U, 256U}, std::pair{256U, 128U}}) {
            const auto texture = spice::pvm::parsing::parsePvrTexture(
                makePvr(format, DataLayout::RectangleTwiddled, width, height,
                    payloadFor(format, DataLayout::RectangleTwiddled, width, height)));
            ASSERT_EQ(texture.dataLayout, DataLayout::RectangleTwiddled);
            const auto decoded = spice::pvm::decoding::decodePvrTexture(texture);
            ASSERT_EQ(decoded.status, ParseStatus::Complete) << width << "x" << height;
            ASSERT_EQ(decoded.mipLevels.size(), 1U);
            EXPECT_EQ(decoded.mipLevels[0].sourceRange.offset, texture.textureDataRange.offset);
            EXPECT_EQ(decoded.mipLevels[0].sourceRange.size, texture.textureDataRange.size);
            const auto linear = spice::pvm::decoding::decodePvrTexture(
                spice::pvm::parsing::parsePvrTexture(
                    makePvr(format, DataLayout::Rectangle, width, height,
                        payloadFor(format, DataLayout::Rectangle, width, height))));
            ASSERT_EQ(linear.status, ParseStatus::Complete);
            EXPECT_EQ(decoded.mipLevels[0].image.pixels,
                linear.mipLevels[0].image.pixels);
        }
    }
}

TEST(SpicePvmDecoder, RejectsInvalidCombinationsAndPayloads)
{
    const auto nonsquare = spice::pvm::parsing::parsePvrTexture(
        makePvr(PixelFormat::Rgb565, DataLayout::Twiddled, 4, 2, std::vector<std::uint8_t>(16)));
    EXPECT_EQ(spice::pvm::decoding::decodePvrTexture(nonsquare).status, ParseStatus::Failed);

    const auto invalidRectangleTwiddled = spice::pvm::parsing::parsePvrTexture(
        makePvr(PixelFormat::Rgb565, DataLayout::RectangleTwiddled, 6, 4,
            std::vector<std::uint8_t>(48U)));
    EXPECT_EQ(spice::pvm::decoding::decodePvrTexture(invalidRectangleTwiddled).status,
        ParseStatus::Failed);

    auto truncatedRectangleTwiddled = payloadFor(
        PixelFormat::Rgb565, DataLayout::RectangleTwiddled, 8, 4);
    truncatedRectangleTwiddled.pop_back();
    EXPECT_EQ(spice::pvm::decoding::decodePvrTexture(
        spice::pvm::parsing::parsePvrTexture(
            makePvr(PixelFormat::Rgb565, DataLayout::RectangleTwiddled, 8, 4,
                truncatedRectangleTwiddled))).status,
        ParseStatus::Failed);

    auto paddedRectangleTwiddled = payloadFor(
        PixelFormat::Rgb565, DataLayout::RectangleTwiddled, 1, 2);
    paddedRectangleTwiddled.resize(32U, 0U);
    EXPECT_EQ(spice::pvm::decoding::decodePvrTexture(
        spice::pvm::parsing::parsePvrTexture(
            makePvr(PixelFormat::Rgb565, DataLayout::RectangleTwiddled, 1, 2,
                paddedRectangleTwiddled))).status,
        ParseStatus::Complete);
    paddedRectangleTwiddled.back() = 1U;
    EXPECT_EQ(spice::pvm::decoding::decodePvrTexture(
        spice::pvm::parsing::parsePvrTexture(
            makePvr(PixelFormat::Rgb565, DataLayout::RectangleTwiddled, 1, 2,
                paddedRectangleTwiddled))).status,
        ParseStatus::Failed);

    auto shortPayload = payloadFor(PixelFormat::Rgb565, DataLayout::Vq, 4, 4);
    shortPayload.pop_back();
    const auto truncated = spice::pvm::parsing::parsePvrTexture(
        makePvr(PixelFormat::Rgb565, DataLayout::Vq, 4, 4, shortPayload));
    EXPECT_EQ(spice::pvm::decoding::decodePvrTexture(truncated).status, ParseStatus::Failed);

    auto invalidIndex = payloadFor(PixelFormat::Rgb565, DataLayout::SmallVq, 16, 16);
    invalidIndex[16 * 8] = 0xFF;
    const auto badIndex = spice::pvm::parsing::parsePvrTexture(
        makePvr(PixelFormat::Rgb565, DataLayout::SmallVq, 16, 16, invalidIndex));
    EXPECT_EQ(spice::pvm::decoding::decodePvrTexture(badIndex).status, ParseStatus::Failed);

    auto extra = payloadFor(PixelFormat::Rgb565, DataLayout::TwiddledMipmaps, 4, 4);
    extra.push_back(0);
    const auto inconsistent = spice::pvm::parsing::parsePvrTexture(
        makePvr(PixelFormat::Rgb565, DataLayout::TwiddledMipmaps, 4, 4, extra));
    EXPECT_EQ(spice::pvm::decoding::decodePvrTexture(inconsistent).status, ParseStatus::Failed);
}

TEST(SpicePvmDecoder, RetainsZeroFilledThirtyTwoByteChunkAlignment)
{
    auto payload = payloadFor(PixelFormat::Rgb565, DataLayout::VqMipmaps, 4, 4);
    const auto logicalSize = payload.size();
    payload.resize((payload.size() + 31) & ~std::size_t{31}, 0);
    const auto decoded = spice::pvm::decoding::decodePvrTexture(
        spice::pvm::parsing::parsePvrTexture(
            makePvr(PixelFormat::Rgb565, DataLayout::VqMipmaps, 4, 4, payload)));
    ASSERT_EQ(decoded.status, ParseStatus::Complete);
    ASSERT_TRUE(decoded.trailingPaddingRange.has_value());
    EXPECT_EQ(decoded.trailingPaddingRange->size, payload.size() - logicalSize);

    payload.back() = 1;
    EXPECT_EQ(spice::pvm::decoding::decodePvrTexture(
        spice::pvm::parsing::parsePvrTexture(
            makePvr(PixelFormat::Rgb565, DataLayout::VqMipmaps, 4, 4, payload))).status,
        ParseStatus::Failed);
}

spice::pvm::model::RgbaImage makeEncodeImage(
    const std::uint32_t width, const std::uint32_t height, const std::uint8_t bias = 0U)
{
    spice::pvm::model::RgbaImage image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(width) * height * 4U);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto offset = (static_cast<std::size_t>(y) * width + x) * 4U;
            image.pixels[offset + 0U] = static_cast<std::uint8_t>((x % 4U) * 61U + bias);
            image.pixels[offset + 1U] = static_cast<std::uint8_t>((y % 4U) * 47U + bias);
            image.pixels[offset + 2U] = static_cast<std::uint8_t>(((x + y) % 4U) * 53U + bias);
            image.pixels[offset + 3U] = static_cast<std::uint8_t>(((x ^ y) & 1U) != 0 ? 255U : 96U);
        }
    }
    return image;
}

TEST(SpicePvmParser, PreservesArbitraryGbixTrailingMetadataThroughEncoding)
{
    spice::pvm::encoding::PvrEncodeOptions options{};
    options.pixelFormat = PixelFormat::Argb4444;
    options.dataLayout = DataLayout::Twiddled;
    options.includeGlobalIndex = true;
    options.globalIndex = 0x89ABCDEFU;
    options.gbixTrailingBytes = {0x10U, 0x20U, 0x30U, 0x40U, 0x50U, 0x60U};
    options.pvrtUnknownHeader = {0xA6U, 0x5BU};
    const auto encoded = spice::pvm::encoding::encodePvrTexture(
        makeEncodeImage(8U, 8U), options);
    ASSERT_TRUE(encoded.ok());

    const auto parsed = spice::pvm::parsing::parsePvrTexture(encoded.bytes);
    ASSERT_EQ(parsed.status, ParseStatus::Complete);
    ASSERT_TRUE(parsed.globalIndex.has_value());
    EXPECT_EQ(*parsed.globalIndex, options.globalIndex);
    EXPECT_EQ(parsed.gbixTrailingBytes, options.gbixTrailingBytes);
    EXPECT_EQ(parsed.pvrtUnknownHeader,
        (std::vector<std::uint8_t>{options.pvrtUnknownHeader[0], options.pvrtUnknownHeader[1]}));
    ASSERT_TRUE(parsed.gbixRange.has_value());
    EXPECT_EQ(parsed.gbixRange->size, 12U + options.gbixTrailingBytes.size());
    EXPECT_EQ(parsed.sourceBytes, encoded.bytes);
}

TEST(SpicePvmEncoder, RoundTripsEveryPromotedPixelFormatAndLayoutDeterministically)
{
    const std::array formats{PixelFormat::Argb1555, PixelFormat::Rgb565, PixelFormat::Argb4444};
    const std::array layouts{
        DataLayout::Twiddled, DataLayout::TwiddledMipmaps, DataLayout::Vq,
        DataLayout::VqMipmaps, DataLayout::Rectangle, DataLayout::RectangleTwiddled,
        DataLayout::SmallVq,
        DataLayout::SmallVqMipmaps, DataLayout::TwiddledMipmapsDma,
    };
    for (const auto format : formats) {
        for (const auto layout : layouts) {
            const bool smallVq = layout == DataLayout::SmallVq || layout == DataLayout::SmallVqMipmaps;
            const bool rectangle = layout == DataLayout::Rectangle ||
                layout == DataLayout::RectangleTwiddled;
            const auto width = smallVq ? 16U :
                (layout == DataLayout::RectangleTwiddled ? 8U : 4U);
            const auto height = rectangle ? 4U : width;
            spice::pvm::encoding::PvrEncodeOptions options{};
            options.pixelFormat = format;
            options.dataLayout = layout;
            options.generateMipmaps = layout == DataLayout::TwiddledMipmaps ||
                layout == DataLayout::VqMipmaps || layout == DataLayout::SmallVqMipmaps ||
                layout == DataLayout::TwiddledMipmapsDma;
            options.includeGlobalIndex = true;
            options.globalIndex = 0x12340000U | static_cast<std::uint8_t>(layout);
            options.pvrtUnknownHeader = {0xA5U, 0x5AU};

            const auto image = makeEncodeImage(width, height, static_cast<std::uint8_t>(format));
            const auto first = spice::pvm::encoding::encodePvrTexture(image, options);
            const auto second = spice::pvm::encoding::encodePvrTexture(image, options);
            ASSERT_TRUE(first.ok()) << static_cast<unsigned>(format) << "/" << static_cast<unsigned>(layout);
            EXPECT_EQ(first.bytes, second.bytes);
            const auto parsed = spice::pvm::parsing::parsePvrTexture(first.bytes);
            ASSERT_EQ(parsed.status, ParseStatus::Complete);
            EXPECT_EQ(parsed.pixelFormat, format);
            EXPECT_EQ(parsed.dataLayout, layout);
            EXPECT_EQ(parsed.globalIndex, options.globalIndex);
            EXPECT_EQ(parsed.pvrtUnknownHeader, (std::vector<std::uint8_t>{0xA5U, 0x5AU}));
            const auto decoded = spice::pvm::decoding::decodePvrTexture(parsed);
            ASSERT_EQ(decoded.status, ParseStatus::Complete)
                << (decoded.diagnostics.empty() ? "" : decoded.diagnostics.front().message);
            ASSERT_FALSE(decoded.mipLevels.empty());
            EXPECT_EQ(decoded.mipLevels.front().width, width);
            EXPECT_EQ(decoded.mipLevels.front().height, height);
            EXPECT_EQ(decoded.mipLevels.size(), first.mipSourceRanges.size());
        }
    }
}

TEST(SpicePvmEncoder, RejectsMipInputForRectangleTwiddledLayout)
{
    const std::array images{makeEncodeImage(8U, 4U), makeEncodeImage(4U, 2U)};
    spice::pvm::encoding::PvrEncodeOptions options{};
    options.pixelFormat = PixelFormat::Rgb565;
    options.dataLayout = DataLayout::RectangleTwiddled;
    EXPECT_FALSE(spice::pvm::encoding::encodePvrTexture(images, options).ok());
}

TEST(SpicePvmEncoder, SupportsAllSmallVqSdkDimensionsAndExplicitMipChains)
{
    for (const auto size : {16U, 32U, 64U}) {
        spice::pvm::encoding::PvrEncodeOptions options{};
        options.pixelFormat = PixelFormat::Rgb565;
        options.dataLayout = DataLayout::SmallVqMipmaps;
        options.generateMipmaps = true;
        const auto encoded = spice::pvm::encoding::encodePvrTexture(makeEncodeImage(size, size), options);
        ASSERT_TRUE(encoded.ok()) << size;
        const auto decoded = spice::pvm::decoding::decodePvrTexture(
            spice::pvm::parsing::parsePvrTexture(encoded.bytes));
        ASSERT_EQ(decoded.status, ParseStatus::Complete) << size;
        EXPECT_EQ(decoded.mipLevels.size(), std::bit_width(size));
        EXPECT_EQ(decoded.mipLevels.front().width, size);
        EXPECT_EQ(decoded.mipLevels.back().width, 1U);
    }

    std::vector<spice::pvm::model::RgbaImage> levels;
    for (std::uint32_t size = 8U;; size /= 2U) {
        levels.push_back(makeEncodeImage(size, size, static_cast<std::uint8_t>(size)));
        if (size == 1U) break;
    }
    spice::pvm::encoding::PvrEncodeOptions options{};
    options.pixelFormat = PixelFormat::Argb4444;
    options.dataLayout = DataLayout::TwiddledMipmapsDma;
    const auto encoded = spice::pvm::encoding::encodePvrTexture(levels, options);
    ASSERT_TRUE(encoded.ok());
    ASSERT_EQ(encoded.mipSourceRanges.size(), levels.size());
    EXPECT_EQ(encoded.mipSourceRanges.front().size, 8U * 8U * 2U);
    EXPECT_EQ(encoded.mipSourceRanges.back().offset, encoded.textureDataRange.offset + 6U);
}

TEST(SpicePvmEncoder, BuildsFormalPvmArchiveAndDiagnosesInvalidRequests)
{
    spice::pvm::encoding::PvrEncodeOptions pvrOptions{};
    pvrOptions.pixelFormat = PixelFormat::Rgb565;
    pvrOptions.dataLayout = DataLayout::Twiddled;
    pvrOptions.includeGlobalIndex = true;
    pvrOptions.globalIndex = 100U;
    const auto first = spice::pvm::encoding::encodePvrTexture(makeEncodeImage(4U, 4U), pvrOptions);
    pvrOptions.globalIndex = 101U;
    const auto second = spice::pvm::encoding::encodePvrTexture(makeEncodeImage(4U, 4U, 7U), pvrOptions);
    ASSERT_TRUE(first.ok());
    ASSERT_TRUE(second.ok());

    const std::array entries{
        spice::pvm::encoding::PvmEncodeEntry{.archiveIndex = 0U, .name = "first", .pvrBytes = first.bytes},
        spice::pvm::encoding::PvmEncodeEntry{.archiveIndex = 1U, .name = "second", .pvrBytes = second.bytes},
    };
    spice::pvm::encoding::PvmEncodeOptions archiveOptions{};
    archiveOptions.headerPadding = {0xCCU, 0xDDU};
    archiveOptions.interstitialMetadata = {'M', 'E', 'T', 'A'};
    const auto encoded = spice::pvm::encoding::encodePvmArchive(entries, archiveOptions);
    ASSERT_TRUE(encoded.ok());
    const auto parsed = spice::pvm::parsing::parsePvmArchive(encoded.bytes);
    ASSERT_EQ(parsed.status, ParseStatus::Complete);
    ASSERT_EQ(parsed.entries.size(), 2U);
    EXPECT_EQ(parsed.entries[0].name, "first");
    EXPECT_EQ(parsed.entries[1].globalIndex, 101U);
    EXPECT_EQ(parsed.headerPadding, archiveOptions.headerPadding);
    EXPECT_EQ(parsed.interstitialMetadata, archiveOptions.interstitialMetadata);

    auto invalidOptions = pvrOptions;
    invalidOptions.dataLayout = DataLayout::SmallVq;
    EXPECT_FALSE(spice::pvm::encoding::encodePvrTexture(makeEncodeImage(8U, 8U), invalidOptions).ok());
    auto invalidArchive = archiveOptions;
    invalidArchive.flags = 0x0010U;
    EXPECT_FALSE(spice::pvm::encoding::encodePvmArchive(entries, invalidArchive).ok());
}

} // namespace
