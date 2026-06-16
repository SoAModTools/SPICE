#include "../SpiceMll/SpiceMll.h"
#include "../SpiceGvm/Encoding/GvrEncoder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
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

void writeMldHeader(std::vector<std::uint8_t>& bytes, std::size_t offset) {
    writeBeU32(bytes, offset + 0x00U, 1U);
    writeBeU32(bytes, offset + 0x04U, 0x14U);
    writeBeU32(bytes, offset + 0x08U, 0U);
    writeBeU32(bytes, offset + 0x0cU, 0x20U);
    writeBeU32(bytes, offset + 0x10U, 0U);
}

void writeName(std::vector<std::uint8_t>& bytes, std::size_t offset, const char* name) {
    for (std::size_t i = 0; name[i] != '\0'; ++i) {
        ASSERT_LT(offset + i, bytes.size());
        bytes[offset + i] = static_cast<std::uint8_t>(name[i]);
    }
}

std::vector<std::uint8_t> makeGvrTextureBytes() {
    spice::gvm::model::RgbaImage image{};
    image.width = 4U;
    image.height = 4U;
    image.rgba8.resize(4U * 4U * 4U, 0xffU);

    spice::gvm::encoding::EncodeOptions options{};
    options.textureFormat = spice::gvm::model::TextureFormat::RGBA8;
    options.hasGlobalIndex = true;
    options.globalIndex = 17U;
    return spice::gvm::encoding::encodeGvr(image, options);
}

std::vector<std::uint8_t> makeMllFixture() {
    std::vector<std::uint8_t> bytes(0x340U, 0U);
    writeBeU32(bytes, 0x00U, 0x0000ffffU);
    writeBeU32(bytes, 0x04U, 0x0002ffffU);
    writeName(bytes, 0x08U, "a.mld");
    writeBeU32(bytes, 0x1cU, 0x00000048U);
    writeBeU32(bytes, 0x20U, 0x00000180U);
    writeBeU32(bytes, 0x24U, 0xffffffffU);
    writeName(bytes, 0x28U, "b.bin");
    writeBeU32(bytes, 0x3cU, 0x00000240U);
    writeBeU32(bytes, 0x40U, 0x00000004U);
    writeBeU32(bytes, 0x44U, 0xffffffffU);
    writeMldHeader(bytes, 0x48U);
    const std::size_t payloadBase = 0x48U;
    writeBeU32(bytes, payloadBase + 0x10U, 0x000000c0U);
    const std::size_t indexEntry = payloadBase + 0x14U;
    writeBeU32(bytes, indexEntry + 0x14U, 0x00000090U);
    writeBeU32(bytes, payloadBase + 0x90U, 1U);
    writeBeU32(bytes, payloadBase + 0x94U, 0x000000a0U);
    bytes[payloadBase + 0xa0U] = 'N';
    bytes[payloadBase + 0xa1U] = 'J';
    bytes[payloadBase + 0xa2U] = 'C';
    bytes[payloadBase + 0xa3U] = 'M';
    writeBeU32(bytes, payloadBase + 0xc0U, 1U);
    writeName(bytes, payloadBase + 0xc4U, "tex_17");
    const auto textureBytes = makeGvrTextureBytes();
    EXPECT_LE(payloadBase + 0x100U + textureBytes.size(), payloadBase + 0x180U);
    std::copy(textureBytes.begin(), textureBytes.end(), bytes.begin() + payloadBase + 0x100U);
    bytes[0x240U] = 'P';
    bytes[0x241U] = 'O';
    bytes[0x242U] = 'F';
    bytes[0x243U] = '0';
    return bytes;
}

std::vector<std::uint8_t> makeInferredCountMllFixture() {
    auto bytes = makeMllFixture();
    writeBeU32(bytes, 0x04U, 0x4000ffffU);
    return bytes;
}

std::filesystem::path makeTempOutputDir(const std::string& name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    ASSERT_TRUE(out.good());
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(out.good());
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(in.good());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

TEST(SpiceMllCorpusExport, ScansDirectoryAndWritesFeedbackArtifacts) {
    const auto root = makeTempOutputDir("spice_mll_corpus_export_scan");
    const auto inputDir = root / "input";
    const auto outputDir = root / "output";
    writeBytes(inputDir / "field" / "sample.mll", makeMllFixture());
    writeBytes(inputDir / "field" / "ignored.mld", makeMllFixture());

    const auto corpus = spice::mll::scanMllCorpus(inputDir);
    const auto summary = spice::mll::summarizeMllCorpusFeedback(corpus);

    ASSERT_TRUE(corpus.inputWasDirectory);
    ASSERT_EQ(corpus.files.size(), 1U);
    EXPECT_EQ(corpus.files[0].relativePath, "field/sample.mll");
    EXPECT_EQ(summary.fileCount, 1U);
    EXPECT_EQ(summary.supportedFileCount, 1U);
    EXPECT_EQ(summary.normalShapeCount, 1U);
    EXPECT_EQ(summary.totalMemberCount, 2U);
    EXPECT_EQ(summary.mldLikeMemberCount, 1U);
    EXPECT_EQ(summary.mldIndexShapeMemberCount, 1U);
    EXPECT_EQ(summary.mldObjectListProbeCount, 3U);
    EXPECT_EQ(summary.nonZeroMldObjectListProbeCount, 1U);
    EXPECT_EQ(summary.plausibleMldObjectListProbeCount, 1U);
    EXPECT_EQ(summary.mldBlockProbeCount, 4U);
    EXPECT_EQ(summary.exactReferencedMldBlockProbeCount, 1U);
    EXPECT_EQ(summary.gvrTextureProbeCount, 1U);
    EXPECT_EQ(summary.parsedGvrTextureProbeCount, 1U);
    EXPECT_EQ(summary.failedGvrTextureProbeCount, 0U);
    EXPECT_EQ(summary.decodedGvrTextureProbeCount, 1U);
    EXPECT_EQ(summary.textureMemberProbeCount, 1U);
    EXPECT_EQ(summary.denseGlobalIndexTextureMemberCount, 1U);
    EXPECT_EQ(summary.textureClusterCount, 1U);
    EXPECT_EQ(summary.headerTextureTableBeforeFirstTextureCount, 1U);
    EXPECT_EQ(summary.indexTexturePointerInsideTextureSpanCount, 0U);
    EXPECT_EQ(summary.preTextureTableProbeCount, 1U);
    EXPECT_EQ(summary.preTextureTableAlignedProbeCount, 1U);
    EXPECT_EQ(summary.preTextureTableEntryCount, 1U);
    EXPECT_EQ(summary.preTextureTablePrintableNameCount, 1U);
    EXPECT_EQ(summary.preTextureTableTextureNameMatchCount, 1U);

    const auto filesCsv = spice::mll::formatMllCorpusFilesCsv(corpus);
    const auto membersCsv = spice::mll::formatMllCorpusMembersCsv(corpus);
    const auto mldObjectListsCsv = spice::mll::formatMllCorpusMldObjectListsCsv(corpus);
    const auto mldBlocksCsv = spice::mll::formatMllCorpusMldBlocksCsv(corpus);
    const auto gvrTexturesCsv = spice::mll::formatMllCorpusGvrTexturesCsv(corpus);
    const auto textureMembersCsv = spice::mll::formatMllCorpusTextureMembersCsv(corpus);
    const auto preTextureTableEntriesCsv = spice::mll::formatMllCorpusPreTextureTableEntriesCsv(corpus);
    const auto indexedBinTablesCsv = spice::mll::formatMllCorpusIndexedBinTablesCsv(corpus);
    const auto anomaliesCsv = spice::mll::formatMllCorpusAnomaliesCsv(corpus);
    const auto histogramCsv = spice::mll::formatMllCorpusPayloadKindHistogramCsv(corpus);
    EXPECT_EQ(filesCsv.rfind("path,rawSize,decodedSize", 0U), 0U);
    EXPECT_NE(filesCsv.find("field/sample.mll"), std::string::npos);
    EXPECT_NE(filesCsv.find(",normal,"), std::string::npos);
    EXPECT_EQ(membersCsv.rfind("filePath,memberIndex,recordOffset", 0U), 0U);
    EXPECT_NE(membersCsv.find("embeddedMldIndexEntryShapePlausible"), std::string::npos);
    EXPECT_EQ(mldObjectListsCsv.rfind("filePath,memberIndex,memberName", 0U), 0U);
    EXPECT_NE(mldObjectListsCsv.find("listBytes32Hex"), std::string::npos);
    EXPECT_NE(mldObjectListsCsv.find("00000001000000a0"), std::string::npos);
    EXPECT_NE(mldObjectListsCsv.find(",180,true,"), std::string::npos);
    EXPECT_NE(mldObjectListsCsv.find("NJCM"), std::string::npos);
    EXPECT_EQ(mldBlocksCsv.rfind("filePath,memberIndex,memberName", 0U), 0U);
    EXPECT_NE(mldBlocksCsv.find(",160,NJCM,true,"), std::string::npos);
    EXPECT_NE(mldBlocksCsv.find("entry=0;field=0x14;list=0x90;valueIndex=0"), std::string::npos);
    EXPECT_NE(mldBlocksCsv.find(",576,4,pof0,false,false,0,POF0,true,"), std::string::npos);
    EXPECT_EQ(gvrTexturesCsv.rfind("filePath,memberIndex,memberName", 0U), 0U);
    EXPECT_NE(gvrTexturesCsv.find(",0,a.mld,72,384,mld,true,true,0,256,272,16,8,"), std::string::npos);
    EXPECT_NE(gvrTexturesCsv.find(",true,true,false,true,17,"), std::string::npos);
    EXPECT_NE(gvrTexturesCsv.find(",RGBA8,None,false,false,4,4,"), std::string::npos);
    EXPECT_NE(gvrTexturesCsv.find(",true,64,1,"), std::string::npos);
    EXPECT_EQ(textureMembersCsv.rfind("filePath,memberIndex,memberName", 0U), 0U);
    EXPECT_NE(textureMembersCsv.find(",0,a.mld,72,384,mld,true,true,1,256,"), std::string::npos);
    EXPECT_NE(textureMembersCsv.find(",true,true,true,true,17,17,1,0,0,true,false,17,"), std::string::npos);
    EXPECT_EQ(preTextureTableEntriesCsv.rfind("filePath,memberIndex,memberName", 0U), 0U);
    EXPECT_NE(preTextureTableEntriesCsv.find(",1,256,192,256,64,true,true,true,true,1,44,1,16,1,0,1,0,196,tex_17,true,false,true,17,"), std::string::npos);
    EXPECT_NE(preTextureTableEntriesCsv.find(",true,0,256,272,"), std::string::npos);
    EXPECT_NE(preTextureTableEntriesCsv.find(",RGBA8,None,false,false,4,4,64,true,true,"), std::string::npos);
    EXPECT_EQ(indexedBinTablesCsv.rfind("filePath,memberIndex,memberName", 0U), 0U);
    EXPECT_EQ(anomaliesCsv.find("field/sample.mll"), std::string::npos);
    EXPECT_NE(histogramCsv.find("field,normal,mld"), std::string::npos);

    const auto written = spice::mll::writeMllCorpusArtifacts(corpus, outputDir);
    EXPECT_TRUE(std::filesystem::exists(written.jsonPath));
    EXPECT_TRUE(std::filesystem::exists(written.filesCsvPath));
    EXPECT_TRUE(std::filesystem::exists(written.membersCsvPath));
    EXPECT_TRUE(std::filesystem::exists(written.mldObjectListsCsvPath));
    EXPECT_TRUE(std::filesystem::exists(written.mldBlocksCsvPath));
    EXPECT_TRUE(std::filesystem::exists(written.gvrTexturesCsvPath));
    EXPECT_TRUE(std::filesystem::exists(written.textureMembersCsvPath));
    EXPECT_TRUE(std::filesystem::exists(written.preTextureTableEntriesCsvPath));
    EXPECT_TRUE(std::filesystem::exists(written.indexedBinTablesCsvPath));
    EXPECT_TRUE(std::filesystem::exists(written.anomaliesCsvPath));
    EXPECT_TRUE(std::filesystem::exists(written.payloadKindHistogramCsvPath));

    const auto json = readText(written.jsonPath);
    EXPECT_NE(json.find("\"fileCount\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"supportedFileCount\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"mldLikeMemberCount\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"mldIndexShapeMemberCount\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"nonZeroMldObjectListProbeCount\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"plausibleMldObjectListProbeCount\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"mldBlockProbeCount\": 4"), std::string::npos);
    EXPECT_NE(json.find("\"exactReferencedMldBlockProbeCount\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"gvrTextureProbeCount\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"parsedGvrTextureProbeCount\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"failedGvrTextureProbeCount\": 0"), std::string::npos);
    EXPECT_NE(json.find("\"decodedGvrTextureProbeCount\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"textureMemberProbeCount\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"denseGlobalIndexTextureMemberCount\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"preTextureTableProbeCount\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"preTextureTableProbe\": {"), std::string::npos);
    EXPECT_NE(json.find("\"declaredEntryCount\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"entryStride\": 44"), std::string::npos);
    EXPECT_NE(json.find("\"trailingPaddingSize\": 16"), std::string::npos);
    EXPECT_NE(json.find("\"name\": \"tex_17\""), std::string::npos);
    EXPECT_NE(json.find("\"nameMatchesKnownTextureGlobalIndex\": true"), std::string::npos);
    EXPECT_NE(json.find("\"orderTextureFormat\": \"RGBA8\""), std::string::npos);
    EXPECT_NE(json.find("\"nameSuffixMatchesOrderTextureGlobalIndex\": true"), std::string::npos);
    EXPECT_NE(json.find("\"textureTableProbe\": {"), std::string::npos);
    EXPECT_NE(json.find("\"globalIndexSequencePreview\": \"17\""), std::string::npos);
    EXPECT_NE(json.find("\"embeddedMldIndexEntryShapePlausible\": true"), std::string::npos);
    EXPECT_NE(json.find("\"tag\": \"NJCM\""), std::string::npos);
    EXPECT_NE(json.find("\"textureFormat\": \"RGBA8\""), std::string::npos);
    EXPECT_NE(json.find("\"targetSignature\": \"NJCM\""), std::string::npos);
}

TEST(SpiceMllCorpusExport, ReportsInferredCountFilesAsAnomalies) {
    const auto root = makeTempOutputDir("spice_mll_corpus_export_inferred_count");
    const auto mllPath = root / "inferred.mll";
    writeBytes(mllPath, makeInferredCountMllFixture());

    const auto corpus = spice::mll::scanMllCorpus(mllPath);
    const auto summary = spice::mll::summarizeMllCorpusFeedback(corpus);

    EXPECT_EQ(summary.fileCount, 1U);
    EXPECT_EQ(summary.supportedFileCount, 0U);
    EXPECT_EQ(summary.normalShapeCount, 1U);
    EXPECT_EQ(summary.warningCount, 1U);

    const auto filesCsv = spice::mll::formatMllCorpusFilesCsv(corpus);
    EXPECT_NE(filesCsv.find("first-member-offset"), std::string::npos);

    const auto anomaliesCsv = spice::mll::formatMllCorpusAnomaliesCsv(corpus);
    EXPECT_NE(anomaliesCsv.find("inferred.mll"), std::string::npos);
    EXPECT_NE(anomaliesCsv.find("first-member-offset"), std::string::npos);
}
