#include "../Compression/Aklz.h"
#include "../SpiceSstSml/SpiceSstSml.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace spice::sstsml;

void writeBeU16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset + 0U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value & 0xFFU);
}

void writeBeI16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::int16_t value) {
    writeBeU16(bytes, offset, static_cast<std::uint16_t>(value));
}

void writeBeU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset + 0U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value & 0xFFU);
}

std::vector<std::uint8_t> makeSml(std::uint32_t recordCount = 2U) {
    std::vector<std::uint8_t> bytes(0x40U, 0U);
    writeBeU32(bytes, 0x00U, 0x534D4C30U);
    writeBeU32(bytes, 0x04U, (recordCount << 16U) | 0xFFFFU);

    writeBeU32(bytes, 0x08U, 0x00000100U);
    writeBeU32(bytes, 0x0CU, 0x28U);
    writeBeU32(bytes, 0x10U, 0x04U);
    writeBeU32(bytes, 0x14U, 0x11111111U);
    bytes[0x28U] = 0xDEU;
    bytes[0x29U] = 0xADU;
    bytes[0x2AU] = 0xBEU;
    bytes[0x2BU] = 0xEFU;

    if (recordCount > 1U) {
        writeBeU32(bytes, 0x18U, 0x00000101U);
        writeBeU32(bytes, 0x1CU, 0x2CU);
        writeBeU32(bytes, 0x20U, 0x08U);
        writeBeU32(bytes, 0x24U, 0x22222222U);
        for (std::uint8_t i = 0U; i < 8U; ++i) {
            bytes[0x2CU + i] = static_cast<std::uint8_t>(0xA0U + i);
        }
    }

    return bytes;
}

std::vector<std::uint8_t> makeSst() {
    std::vector<std::uint8_t> bytes(0xF0U, 0U);

    writeBeU32(bytes, 0x00U, 0xAAA00000U);
    writeBeU32(bytes, 0x04U, 0x0002FFFFU);
    writeBeU32(bytes, 0x08U, 0xBBBB0000U);
    writeBeU32(bytes, 0x0CU, 0x20U);

    writeBeU32(bytes, 0x10U, 0xAAA00001U);
    writeBeU32(bytes, 0x14U, 0xCCCC0000U);
    writeBeU32(bytes, 0x18U, 0xDDDD0000U);
    writeBeU32(bytes, 0x1CU, 0xC0U);

    writeBeU32(bytes, 0x20U, 2U);
    writeBeI16(bytes, 0x24U, 2);
    writeBeI16(bytes, 0x26U, 0);
    writeBeU32(bytes, 0x28U, 0x20000004U);
    writeBeU32(bytes, 0x2CU, 0x20000008U);
    writeBeU32(bytes, 0x30U, 0x12345678U);

    writeBeI16(bytes, 0x34U, 11);
    writeBeI16(bytes, 0x36U, 0);
    writeBeU32(bytes, 0x38U, 0x0B000004U);
    writeBeU32(bytes, 0x3CU, 0x0B000008U);
    writeBeU32(bytes, 0x40U, 0x87654321U);

    writeBeI16(bytes, 0x44U, -1);
    writeBeI16(bytes, 0x46U, 0);

    writeBeI16(bytes, 0x54U, 1);
    writeBeU16(bytes, 0x56U, 0x7777U);
    writeBeU32(bytes, 0x58U, 0x44556677U);

    writeBeI16(bytes, 0x98U, 0);
    writeBeI16(bytes, 0x9CU, 0x0011);
    writeBeI16(bytes, 0x9EU, 0x0022);
    writeBeI16(bytes, 0xB8U, 0x0200);
    writeBeI16(bytes, 0xBAU, 0x1400);
    writeBeI16(bytes, 0xBCU, static_cast<std::int16_t>(0xB400U));

    writeBeU32(bytes, 0xC0U, 1U);
    writeBeI16(bytes, 0xC4U, 3);
    writeBeI16(bytes, 0xC6U, 0);
    writeBeU32(bytes, 0xC8U, 0x30000004U);
    writeBeU32(bytes, 0xCCU, 0x30000008U);
    writeBeU32(bytes, 0xD0U, 0xCAFEBABEU);

    writeBeI16(bytes, 0xD4U, -1);
    writeBeI16(bytes, 0xD6U, 0);

    writeBeI16(bytes, 0xE4U, 0);
    writeBeU16(bytes, 0xE6U, 0x2222U);
    writeBeI16(bytes, 0xE8U, 0x0033);
    writeBeI16(bytes, 0xEAU, 0x0044);

    return bytes;
}

bool hasInfoDiagnosticContaining(const std::vector<ParseDiagnostic>& diagnostics, const std::string& text) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const ParseDiagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Info &&
            diagnostic.message.find(text) != std::string::npos;
    });
}

} // namespace

TEST(SpiceSstSmlParser, ParsesUncompressedSmlTableAndEmbeddedMldSpans) {
    const auto bytes = makeSml();
    const auto result = SmlParser::parse(bytes);

    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.sourceWasCompressedAklz);
    EXPECT_EQ(result.rawHeader0, 0x534D4C30U);
    EXPECT_EQ(result.rawRecordCountWord, 0x0002FFFFU);
    EXPECT_EQ(result.recordCount, 2U);
    ASSERT_EQ(result.records.size(), 2U);

    EXPECT_EQ(result.records[0].rawWord0, 0x00000100U);
    EXPECT_EQ(result.records[0].embeddedMldOffset, 0x28U);
    EXPECT_EQ(result.records[0].embeddedMldSize, 0x04U);
    EXPECT_TRUE(result.records[0].embeddedMldInBounds);
    EXPECT_EQ(result.records[0].embeddedMldBytes, (std::vector<std::uint8_t>{ 0xDEU, 0xADU, 0xBEU, 0xEFU }));

    EXPECT_EQ(result.records[1].embeddedMldOffset, 0x2CU);
    EXPECT_EQ(result.records[1].embeddedMldBytes.size(), 8U);
}

TEST(SpiceSstSmlParser, ParsesSstTopRecordsCommandBlocksAndSentinels) {
    const auto bytes = makeSst();
    const auto result = SstParser::parse(bytes);

    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.sourceWasCompressedAklz);
    EXPECT_EQ(result.recordCount, 2U);
    ASSERT_EQ(result.topLevelRecords.size(), 2U);
    EXPECT_EQ(result.topLevelRecords[0].rawWord4, 0x0002FFFFU);
    EXPECT_EQ(result.topLevelRecords[0].commandBlockOffset, 0x20U);
    EXPECT_EQ(result.topLevelRecords[1].commandBlockOffset, 0xC0U);

    ASSERT_EQ(result.commandBlocks.size(), 2U);
    EXPECT_TRUE(result.commandBlocks[0].valid);
    EXPECT_EQ(result.commandBlocks[0].commandCount, 2U);
    EXPECT_EQ(result.commandBlocks[0].sentinelOffset, 0x44U);
    EXPECT_EQ(result.commandBlocks[0].sentinelType, -1);
    EXPECT_EQ(result.commandBlocks[0].payloadStartOffset, 0x54U);
}

TEST(SpiceSstSmlParser, IgnoresCommandRecordWord12AsOnDiskPayloadOffset) {
    const auto bytes = makeSst();
    const auto result = SstParser::parse(bytes);

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.commandBlocks[0].commands.size(), 2U);
    const auto& command = result.commandBlocks[0].commands[0];
    EXPECT_EQ(command.onDiskWord12, 0x12345678U);
    EXPECT_EQ(command.payloadOffset, 0x54U);
    EXPECT_EQ(command.payloadSize, 0x44U);
    ASSERT_TRUE(command.modelIndex.has_value());
    EXPECT_EQ(*command.modelIndex, 1);
}

TEST(SpiceSstSmlParser, JoinedParserDetectsCountAgreementAndModelIndexLinks) {
    const auto sml = makeSml();
    const auto sst = makeSst();
    const auto result = BattleStageParser::parsePair(sml, sst, "s999");

    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.recordCountsAgree);
    ASSERT_EQ(result.commandLinks.size(), 3U);
    EXPECT_TRUE(result.commandLinks[0].resolved);
    EXPECT_EQ(result.commandLinks[0].modelIndex, 1);
    ASSERT_TRUE(result.commandLinks[0].smlRecordIndex.has_value());
    EXPECT_EQ(*result.commandLinks[0].smlRecordIndex, 1U);
    EXPECT_EQ(result.commandTypeHistogram.size(), 3U);
}

TEST(SpiceSstSmlParser, JoinedParserReportsCountMismatch) {
    const auto sml = makeSml(1U);
    const auto sst = makeSst();
    const auto result = BattleStageParser::parsePair(sml, sst, "s998");

    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.recordCountsAgree);
}

TEST(SpiceSstSmlParser, Type11UsesWalkerSpanAndOnlyProvisionalFields) {
    const auto bytes = makeSst();
    const auto result = SstParser::parse(bytes);

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.commandBlocks[0].commands.size(), 2U);
    const auto& command = result.commandBlocks[0].commands[1];
    EXPECT_EQ(command.type, 11);
    EXPECT_EQ(command.payloadSize, 0x18U);
    EXPECT_EQ(command.fieldSummaries.size(), 6U);
    EXPECT_EQ(command.fieldSummaries[1].name, "fadeRampModeGate");
    EXPECT_EQ(command.fieldSummaries[2].kind, CommandFieldKind::Counter);
    EXPECT_TRUE(hasInfoDiagnosticContaining(result.diagnostics, "0x18 walker span"));
}

TEST(SpiceSstSmlParser, Type11ExposesSeparateTrailingConsumerWindow) {
    const auto bytes = makeSst();
    const auto result = SstParser::parse(bytes);

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.commandBlocks[0].commands.size(), 2U);
    const auto& command = result.commandBlocks[0].commands[1];
    ASSERT_EQ(command.consumerWindows.size(), 1U);
    const auto& window = command.consumerWindows[0];
    EXPECT_EQ(window.name, "type11TrailingConsumerWindow");
    EXPECT_EQ(window.offset, command.payloadOffset + command.payloadSize);
    EXPECT_EQ(window.size, 0x0EU);
    EXPECT_TRUE(window.inBounds);
    ASSERT_EQ(window.fieldSummaries.size(), 3U);
    EXPECT_EQ(window.fieldSummaries[0].offset, 0x20U);
    EXPECT_EQ(window.fieldSummaries[0].name, "motionStepMagnitude");
    EXPECT_EQ(window.fieldSummaries[0].scope, CommandFieldScope::ConsumerTrailing);
    EXPECT_FALSE(window.fieldSummaries[0].provisional);
    EXPECT_EQ(window.fieldSummaries[1].name, "rampDuration");
    EXPECT_EQ(window.fieldSummaries[2].name, "trailingRampParameter");
}

TEST(SpiceSstSmlParser, ConservativeMetadataKeepsEvidenceAndProvisionalStatus) {
    const auto type2Fields = SstParser::fieldSummariesForType(2);
    const auto type6Fields = SstParser::fieldSummariesForType(6);

    ASSERT_FALSE(type2Fields.empty());
    const auto controlWord = std::find_if(type2Fields.begin(), type2Fields.end(), [](const auto& field) {
        return field.name == "packedControlWord";
    });
    ASSERT_NE(controlWord, type2Fields.end());
    EXPECT_EQ(controlWord->evidence, CommandFieldEvidence::GekkoAndCorpus);
    EXPECT_TRUE(controlWord->provisional);

    ASSERT_FALSE(type6Fields.empty());
    EXPECT_EQ(type6Fields[0].evidence, CommandFieldEvidence::CodeSupportedCorpusAbsent);
    EXPECT_TRUE(type6Fields[0].provisional);
}

TEST(SpiceSstSmlParser, ParsesAklzWrappedInputs) {
    const auto sml = makeSml();
    const auto sst = makeSst();
    const auto compressedSml = spice::compression::aklz::compress(sml);
    const auto compressedSst = spice::compression::aklz::compress(sst);
    ASSERT_TRUE(compressedSml.ok());
    ASSERT_TRUE(compressedSst.ok());

    const auto smlResult = SmlParser::parse(compressedSml.bytes);
    const auto sstResult = SstParser::parse(compressedSst.bytes);

    ASSERT_TRUE(smlResult.ok());
    ASSERT_TRUE(sstResult.ok());
    EXPECT_TRUE(smlResult.sourceWasCompressedAklz);
    EXPECT_TRUE(sstResult.sourceWasCompressedAklz);
    EXPECT_EQ(smlResult.records.size(), 2U);
    EXPECT_EQ(sstResult.commandBlocks.size(), 2U);
}
