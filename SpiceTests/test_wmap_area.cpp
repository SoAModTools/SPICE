#include "../SpiceWMap/SpiceWMap.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

spice::wmap::WMapAreaDocument makeDocument() {
    spice::wmap::WMapAreaDocument document{};
    for (std::size_t layer = 0U; layer < spice::wmap::kWMapAreaLayerCount; ++layer) {
        for (std::size_t row = 0U; row < spice::wmap::kWMapAreaRowCount; ++row) {
            for (std::size_t column = 0U; column < spice::wmap::kWMapAreaColumnCount; ++column) {
                document.cell(layer, row, column) = static_cast<std::uint8_t>(
                    (layer * 7U + row * 3U + column) % 16U);
            }
        }
    }
    return document;
}

} // namespace

TEST(SpiceWMap, WritesAndParsesRawGridInLayerRowColumnOrder) {
    const auto expected = makeDocument();
    const auto written = spice::wmap::WMapAreaFileWriter{}.write(
        expected,
        spice::wmap::WMapAreaStorage::Raw);

    ASSERT_TRUE(written.ok());
    ASSERT_EQ(written.bytes.size(), spice::wmap::kWMapAreaSerializedSize);
    EXPECT_EQ(
        written.bytes[spice::wmap::serializedOffset(1U, 0U, 0U)],
        expected.cell(1U, 0U, 0U));
    EXPECT_EQ(
        written.bytes[spice::wmap::serializedOffset(2U, 23U, 27U)],
        expected.cell(2U, 23U, 27U));

    const auto parsed = spice::wmap::WMapAreaParser::parse(written.bytes);
    ASSERT_TRUE(parsed.ok());
    ASSERT_TRUE(parsed.document.has_value());
    EXPECT_FALSE(parsed.sourceWasCompressedAklz);
    EXPECT_EQ(parsed.rawSize, spice::wmap::kWMapAreaSerializedSize);
    EXPECT_EQ(parsed.decodedSize, spice::wmap::kWMapAreaSerializedSize);
    EXPECT_EQ(*parsed.document, expected);
}

TEST(SpiceWMap, WritesAndParsesAklzWithoutChangingTheDocument) {
    const auto expected = makeDocument();
    const auto written = spice::wmap::WMapAreaFileWriter{}.write(
        expected,
        spice::wmap::WMapAreaStorage::Aklz);

    ASSERT_TRUE(written.ok());
    ASSERT_FALSE(written.bytes.empty());
    const auto parsed = spice::wmap::WMapAreaParser::parse(written.bytes);
    ASSERT_TRUE(parsed.ok());
    ASSERT_TRUE(parsed.document.has_value());
    EXPECT_TRUE(parsed.sourceWasCompressedAklz);
    EXPECT_EQ(parsed.decodedSize, spice::wmap::kWMapAreaSerializedSize);
    EXPECT_EQ(*parsed.document, expected);
}

TEST(SpiceWMap, RejectsWrongSizeAndUnsupportedCellIds) {
    auto wrongSize = std::vector<std::uint8_t>(
        spice::wmap::kWMapAreaSerializedSize - 1U,
        0U);
    const auto sizeResult = spice::wmap::WMapAreaParser::parse(wrongSize);
    EXPECT_FALSE(sizeResult.ok());
    EXPECT_FALSE(sizeResult.document.has_value());

    auto invalid = std::vector<std::uint8_t>(
        spice::wmap::kWMapAreaSerializedSize,
        0U);
    invalid[spice::wmap::serializedOffset(2U, 3U, 4U)] = 16U;
    const auto valueResult = spice::wmap::WMapAreaParser::parse(invalid);
    EXPECT_FALSE(valueResult.ok());
    ASSERT_EQ(valueResult.diagnostics.size(), 1U);
    EXPECT_EQ(
        valueResult.diagnostics.front().offset,
        spice::wmap::serializedOffset(2U, 3U, 4U));

    auto invalidDocument = makeDocument();
    invalidDocument.cell(0U, 1U, 2U) = 0xffU;
    const auto writeResult = spice::wmap::WMapAreaFileWriter{}.write(
        invalidDocument,
        spice::wmap::WMapAreaStorage::Raw);
    EXPECT_FALSE(writeResult.ok());
    EXPECT_TRUE(writeResult.bytes.empty());
}
