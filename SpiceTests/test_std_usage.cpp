#include "../Compression/Aklz.h"
#include "../SpiceStd/SpiceStd.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::filesystem::path makeTempOutputDir(const std::string& name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

std::vector<std::uint8_t> bytesFromText(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
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

const spice::stdfile::StdUsageFile* findFile(
    const spice::stdfile::StdUsageScanResult& scan,
    const std::string& path) {
    for (const auto& file : scan.files) {
        if (file.relativePath == path) {
            return &file;
        }
    }
    return nullptr;
}

} // namespace

TEST(SpiceStdUsage, ScansDirectoryAndClassifiesStdUsageBuckets) {
    const auto root = makeTempOutputDir("spice_std_usage_scan");
    const auto inputDir = root / "input";
    const auto outputDir = root / "output";

    const auto mAnimationBytes = bytesFromText("MOTION_TABLE std motion sample");
    const auto compressed = spice::compression::aklz::compress(mAnimationBytes);
    ASSERT_TRUE(compressed.ok());

    writeBytes(inputDir / "bchara" / "m001.std", compressed.bytes);
    writeBytes(inputDir / "bchara" / "common.std", bytesFromText("COMMON_STD shared data"));
    writeBytes(inputDir / "bchara" / "cr001.std", bytesFromText("CHARACTER_STD cr resource"));
    writeBytes(inputDir / "field" / "field_only.std", bytesFromText("FIELD_STD unexpected"));
    writeBytes(inputDir / "ignored.bin", bytesFromText("ignored"));

    const auto scan = spice::stdfile::scanStdUsage(inputDir);
    const auto summary = spice::stdfile::summarizeStdUsage(scan);

    ASSERT_TRUE(scan.inputWasDirectory);
    ASSERT_EQ(scan.files.size(), 4U);
    EXPECT_EQ(summary.fileCount, 4U);
    EXPECT_EQ(summary.aklzCompressedFileCount, 1U);
    EXPECT_EQ(summary.decodeErrorCount, 0U);
    EXPECT_EQ(summary.alxKnownCoveredPatternCount, 1U);
    EXPECT_EQ(summary.bcharaFileCount, 3U);
    EXPECT_EQ(summary.otherDirectoryFileCount, 1U);

    const auto* mAnimation = findFile(scan, "bchara/m001.std");
    ASSERT_NE(mAnimation, nullptr);
    EXPECT_TRUE(mAnimation->sourceWasCompressedAklz);
    EXPECT_TRUE(mAnimation->decodedOk);
    EXPECT_TRUE(mAnimation->alxKnownCoveredPattern);
    EXPECT_EQ(mAnimation->usageBucket, spice::stdfile::StdUsageBucket::BcharaMFamily);
    EXPECT_EQ(mAnimation->decodedSize, mAnimationBytes.size());
    EXPECT_EQ(mAnimation->decodedHeader16Hex, "4d4f54494f4e5f5441424c4520737464");
    ASSERT_FALSE(mAnimation->printableStrings.empty());
    EXPECT_EQ(mAnimation->printableStrings[0], "MOTION_TABLE std motion sample");

    const auto* common = findFile(scan, "bchara/common.std");
    ASSERT_NE(common, nullptr);
    EXPECT_EQ(common->usageBucket, spice::stdfile::StdUsageBucket::BcharaCommon);
    EXPECT_FALSE(common->alxKnownCoveredPattern);

    const auto* character = findFile(scan, "bchara/cr001.std");
    ASSERT_NE(character, nullptr);
    EXPECT_EQ(character->usageBucket, spice::stdfile::StdUsageBucket::BcharaCharacterResource);

    const auto* field = findFile(scan, "field/field_only.std");
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->usageBucket, spice::stdfile::StdUsageBucket::OtherDirectory);

    const auto filesCsv = spice::stdfile::formatStdUsageFilesCsv(scan);
    const auto bucketsCsv = spice::stdfile::formatStdUsageBucketsCsv(scan);
    EXPECT_EQ(filesCsv.rfind("path,absolutePath,directory,stem,usageBucket", 0U), 0U);
    EXPECT_NE(filesCsv.find("bchara/m001.std"), std::string::npos);
    EXPECT_NE(filesCsv.find("bchara_m_family,true"), std::string::npos);
    EXPECT_NE(filesCsv.find("field/field_only.std"), std::string::npos);
    EXPECT_NE(bucketsCsv.find("bchara_m_family,1,1,0,1"), std::string::npos);
    EXPECT_NE(bucketsCsv.find("other_directory,1,0,0,0"), std::string::npos);

    const auto written = spice::stdfile::writeStdUsageArtifacts(scan, outputDir);
    EXPECT_TRUE(std::filesystem::exists(written.filesCsvPath));
    EXPECT_TRUE(std::filesystem::exists(written.bucketsCsvPath));
    EXPECT_NE(readText(written.filesCsvPath).find("bchara/cr001.std"), std::string::npos);
}
