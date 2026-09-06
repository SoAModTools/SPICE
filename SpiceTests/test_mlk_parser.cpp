#include "../SpiceMlk/SpiceMlk.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

void writeBeU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    ASSERT_LE(offset + 4U, bytes.size());
    bytes[offset + 0U] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value & 0xffU);
}
std::vector<std::uint8_t> makeNormalMlkFixture() {
    std::vector<std::uint8_t> bytes(0x60U, 0U);
    writeBeU32(bytes, 0x00U, 0x0000ffffU);
    writeBeU32(bytes, 0x04U, 0x0002ffffU);

    writeBeU32(bytes, 0x08U, 0x12345678U);
    writeBeU32(bytes, 0x0cU, 0x00000028U);
    writeBeU32(bytes, 0x10U, 0x00000008U);
    writeBeU32(bytes, 0x14U, 0x00000030U);

    writeBeU32(bytes, 0x18U, 0x9abcdef0U);
    writeBeU32(bytes, 0x1cU, 0x00000040U);
    writeBeU32(bytes, 0x20U, 0x00000004U);
    writeBeU32(bytes, 0x24U, 0x00000044U);

    bytes[0x28U] = 'N';
    bytes[0x29U] = 'J';
    bytes[0x2aU] = 'C';
    bytes[0x2bU] = 'M';
    bytes[0x40U] = 'P';
    bytes[0x41U] = 'O';
    bytes[0x42U] = 'F';
    bytes[0x43U] = '0';
    return bytes;
}

std::vector<std::uint8_t> makeFirstPayloadCountCandidateFixture() {
    auto bytes = makeNormalMlkFixture();
    writeBeU32(bytes, 0x0cU, 0x00000038U);
    bytes[0x38U] = 'N';
    bytes[0x39U] = 'J';
    bytes[0x3aU] = 'C';
    bytes[0x3bU] = 'M';
    return bytes;
}

std::vector<std::uint8_t> makeMalformedRecordSpanFixture() {
    auto bytes = makeNormalMlkFixture();
    writeBeU32(bytes, 0x0cU, 0x0000005cU);
    writeBeU32(bytes, 0x10U, 0x00000008U);
    return bytes;
}

} // namespace

TEST(SpiceMlkParser, BuildsSupportedFileForNormalTableShape) {
    const auto parsed = spice::mlk::MlkParser::parse(makeNormalMlkFixture(), "normal.mlk");

    ASSERT_TRUE(parsed.ok());
    EXPECT_TRUE(parsed.supported);
    EXPECT_EQ(parsed.sourcePath, "normal.mlk");
    EXPECT_EQ(parsed.tableShape, spice::mlk::MlkTableShape::Normal);
    EXPECT_EQ(parsed.runtimeRecordCount, 2);
    EXPECT_EQ(parsed.rawRecordCountCandidate, 2U);
    EXPECT_EQ(parsed.selectedRecordCount, 2U);
    EXPECT_EQ(parsed.descriptorRecordCount, 2U);
    EXPECT_EQ(parsed.unavailableTrailingRecordCount, 0U);
    EXPECT_EQ(parsed.recordCountSource, spice::mlk::MlkRecordCountSource::HeaderU16At04);
    EXPECT_EQ(parsed.records.size(), 2U);
    EXPECT_EQ(parsed.records[0].payloadKind, spice::mlk::MlkPayloadKind::NinjaChunk);
    EXPECT_EQ(parsed.records[1].payloadKind, spice::mlk::MlkPayloadKind::Pof0);
}

TEST(SpiceMlkParser, PreservesFirstPayloadCountCandidateAsEvidenceOnly) {
    const auto parsed = spice::mlk::MlkParser::parse(makeFirstPayloadCountCandidateFixture(), "candidate.mlk");

    EXPECT_TRUE(parsed.ok());
    EXPECT_FALSE(parsed.supported);
    EXPECT_EQ(parsed.tableShape, spice::mlk::MlkTableShape::FirstPayloadCountCandidate);
    EXPECT_EQ(parsed.runtimeRecordCount, 2);
    EXPECT_EQ(parsed.selectedRecordCount, 2U);
    EXPECT_EQ(parsed.recordCountInferredFromFirstPayloadOffset, 3U);
    EXPECT_FALSE(parsed.recordCountMatchesFirstPayloadOffset);
    ASSERT_EQ(parsed.records.size(), 2U);
}

TEST(SpiceMlkParser, KeepsMalformedRecordsAndDiagnosticsVisible) {
    const auto parsed = spice::mlk::MlkParser::parse(makeMalformedRecordSpanFixture(), "malformed.mlk");

    EXPECT_FALSE(parsed.ok());
    EXPECT_FALSE(parsed.supported);
    EXPECT_EQ(parsed.tableShape, spice::mlk::MlkTableShape::MalformedRecordSpans);
    ASSERT_EQ(parsed.records.size(), 2U);
    EXPECT_FALSE(parsed.records[0].payloadInBounds);
    ASSERT_FALSE(parsed.diagnostics.empty());
}
