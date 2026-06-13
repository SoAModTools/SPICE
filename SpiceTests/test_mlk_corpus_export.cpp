#include "../SpiceMlk/SpiceMlk.h"

#include <gtest/gtest.h>

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

std::vector<std::uint8_t> makeMlkWithFirstPayloadCountCandidateFixture() {
    auto bytes = makeMlkProbeFixture();
    writeBeU32(bytes, 0x0cU, 0x00000038U);
    bytes[0x38U] = 'N';
    bytes[0x39U] = 'J';
    bytes[0x3aU] = 'C';
    bytes[0x3bU] = 'M';
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

TEST(SpiceMlkCorpusExport, ScansDirectoryAndWritesJsonCsvArtifacts) {
    const auto root = makeTempOutputDir("spice_mlk_corpus_export_scan");
    const auto inputDir = root / "input";
    const auto outputDir = root / "output";
    const auto mlkPath = inputDir / "dir,one" / "sample.mlk";
    writeBytes(mlkPath, makeMlkProbeFixture());
    writeBytes(inputDir / "ignored.mld", makeMlkProbeFixture());

    const auto corpus = spice::mlk::scanMlkCorpus(inputDir);

    ASSERT_TRUE(corpus.inputWasDirectory);
    ASSERT_EQ(corpus.files.size(), 1U);
    EXPECT_EQ(corpus.files[0].relativePath, "dir,one/sample.mlk");
    EXPECT_EQ(corpus.files[0].scan.selectedRecordCount, 2U);
    ASSERT_EQ(corpus.files[0].records.size(), 2U);

    const auto filesCsv = spice::mlk::formatMlkCorpusFilesCsv(corpus);
    const auto recordsCsv = spice::mlk::formatMlkCorpusRecordsCsv(corpus);
    const auto histogramCsv = spice::mlk::formatMlkCorpusWord12HistogramCsv(corpus);
    const auto anomaliesCsv = spice::mlk::formatMlkCorpusAnomaliesCsv(corpus);
    const auto word12ByKindCsv = spice::mlk::formatMlkCorpusRawWord12ByKindCsv(corpus);
    EXPECT_EQ(filesCsv.rfind("path,rawSize,decodedSize", 0U), 0U);
    EXPECT_NE(filesCsv.find("\"dir,one/sample.mlk\""), std::string::npos);
    EXPECT_NE(filesCsv.find(",normal,"), std::string::npos);
    EXPECT_EQ(recordsCsv.rfind("filePath,recordIndex,recordOffset", 0U), 0U);
    EXPECT_NE(histogramCsv.find("directory,recordCountSource,rawWord12,count"), std::string::npos);
    EXPECT_EQ(anomaliesCsv.find("dir,one/sample.mlk"), std::string::npos);
    EXPECT_NE(word12ByKindCsv.find("directory,tableShape,payloadKind,rawWord12,count"), std::string::npos);
    EXPECT_NE(word12ByKindCsv.find("normal,ninja-chunk,48,1"), std::string::npos);

    const auto written = spice::mlk::writeMlkCorpusArtifacts(corpus, outputDir);
    EXPECT_TRUE(std::filesystem::exists(written.jsonPath));
    EXPECT_TRUE(std::filesystem::exists(written.filesCsvPath));
    EXPECT_TRUE(std::filesystem::exists(written.recordsCsvPath));
    EXPECT_TRUE(std::filesystem::exists(written.word12HistogramCsvPath));
    EXPECT_TRUE(std::filesystem::exists(written.anomaliesCsvPath));
    EXPECT_TRUE(std::filesystem::exists(written.word12ByKindCsvPath));
    EXPECT_TRUE(std::filesystem::exists(written.embeddedMldSummaryCsvPath));

    const auto json = readText(written.jsonPath);
    EXPECT_NE(json.find("\"fileCount\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"recordCountSource\": \"header-u16-at-0x04\""), std::string::npos);
    EXPECT_NE(json.find("\"tableShape\": \"normal\""), std::string::npos);
}

TEST(SpiceMlkCorpusExport, AttemptsEntryListParseForPlausibleEmbeddedMldPayloads) {
    const auto root = makeTempOutputDir("spice_mlk_corpus_export_embedded_mld");
    const auto mlkPath = root / "embedded.mlk";
    writeBytes(mlkPath, makeMlkWithEmbeddedMldFixture());

    const auto corpus = spice::mlk::scanMlkCorpus(mlkPath);

    ASSERT_FALSE(corpus.inputWasDirectory);
    ASSERT_EQ(corpus.files.size(), 1U);
    ASSERT_EQ(corpus.files[0].records.size(), 1U);
    EXPECT_TRUE(corpus.files[0].records[0].record.embeddedMldHeader.plausible);
    EXPECT_TRUE(corpus.files[0].records[0].embeddedMldParse.attempted);

    const auto recordsCsv = spice::mlk::formatMlkCorpusRecordsCsv(corpus);
    EXPECT_NE(recordsCsv.find("embedded.mlk,0,8"), std::string::npos);
    EXPECT_NE(recordsCsv.find(",true,"), std::string::npos);

    const auto json = spice::mlk::formatMlkCorpusJson(corpus);
    EXPECT_NE(json.find("\"embeddedMldParseAttempted\": true"), std::string::npos);
    EXPECT_NE(json.find("\"embeddedMldObjectReferenceCount\""), std::string::npos);
    EXPECT_NE(json.find("\"embeddedMldSampleFxnNames\""), std::string::npos);

    const auto embeddedSummaryCsv = spice::mlk::formatMlkCorpusEmbeddedMldSummaryCsv(corpus);
    EXPECT_EQ(embeddedSummaryCsv.rfind("filePath,recordIndex,key,rawWord12", 0U), 0U);
    EXPECT_NE(embeddedSummaryCsv.find("embedded.mlk,0,"), std::string::npos);
}

TEST(SpiceMlkCorpusExport, ReportsFirstPayloadCountCandidateAnomalies) {
    const auto root = makeTempOutputDir("spice_mlk_corpus_export_first_payload_candidate");
    const auto mlkPath = root / "candidate.mlk";
    writeBytes(mlkPath, makeMlkWithFirstPayloadCountCandidateFixture());

    const auto corpus = spice::mlk::scanMlkCorpus(mlkPath);

    const auto filesCsv = spice::mlk::formatMlkCorpusFilesCsv(corpus);
    EXPECT_NE(filesCsv.find("first-payload-count-candidate"), std::string::npos);

    const auto anomaliesCsv = spice::mlk::formatMlkCorpusAnomaliesCsv(corpus);
    EXPECT_NE(anomaliesCsv.find("candidate.mlk"), std::string::npos);
    EXPECT_NE(anomaliesCsv.find("first-payload-count-candidate"), std::string::npos);
}
