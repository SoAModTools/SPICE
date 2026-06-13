#include "../SpiceMlk/SpiceMlk.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kEntryOffset = 0x20U;
constexpr std::size_t kListGroundLinks = 0x100U;
constexpr std::size_t kListParam2 = 0x108U;
constexpr std::size_t kListFunctionParams = 0x110U;
constexpr std::size_t kListObjects = 0x11CU;
constexpr std::size_t kListGrounds = 0x124U;
constexpr std::size_t kListMotions = 0x12CU;
constexpr std::size_t kGrndOffset = 0x140U;
constexpr std::size_t kTextureTable = 0x170U;

void writeBeU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    ASSERT_LE(offset + 4U, bytes.size());
    bytes[offset + 0U] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value & 0xffU);
}

void writeBeU16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    ASSERT_LE(offset + 2U, bytes.size());
    bytes[offset + 0U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value & 0xffU);
}

void writeBeF32(std::vector<std::uint8_t>& bytes, std::size_t offset, float value) {
    writeBeU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void writeTag(std::vector<std::uint8_t>& bytes, std::size_t offset, const char* tag) {
    ASSERT_LE(offset + 4U, bytes.size());
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

std::vector<std::uint8_t> makeMinimalMld(std::string fxnName = "wall") {
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
    writeBeU32(bytes, kEntryOffset + 0x20U, 0U);
    fxnName.resize(std::min<std::size_t>(fxnName.size(), 31U));
    std::copy(fxnName.begin(), fxnName.end(), bytes.begin() + static_cast<std::ptrdiff_t>(kEntryOffset + 0x24U));
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
    const std::array<std::uint32_t, 0> empty{};
    writeList(bytes, kListGroundLinks, groundLinks);
    writeList(bytes, kListParam2, empty);
    writeList(bytes, kListFunctionParams, functionParams);
    writeList(bytes, kListObjects, empty);
    writeList(bytes, kListGrounds, grounds);
    writeList(bytes, kListMotions, empty);

    writeTag(bytes, kGrndOffset, "GRND");
    writeBeU32(bytes, kGrndOffset + 4U, 0x2CU);
    writeBeU32(bytes, kGrndOffset + 0x10U, 0);
    writeBeU32(bytes, kGrndOffset + 0x14U, 0);
    writeBeU16(bytes, kGrndOffset + 0x20U, 2U);
    writeBeU16(bytes, kGrndOffset + 0x22U, 3U);
    writeBeU16(bytes, kGrndOffset + 0x24U, 4U);
    writeBeU16(bytes, kGrndOffset + 0x26U, 5U);
    writeBeU16(bytes, kGrndOffset + 0x28U, 0U);
    writeBeU16(bytes, kGrndOffset + 0x2AU, 0U);

    writeBeU32(bytes, kTextureTable, 0U);
    return bytes;
}

struct MlkRecordFixture {
    std::uint32_t key{};
    std::uint32_t rawWord12{};
    std::vector<std::uint8_t> payload{};
};

std::vector<std::uint8_t> makeMlk(std::vector<MlkRecordFixture> records) {
    const auto tableEnd = 0x08U + (records.size() * 0x10U);
    std::size_t totalSize = tableEnd;
    for (const auto& record : records) {
        totalSize += record.payload.size();
    }

    std::vector<std::uint8_t> bytes(totalSize, 0U);
    writeBeU32(bytes, 0x00U, 0x0000ffffU);
    writeBeU32(bytes, 0x04U, (static_cast<std::uint32_t>(records.size()) << 16U) | 0xffffU);

    std::uint32_t payloadOffset = static_cast<std::uint32_t>(tableEnd);
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto recordOffset = 0x08U + (i * 0x10U);
        writeBeU32(bytes, recordOffset + 0x00U, records[i].key);
        writeBeU32(bytes, recordOffset + 0x04U, payloadOffset);
        writeBeU32(bytes, recordOffset + 0x08U, static_cast<std::uint32_t>(records[i].payload.size()));
        writeBeU32(bytes, recordOffset + 0x0CU, records[i].rawWord12);
        std::copy(
            records[i].payload.begin(),
            records[i].payload.end(),
            bytes.begin() + static_cast<std::ptrdiff_t>(payloadOffset));
        payloadOffset += static_cast<std::uint32_t>(records[i].payload.size());
    }
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

TEST(SpiceMlkBlenderIrExport, WritesCombinedSceneManifestAndRecordsCsv) {
    const auto root = makeTempOutputDir("spice_mlk_blender_ir_single");
    const auto mlkPath = root / "sample.mlk";
    const auto outputDir = root / "out";
    writeBytes(mlkPath, makeMlk({
        MlkRecordFixture{
            .key = 7704300U,
            .rawWord12 = 5U,
            .payload = makeMinimalMld("camera"),
        },
    }));

    const auto exported = spice::mlk::exportMlkBlenderIr(mlkPath, outputDir);

    ASSERT_FALSE(exported.inputWasDirectory);
    ASSERT_EQ(exported.files.size(), 1U);
    const auto& file = exported.files[0];
    EXPECT_EQ(file.relativePath, "sample.mlk");
    EXPECT_EQ(file.recordCount, 1U);
    EXPECT_EQ(file.parsedRecordCount, 1U);
    EXPECT_EQ(file.skippedRecordCount, 0U);
    EXPECT_TRUE(std::filesystem::exists(file.combinedBlenderIrPath));
    EXPECT_TRUE(std::filesystem::exists(file.manifestPath));
    EXPECT_TRUE(std::filesystem::exists(file.recordsCsvPath));

    const auto json = readText(file.combinedBlenderIrPath);
    EXPECT_NE(json.find("\"sourceRecord\":{\"containerKind\":\"mlk\""), std::string::npos);
    EXPECT_NE(json.find("\"containerPath\":\"sample.mlk\""), std::string::npos);
    EXPECT_NE(json.find("\"recordIndex\":0"), std::string::npos);
    EXPECT_NE(json.find("\"key\":7704300"), std::string::npos);
    EXPECT_NE(json.find("\"generatedMldName\":\"E7704300.MLD\""), std::string::npos);
    EXPECT_NE(json.find("\"rawWord12\":5"), std::string::npos);
    EXPECT_NE(json.find("sample_record_000__"), std::string::npos);

    const auto manifest = readText(file.manifestPath);
    EXPECT_NE(manifest.find("\"parsedRecordCount\": 1"), std::string::npos);
    EXPECT_NE(manifest.find("\"generatedMldName\": \"E7704300.MLD\""), std::string::npos);
    EXPECT_NE(manifest.find("\"status\": \"parsed\""), std::string::npos);

    const auto csv = readText(file.recordsCsvPath);
    EXPECT_EQ(csv.rfind("filePath,recordIndex,recordOffset,key,generatedMldName", 0U), 0U);
    EXPECT_NE(csv.find("sample.mlk,0,8,7704300,E7704300.MLD,5"), std::string::npos);
}

TEST(SpiceMlkBlenderIrExport, PreservesRelativeParentsAndReportsSkippedRows) {
    const auto root = makeTempOutputDir("spice_mlk_blender_ir_directory");
    const auto inputDir = root / "input";
    const auto outputDir = root / "out";
    const auto mlkPath = inputDir / "beff" / "combo.mlk";
    writeBytes(mlkPath, makeMlk({
        MlkRecordFixture{
            .key = 7704300U,
            .rawWord12 = 5U,
            .payload = makeMinimalMld("camera0"),
        },
        MlkRecordFixture{
            .key = 104771U,
            .rawWord12 = 7U,
            .payload = makeMinimalMld("camera1"),
        },
        MlkRecordFixture{
            .key = 1U,
            .rawWord12 = 99U,
            .payload = std::vector<std::uint8_t>{ 'P', 'O', 'F', '0' },
        },
    }));
    writeBytes(inputDir / "ignored.mld", makeMinimalMld("ignored"));

    const auto exported = spice::mlk::exportMlkBlenderIr(inputDir, outputDir);

    ASSERT_TRUE(exported.inputWasDirectory);
    ASSERT_EQ(exported.files.size(), 1U);
    const auto& file = exported.files[0];
    EXPECT_EQ(file.relativePath, "beff/combo.mlk");
    EXPECT_EQ(file.outputDir, outputDir / "beff" / "combo");
    EXPECT_EQ(file.recordCount, 3U);
    EXPECT_EQ(file.parsedRecordCount, 2U);
    EXPECT_EQ(file.skippedRecordCount, 1U);

    const auto json = readText(file.combinedBlenderIrPath);
    EXPECT_NE(json.find("\"recordIndex\":0"), std::string::npos);
    EXPECT_NE(json.find("\"recordIndex\":1"), std::string::npos);
    EXPECT_NE(json.find("\"rawWord12\":5"), std::string::npos);
    EXPECT_NE(json.find("\"rawWord12\":7"), std::string::npos);
    EXPECT_NE(json.find("combo_record_000__"), std::string::npos);
    EXPECT_NE(json.find("combo_record_001__"), std::string::npos);

    const auto manifest = readText(file.manifestPath);
    EXPECT_NE(manifest.find("\"skippedRecordCount\": 1"), std::string::npos);
    EXPECT_NE(manifest.find("\"skipReason\": \"payload-kind-not-mld\""), std::string::npos);

    const auto csv = readText(file.recordsCsvPath);
    EXPECT_NE(csv.find("beff/combo.mlk,2,40,1,E0000001.MLD,99"), std::string::npos);
    EXPECT_NE(csv.find("payload-kind-not-mld"), std::string::npos);
}

TEST(SpiceMlkBlenderIrExport, GeneratesNamesLikeResourceNameBuilder) {
    EXPECT_EQ(spice::mlk::generatedMldNameForKey(104771U), "E0104771.MLD");
    EXPECT_EQ(spice::mlk::generatedMldNameForKey(7705100U), "E7705100.MLD");
}
