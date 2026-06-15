#include "../Compression/Aklz.h"
#include "../SpiceMLD/SpiceMLD.h"
#include "../SpiceSstSml/SpiceSstSml.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
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

void writeBeF32(std::vector<std::uint8_t>& bytes, std::size_t offset, float value) {
    writeBeU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
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

std::vector<std::uint8_t> makeSstWithType1LightingRows() {
    std::vector<std::uint8_t> bytes(0x114U, 0U);

    writeBeU32(bytes, 0x00U, 0xAAA00000U);
    writeBeU32(bytes, 0x04U, 0x0001FFFFU);
    writeBeU32(bytes, 0x08U, 0xBBBB0000U);
    writeBeU32(bytes, 0x0CU, 0x20U);

    writeBeU32(bytes, 0x20U, 1U);
    writeBeI16(bytes, 0x24U, 1);
    writeBeI16(bytes, 0x26U, 0);
    writeBeU32(bytes, 0x28U, 0x10000004U);
    writeBeU32(bytes, 0x2CU, 0x10000008U);
    writeBeU32(bytes, 0x30U, 0x01010101U);

    writeBeI16(bytes, 0x34U, -1);
    writeBeI16(bytes, 0x36U, 0);

    constexpr std::size_t firstRow = 0x44U;
    bytes[firstRow + 0x00U] = 4U;
    writeBeI16(bytes, firstRow + 0x02U, 2);
    writeBeU32(bytes, firstRow + 0x04U, 0x60000000U);
    writeBeI16(bytes, firstRow + 0x08U, 0);
    writeBeF32(bytes, firstRow + 0x0CU, 1.25F);
    writeBeF32(bytes, firstRow + 0x10U, -2.5F);
    writeBeF32(bytes, firstRow + 0x14U, 3.75F);
    writeBeF32(bytes, firstRow + 0x30U, 0.1F);
    writeBeF32(bytes, firstRow + 0x34U, 0.2F);
    writeBeF32(bytes, firstRow + 0x38U, 0.3F);
    writeBeF32(bytes, firstRow + 0x3CU, 0.4F);
    writeBeF32(bytes, firstRow + 0x40U, 0.5F);
    writeBeF32(bytes, firstRow + 0x44U, 0.6F);
    writeBeF32(bytes, firstRow + 0x48U, 30.0F);
    writeBeF32(bytes, firstRow + 0x4CU, 60.0F);
    writeBeU32(bytes, firstRow + 0x64U, 0x11223344U);

    constexpr std::size_t sentinelRow = firstRow + 0x68U;
    bytes[sentinelRow + 0x00U] = 0xFFU;
    writeBeI16(bytes, sentinelRow + 0x02U, -1);
    writeBeU32(bytes, sentinelRow + 0x04U, 0x20000000U);
    writeBeI16(bytes, sentinelRow + 0x08U, 3);
    writeBeF32(bytes, sentinelRow + 0x0CU, 9.0F);
    writeBeU32(bytes, sentinelRow + 0x64U, 0x55667788U);

    return bytes;
}

void writeTag(std::vector<std::uint8_t>& bytes, std::size_t offset, const char* tag) {
    bytes[offset + 0U] = static_cast<std::uint8_t>(tag[0]);
    bytes[offset + 1U] = static_cast<std::uint8_t>(tag[1]);
    bytes[offset + 2U] = static_cast<std::uint8_t>(tag[2]);
    bytes[offset + 3U] = static_cast<std::uint8_t>(tag[3]);
}

void writeList(std::vector<std::uint8_t>& bytes, std::size_t offset, std::span<const std::uint32_t> values) {
    writeBeU32(bytes, offset, static_cast<std::uint32_t>(values.size()));
    for (std::size_t i = 0; i < values.size(); ++i) {
        writeBeU32(bytes, offset + 4U + (i * 4U), values[i]);
    }
}

std::vector<std::uint8_t> makeMinimalEmbeddedMld() {
    constexpr std::size_t kEntryOffset = 0x20U;
    constexpr std::size_t kListGroundLinks = 0x100U;
    constexpr std::size_t kListParam2 = 0x108U;
    constexpr std::size_t kListFunctionParams = 0x110U;
    constexpr std::size_t kListObjects = 0x11CU;
    constexpr std::size_t kListGrounds = 0x124U;
    constexpr std::size_t kListMotions = 0x12CU;
    constexpr std::size_t kGrndOffset = 0x140U;
    constexpr std::size_t kTextureTable = 0x170U;

    std::vector<std::uint8_t> bytes(0x180U, 0U);
    writeBeU32(bytes, 0x00U, 1U);
    writeBeU32(bytes, 0x04U, static_cast<std::uint32_t>(kEntryOffset));
    writeBeU32(bytes, 0x08U, static_cast<std::uint32_t>(kListFunctionParams));
    writeBeU32(bytes, 0x0CU, static_cast<std::uint32_t>(kGrndOffset));
    writeBeU32(bytes, 0x10U, static_cast<std::uint32_t>(kTextureTable));

    writeBeU32(bytes, kEntryOffset + 0x00U, 0x101U);
    writeBeU32(bytes, kEntryOffset + 0x04U, 0x202U);
    writeBeU32(bytes, kEntryOffset + 0x08U, static_cast<std::uint32_t>(kListGroundLinks));
    writeBeU32(bytes, kEntryOffset + 0x0CU, static_cast<std::uint32_t>(kListParam2));
    writeBeU32(bytes, kEntryOffset + 0x10U, static_cast<std::uint32_t>(kListFunctionParams));
    writeBeU32(bytes, kEntryOffset + 0x14U, static_cast<std::uint32_t>(kListObjects));
    writeBeU32(bytes, kEntryOffset + 0x18U, static_cast<std::uint32_t>(kListGrounds));
    writeBeU32(bytes, kEntryOffset + 0x1CU, static_cast<std::uint32_t>(kListMotions));
    const std::string fxn = "wall";
    std::copy(fxn.begin(), fxn.end(), bytes.begin() + static_cast<std::ptrdiff_t>(kEntryOffset + 0x24U));
    writeBeF32(bytes, kEntryOffset + 0x44U, 1.0F);
    writeBeF32(bytes, kEntryOffset + 0x48U, 2.0F);
    writeBeF32(bytes, kEntryOffset + 0x4CU, 3.0F);
    writeBeF32(bytes, kEntryOffset + 0x50U, 4.0F);
    writeBeF32(bytes, kEntryOffset + 0x54U, 5.0F);
    writeBeF32(bytes, kEntryOffset + 0x58U, 6.0F);
    writeBeF32(bytes, kEntryOffset + 0x5CU, 1.0F);
    writeBeF32(bytes, kEntryOffset + 0x60U, 1.0F);
    writeBeF32(bytes, kEntryOffset + 0x64U, 1.0F);

    const std::uint32_t groundLinks[] = { 7U };
    const std::uint32_t functionParams[] = { 0x333U, 0x444U };
    const std::uint32_t grounds[] = { static_cast<std::uint32_t>(kGrndOffset) };
    const std::vector<std::uint32_t> empty{};
    writeList(bytes, kListGroundLinks, groundLinks);
    writeList(bytes, kListParam2, empty);
    writeList(bytes, kListFunctionParams, functionParams);
    writeList(bytes, kListObjects, empty);
    writeList(bytes, kListGrounds, grounds);
    writeList(bytes, kListMotions, empty);

    writeTag(bytes, kGrndOffset, "GRND");
    writeBeU32(bytes, kGrndOffset + 4U, 0x2CU);
    writeBeU16(bytes, kGrndOffset + 0x20U, 2U);
    writeBeU16(bytes, kGrndOffset + 0x22U, 3U);
    writeBeU16(bytes, kGrndOffset + 0x24U, 4U);
    writeBeU16(bytes, kGrndOffset + 0x26U, 5U);

    writeBeU32(bytes, kTextureTable, 0U);
    return bytes;
}

std::vector<std::uint8_t> makeSmlWithEmbeddedMld(std::span<const std::uint8_t> embeddedMld) {
    std::vector<std::uint8_t> bytes(0x20U + embeddedMld.size(), 0U);
    writeBeU32(bytes, 0x00U, 0x534D4C30U);
    writeBeU32(bytes, 0x04U, 0x0001FFFFU);
    writeBeU32(bytes, 0x08U, 0x00000100U);
    writeBeU32(bytes, 0x0CU, 0x20U);
    writeBeU32(bytes, 0x10U, static_cast<std::uint32_t>(embeddedMld.size()));
    writeBeU32(bytes, 0x14U, 0x11111111U);
    std::copy(embeddedMld.begin(), embeddedMld.end(), bytes.begin() + 0x20U);
    return bytes;
}

std::vector<std::uint8_t> makeSmlWithOutOfBoundsPayload() {
    std::vector<std::uint8_t> bytes(0x20U, 0U);
    writeBeU32(bytes, 0x00U, 0x534D4C30U);
    writeBeU32(bytes, 0x04U, 0x0001FFFFU);
    writeBeU32(bytes, 0x08U, 0x00000100U);
    writeBeU32(bytes, 0x0CU, 0x80U);
    writeBeU32(bytes, 0x10U, 0x10U);
    writeBeU32(bytes, 0x14U, 0x11111111U);
    return bytes;
}

std::vector<std::uint8_t> makeSstWithType0AndExtraCommand() {
    std::vector<std::uint8_t> bytes(0xB0U, 0U);
    writeBeU32(bytes, 0x00U, 0xAAA00000U);
    writeBeU32(bytes, 0x04U, 0x0001FFFFU);
    writeBeU32(bytes, 0x08U, 0xBBBB0000U);
    writeBeU32(bytes, 0x0CU, 0x20U);

    writeBeU32(bytes, 0x20U, 2U);
    writeBeI16(bytes, 0x24U, 0);
    writeBeI16(bytes, 0x26U, 0);
    writeBeU32(bytes, 0x28U, 0x20000004U);
    writeBeU32(bytes, 0x2CU, 0x20000008U);
    writeBeU32(bytes, 0x30U, 0x01020304U);

    writeBeI16(bytes, 0x34U, 3);
    writeBeI16(bytes, 0x36U, 0);
    writeBeU32(bytes, 0x38U, 0x30000004U);
    writeBeU32(bytes, 0x3CU, 0x30000008U);
    writeBeU32(bytes, 0x40U, 0x05060708U);

    writeBeI16(bytes, 0x44U, -1);
    writeBeI16(bytes, 0x46U, 0);

    constexpr std::size_t type0Payload = 0x54U;
    writeBeI16(bytes, type0Payload + 0x16U, -3);
    writeBeI16(bytes, type0Payload + 0x18U, 3);
    writeBeF32(bytes, type0Payload + 0x1CU, 10.0F);
    writeBeF32(bytes, type0Payload + 0x20U, 20.0F);
    writeBeF32(bytes, type0Payload + 0x24U, 30.0F);
    writeBeU32(bytes, type0Payload + 0x28U, 0x00000100U);
    writeBeU32(bytes, type0Payload + 0x2CU, 0x00000200U);
    writeBeU32(bytes, type0Payload + 0x30U, 0x00000300U);
    writeBeF32(bytes, type0Payload + 0x34U, 1.0F);
    writeBeF32(bytes, type0Payload + 0x38U, 1.0F);
    writeBeF32(bytes, type0Payload + 0x3CU, 1.0F);
    bytes[type0Payload + 0x44U] = 2U;

    constexpr std::size_t type3Payload = type0Payload + 0x4CU;
    writeBeI16(bytes, type3Payload + 0x00U, 0);
    writeBeU16(bytes, type3Payload + 0x02U, 0x2222U);
    writeBeI16(bytes, type3Payload + 0x04U, 0x0033);
    writeBeI16(bytes, type3Payload + 0x06U, 0x0044);

    return bytes;
}

std::filesystem::path makeTempOutputDir(const std::string& name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> readAllBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool hasInfoDiagnosticContaining(const std::vector<ParseDiagnostic>& diagnostics, const std::string& text) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const ParseDiagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Info &&
            diagnostic.message.find(text) != std::string::npos;
    });
}

spice::mld::model::BlenderIrScene makeSingleEntryBlenderIrScene(
    std::uint32_t sourceEntryId,
    std::uint32_t objectAddress,
    std::uint32_t motionAddress,
    std::string textureName) {
    spice::mld::model::BlenderIrScene scene{};

    spice::mld::model::BlenderIrTexture texture{};
    texture.hasTextureId = true;
    texture.textureId = 7U;
    texture.textureName = textureName;
    scene.textures.push_back(std::move(texture));

    spice::mld::model::BlenderIrMesh mesh{};
    spice::mld::model::BlenderIrMaterial material{};
    material.textureName = textureName;
    mesh.materials.push_back(std::move(material));
    scene.meshes.push_back(std::move(mesh));

    spice::mld::model::BlenderIrObjectTree tree{};
    tree.label = "tree";
    tree.sourceObjectAddress = objectAddress;
    spice::mld::model::BlenderIrNode node{};
    node.meshIndex = 0U;
    tree.nodes.push_back(std::move(node));
    tree.rootNodeIndices.push_back(0U);
    scene.objectTrees.push_back(std::move(tree));

    spice::mld::model::BlenderIrInstance instance{};
    instance.sourceEntryId = sourceEntryId;
    instance.tableIndex = 0U;
    instance.objectAddresses.push_back(objectAddress);
    instance.meshIndices.push_back(0U);
    instance.objectTreeIndices.push_back(0U);
    scene.indexEntries.push_back(std::move(instance));

    spice::mld::model::BlenderIrAnimation animation{};
    animation.sourceEntryId = sourceEntryId;
    animation.tableIndex = 0U;
    animation.sourceObjectAddress = objectAddress;
    animation.sourceMotionAddress = motionAddress;
    animation.motionSlot = 1U;
    animation.objectTreeIndex = 0U;
    spice::mld::model::BlenderIrNodeAnimation nodeAnimation{};
    nodeAnimation.nodeIndex = 0U;
    animation.nodes.push_back(std::move(nodeAnimation));
    scene.animations.push_back(std::move(animation));

    return scene;
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

TEST(SpiceSstSmlParser, SummarizesValidLookingEmbeddedMldHeader) {
    const auto embeddedMld = makeMinimalEmbeddedMld();
    const auto result = SmlParser::parse(makeSmlWithEmbeddedMld(embeddedMld));

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.records.size(), 1U);
    ASSERT_TRUE(result.records[0].embeddedMldSummary.has_value());
    const auto& summary = *result.records[0].embeddedMldSummary;
    EXPECT_TRUE(summary.parseAttempted);
    EXPECT_TRUE(summary.validLookingHeader);
    ASSERT_TRUE(summary.entryCount.has_value());
    EXPECT_EQ(*summary.entryCount, 1U);
    ASSERT_TRUE(summary.indexTableOffset.has_value());
    EXPECT_EQ(*summary.indexTableOffset, 0x20U);
    ASSERT_TRUE(summary.textureTableOffset.has_value());
    EXPECT_EQ(*summary.textureTableOffset, 0x170U);
    ASSERT_TRUE(summary.textureArchiveCount.has_value());
    EXPECT_EQ(*summary.textureArchiveCount, 0U);
    EXPECT_FALSE(summary.hasNjcm);
    EXPECT_FALSE(summary.hasGcix);
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

TEST(SpiceSstSmlParser, JoinedParserDetectsCountAgreementAndLocalObjectSlotLinks) {
    const auto embeddedMld = makeMinimalEmbeddedMld();
    const std::uint32_t firstPayloadOffset = 0x28U;
    const std::uint32_t secondPayloadOffset =
        firstPayloadOffset + static_cast<std::uint32_t>(embeddedMld.size());
    std::vector<std::uint8_t> sml(secondPayloadOffset + embeddedMld.size(), 0U);
    writeBeU32(sml, 0x00U, 0x534D4C30U);
    writeBeU32(sml, 0x04U, 0x0002FFFFU);
    writeBeU32(sml, 0x08U, 0x00000100U);
    writeBeU32(sml, 0x0CU, firstPayloadOffset);
    writeBeU32(sml, 0x10U, static_cast<std::uint32_t>(embeddedMld.size()));
    writeBeU32(sml, 0x14U, 0x11111111U);
    writeBeU32(sml, 0x18U, 0x00000101U);
    writeBeU32(sml, 0x1CU, secondPayloadOffset);
    writeBeU32(sml, 0x20U, static_cast<std::uint32_t>(embeddedMld.size()));
    writeBeU32(sml, 0x24U, 0x22222222U);
    std::copy(embeddedMld.begin(), embeddedMld.end(), sml.begin() + firstPayloadOffset);
    std::copy(embeddedMld.begin(), embeddedMld.end(), sml.begin() + secondPayloadOffset);
    const auto sst = makeSst();
    const auto result = BattleStageParser::parsePair(sml, sst, "s999");

    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.recordCountsAgree);
    EXPECT_EQ(result.activeRowRuntimeContext.provedRowStride, 0x14U);
    EXPECT_EQ(result.activeRowRuntimeContext.allocationWidthPerRecord, 0x2CU);
    ASSERT_EQ(result.localObjectSlotLinks.size(), 3U);
    EXPECT_EQ(result.localObjectSlotLinks[0].topLevelRecordIndex, 0U);
    EXPECT_EQ(result.localObjectSlotLinks[0].localSlotIndex, 1);
    EXPECT_TRUE(result.localObjectSlotLinks[0].slotIndexRangeKnown);
    EXPECT_FALSE(result.localObjectSlotLinks[0].slotIndexInRange);
    ASSERT_TRUE(result.localObjectSlotLinks[0].owningSmlRecordIndex.has_value());
    EXPECT_EQ(*result.localObjectSlotLinks[0].owningSmlRecordIndex, 0U);
    ASSERT_TRUE(result.localObjectSlotLinks[0].localSlotCount.has_value());
    EXPECT_EQ(*result.localObjectSlotLinks[0].localSlotCount, 1U);
    EXPECT_EQ(result.commandTypeHistogram.size(), 3U);
}

TEST(SpiceSstSmlParser, JoinedParserResolvesModelIndexAsSameRecordLocalSlot) {
    const auto sml = makeSmlWithEmbeddedMld(makeMinimalEmbeddedMld());
    const auto sst = makeSstWithType0AndExtraCommand();
    const auto result = BattleStageParser::parsePair(sml, sst, "s777");

    ASSERT_TRUE(result.ok());
    const auto linkIt = std::find_if(result.localObjectSlotLinks.begin(),
        result.localObjectSlotLinks.end(),
        [](const ResolvedLocalObjectSlotLink& link) {
            return link.commandType == 3;
        });
    ASSERT_NE(linkIt, result.localObjectSlotLinks.end());
    EXPECT_EQ(linkIt->topLevelRecordIndex, 0U);
    EXPECT_EQ(linkIt->localSlotIndex, 0);
    EXPECT_TRUE(linkIt->slotIndexRangeKnown);
    EXPECT_TRUE(linkIt->slotIndexInRange);
    ASSERT_TRUE(linkIt->owningSmlRecordIndex.has_value());
    EXPECT_EQ(*linkIt->owningSmlRecordIndex, 0U);
}

TEST(SpiceSstSmlParser, JoinedParserWarnsForOutOfRangeLocalObjectSlot) {
    const auto sml = makeSmlWithEmbeddedMld(makeMinimalEmbeddedMld());
    auto sst = makeSstWithType0AndExtraCommand();
    constexpr std::size_t type3Payload = 0x54U + 0x4CU;
    writeBeI16(sst, type3Payload, 2);

    const auto result = BattleStageParser::parsePair(sml, sst, "s776");

    EXPECT_TRUE(result.ok());
    const auto warning = std::find_if(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Warning &&
            diagnostic.message.find("local object slot range") != std::string::npos;
    });
    EXPECT_NE(warning, result.diagnostics.end());
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

TEST(SpiceSstSmlParser, DecodesType1LightingRowsAndStopsAtSentinel) {
    const auto bytes = makeSstWithType1LightingRows();
    const auto result = SstParser::parse(bytes);

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.commandBlocks.size(), 1U);
    ASSERT_EQ(result.commandBlocks[0].commands.size(), 1U);
    const auto& command = result.commandBlocks[0].commands[0];
    EXPECT_EQ(command.type, 1);
    EXPECT_EQ(command.payloadSize, 0xD0U);
    EXPECT_EQ(command.payloadBytes.size(), 0xD0U);
    ASSERT_EQ(command.type1LightingRows.size(), 2U);

    const auto& row = command.type1LightingRows[0];
    EXPECT_EQ(row.index, 0U);
    EXPECT_EQ(row.rowOffset, command.payloadOffset);
    EXPECT_EQ(row.state, 4);
    EXPECT_FALSE(row.sentinel);
    EXPECT_EQ(row.classSelector, 2);
    EXPECT_EQ(row.flags, 0x60000000U);
    EXPECT_TRUE(row.enablesLightSetup);
    EXPECT_TRUE(row.enablesVectorSetup);
    EXPECT_EQ(row.runtimeSlotId, 0);
    EXPECT_FLOAT_EQ(row.lightVector.x, 1.25F);
    EXPECT_FLOAT_EQ(row.lightVector.y, -2.5F);
    EXPECT_FLOAT_EQ(row.lightVector.z, 3.75F);
    EXPECT_FLOAT_EQ(row.slotRgb.x, 0.1F);
    EXPECT_FLOAT_EQ(row.slotRgb.y, 0.2F);
    EXPECT_FLOAT_EQ(row.slotRgb.z, 0.3F);
    EXPECT_FLOAT_EQ(row.globalRgb.x, 0.4F);
    EXPECT_FLOAT_EQ(row.globalRgb.y, 0.5F);
    EXPECT_FLOAT_EQ(row.globalRgb.z, 0.6F);
    EXPECT_FLOAT_EQ(row.attenuationOrSpot0, 30.0F);
    EXPECT_FLOAT_EQ(row.attenuationOrSpot1, 60.0F);
    EXPECT_EQ(row.rawTailWord, 0x11223344U);
    EXPECT_EQ(row.rawBytes.size(), 0x68U);

    const auto& sentinel = command.type1LightingRows[1];
    EXPECT_EQ(sentinel.index, 1U);
    EXPECT_TRUE(sentinel.sentinel);
    EXPECT_EQ(sentinel.state, -1);
    EXPECT_EQ(sentinel.classSelector, -1);
    EXPECT_EQ(sentinel.runtimeSlotId, 3);
    EXPECT_FALSE(sentinel.enablesLightSetup);
    EXPECT_TRUE(sentinel.enablesVectorSetup);
    EXPECT_EQ(sentinel.rawTailWord, 0x55667788U);
}

TEST(SpiceSstSmlParser, Type1FieldSummariesUseLightingMetadata) {
    const auto fields = SstParser::fieldSummariesForType(1);

    ASSERT_FALSE(fields.empty());
    const auto runtimeSlot = std::find_if(fields.begin(), fields.end(), [](const auto& field) {
        return field.name == "runtimeSlotId";
    });
    ASSERT_NE(runtimeSlot, fields.end());
    EXPECT_EQ(runtimeSlot->kind, CommandFieldKind::RuntimeSlot);
    EXPECT_EQ(runtimeSlot->evidence, CommandFieldEvidence::GekkoAndCorpus);

    const auto slotRgb = std::find_if(fields.begin(), fields.end(), [](const auto& field) {
        return field.name == "slotRgbR";
    });
    ASSERT_NE(slotRgb, fields.end());
    EXPECT_EQ(slotRgb->width, CommandFieldWidth::F32);
    EXPECT_EQ(slotRgb->evidence, CommandFieldEvidence::GekkoAndCorpus);

    const auto attenuation = std::find_if(fields.begin(), fields.end(), [](const auto& field) {
        return field.name == "attenuationOrSpot1";
    });
    ASSERT_NE(attenuation, fields.end());
    EXPECT_TRUE(attenuation->provisional);
}

TEST(SpiceSstSmlParser, Type0FieldSummariesUseCallbackBackedTransformMetadata) {
    const auto fields = SstParser::fieldSummariesForType(0);

    const auto position = std::find_if(fields.begin(), fields.end(), [](const auto& field) {
        return field.name == "transformPositionX";
    });
    ASSERT_NE(position, fields.end());
    EXPECT_EQ(position->width, CommandFieldWidth::F32);
    EXPECT_EQ(position->kind, CommandFieldKind::VectorComponent);
    EXPECT_EQ(position->evidence, CommandFieldEvidence::GekkoAndCorpus);

    const auto rotation = std::find_if(fields.begin(), fields.end(), [](const auto& field) {
        return field.name == "rotationAngleY";
    });
    ASSERT_NE(rotation, fields.end());
    EXPECT_EQ(rotation->width, CommandFieldWidth::U32);
    EXPECT_EQ(rotation->kind, CommandFieldKind::RotationComponent);
    EXPECT_EQ(rotation->evidence, CommandFieldEvidence::Gekko);

    const auto scale = std::find_if(fields.begin(), fields.end(), [](const auto& field) {
        return field.name == "scaleZ";
    });
    ASSERT_NE(scale, fields.end());
    EXPECT_EQ(scale->width, CommandFieldWidth::F32);
    EXPECT_EQ(scale->evidence, CommandFieldEvidence::GekkoAndCorpus);

    const auto renderAction = std::find_if(fields.begin(), fields.end(), [](const auto& field) {
        return field.name == "renderActionByte";
    });
    ASSERT_NE(renderAction, fields.end());
    EXPECT_EQ(renderAction->offset, 0x44U);
    EXPECT_EQ(renderAction->width, CommandFieldWidth::U8);
    EXPECT_EQ(renderAction->evidence, CommandFieldEvidence::GekkoAndCorpus);
}

TEST(SpiceSstSmlParser, ConservativeMetadataKeepsEvidenceAndProvisionalStatus) {
    const auto type2Fields = SstParser::fieldSummariesForType(2);
    const auto type3Fields = SstParser::fieldSummariesForType(3);
    const auto type8Fields = SstParser::fieldSummariesForType(8);
    const auto type6Fields = SstParser::fieldSummariesForType(6);

    ASSERT_FALSE(type2Fields.empty());
    const auto controlWord = std::find_if(type2Fields.begin(), type2Fields.end(), [](const auto& field) {
        return field.name == "packedControlWord";
    });
    ASSERT_NE(controlWord, type2Fields.end());
    EXPECT_EQ(controlWord->evidence, CommandFieldEvidence::GekkoAndCorpus);
    EXPECT_TRUE(controlWord->provisional);

    const auto minDistance = std::find_if(type2Fields.begin(), type2Fields.end(), [](const auto& field) {
        return field.name == "minimumDistanceRange";
    });
    ASSERT_NE(minDistance, type2Fields.end());
    EXPECT_EQ(minDistance->offset, 0x1CU);
    EXPECT_EQ(minDistance->width, CommandFieldWidth::F32);

    const auto yMinimum = std::find_if(type2Fields.begin(), type2Fields.end(), [](const auto& field) {
        return field.name == "optionalYMinimum";
    });
    ASSERT_NE(yMinimum, type2Fields.end());
    EXPECT_EQ(yMinimum->offset, 0x34U);

    const auto radialMaximum = std::find_if(type2Fields.begin(), type2Fields.end(), [](const auto& field) {
        return field.name == "optionalXZRadialMaximum";
    });
    ASSERT_NE(radialMaximum, type2Fields.end());
    EXPECT_EQ(radialMaximum->offset, 0x40U);

    ASSERT_EQ(type3Fields.size(), 4U);
    EXPECT_EQ(type3Fields[0].name, "modelIndex");
    EXPECT_EQ(type3Fields[0].evidence, CommandFieldEvidence::GekkoAndCorpus);
    EXPECT_FALSE(type3Fields[0].provisional);
    EXPECT_EQ(type3Fields[1].name, "nodeTraversalLookupKey");
    EXPECT_EQ(type3Fields[1].width, CommandFieldWidth::U16);
    EXPECT_EQ(type3Fields[1].evidence, CommandFieldEvidence::GekkoAndCorpus);
    EXPECT_TRUE(type3Fields[1].provisional);
    EXPECT_EQ(type3Fields[2].name, "textureCoordinateDeltaU");
    EXPECT_EQ(type3Fields[2].width, CommandFieldWidth::I16);
    EXPECT_EQ(type3Fields[2].evidence, CommandFieldEvidence::GekkoAndCorpus);
    EXPECT_TRUE(type3Fields[2].provisional);
    EXPECT_EQ(type3Fields[3].name, "textureCoordinateDeltaV");
    EXPECT_EQ(type3Fields[3].width, CommandFieldWidth::I16);
    EXPECT_EQ(type3Fields[3].evidence, CommandFieldEvidence::GekkoAndCorpus);
    EXPECT_TRUE(type3Fields[3].provisional);

    ASSERT_EQ(type8Fields.size(), 7U);
    EXPECT_EQ(type8Fields[0].name, "modelIndex");
    EXPECT_EQ(type8Fields[0].evidence, CommandFieldEvidence::GekkoAndCorpus);
    EXPECT_FALSE(type8Fields[0].provisional);
    EXPECT_EQ(type8Fields[1].name, "nodeTraversalLookupKey");
    EXPECT_EQ(type8Fields[1].kind, CommandFieldKind::LookupKey);
    EXPECT_EQ(type8Fields[1].evidence, CommandFieldEvidence::GekkoAndCorpus);
    EXPECT_TRUE(type8Fields[1].provisional);
    EXPECT_EQ(type8Fields[2].name, "textureTileWidth");
    EXPECT_EQ(type8Fields[3].name, "textureTileHeight");
    EXPECT_EQ(type8Fields[4].name, "texturePageSize");
    EXPECT_EQ(type8Fields[5].name, "textureAnimationFrameCount");
    EXPECT_EQ(type8Fields[5].kind, CommandFieldKind::Counter);
    EXPECT_EQ(type8Fields[6].name, "frameHoldDuration");
    EXPECT_EQ(type8Fields[6].kind, CommandFieldKind::Duration);
    for (const auto& field : type8Fields) {
        EXPECT_EQ(field.evidence, CommandFieldEvidence::GekkoAndCorpus);
    }

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

TEST(SpiceSstSmlExport, ExtractsEmbeddedMldPayloadsAndReportsMissingSst) {
    const auto smlParsed = SmlParser::parse(makeSml(), "s999.sml");
    ASSERT_TRUE(smlParsed.ok());

    const auto outputDir = makeTempOutputDir("spice_sst_sml_export_missing_sst");
    SmlEmbeddedMldExportOptions options{};
    options.stageOutputDir = outputDir / "s999";
    options.stem = "s999";
    options.writeEmbeddedMldPayloads = true;
    options.writeCommandMap = true;

    const auto result = exportSmlEmbeddedMldsAndCommandMap(smlParsed, nullptr, options);

    EXPECT_TRUE(result.wroteManifest);
    EXPECT_FALSE(result.wroteCommandMap);
    ASSERT_EQ(result.entries.size(), 2U);
    EXPECT_TRUE(result.entries[0].wroteEmbeddedMld);
    EXPECT_TRUE(std::filesystem::exists(options.stageOutputDir / "embedded_mld" / "s999_sml_entry_0.mld"));
    EXPECT_TRUE(std::filesystem::exists(options.stageOutputDir / "embedded_mld" / "s999_sml_entry_1.mld"));

    const auto manifest = readTextFile(result.manifestPath);
    EXPECT_NE(manifest.find("No same-stem SST"), std::string::npos);
}

TEST(SpiceSstSmlExport, ReportsOutOfBoundsSmlRecordWithoutWritingPayload) {
    const auto smlParsed = SmlParser::parse(makeSmlWithOutOfBoundsPayload(), "bad.sml");
    ASSERT_FALSE(smlParsed.ok());

    const auto outputDir = makeTempOutputDir("spice_sst_sml_export_oob");
    SmlEmbeddedMldExportOptions options{};
    options.stageOutputDir = outputDir / "bad";
    options.stem = "bad";
    options.writeEmbeddedMldPayloads = true;

    const auto result = exportSmlEmbeddedMldsAndCommandMap(smlParsed, nullptr, options);

    ASSERT_EQ(result.entries.size(), 1U);
    EXPECT_FALSE(result.entries[0].wroteEmbeddedMld);
    EXPECT_FALSE(std::filesystem::exists(options.stageOutputDir / "embedded_mld" / "bad_sml_entry_0.mld"));
    ASSERT_FALSE(result.entries[0].diagnostics.empty());
    EXPECT_NE(result.entries[0].diagnostics[0].find("out of bounds"), std::string::npos);
}

TEST(SpiceSstSmlExport, WritesSameIndexCommandMapWithType0AndExtraCommandMetadata) {
    const auto smlParsed = SmlParser::parse(makeSml(1U), "s777.sml");
    const auto sstParsed = SstParser::parse(makeSstWithType0AndExtraCommand(), "s777.sst");
    ASSERT_TRUE(smlParsed.ok());
    ASSERT_TRUE(sstParsed.ok());

    const auto outputDir = makeTempOutputDir("spice_sst_sml_export_command_map");
    SmlEmbeddedMldExportOptions options{};
    options.stageOutputDir = outputDir / "s777";
    options.stageAnnotationRepositoryDir = outputDir / "state_annotations";
    options.stem = "s777";
    options.writeEmbeddedMldPayloads = true;
    options.writeCommandMap = true;
    const auto combinedIrSourcePath = outputDir / "transient" / "s777_combined_blender_ir_scene.json";
    std::filesystem::create_directories(combinedIrSourcePath.parent_path());
    {
        std::ofstream combinedIrSource(combinedIrSourcePath, std::ios::binary);
        combinedIrSource << "{\"scene\":\"combined\"}\n";
    }
    options.combinedBlenderIrPath = combinedIrSourcePath;
    spice::sstsml::SmlBlenderIrEntrySummary blenderSummary{};
    blenderSummary.meshCount = 3U;
    blenderSummary.objectTreeCount = 1U;
    blenderSummary.indexEntryCount = 1U;
    blenderSummary.textureCount = 2U;
    blenderSummary.animationCount = 1U;
    blenderSummary.animationNodeCount = 2U;
    blenderSummary.animationPositionKeyCount = 60U;
    blenderSummary.animationRotationKeyCount = 60U;
    blenderSummary.varyingAnimationChannelCount = 2U;
    blenderSummary.indexEntryNames.push_back("wall.nj");
    blenderSummary.varyingAnimationChannels.push_back("table=0,objectTree=0,motionSlot=0,node=0,channel=position");
    options.blenderIrSummariesByRecordIndex[0U] = blenderSummary;

    const auto result = exportSmlEmbeddedMldsAndCommandMap(smlParsed, &sstParsed, options);

    ASSERT_TRUE(result.commandMapPath.has_value());
    EXPECT_TRUE(result.wroteCommandMap);
    const auto commandMap = readTextFile(*result.commandMapPath);
    EXPECT_NE(commandMap.find("\"index\":0"), std::string::npos);
    EXPECT_NE(commandMap.find("\"type\":0"), std::string::npos);
    EXPECT_NE(commandMap.find("\"type\":3"), std::string::npos);
    EXPECT_NE(commandMap.find("\"battleObjectClassSelector\":3"), std::string::npos);
    EXPECT_NE(commandMap.find("\"lookupResourceIndex\":-3"), std::string::npos);
    EXPECT_NE(commandMap.find("\"renderActionByte\":2"), std::string::npos);
    EXPECT_NE(commandMap.find("\"embeddedMldSummary\""), std::string::npos);
    EXPECT_NE(commandMap.find("\"activeRowRuntimeContext\""), std::string::npos);
    EXPECT_NE(commandMap.find("\"provedRowStride\":20"), std::string::npos);
    EXPECT_NE(commandMap.find("\"localObjectSlotLink\""), std::string::npos);
    EXPECT_NE(commandMap.find("\"owningSmlRecordIndex\":0"), std::string::npos);
    EXPECT_NE(commandMap.find("\"localSlotIndex\":0"), std::string::npos);
    EXPECT_EQ(commandMap.find("\"resolvedSmlRecordIndex\""), std::string::npos);

    ASSERT_TRUE(result.stageAnnotationTemplatePath.has_value());
    EXPECT_EQ(*result.stageAnnotationTemplatePath, outputDir / "state_annotations" / "s777" / "s777.stage_annotation.json");
    EXPECT_TRUE(result.wroteStageAnnotationTemplate);
    ASSERT_TRUE(result.stageAnnotationMediaDir.has_value());
    EXPECT_TRUE(result.createdStageAnnotationMediaDir);
    EXPECT_FALSE(std::filesystem::exists(*result.stageAnnotationMediaDir / "entry_0"));
    ASSERT_TRUE(result.stageAnnotationCombinedBlenderIrPath.has_value());
    EXPECT_TRUE(result.copiedStageAnnotationCombinedBlenderIr);
    EXPECT_EQ(
        *result.stageAnnotationCombinedBlenderIrPath,
        outputDir / "state_annotations" / "s777" / "s777_combined_blender_ir_scene.json");
    EXPECT_EQ(readTextFile(*result.stageAnnotationCombinedBlenderIrPath), "{\"scene\":\"combined\"}\n");
    const auto annotationTemplate = readTextFile(*result.stageAnnotationTemplatePath);
    EXPECT_NE(annotationTemplate.find("\"schema\":\"spice_sst_sml_stage_annotation_v1\""), std::string::npos);
    EXPECT_NE(annotationTemplate.find("\"documentRole\":\"living_stage_annotation\""), std::string::npos);
    EXPECT_NE(annotationTemplate.find("\"mediaDirectory\":\"s777.stage_annotation_media\""), std::string::npos);
    EXPECT_NE(annotationTemplate.find("\"combinedBlenderIrScene\":\"s777_combined_blender_ir_scene.json\""), std::string::npos);
    EXPECT_NE(annotationTemplate.find("\"stageNotes\""), std::string::npos);
    EXPECT_NE(annotationTemplate.find("\"overview\":\"\""), std::string::npos);
    EXPECT_NE(annotationTemplate.find("\"layoutNotes\":\"\""), std::string::npos);
    EXPECT_NE(annotationTemplate.find("\"runtimeNotes\":\"\""), std::string::npos);
    EXPECT_NE(annotationTemplate.find("\"smlSstNotes\":\"\""), std::string::npos);
    EXPECT_NE(annotationTemplate.find("\"resources\":[]"), std::string::npos);
    EXPECT_EQ(annotationTemplate.find("\"mediaDirectory\":\"s777.stage_annotation_media/entry_0\""), std::string::npos);
    EXPECT_NE(annotationTemplate.find("\"humanAnnotations\""), std::string::npos);
    EXPECT_NE(annotationTemplate.find("\"media\":[]"), std::string::npos);
    EXPECT_NE(annotationTemplate.find("\"visualRole\":\"\""), std::string::npos);
    EXPECT_NE(annotationTemplate.find("\"blenderIrSummary\""), std::string::npos);
    EXPECT_NE(annotationTemplate.find("\"meshCount\":3"), std::string::npos);
    EXPECT_NE(annotationTemplate.find("\"indexEntryNames\""), std::string::npos);
    EXPECT_NE(annotationTemplate.find("wall.nj"), std::string::npos);
    EXPECT_NE(annotationTemplate.find("\"hasVaryingAnimation\":true"), std::string::npos);
    EXPECT_NE(annotationTemplate.find("\"commandTypes\":[0,3]"), std::string::npos);
    EXPECT_NE(annotationTemplate.find("\"commandTypeHistogram\""), std::string::npos);
    EXPECT_NE(annotationTemplate.find("\"localObjectSlotLink\""), std::string::npos);
    EXPECT_EQ(annotationTemplate.find("\"resolvedSmlRecordIndex\""), std::string::npos);
    EXPECT_NE(annotationTemplate.find("\"suspectedRuntimeBehavior\":\"\""), std::string::npos);
}

TEST(SpiceSstSmlExport, PreservesExistingLivingStageAnnotationDocument) {
    const auto smlParsed = SmlParser::parse(makeSml(1U), "s779.sml");
    const auto sstParsed = SstParser::parse(makeSst(), "s779.sst");
    ASSERT_TRUE(smlParsed.ok());
    ASSERT_TRUE(sstParsed.ok());

    const auto outputDir = makeTempOutputDir("spice_sst_sml_export_preserve_annotation");
    const auto annotationDir = outputDir / "state_annotations" / "s779";
    std::filesystem::create_directories(annotationDir);
    const auto annotationPath = annotationDir / "s779.stage_annotation.json";
    {
        std::ofstream existing(annotationPath, std::ios::binary);
        existing << "{\"schema\":\"spice_sst_sml_stage_annotation_v1\",\"manual\":\"keep me\"}\n";
    }

    SmlEmbeddedMldExportOptions options{};
    options.stageOutputDir = outputDir / "s779";
    options.stageAnnotationRepositoryDir = outputDir / "state_annotations";
    options.stem = "s779";
    options.writeEmbeddedMldPayloads = false;
    options.writeCommandMap = false;
    const auto combinedIrSourcePath = outputDir / "transient" / "s779_combined_blender_ir_scene.json";
    std::filesystem::create_directories(combinedIrSourcePath.parent_path());
    {
        std::ofstream combinedIrSource(combinedIrSourcePath, std::ios::binary);
        combinedIrSource << "{\"scene\":\"updated\"}\n";
    }
    options.combinedBlenderIrPath = combinedIrSourcePath;

    const auto result = exportSmlEmbeddedMldsAndCommandMap(smlParsed, &sstParsed, options);

    ASSERT_TRUE(result.stageAnnotationTemplatePath.has_value());
    EXPECT_EQ(*result.stageAnnotationTemplatePath, annotationPath);
    EXPECT_TRUE(result.wroteStageAnnotationTemplate);
    ASSERT_TRUE(result.stageAnnotationMediaDir.has_value());
    EXPECT_FALSE(std::filesystem::exists(*result.stageAnnotationMediaDir / "entry_0"));
    EXPECT_EQ(readTextFile(annotationPath), "{\"schema\":\"spice_sst_sml_stage_annotation_v1\",\"manual\":\"keep me\"}\n");
    ASSERT_TRUE(result.stageAnnotationCombinedBlenderIrPath.has_value());
    EXPECT_TRUE(result.copiedStageAnnotationCombinedBlenderIr);
    EXPECT_EQ(readTextFile(*result.stageAnnotationCombinedBlenderIrPath), "{\"scene\":\"updated\"}\n");
    EXPECT_FALSE(result.diagnostics.empty());
}

TEST(SpiceSstSmlExport, PreservesRecordCountMismatchInCommandMap) {
    const auto smlParsed = SmlParser::parse(makeSml(1U), "s778.sml");
    const auto sstParsed = SstParser::parse(makeSst(), "s778.sst");
    ASSERT_TRUE(smlParsed.ok());
    ASSERT_TRUE(sstParsed.ok());

    const auto outputDir = makeTempOutputDir("spice_sst_sml_export_mismatch");
    SmlEmbeddedMldExportOptions options{};
    options.stageOutputDir = outputDir / "s778";
    options.stem = "s778";
    options.writeEmbeddedMldPayloads = true;
    options.writeCommandMap = true;

    const auto result = exportSmlEmbeddedMldsAndCommandMap(smlParsed, &sstParsed, options);

    ASSERT_TRUE(result.commandMapPath.has_value());
    const auto commandMap = readTextFile(*result.commandMapPath);
    EXPECT_NE(commandMap.find("\"agree\":false"), std::string::npos);
    EXPECT_NE(commandMap.find("\"index\":1"), std::string::npos);
    const auto manifest = readTextFile(result.manifestPath);
    EXPECT_NE(manifest.find("record counts differ"), std::string::npos);
}

TEST(SpiceSstSmlExport, ExtractedEmbeddedMldCanBeParsedByMldParser) {
    const auto embeddedMld = makeMinimalEmbeddedMld();
    const auto smlParsed = SmlParser::parse(makeSmlWithEmbeddedMld(embeddedMld), "s123.sml");
    ASSERT_TRUE(smlParsed.ok());

    const auto outputDir = makeTempOutputDir("spice_sst_sml_export_mld_parse");
    SmlEmbeddedMldExportOptions options{};
    options.stageOutputDir = outputDir / "s123";
    options.stem = "s123";
    options.writeEmbeddedMldPayloads = true;

    const auto result = exportSmlEmbeddedMldsAndCommandMap(smlParsed, nullptr, options);
    ASSERT_EQ(result.entries.size(), 1U);
    ASSERT_TRUE(result.entries[0].wroteEmbeddedMld);

    const auto exportedBytes = readAllBytes(result.entries[0].embeddedMldPath);
    ASSERT_FALSE(exportedBytes.empty());
    spice::mld::parsing::MldParser parser{};
    spice::mld::parsing::ParseOptions parseOptions{};
    parseOptions.buildBlenderIntermediateIr = false;
    const auto parsed = parser.parse(std::span<const std::uint8_t>(exportedBytes.data(), exportedBytes.size()), parseOptions);

    ASSERT_EQ(parsed.entryList.size(), 1U);
    EXPECT_EQ(parsed.entryList[0].fxnName, "wall");
}

TEST(SpiceSstSmlExport, CombinesSmlBlenderIrScenesAndRemapsAnimations) {
    spice::sstsml::exporting::SmlBlenderIrCombiner combiner{};

    combiner.appendEntryScene(makeSingleEntryBlenderIrScene(10U, 0x100U, 0x200U, "shared"), "s006", 0U);
    combiner.appendEntryScene(makeSingleEntryBlenderIrScene(11U, 0x300U, 0x400U, "shared"), "s006", 1U);

    const auto& combined = combiner.scene();
    ASSERT_EQ(combined.meshes.size(), 2U);
    ASSERT_EQ(combined.objectTrees.size(), 2U);
    ASSERT_EQ(combined.indexEntries.size(), 2U);
    ASSERT_EQ(combined.textures.size(), 2U);
    ASSERT_EQ(combined.animations.size(), 2U);

    EXPECT_EQ(combined.textures[0].textureName, "s006_entry_00__shared");
    EXPECT_EQ(combined.textures[1].textureName, "s006_entry_01__shared");
    ASSERT_EQ(combined.meshes[0].materials.size(), 1U);
    ASSERT_EQ(combined.meshes[1].materials.size(), 1U);
    EXPECT_EQ(combined.meshes[0].materials[0].textureName, "s006_entry_00__shared");
    EXPECT_EQ(combined.meshes[1].materials[0].textureName, "s006_entry_01__shared");

    ASSERT_TRUE(combined.objectTrees[0].nodes[0].meshIndex.has_value());
    ASSERT_TRUE(combined.objectTrees[1].nodes[0].meshIndex.has_value());
    EXPECT_EQ(*combined.objectTrees[0].nodes[0].meshIndex, 0U);
    EXPECT_EQ(*combined.objectTrees[1].nodes[0].meshIndex, 1U);

    EXPECT_EQ(combined.indexEntries[0].tableIndex, 0U);
    EXPECT_EQ(combined.indexEntries[1].tableIndex, 1U);
    ASSERT_EQ(combined.indexEntries[1].meshIndices.size(), 1U);
    ASSERT_EQ(combined.indexEntries[1].objectTreeIndices.size(), 1U);
    EXPECT_EQ(combined.indexEntries[1].meshIndices[0], 1U);
    EXPECT_EQ(combined.indexEntries[1].objectTreeIndices[0], 1U);

    EXPECT_EQ(combined.animations[0].tableIndex, 0U);
    EXPECT_EQ(combined.animations[0].objectTreeIndex, 0U);
    EXPECT_EQ(combined.animations[1].tableIndex, 1U);
    EXPECT_EQ(combined.animations[1].objectTreeIndex, 1U);
    ASSERT_EQ(combined.animations[1].nodes.size(), 1U);
    EXPECT_EQ(combined.animations[1].nodes[0].nodeIndex, 0U);
}

TEST(SpiceSstSmlExport, CombinedSmlBlenderIrCanApplySstType0PlacementOverlay) {
    spice::sstsml::exporting::SmlBlenderIrCombiner combiner{};

    auto scene = makeSingleEntryBlenderIrScene(10U, 0x100U, 0x200U, "shared");
    scene.indexEntries[0].transform.position = spice::mld::model::Vec3{ 1.0F, 2.0F, 3.0F };
    scene.indexEntries[0].transform.scale = spice::mld::model::Vec3{ 2.0F, 2.0F, 2.0F };

    spice::sstsml::exporting::SmlBlenderIrSstPlacementOverlay overlay{};
    overlay.hasPosition = true;
    overlay.position = spice::mld::model::Vec3{ 10.0F, 20.0F, 30.0F };
    overlay.hasScale = true;
    overlay.scale = spice::mld::model::Vec3{ 3.0F, 4.0F, 5.0F };
    overlay.hasRotationRaw = true;
    overlay.rotationRaw = spice::mld::model::Vec3{ 256.0F, 512.0F, 768.0F };
    overlay.sourceDescription = "SST record 0 command 0 payloadOffset=0x54";

    combiner.appendEntryScene(std::move(scene), "s006", 0U, overlay);

    const auto& combined = combiner.scene();
    ASSERT_EQ(combined.indexEntries.size(), 1U);
    EXPECT_FLOAT_EQ(combined.indexEntries[0].transform.position.x, 11.0F);
    EXPECT_FLOAT_EQ(combined.indexEntries[0].transform.position.y, 22.0F);
    EXPECT_FLOAT_EQ(combined.indexEntries[0].transform.position.z, 33.0F);
    EXPECT_FLOAT_EQ(combined.indexEntries[0].transform.scale.x, 6.0F);
    EXPECT_FLOAT_EQ(combined.indexEntries[0].transform.scale.y, 8.0F);
    EXPECT_FLOAT_EQ(combined.indexEntries[0].transform.scale.z, 10.0F);
    ASSERT_FALSE(combined.diagnostics.empty());
    EXPECT_NE(combined.diagnostics.back().find("SST type 0 placement overlay"), std::string::npos);
    EXPECT_NE(combined.diagnostics.back().find("rotationRaw"), std::string::npos);
}
