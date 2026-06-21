#include "../SpiceBin/SpiceBin.h"
#include "../Compression/Aklz.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

void writeBeU32(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint32_t value) {
    bytes[offset + 0U] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value & 0xffU);
}

std::vector<std::uint8_t> makeIndexedBinFixture() {
    std::vector<std::uint8_t> bytes(0x50U, 0U);
    writeBeU32(bytes, 0x00U, 2U);
    writeBeU32(bytes, 0x04U, 0x10U);
    writeBeU32(bytes, 0x08U, 0x20U);
    writeBeU32(bytes, 0x1cU, 0x0cU);
    writeBeU32(bytes, 0x20U, 0x30U);
    bytes[0x24U] = 0x41U;
    bytes[0x25U] = 0x42U;
    writeBeU32(bytes, 0x2cU, 0x0cU);
    writeBeU32(bytes, 0x30U, 0x40U);
    bytes[0x34U] = 0x43U;
    bytes[0x35U] = 0x44U;
    return bytes;
}

std::vector<std::uint8_t> makeMldLikeIndexedBinFixture() {
    std::vector<std::uint8_t> bytes(0x100U, 0U);
    writeBeU32(bytes, 0x00U, 1U);
    writeBeU32(bytes, 0x04U, 0x14U);
    writeBeU32(bytes, 0x08U, 0U);
    writeBeU32(bytes, 0x0cU, 0x20U);
    writeBeU32(bytes, 0x10U, 0U);
    writeBeU32(bytes, 0x1cU, 0x08U);
    writeBeU32(bytes, 0x20U, 0x30U);
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

TEST(SpiceBinParser, ProbesIndexedLayoutTables) {
    const auto parsed = spice::bin::parseBytes(makeIndexedBinFixture(), "layout.bin");

    ASSERT_TRUE(parsed.ok());
    EXPECT_EQ(parsed.rawSize, 0x50U);
    const auto& probe = parsed.indexedTableProbe;
    EXPECT_TRUE(probe.present);
    EXPECT_TRUE(probe.headerInBounds);
    EXPECT_EQ(probe.count, 2U);
    EXPECT_EQ(probe.offsetTableOffset, 0x04U);
    EXPECT_EQ(probe.offsetTableEndOffset, 0x0cU);
    EXPECT_EQ(probe.dataBaseOffset, 0x0cU);
    EXPECT_TRUE(probe.offsetTableInBounds);
    EXPECT_TRUE(probe.offsetsInBounds);
    EXPECT_TRUE(probe.offsetsMonotonic);
    EXPECT_EQ(probe.firstRecordOffset, 0x1cU);
    EXPECT_EQ(probe.lastRecordOffset, 0x2cU);
    EXPECT_EQ(probe.offsetsPreview, "16 32");
    ASSERT_EQ(probe.samples.size(), 2U);
    EXPECT_EQ(probe.samples[0].recordOffset, 0x1cU);
    EXPECT_EQ(probe.samples[0].word0, 0x0cU);
    EXPECT_TRUE(probe.samples[0].word0EqualsDataBaseOffset);
    EXPECT_EQ(probe.samples[0].word4, 0x30U);
    EXPECT_TRUE(probe.samples[0].word4TargetInBounds);
    EXPECT_EQ(probe.samples[0].word8, 0x41420000U);
    EXPECT_EQ(probe.samples[0].word12, 0U);
    EXPECT_EQ(probe.samples[0].word16, 0x0cU);
    EXPECT_EQ(probe.samples[0].word20, 0x40U);
    EXPECT_EQ(probe.samples[0].word24, 0x43440000U);
    EXPECT_EQ(probe.samples[0].bytes16Hex.substr(0U, 16U), "0000000c00000030");
    EXPECT_EQ(probe.samples[0].bytes32Hex.substr(0U, 24U), "0000000c0000003041420000");
    EXPECT_EQ(probe.samples[1].recordOffset, 0x2cU);
    EXPECT_EQ(probe.samples[1].word0, 0x0cU);
    EXPECT_EQ(probe.samples[1].word4, 0x40U);
}

TEST(SpiceBinParser, ProbesIndexedLayoutEvenWhenBytesLookMldLike) {
    const auto probe = spice::bin::probeIndexedTable(makeMldLikeIndexedBinFixture());

    EXPECT_TRUE(probe.present);
    EXPECT_EQ(probe.count, 1U);
    EXPECT_TRUE(probe.offsetTableInBounds);
    EXPECT_EQ(probe.dataBaseOffset, 0x08U);
    ASSERT_EQ(probe.samples.size(), 1U);
    EXPECT_TRUE(probe.samples[0].word0EqualsDataBaseOffset);
    EXPECT_TRUE(probe.samples[0].word4TargetInBounds);
}

TEST(SpiceBinCorpus, ScansDirectoryAndWritesIndexedTableArtifacts) {
    const auto root = makeTempOutputDir("spice_bin_corpus_scan");
    const auto inputDir = root / "input";
    const auto outputDir = root / "output";

    const auto indexed = makeIndexedBinFixture();
    const auto compressed = spice::compression::aklz::compress(indexed);
    ASSERT_TRUE(compressed.ok());

    writeBytes(inputDir / "field" / "layout.bin", indexed);
    writeBytes(inputDir / "battle" / "HrsBinCW.bin", compressed.bytes);
    writeBytes(inputDir / "ignored.txt", indexed);

    const auto corpus = spice::bin::scanBinCorpus(inputDir);
    const auto summary = spice::bin::summarizeBinCorpusFeedback(corpus);

    ASSERT_TRUE(corpus.inputWasDirectory);
    ASSERT_EQ(corpus.files.size(), 2U);
    EXPECT_EQ(summary.fileCount, 2U);
    EXPECT_EQ(summary.aklzCompressedFileCount, 1U);
    EXPECT_EQ(summary.decodeErrorCount, 0U);
    EXPECT_EQ(summary.indexedTableProbeCount, 2U);
    EXPECT_EQ(summary.plausibleIndexedTableProbeCount, 2U);
    EXPECT_EQ(summary.errorCount, 0U);

    const auto filesCsv = spice::bin::formatBinCorpusFilesCsv(corpus);
    const auto indexedTablesCsv = spice::bin::formatBinCorpusIndexedTablesCsv(corpus);

    EXPECT_EQ(filesCsv.rfind("path,absolutePath,rawSize,decodedSize", 0U), 0U);
    EXPECT_NE(filesCsv.find("battle/HrsBinCW.bin"), std::string::npos);
    EXPECT_NE(filesCsv.find("field/layout.bin"), std::string::npos);
    EXPECT_NE(filesCsv.find(",true,true,,true,true,"), std::string::npos);
    EXPECT_NE(filesCsv.find(",false,true,,true,true,"), std::string::npos);
    EXPECT_EQ(indexedTablesCsv.rfind("filePath,rawSize,decodedSize", 0U), 0U);
    EXPECT_NE(indexedTablesCsv.find("battle/HrsBinCW.bin"), std::string::npos);
    EXPECT_NE(indexedTablesCsv.find("field/layout.bin"), std::string::npos);
    EXPECT_NE(indexedTablesCsv.find(",2,4,12,12,true,true,true,28,44,2,16 32,0,"), std::string::npos);

    const auto written = spice::bin::writeBinCorpusArtifacts(corpus, outputDir);
    EXPECT_TRUE(std::filesystem::exists(written.filesCsvPath));
    EXPECT_TRUE(std::filesystem::exists(written.indexedTablesCsvPath));
    EXPECT_NE(readText(written.indexedTablesCsvPath).find("battle/HrsBinCW.bin"), std::string::npos);
}
