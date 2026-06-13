#include "../Compression/Aklz.h"
#include "../SpiceMlk/SpiceMlk.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

void writeBeU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    ASSERT_LE(offset + 4U, bytes.size());
    bytes[offset + 0U] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value & 0xffU);
}

std::vector<std::uint8_t> makeMlkProbeFixture() {
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
    bytes[0x2cU] = 'd';
    bytes[0x2dU] = 'a';
    bytes[0x2eU] = 't';
    bytes[0x2fU] = 'a';

    bytes[0x40U] = 'P';
    bytes[0x41U] = 'O';
    bytes[0x42U] = 'F';
    bytes[0x43U] = '0';
    return bytes;
}

std::vector<std::uint8_t> makeMlkWithEmbeddedMldFixture() {
    std::vector<std::uint8_t> bytes(0x100U, 0U);
    writeBeU32(bytes, 0x00U, 0x0000ffffU);
    writeBeU32(bytes, 0x04U, 0x0001ffffU);
    writeBeU32(bytes, 0x08U, 0x001d01c8U);
    writeBeU32(bytes, 0x0cU, 0x00000018U);
    writeBeU32(bytes, 0x10U, 0x000000e8U);
    writeBeU32(bytes, 0x14U, 0x00000004U);

    writeBeU32(bytes, 0x18U, 0x00000001U);
    writeBeU32(bytes, 0x1cU, 0x00000014U);
    writeBeU32(bytes, 0x20U, 0x0000007cU);
    writeBeU32(bytes, 0x24U, 0x000000c0U);
    writeBeU32(bytes, 0x28U, 0x000000e0U);
    return bytes;
}

} // namespace

TEST(SpiceMlkScanner, ProbesRawMlkRecordTableAndPayloadSignatures) {
    const auto bytes = makeMlkProbeFixture();

    const auto result = spice::mlk::MlkScanner::scan(bytes, "fixture.mlk");

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.sourcePath, "fixture.mlk");
    EXPECT_FALSE(result.sourceWasCompressedAklz);
    EXPECT_EQ(result.decodedSize, bytes.size());
    EXPECT_EQ(result.headerWords[0], 0x0000ffffU);
    EXPECT_EQ(result.headerWords[1], 0x0002ffffU);
    EXPECT_EQ(result.recordCountCandidate, 2U);
    EXPECT_EQ(result.selectedRecordCount, 2U);
    EXPECT_EQ(result.recordCountSource, spice::mlk::MlkRecordCountSource::HeaderU16At04);
    EXPECT_EQ(result.signedRecordCountCandidate, 2);
    EXPECT_TRUE(result.recordTableInBounds);
    EXPECT_EQ(result.recordTableEndOffset, 0x28U);
    EXPECT_EQ(result.firstPayloadOffset, 0x28U);
    EXPECT_EQ(result.recordCountInferredFromFirstPayloadOffset, 2U);
    EXPECT_TRUE(result.recordCountMatchesFirstPayloadOffset);

    ASSERT_EQ(result.records.size(), 2U);
    EXPECT_EQ(result.records[0].key, 0x12345678U);
    EXPECT_EQ(result.records[0].payloadOffset, 0x28U);
    EXPECT_EQ(result.records[0].payloadSize, 0x08U);
    EXPECT_EQ(result.records[0].rawWord12, 0x30U);
    EXPECT_TRUE(result.records[0].payloadInBounds);
    EXPECT_EQ(result.records[0].payloadSignature, "NJCM");
    EXPECT_EQ(result.records[0].payloadKind, spice::mlk::MlkPayloadKind::NinjaChunk);

    EXPECT_EQ(result.records[1].key, 0x9abcdef0U);
    EXPECT_EQ(result.records[1].payloadSignature, "POF0");
    EXPECT_EQ(result.records[1].payloadKind, spice::mlk::MlkPayloadKind::Pof0);
}

TEST(SpiceMlkScanner, ClassifiesRecordPayloadsWithPlausibleMldHeaders) {
    const auto bytes = makeMlkWithEmbeddedMldFixture();

    const auto result = spice::mlk::MlkScanner::scan(bytes, "embedded-mld.mlk");

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.records.size(), 1U);
    EXPECT_EQ(result.records[0].payloadKind, spice::mlk::MlkPayloadKind::MldFile);
    EXPECT_TRUE(result.records[0].embeddedMldHeader.plausible);
    EXPECT_EQ(result.records[0].embeddedMldHeader.entryCount, 1U);
    EXPECT_EQ(result.records[0].embeddedMldHeader.indexTableOffset, 0x14U);
    EXPECT_EQ(result.records[0].embeddedMldHeader.functionParametersOffset, 0x7cU);
    EXPECT_EQ(result.records[0].embeddedMldHeader.realDataOffset, 0xc0U);
    EXPECT_EQ(result.records[0].embeddedMldHeader.textureTableOffset, 0xe0U);
}

TEST(SpiceMlkScanner, DecodesAklzWrappedInputBeforeProbing) {
    const auto raw = makeMlkProbeFixture();
    const auto compressed = spice::compression::aklz::compress(raw);
    ASSERT_TRUE(compressed.ok()) << spice::compression::aklz::errorToString(compressed.error);

    const auto result = spice::mlk::MlkScanner::scan(compressed.bytes, "fixture.aklz.mlk");

    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.sourceWasCompressedAklz);
    EXPECT_EQ(result.rawSize, compressed.bytes.size());
    EXPECT_EQ(result.decodedSize, raw.size());
    ASSERT_EQ(result.records.size(), 2U);
    EXPECT_EQ(result.records[0].payloadSignature, "NJCM");
}

TEST(SpiceMlkScanner, ReportsOutOfBoundsPayloadSpan) {
    auto bytes = makeMlkProbeFixture();
    writeBeU32(bytes, 0x0cU, 0x0000005cU);
    writeBeU32(bytes, 0x10U, 0x00000008U);

    const auto result = spice::mlk::MlkScanner::scan(bytes);

    EXPECT_FALSE(result.ok());
    ASSERT_EQ(result.records.size(), 2U);
    EXPECT_FALSE(result.records[0].payloadInBounds);
    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_EQ(result.diagnostics[0].severity, spice::mlk::DiagnosticSeverity::Error);
}

TEST(SpiceMlkScanner, FallsBackToFirstPayloadOffsetCountWhenHeaderTableIsOutOfBounds) {
    auto bytes = makeMlkProbeFixture();
    writeBeU32(bytes, 0x04U, 0x5200ffffU);

    const auto result = spice::mlk::MlkScanner::scan(bytes);

    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.recordCountCandidate, 0x5200U);
    EXPECT_EQ(result.selectedRecordCount, 2U);
    EXPECT_EQ(result.recordCountInferredFromFirstPayloadOffset, 2U);
    EXPECT_EQ(result.recordCountSource, spice::mlk::MlkRecordCountSource::FirstPayloadOffset);
    EXPECT_TRUE(result.recordTableInBounds);
    ASSERT_EQ(result.records.size(), 2U);
    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_EQ(result.diagnostics.front().severity, spice::mlk::DiagnosticSeverity::Warning);
}

TEST(SpiceMlkScanner, WarnsWhenFirstPayloadOffsetDoesNotMatchRecordCountCandidate) {
    auto bytes = makeMlkProbeFixture();
    writeBeU32(bytes, 0x0cU, 0x00000038U);
    bytes[0x38U] = 'N';
    bytes[0x39U] = 'J';
    bytes[0x3aU] = 'C';
    bytes[0x3bU] = 'M';

    const auto result = spice::mlk::MlkScanner::scan(bytes);

    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.recordCountCandidate, 2U);
    EXPECT_EQ(result.selectedRecordCount, 2U);
    EXPECT_EQ(result.recordCountSource, spice::mlk::MlkRecordCountSource::HeaderU16At04);
    EXPECT_EQ(result.firstPayloadOffset, 0x38U);
    EXPECT_EQ(result.recordCountInferredFromFirstPayloadOffset, 3U);
    EXPECT_FALSE(result.recordCountMatchesFirstPayloadOffset);
    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_EQ(result.diagnostics.front().severity, spice::mlk::DiagnosticSeverity::Warning);
}

TEST(SpiceMlkScanner, WarnsOnDuplicateRecordKeysButKeepsScanning) {
    auto bytes = makeMlkProbeFixture();
    writeBeU32(bytes, 0x18U, 0x12345678U);

    const auto result = spice::mlk::MlkScanner::scan(bytes);

    EXPECT_TRUE(result.ok());
    ASSERT_EQ(result.records.size(), 2U);
    EXPECT_TRUE(result.records[1].duplicateKey);
    ASSERT_EQ(result.diagnostics.size(), 1U);
    EXPECT_EQ(result.diagnostics[0].severity, spice::mlk::DiagnosticSeverity::Warning);
}
