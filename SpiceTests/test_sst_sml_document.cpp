#include "../Compression/Aklz.h"
#include "../SpiceRoot/Binary/EndianWriter.h"
#include "../SpiceRoot/Binary/EndianReader.h"
#include "../SpiceMLD/MldBlenderIrProjector.h"
#include "../SpiceSstSml/SstSmlDocumentImporter.h"
#include "../SpiceSstSml/SstSmlDocumentValidator.h"
#include "../SpiceSstSml/SstSmlDocumentAnalysis.h"
#include "../SpiceSstSml/SstSmlDocumentMaterialization.h"
#include "../SpiceSstSml/SstSmlExport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace {

using spice::root::Endian;
using spice::root::EndianSpanWriter;
using namespace spice::sstsml;

std::vector<std::uint8_t> makeMinimalMld(const Endian endian) {
    std::vector<std::uint8_t> bytes(0x340U, 0xCDU);
    EndianSpanWriter writer(bytes, endian);
    writer.write_u32_at(0x00U, 1U);
    writer.write_u32_at(0x04U, 0x20U);
    writer.write_u32_at(0x08U, 0x108U);
    writer.write_u32_at(0x0CU, 0x180U);
    writer.write_u32_at(0x10U, 0x320U);
    writer.write_u32_at(0x20U, 7U);
    writer.write_u32_at(0x24U, 9U);
    writer.write_u32_at(0x28U, 0x100U);
    writer.write_u32_at(0x2CU, 0x108U);
    writer.write_u32_at(0x30U, 0x108U);
    writer.write_u32_at(0x34U, 0x118U);
    writer.write_u32_at(0x38U, 0x120U);
    writer.write_u32_at(0x3CU, 0x128U);
    writer.write_u32_at(0x40U, 0U);
    std::fill(bytes.begin() + 0x44U, bytes.begin() + 0x58U, 0U);
    const std::string name = "stage";
    std::copy(name.begin(), name.end(), bytes.begin() + 0x44U);
    writer.write_f32_at(0x7CU, 1.0F);
    writer.write_f32_at(0x80U, 1.0F);
    writer.write_f32_at(0x84U, 1.0F);
    writer.write_u32_at(0x100U, 0U);
    writer.write_u32_at(0x108U, 2U);
    writer.write_u32_at(0x10CU, 11U);
    writer.write_u32_at(0x110U, 22U);
    writer.write_u32_at(0x118U, 0U);
    writer.write_u32_at(0x120U, 0U);
    writer.write_u32_at(0x128U, 0U);
    writer.write_u32_at(0x320U, 0U);
    return bytes;
}
std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>> makePair(
    const Endian endian, std::span<const std::uint8_t> embeddedMld = {}) {
    const std::array<std::uint8_t, 4U> invalidMld{ 'M', 'L', 'D', '!' };
    if (embeddedMld.empty()) embeddedMld = invalidMld;
    std::vector<std::uint8_t> sml(0x18U + embeddedMld.size(), 0U);
    EndianSpanWriter smlWriter(sml, endian);
    smlWriter.write_u16_at(0U, 1U);
    smlWriter.write_u16_at(2U, 0xFFFFU);
    smlWriter.write_u16_at(4U, 1U);
    smlWriter.write_u16_at(6U, 0xFFFFU);
    smlWriter.write_u32_at(8U, 0U);
    smlWriter.write_u32_at(0x0CU, 0x18U);
    smlWriter.write_u32_at(0x10U, static_cast<std::uint32_t>(embeddedMld.size()));
    smlWriter.write_u32_at(0x14U, 0xFFFFFFFFU);
    std::copy(embeddedMld.begin(), embeddedMld.end(), sml.begin() + 0x18U);

    constexpr std::size_t blockOffset = 0x10U;
    constexpr std::size_t payloadSize = 0x4CU;
    constexpr std::size_t blockSize = 4U + 0x10U + 0x10U + payloadSize + 81U;
    std::vector<std::uint8_t> sst(blockOffset + blockSize, 0U);
    EndianSpanWriter sstWriter(sst, endian);
    sstWriter.write_u16_at(0U, 1U);
    sstWriter.write_u16_at(2U, 0xFFFFU);
    sstWriter.write_u16_at(4U, 1U);
    sstWriter.write_u16_at(6U, 0xFFFFU);
    sstWriter.write_u32_at(8U, 0U);
    sstWriter.write_u32_at(0x0CU, static_cast<std::uint32_t>(blockOffset));
    sstWriter.write_u32_at(blockOffset, 1U);
    sstWriter.write_i16_at(blockOffset + 4U, 0);
    sstWriter.write_i16_at(blockOffset + 6U, 7);
    sstWriter.write_u32_at(blockOffset + 8U, 0x12345678U);
    sstWriter.write_u32_at(blockOffset + 12U, 0xABCDEF01U);
    sstWriter.write_u32_at(blockOffset + 16U, 0U);
    const auto sentinelOffset = blockOffset + 20U;
    sstWriter.write_i16_at(sentinelOffset, -1);
    sstWriter.write_i16_at(sentinelOffset + 2U, 0);
    sstWriter.write_u32_at(sentinelOffset + 4U, 0xFFFFFFFFU);
    sstWriter.write_u32_at(sentinelOffset + 8U, 0xFFFFFFFFU);
    sstWriter.write_u32_at(sentinelOffset + 12U, 0xFFFFFFFFU);
    const auto payloadOffset = sentinelOffset + 16U;
    sstWriter.write_i16_at(payloadOffset + 0x16U, 3);
    sstWriter.write_i16_at(payloadOffset + 0x18U, 4);
    sstWriter.write_f32_at(payloadOffset + 0x1CU, 1.25F);
    sstWriter.write_f32_at(payloadOffset + 0x20U, 2.5F);
    sstWriter.write_f32_at(payloadOffset + 0x24U, 3.75F);
    sstWriter.write_u32_at(payloadOffset + 0x28U, 0x1000U);
    sstWriter.write_u32_at(payloadOffset + 0x2CU, 0x2000U);
    sstWriter.write_u32_at(payloadOffset + 0x30U, 0x3000U);
    sstWriter.write_f32_at(payloadOffset + 0x34U, 1.0F);
    sstWriter.write_f32_at(payloadOffset + 0x38U, 1.5F);
    sstWriter.write_f32_at(payloadOffset + 0x3CU, 2.0F);
    sst[payloadOffset + 0x44U] = 2U;
    for (std::size_t index = 0U; index < 81U; ++index) {
        sst[payloadOffset + payloadSize + index] = static_cast<std::uint8_t>(index % 3U);
    }
    return { std::move(sml), std::move(sst) };
}

std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>> makeLightingPair(const Endian endian) {
    auto basePair = makePair(endian);
    auto sml = std::move(basePair.first);
    constexpr std::size_t blockOffset = 0x10U;
    constexpr std::size_t payloadSize = 0xD0U;
    constexpr std::size_t payloadOffset = blockOffset + 4U + 0x10U + 0x10U;
    std::vector<std::uint8_t> sst(payloadOffset + payloadSize + 81U, 0U);
    EndianSpanWriter writer(sst, endian);
    writer.write_u16_at(0U, 1U);
    writer.write_u16_at(2U, 0xFFFFU);
    writer.write_u16_at(4U, 1U);
    writer.write_u16_at(6U, 0xFFFFU);
    writer.write_u32_at(8U, 0U);
    writer.write_u32_at(0x0CU, blockOffset);
    writer.write_u32_at(blockOffset, 1U);
    writer.write_i16_at(blockOffset + 4U, 1);
    writer.write_i16_at(blockOffset + 6U, 0);
    const auto sentinelOffset = blockOffset + 20U;
    writer.write_i16_at(sentinelOffset, -1);
    writer.write_u32_at(sentinelOffset + 4U, 0xFFFFFFFFU);
    writer.write_u32_at(sentinelOffset + 8U, 0xFFFFFFFFU);
    writer.write_u32_at(sentinelOffset + 12U, 0xFFFFFFFFU);
    writer.write_i8_at(payloadOffset, 4);
    writer.write_i16_at(payloadOffset + 2U, 2);
    writer.write_u32_at(payloadOffset + 4U, 0x60000000U);
    writer.write_i16_at(payloadOffset + 8U, 3);
    writer.write_f32_at(payloadOffset + 0x0CU, 1.0F);
    writer.write_f32_at(payloadOffset + 0x30U, 0.25F);
    writer.write_f32_at(payloadOffset + 0x3CU, 0.5F);
    writer.write_f32_at(payloadOffset + 0x48U, 0.75F);
    writer.write_f32_at(payloadOffset + 0x4CU, 1.25F);
    writer.write_u32_at(payloadOffset + 0x64U, 0x12345678U);
    writer.write_i8_at(payloadOffset + 0x68U, -1);
    for (std::size_t index = 0U; index < 81U; ++index) {
        sst[payloadOffset + payloadSize + index] = static_cast<std::uint8_t>(index % 4U);
    }
    return { std::move(sml), std::move(sst) };
}

void writeFile(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::ofstream output(path, std::ios::binary);
    ASSERT_TRUE(output.good());
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output.good());
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), {});
}

} // namespace

TEST(SpiceSstSmlDocument, ImportsEquivalentTypedSemanticsForBothEndianModes) {
    const auto [bigSml, bigSst] = makePair(Endian::Big);
    const auto [littleSml, littleSst] = makePair(Endian::Little);
    const auto big = SstSmlDocumentImporter::importBytes(bigSml, bigSst);
    const auto little = SstSmlDocumentImporter::importBytes(littleSml, littleSst);
    ASSERT_TRUE(big.ok());
    ASSERT_TRUE(little.ok());
    EXPECT_EQ(big.receipt.sml.endian, Endian::Big);
    EXPECT_EQ(big.receipt.sst.endian, Endian::Big);
    EXPECT_EQ(little.receipt.sml.endian, Endian::Little);
    EXPECT_EQ(little.receipt.sst.endian, Endian::Little);
    ASSERT_TRUE(big.document.has_value());
    ASSERT_TRUE(little.document.has_value());
    EXPECT_EQ(big.document->stageId, little.document->stageId);
    EXPECT_EQ(big.document->stageHeaderSentinel, little.document->stageHeaderSentinel);
    ASSERT_EQ(big.document->members.size(), 1U);
    ASSERT_EQ(little.document->members.size(), 1U);
    const auto& bigCommand = big.document->members[0].sst.commandBlock.commands[0];
    const auto& littleCommand = little.document->members[0].sst.commandBlock.commands[0];
    EXPECT_EQ(bigCommand.fields, littleCommand.fields);
    EXPECT_EQ(bigCommand.placement, littleCommand.placement);
    EXPECT_EQ(big.document->members[0].sst.commandBlock.battleGrid,
        little.document->members[0].sst.commandBlock.battleGrid);
    EXPECT_TRUE(SstSmlDocumentValidator::validate(*big.document).ok());
    EXPECT_TRUE(SstSmlDocumentValidator::validate(*little.document).ok());
    const auto analysis = SstSmlDocumentAnalyzer::analyze(*big.document, big.receipt);
    ASSERT_TRUE(analysis.ok());
    ASSERT_EQ(analysis.commandTypeHistogram.size(), 1U);
    const std::pair<std::int16_t, std::uint32_t> expectedHistogramEntry{ 0, 1U };
    EXPECT_EQ(analysis.commandTypeHistogram[0], expectedHistogramEntry);
    EXPECT_EQ(analysis.commands.size(), 1U);
    EXPECT_EQ(analysis.commands[0].fields.size(), bigCommand.fields.size());
    const auto rebuiltBig = materializeCommandPayload(bigCommand, Endian::Big);
    const auto rebuiltLittle = materializeCommandPayload(littleCommand, Endian::Little);
    ASSERT_TRUE(rebuiltBig.ok());
    ASSERT_TRUE(rebuiltLittle.ok());
    EXPECT_EQ(rebuiltBig.bytes, (std::vector<std::uint8_t>(bigSst.begin() + 0x34U, bigSst.begin() + 0x80U)));
    EXPECT_EQ(rebuiltLittle.bytes,
        (std::vector<std::uint8_t>(littleSst.begin() + 0x34U, littleSst.begin() + 0x80U)));
}

TEST(SpiceSstSmlDocument, ImportsAklzWrappedPairAndRecordsReceipt) {
    const auto [sml, sst] = makePair(Endian::Big);
    const auto compressedSml = spice::compression::aklz::compress(sml);
    const auto compressedSst = spice::compression::aklz::compress(sst);
    ASSERT_TRUE(compressedSml.ok());
    ASSERT_TRUE(compressedSst.ok());
    const auto imported = SstSmlDocumentImporter::importBytes(compressedSml.bytes, compressedSst.bytes);
    ASSERT_TRUE(imported.ok());
    EXPECT_EQ(imported.receipt.sml.wrapper, SstSmlSourceWrapper::Aklz);
    EXPECT_EQ(imported.receipt.sst.wrapper, SstSmlSourceWrapper::Aklz);
    EXPECT_EQ(imported.receipt.sml.rawSize, compressedSml.bytes.size());
    EXPECT_EQ(imported.receipt.sml.decodedSize, sml.size());
}

TEST(SpiceSstSmlDocument, OwnsDecodedEmbeddedMldAndKeysItsImportReceipt) {
    for (const auto endian : { Endian::Big, Endian::Little }) {
        const auto sourceMld = makeMinimalMld(endian);
        const auto [sml, sst] = makePair(endian, sourceMld);
        const auto imported = SstSmlDocumentImporter::importBytes(sml, sst);
        ASSERT_TRUE(imported.ok());
        ASSERT_TRUE(imported.document.has_value());
        const auto& resource = imported.document->members.front().sml.resource;
        const auto* mld = std::get_if<spice::mld::MldDocument>(&resource.content);
        ASSERT_NE(mld, nullptr);
        ASSERT_EQ(mld->entries.size(), 1U);
        EXPECT_EQ(mld->entries.front().functionName, "stage");
        const auto* nestedReceipt = imported.receipt.embeddedMld(resource.id);
        ASSERT_NE(nestedReceipt, nullptr);
        EXPECT_EQ(nestedReceipt->endian, endian);
        const auto materialized = materializeEmbeddedResource(resource, imported.receipt);
        ASSERT_TRUE(materialized.ok());
        const auto reparsed = spice::mld::MldDocumentImporter::importBytes(materialized.bytes);
        ASSERT_TRUE(reparsed.ok());
        EXPECT_EQ(reparsed.document->entries.front().functionName, mld->entries.front().functionName);
        EXPECT_TRUE(SstSmlDocumentValidator::validate(*imported.document, &imported.receipt).ok());
    }
}

TEST(SpiceSstSmlDocument, EmbeddedMldSemanticEditSurvivesMaterialization) {
    const auto sourceMld = makeMinimalMld(Endian::Big);
    const auto [sml, sst] = makePair(Endian::Big, sourceMld);
    const auto imported = SstSmlDocumentImporter::importBytes(sml, sst);
    ASSERT_TRUE(imported.ok());
    auto document = *imported.document;
    auto& resource = document.members.front().sml.resource;
    auto& mld = std::get<spice::mld::MldDocument>(resource.content);
    mld.entries.front().functionName = "edited";
    const auto materialized = materializeEmbeddedResource(resource, imported.receipt);
    ASSERT_TRUE(materialized.ok());
    const auto reparsed = spice::mld::MldDocumentImporter::importBytes(materialized.bytes);
    ASSERT_TRUE(reparsed.ok());
    EXPECT_EQ(reparsed.document->entries.front().functionName, "edited");
}

TEST(SpiceSstSmlDocument, ConstructedEmbeddedMldProjectsWithoutReceiptButExtractionNeedsTarget) {
    spice::mld::MldDocument mld{};
    spice::mld::MldEntry entry{};
    entry.id = spice::mld::MldEntryId{ 1U };
    entry.entryId = 42U;
    entry.tableId = -7;
    entry.functionName = "constructed";
    mld.entries.push_back(entry);
    mld.layout.push_back(entry.id);
    SmlEmbeddedResource resource{ SmlEmbeddedResourceId{ 1U }, std::move(mld) };
    const SstSmlDocumentImportReceipt emptyReceipt{};

    EXPECT_TRUE(spice::mld::MldBlenderIrProjector::project(
        std::get<spice::mld::MldDocument>(resource.content)).ok());
    EXPECT_FALSE(materializeEmbeddedResource(resource, emptyReceipt).ok());
    const auto extracted = materializeEmbeddedResource(resource, emptyReceipt,
        spice::mld::MldWriteTarget{
            spice::mld::MldPlatform::Dreamcast,
            spice::mld::MldWrapper::Raw,
        });
    ASSERT_TRUE(extracted.ok());
    const auto reparsed = spice::mld::MldDocumentImporter::importBytes(extracted.bytes);
    ASSERT_TRUE(reparsed.ok());
    EXPECT_EQ(reparsed.document->entries.front().functionName, "constructed");
}

TEST(SpiceSstSmlDocument, RecordsSha256ForBothOriginalInputs) {
    const auto sst = makePair(Endian::Little).second;
    const auto imported = SstSmlDocumentImporter::importBytes({}, sst);
    EXPECT_FALSE(imported.ok());
    const std::array<std::uint8_t, 32U> expectedSml{
        0xE3U, 0xB0U, 0xC4U, 0x42U, 0x98U, 0xFCU, 0x1CU, 0x14U,
        0x9AU, 0xFBU, 0xF4U, 0xC8U, 0x99U, 0x6FU, 0xB9U, 0x24U,
        0x27U, 0xAEU, 0x41U, 0xE4U, 0x64U, 0x9BU, 0x93U, 0x4CU,
        0xA4U, 0x95U, 0x99U, 0x1BU, 0x78U, 0x52U, 0xB8U, 0x55U,
    };
    EXPECT_EQ(imported.receipt.sml.rawSha256, expectedSml);
    EXPECT_NE(imported.receipt.sml.rawSha256, imported.receipt.sst.rawSha256);
}

TEST(SpiceSstSmlDocument, RejectsIncompletePairWithoutPartialDocument) {
    auto [sml, sst] = makePair(Endian::Big);
    sst.resize(12U);
    const auto imported = SstSmlDocumentImporter::importBytes(sml, sst);
    EXPECT_FALSE(imported.ok());
    EXPECT_FALSE(imported.document.has_value());
    EXPECT_TRUE(std::any_of(imported.diagnostics.begin(), imported.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity == SstSmlDiagnosticSeverity::Error;
    }));
}

TEST(SpiceSstSmlDocument, RejectsMismatchedCountsWithoutPartialDocument) {
    auto [sml, sst] = makePair(Endian::Big);
    EndianSpanWriter(sst, Endian::Big).write_u16_at(4U, 2U);
    const auto imported = SstSmlDocumentImporter::importBytes(sml, sst);
    EXPECT_FALSE(imported.ok());
    EXPECT_FALSE(imported.document.has_value());
}

TEST(SpiceSstSmlDocument, RetainsUnknownCommandBodyAsExplicitOpaqueTail) {
    auto [sml, sst] = makePair(Endian::Big);
    EndianSpanWriter(sst, Endian::Big).write_i16_at(0x14U, 99);
    const auto imported = SstSmlDocumentImporter::importBytes(sml, sst);
    ASSERT_TRUE(imported.ok());
    const auto& block = imported.document->members[0].sst.commandBlock;
    ASSERT_EQ(block.commands.size(), 1U);
    EXPECT_FALSE(block.commands[0].payloadSpanKnown);
    EXPECT_TRUE(block.commands[0].opaquePayloadFragments.empty());
    EXPECT_FALSE(block.battleGrid.has_value());
    ASSERT_TRUE(block.trailingOpaque.has_value());
    EXPECT_EQ(block.trailingOpaque->bytes.size(), 157U);
}

TEST(SpiceSstSmlDocument, EditedSpecializedPlacementMaterializesCanonically) {
    const auto [sml, sst] = makePair(Endian::Big);
    const auto imported = SstSmlDocumentImporter::importBytes(sml, sst);
    ASSERT_TRUE(imported.ok());
    auto document = *imported.document;
    auto& placement = *document.members[0].sst.commandBlock.commands[0].placement;
    placement.positionX += 1.0F;
    const auto validation = SstSmlDocumentValidator::validate(document);
    ASSERT_TRUE(validation.ok());
    const auto payload = materializeCommandPayload(
        document.members[0].sst.commandBlock.commands[0], Endian::Big);
    ASSERT_TRUE(payload.ok());
    EXPECT_FLOAT_EQ(spice::root::EndianReader(payload.bytes, Endian::Big).read_f32(0x1CU), 2.25F);
}

TEST(SpiceSstSmlDocument, SpecializedLightingOwnsAndMaterializesBothRows) {
    for (const auto endian : { Endian::Big, Endian::Little }) {
        const auto [sml, sst] = makeLightingPair(endian);
        const auto imported = SstSmlDocumentImporter::importBytes(sml, sst);
        ASSERT_TRUE(imported.ok());
        const auto& command = imported.document->members.front().sst.commandBlock.commands.front();
        EXPECT_TRUE(command.fields.empty());
        ASSERT_EQ(command.lightingRows.size(), 2U);
        EXPECT_FALSE(command.lightingRows.front().sentinel);
        EXPECT_TRUE(command.lightingRows.back().sentinel);
        const auto rebuilt = materializeCommandPayload(command, endian);
        ASSERT_TRUE(rebuilt.ok());
        EXPECT_EQ(rebuilt.bytes,
            (std::vector<std::uint8_t>(sst.begin() + 0x34U, sst.begin() + 0x104U)));
    }
}

TEST(SpiceSstSmlDocument, ValidatorRejectsOverlappingSemanticAndOpaqueCommandOwnership) {
    const auto [sml, sst] = makePair(Endian::Big);
    const auto imported = SstSmlDocumentImporter::importBytes(sml, sst);
    ASSERT_TRUE(imported.ok());
    auto document = *imported.document;
    auto& command = document.members[0].sst.commandBlock.commands[0];
    command.opaquePayloadFragments.push_back({
        document.allocateOpaqueBlockId(), 0x1CU, { 0U, 0U, 0U, 0U } });
    const auto validation = SstSmlDocumentValidator::validate(document);
    EXPECT_FALSE(validation.ok());
    EXPECT_TRUE(std::any_of(validation.diagnostics.begin(), validation.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.message.find("overlap") != std::string::npos;
    }));
}

TEST(SpiceSstSmlDocument, AllocatorsCoverEveryStableDocumentIdCategory) {
    const auto [sml, sst] = makePair(Endian::Big);
    const auto imported = SstSmlDocumentImporter::importBytes(sml, sst);
    ASSERT_TRUE(imported.ok());
    const auto& document = *imported.document;
    EXPECT_GT(document.allocateStageMemberId().value, 1U);
    EXPECT_GT(document.allocateSmlRecordId().value, 1U);
    EXPECT_GT(document.allocateEmbeddedResourceId().value, 1U);
    EXPECT_GT(document.allocateSstRecordId().value, 1U);
    EXPECT_GT(document.allocateCommandBlockId().value, 1U);
    EXPECT_GT(document.allocateCommandId().value, 1U);
    EXPECT_GT(document.allocateCommandFieldId().value, 1U);
    EXPECT_GT(document.allocatePlacementId().value, 1U);
    EXPECT_EQ(document.allocateLightingRowId().value, 1U);
    EXPECT_GT(document.allocateBattleGridTerrainId().value, 1U);
    EXPECT_GT(document.allocateOpaqueBlockId().value, 1U);
}

TEST(SpiceSstSmlDocument, ValidatorRejectsDuplicateAndMissingLayoutOwnership) {
    const auto [sml, sst] = makePair(Endian::Big);
    const auto imported = SstSmlDocumentImporter::importBytes(sml, sst);
    ASSERT_TRUE(imported.ok());
    auto document = *imported.document;
    document.members[0].sst.id = SstRecordId{};
    document.members[0].sst.commandBlock.battleGrid->id = SstBattleGridTerrainId{};
    document.smlBodyLayout.clear();
    document.sstBodyLayout.push_back(document.members[0].sst.commandBlock.id);
    const auto validation = SstSmlDocumentValidator::validate(document);
    EXPECT_FALSE(validation.ok());
    EXPECT_TRUE(std::any_of(validation.diagnostics.begin(), validation.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.message == "SST record ID is zero";
    }));
    EXPECT_TRUE(std::any_of(validation.diagnostics.begin(), validation.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.message == "SST battle-grid terrain ID is zero";
    }));
    EXPECT_TRUE(std::any_of(validation.diagnostics.begin(), validation.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.message.find("SML body layout does not own every embedded resource") != std::string::npos;
    }));
    EXPECT_TRUE(std::any_of(validation.diagnostics.begin(), validation.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.message.find("more than once") != std::string::npos;
    }));
}

TEST(SpiceSstSmlDocument, ResolvesSideBySideSiblingCaseInsensitively) {
    const auto [sml, sst] = makePair(Endian::Little);
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory = std::filesystem::temp_directory_path() / ("spice_sstsml_" + unique);
    ASSERT_TRUE(std::filesystem::create_directories(directory));
    const auto smlPath = directory / "S001.SML";
    const auto sstPath = directory / "s001.sst";
    writeFile(smlPath, sml);
    writeFile(sstPath, sst);
    const auto imported = SstSmlDocumentImporter::importFile(sstPath);
    EXPECT_TRUE(imported.ok());
    EXPECT_EQ(imported.receipt.sml.path, smlPath);
    EXPECT_EQ(imported.receipt.sst.path, sstPath);
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

TEST(SpiceSstSmlDocument, SecondaryResearchExportConsumesDocumentAndAnalysis) {
    const auto [sml, sst] = makePair(Endian::Big);
    const auto imported = SstSmlDocumentImporter::importBytes(sml, sst);
    ASSERT_TRUE(imported.ok());
    const auto analysis = SstSmlDocumentAnalyzer::analyze(*imported.document, imported.receipt);
    ASSERT_TRUE(analysis.ok());
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory = std::filesystem::temp_directory_path() / ("spice_sstsml_export_" + unique);
    SmlEmbeddedMldExportOptions options{};
    options.stageOutputDir = directory;
    options.stem = "s001";
    options.writeStageAnnotationTemplate = false;
    const auto exported = exportSmlEmbeddedMldsAndCommandMap(
        *imported.document, imported.receipt, analysis, options);
    EXPECT_TRUE(exported.wroteManifest);
    EXPECT_TRUE(exported.wroteCommandMap);
    ASSERT_TRUE(exported.commandMapPath.has_value());
    const auto json = readFile(*exported.commandMapPath);
    const std::string text(json.begin(), json.end());
    EXPECT_NE(text.find("\"recordCounts\":{\"sml\":1,\"sst\":1,\"agree\":true}"), std::string::npos);
    EXPECT_NE(text.find("\"type\":0"), std::string::npos);
    EXPECT_TRUE(std::filesystem::is_regular_file(directory / "embedded_mld" / "s001_sml_entry_0.mld"));
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}
