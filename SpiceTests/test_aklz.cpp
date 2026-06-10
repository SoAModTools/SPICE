#include "../Compression/Aklz.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

using soasim::compression::aklz::AklzError;
using soasim::compression::aklz::compress;
using soasim::compression::aklz::decompress;
using soasim::compression::aklz::isAklz;

std::uint32_t readBeU32(std::span<const std::uint8_t> bytes, const std::size_t offset) {
    return (std::uint32_t(bytes[offset + 0U]) << 24U) |
        (std::uint32_t(bytes[offset + 1U]) << 16U) |
        (std::uint32_t(bytes[offset + 2U]) << 8U) |
        std::uint32_t(bytes[offset + 3U]);
}

bool hasBackReference(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < soasim::compression::aklz::kHeaderSize) {
        return false;
    }

    const auto decompressedSize = readBeU32(bytes, soasim::compression::aklz::kMagicSize);
    std::size_t cursor = soasim::compression::aklz::kHeaderSize;
    std::uint32_t produced = 0U;
    while (cursor < bytes.size() && produced < decompressedSize) {
        const auto flags = bytes[cursor++];
        for (std::uint8_t bit = 0U; bit < 8U && produced < decompressedSize; ++bit) {
            const bool literal = (flags & (1U << bit)) != 0U;
            if (literal) {
                if (cursor >= bytes.size()) {
                    return false;
                }
                ++cursor;
                ++produced;
            } else {
                if (cursor + 1U >= bytes.size()) {
                    return false;
                }
                const auto length = static_cast<std::uint32_t>((bytes[cursor + 1U] & 0x0FU) + 3U);
                cursor += 2U;
                produced += length;
                return true;
            }
        }
    }
    return false;
}

void expectRoundTrip(std::span<const std::uint8_t> input) {
    const auto encoded = compress(input);
    ASSERT_TRUE(encoded.ok()) << soasim::compression::aklz::errorToString(encoded.error);
    ASSERT_TRUE(isAklz(encoded.bytes));
    const auto decoded = decompress(encoded.bytes);
    ASSERT_TRUE(decoded.ok()) << soasim::compression::aklz::errorToString(decoded.error);
    EXPECT_EQ(decoded.bytes, std::vector<std::uint8_t>(input.begin(), input.end()));
}

} // namespace

TEST(AklzCompression, WritesHeaderMagicAndBigEndianSize) {
    const std::vector<std::uint8_t> input{ 0x10U, 0x20U, 0x30U, 0x40U };
    const auto encoded = compress(input);
    ASSERT_TRUE(encoded.ok());
    ASSERT_GE(encoded.bytes.size(), soasim::compression::aklz::kHeaderSize);
    EXPECT_TRUE(isAklz(encoded.bytes));
    EXPECT_EQ(readBeU32(encoded.bytes, soasim::compression::aklz::kMagicSize), input.size());
}

TEST(AklzCompression, EmptyInputRoundTrips) {
    const std::vector<std::uint8_t> input{};
    expectRoundTrip(input);
}

TEST(AklzCompression, LiteralHeavyInputRoundTrips) {
    std::vector<std::uint8_t> input;
    for (std::uint16_t value = 0U; value < 255U; ++value) {
        input.push_back(static_cast<std::uint8_t>(value));
    }
    expectRoundTrip(input);
}

TEST(AklzCompression, RepeatedByteInputRoundTripsAndUsesBackReference) {
    const std::vector<std::uint8_t> input(64U, 0x41U);
    const auto encoded = compress(input);
    ASSERT_TRUE(encoded.ok());
    EXPECT_TRUE(hasBackReference(encoded.bytes));
    const auto decoded = decompress(encoded.bytes);
    ASSERT_TRUE(decoded.ok());
    EXPECT_EQ(decoded.bytes, input);
}

TEST(AklzCompression, RepeatedPhraseInputRoundTrips) {
    std::vector<std::uint8_t> input;
    const std::string phrase = "pirate-ship-";
    for (int i = 0; i < 20; ++i) {
        input.insert(input.end(), phrase.begin(), phrase.end());
    }
    expectRoundTrip(input);
}

TEST(AklzCompression, MatchLengthBoundariesRoundTrip) {
    const std::vector<std::uint8_t> len3{ 'a', 'b', 'c', 'a', 'b', 'c' };
    const std::vector<std::uint8_t> len18{
        '0', '1', '2', '3', '4', '5', '6', '7', '8',
        '9', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h',
        '0', '1', '2', '3', '4', '5', '6', '7', '8',
        '9', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'
    };
    auto len19 = len18;
    len19.push_back('0');
    expectRoundTrip(len3);
    expectRoundTrip(len18);
    expectRoundTrip(len19);
}

TEST(AklzCompression, LeadingZeroHistoryRoundTrips) {
    std::vector<std::uint8_t> input(24U, 0U);
    input.push_back(0x80U);
    input.push_back(0x00U);
    input.push_back(0x00U);
    const auto encoded = compress(input);
    ASSERT_TRUE(encoded.ok());
    EXPECT_TRUE(hasBackReference(encoded.bytes));
    const auto decoded = decompress(encoded.bytes);
    ASSERT_TRUE(decoded.ok());
    EXPECT_EQ(decoded.bytes, input);
}

TEST(AklzCompression, OverlappingBackReferenceRoundTrips) {
    const std::vector<std::uint8_t> input{
        'a', 'b', 'a', 'b', 'a', 'b', 'a', 'b', 'a', 'b',
        'a', 'b', 'a', 'b', 'a', 'b', 'a', 'b', 'a', 'b'
    };
    expectRoundTrip(input);
}

TEST(AklzCompression, InputTooLargeReturnsError) {
    const std::vector<std::uint8_t> input{ 1U, 2U, 3U, 4U };
    const auto encoded = compress(input, 3U);
    EXPECT_EQ(encoded.error, AklzError::InputTooLarge);
    EXPECT_TRUE(encoded.bytes.empty());
}
