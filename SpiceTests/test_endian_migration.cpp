#include "../SpiceBin/BinParser.h"
#include "../Compression/Aklz.h"
#include "../SpiceGrinder/Cli/CliParser.h"
#include "../SpiceMix/Application/OperationRunner.h"
#include "../SpiceMlk/MlkParser.h"
#include "../SpiceMll/MllBinaryExporter.h"
#include "../SpiceMll/MllParser.h"
#include "../SpiceRoot/Binary/EndianReader.h"
#include "../SpiceRoot/Binary/EndianWriter.h"
#include "../SpiceSstSml/SstSmlDocumentImporter.h"
#include "../SpiceStd/StdFileWriter.h"
#include "../SpiceStd/StdParser.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

using spice::root::Endian;
using spice::root::EndianSpanWriter;

std::vector<std::uint8_t> makeStd(Endian endian) {
    std::vector<std::uint8_t> bytes(0x28U, 0U);
    EndianSpanWriter writer(bytes, endian);
    writer.write_u16_at(0U, 0x12U);
    writer.write_u16_at(2U, 0x34U);
    writer.write_u32_at(8U, 1U);
    writer.write_i16_at(0x10U, 7);
    writer.write_i16_at(0x12U, 2);
    writer.write_u32_at(0x18U, 0x12345678U);
    return bytes;
}

std::vector<std::uint8_t> makeMlk(Endian endian) {
    std::vector<std::uint8_t> bytes(0x1CU, 0U);
    EndianSpanWriter writer(bytes, endian);
    writer.write_u16_at(4U, 1U);
    writer.write_u32_at(8U, 0x22U);
    writer.write_u32_at(0x0CU, 0x18U);
    writer.write_u32_at(0x10U, 4U);
    bytes[0x18U] = 'P'; bytes[0x19U] = 'O'; bytes[0x1AU] = 'F'; bytes[0x1BU] = '0';
    return bytes;
}

std::vector<std::uint8_t> makeMll(Endian endian) {
    std::vector<std::uint8_t> bytes(0x2CU, 0U);
    EndianSpanWriter writer(bytes, endian);
    writer.write_u32_at(0U, 0x0000FFFFU);
    writer.write_u32_at(4U, endian == Endian::Big ? 0x0001FFFFU : 0xFFFF0001U);
    const std::string name = "sample.bin";
    std::copy(name.begin(), name.end(), bytes.begin() + 8);
    writer.write_u32_at(0x1CU, 0x28U);
    writer.write_u32_at(0x20U, 4U);
    writer.write_u32_at(0x24U, 0xFFFFFFFFU);
    bytes[0x28U] = 'P'; bytes[0x29U] = 'O'; bytes[0x2AU] = 'F'; bytes[0x2BU] = '0';
    return bytes;
}

std::vector<std::uint8_t> makeBin(Endian endian) {
    std::vector<std::uint8_t> bytes(0x28U, 0U);
    EndianSpanWriter writer(bytes, endian);
    writer.write_u32_at(0U, 1U);
    writer.write_u32_at(4U, 0U);
    writer.write_u32_at(8U, 8U);
    writer.write_u32_at(12U, 0x20U);
    return bytes;
}

std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>> makeStage(Endian endian) {
    std::vector<std::uint8_t> embedded(0x7CU, 0U);
    EndianSpanWriter embeddedWriter(embedded, endian);
    embeddedWriter.write_u32_at(0U, 1U);
    embeddedWriter.write_u32_at(4U, 0x14U);

    std::vector<std::uint8_t> sml(0x18U + embedded.size(), 0U);
    EndianSpanWriter smlWriter(sml, endian);
    smlWriter.write_u16_at(4U, 1U);
    smlWriter.write_u32_at(0x0CU, 0x18U);
    smlWriter.write_u32_at(0x10U, static_cast<std::uint32_t>(embedded.size()));
    std::copy(embedded.begin(), embedded.end(), sml.begin() + 0x18U);

    std::vector<std::uint8_t> sst(0x24U, 0U);
    EndianSpanWriter sstWriter(sst, endian);
    sstWriter.write_u16_at(4U, 1U);
    sstWriter.write_u32_at(0x0CU, 0x10U);
    sstWriter.write_u32_at(0x10U, 0U);
    sstWriter.write_i16_at(0x14U, -1);
    return { std::move(sml), std::move(sst) };
}

void writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    ASSERT_TRUE(out.good());
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(out.good());
}

} // namespace

TEST(SpiceRootEndianMigration, CheckedRangesAndMutableWriterSupportBothEndianModes) {
    EXPECT_EQ(spice::root::checked_add(4U, 8U), 12U);
    EXPECT_FALSE(spice::root::checked_multiply(std::numeric_limits<std::size_t>::max(), 2U));
    EXPECT_EQ(spice::root::checked_table_end(4U, 3U, 8U), 28U);
    EXPECT_FALSE(spice::root::checked_table_end(4U, std::numeric_limits<std::size_t>::max(), 8U));
    std::vector<std::uint8_t> bytes(8U, 0U);
    EndianSpanWriter(bytes, Endian::Little).write_u32_at(0U, 0x12345678U);
    EndianSpanWriter(bytes, Endian::Big).write_u32_at(4U, 0x12345678U);
    const spice::root::EndianReader little(bytes, Endian::Little);
    const spice::root::EndianReader big(bytes, Endian::Big);
    EXPECT_EQ(little.read_u32(0U), 0x12345678U);
    EXPECT_EQ(big.read_u32(4U), 0x12345678U);
    EXPECT_TRUE(little.try_subspan(2U, 4U));
    EXPECT_FALSE(little.try_subspan(7U, 2U));
    EXPECT_THROW(EndianSpanWriter(bytes, Endian::Big).write_u32_at(6U, 1U), std::out_of_range);

    spice::root::EndianWriter owning(Endian::Little);
    owning.write_u16(0x1234U);
    owning.write_u32(0x89abcdefU);
    const spice::root::EndianReader owningReader(owning.data(), Endian::Little);
    EXPECT_EQ(owningReader.read_u16(0U), 0x1234U);
    EXPECT_EQ(owningReader.read_u32(2U), 0x89abcdefU);
}

TEST(SpiceEndianMigration, AutoDetectsEquivalentSstSmlPairs) {
    for (const auto endian : { Endian::Big, Endian::Little }) {
        const auto [sml, sst] = makeStage(endian);
        const auto parsed = spice::sstsml::SstSmlDocumentImporter::importBytes(sml, sst);
        ASSERT_TRUE(parsed.ok());
        EXPECT_EQ(parsed.receipt.sml.endian, endian);
        EXPECT_EQ(parsed.receipt.sst.endian, endian);
        EXPECT_EQ(parsed.document->members.size(), 1U);
    }
}

TEST(SpiceEndianMigration, RejectsMixedEndianPairsAndRecordsForcedEndian) {
    const auto [bigSml, bigSst] = makeStage(Endian::Big);
    const auto [littleSml, littleSst] = makeStage(Endian::Little);
    const auto mixed = spice::sstsml::SstSmlDocumentImporter::importBytes(bigSml, littleSst);
    EXPECT_FALSE(mixed.ok());
    EXPECT_TRUE(std::any_of(mixed.diagnostics.begin(), mixed.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.message.find("byte orders do not agree") != std::string::npos;
    }));

    const auto forced = spice::mlk::MlkParser::parse(makeMlk(Endian::Little), {}, { .forcedEndian = Endian::Little });
    ASSERT_TRUE(forced.ok());
    EXPECT_TRUE(forced.endianWasForced);
    EXPECT_EQ(forced.sourceEndian, Endian::Little);
}

TEST(SpiceEndianMigration, StdPreservesLittleEndianThroughEditingAndWriting) {
    auto parsed = spice::stdfile::parseBytes(makeStd(Endian::Little));
    ASSERT_TRUE(parsed.ok());
    ASSERT_EQ(parsed.sourceEndian, Endian::Little);
    parsed.actionRows.rows[0].flags = 0x89ABCDEFU;
    const auto written = spice::stdfile::StdFileWriter{}.write(parsed, {
        .sourceEncoding = spice::stdfile::StdSourceEncoding::Plain,
        .preserveExactSourceWhenUnchanged = false,
    });
    ASSERT_TRUE(written.ok());
    const auto reparsed = spice::stdfile::parseBytes(written.bytes);
    ASSERT_TRUE(reparsed.ok());
    EXPECT_EQ(reparsed.sourceEndian, Endian::Little);
    EXPECT_EQ(reparsed.actionRows.rows[0].flags, 0x89ABCDEFU);
}

TEST(SpiceEndianMigration, AutoDetectsMlkMllAndIndexedBin) {
    for (const auto endian : { Endian::Big, Endian::Little }) {
        const auto mlk = spice::mlk::MlkParser::parse(makeMlk(endian));
        ASSERT_TRUE(mlk.ok()); EXPECT_EQ(mlk.sourceEndian, endian); EXPECT_EQ(mlk.records.size(), 1U);
        const auto mllBytes = makeMll(endian);
        const auto mll = spice::mll::MllParser::parse(mllBytes);
        ASSERT_TRUE(mll.ok()); EXPECT_EQ(mll.sourceEndian, endian); EXPECT_EQ(mll.members.size(), 1U);
        EXPECT_EQ(spice::mll::MllBinaryExporter{}.exportFile(mll), mllBytes);
        const auto bin = spice::bin::parseBytes(makeBin(endian));
        ASSERT_TRUE(bin.sourceEndian); EXPECT_EQ(*bin.sourceEndian, endian); EXPECT_EQ(bin.indexedTableProbe.count, 1U);
    }
}

TEST(SpiceEndianMigration, IndexedBinRequiresForcedEndianWhenStructuralScoresTie) {
    std::vector<std::uint8_t> ambiguous(4U + 65536U * sizeof(std::uint32_t), 0U);
    ambiguous[2U] = 1U;
    const auto automatic = spice::bin::parseBytes(ambiguous);
    EXPECT_FALSE(automatic.sourceEndian.has_value());
    EXPECT_TRUE(std::any_of(automatic.diagnostics.begin(), automatic.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.message.find("ambiguous") != std::string::npos;
    }));
    const auto forced = spice::bin::parseBytes(std::move(ambiguous), {}, { .forcedEndian = Endian::Little });
    ASSERT_TRUE(forced.sourceEndian.has_value());
    EXPECT_EQ(*forced.sourceEndian, Endian::Little);
    EXPECT_TRUE(forced.endianWasForced);
}

TEST(SpiceEndianMigration, IndexedBinDecodesAklzBeforeEndianDetection) {
    const auto raw = makeBin(Endian::Big);
    const auto compressed = spice::compression::aklz::compress(raw);
    ASSERT_TRUE(compressed.ok());

    const auto parsed = spice::bin::parseBytes(compressed.bytes);
    EXPECT_TRUE(parsed.sourceWasCompressedAklz);
    EXPECT_EQ(parsed.rawSize, compressed.bytes.size());
    EXPECT_EQ(parsed.decodedSize, raw.size());
    EXPECT_EQ(parsed.decodedBytes, raw);
    ASSERT_TRUE(parsed.sourceEndian.has_value());
    EXPECT_EQ(*parsed.sourceEndian, Endian::Big);
    EXPECT_EQ(parsed.indexedTableProbe.count, 1U);
}

TEST(SpiceEndianMigration, AklzDoesNotResolveStructurallyAmbiguousEndian) {
    std::vector<std::uint8_t> ambiguousMlk(0x18U, 0U);
    const auto compressedMlk = spice::compression::aklz::compress(ambiguousMlk);
    ASSERT_TRUE(compressedMlk.ok());
    const auto mlk = spice::mlk::MlkParser::parse(compressedMlk.bytes);
    EXPECT_FALSE(mlk.ok());
    EXPECT_TRUE(std::any_of(mlk.diagnostics.begin(), mlk.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.message.find("byte order is ambiguous") != std::string::npos;
    }));

    std::vector<std::uint8_t> ambiguousMll(0x28U, 0U);
    const auto compressedMll = spice::compression::aklz::compress(ambiguousMll);
    ASSERT_TRUE(compressedMll.ok());
    const auto mll = spice::mll::MllParser::parse(compressedMll.bytes);
    EXPECT_FALSE(mll.ok());
    EXPECT_TRUE(std::any_of(mll.diagnostics.begin(), mll.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.message.find("byte order is ambiguous") != std::string::npos;
    }));
}

TEST(SpiceGrinderCli, ParsesDreamcastParityAuditAndEnforcesEuGroup) {
    const std::string_view valid[]{ "audit-dreamcast-parity", "--dreamcast-us", "dc", "--gamecube-us", "gc", "--output", "out" };
    const auto parsed = spice::grinder::cli::parse(valid);
    ASSERT_EQ(parsed.disposition, spice::grinder::cli::ParseDisposition::Run);
    ASSERT_TRUE(std::holds_alternative<spice::mix::AuditDreamcastParityRequest>(*parsed.request));
    const std::string_view invalid[]{ "audit-dreamcast-parity", "--dreamcast-us", "dc", "--gamecube-us", "gc", "--output", "out", "--gamecube-eu", "eu" };
    EXPECT_EQ(spice::grinder::cli::parse(invalid).disposition, spice::grinder::cli::ParseDisposition::Error);
}

TEST(SpiceDreamcastParityAudit, WritesUnifiedAndPerFormatReports) {
    const auto root = std::filesystem::temp_directory_path() / "spice_dreamcast_parity_test";
    std::filesystem::remove_all(root);
    const auto dc = root / "dc";
    const auto gc = root / "gc";
    const auto output = root / "out";
    writeFile(dc / "FIELD" / "sample.STD", makeStd(Endian::Little));
    writeFile(gc / "FIELD" / "sample.STD", makeStd(Endian::Big));
    writeFile(dc / "FIELD" / "sample.MLK", makeMlk(Endian::Little));
    writeFile(gc / "FIELD" / "sample.MLK", makeMlk(Endian::Big));
    spice::mix::OperationRequest request = spice::mix::AuditDreamcastParityRequest{ dc, gc, {}, {}, {}, output };
    spice::mix::OperationContext context{};
    const auto result = spice::mix::OperationRunner{}.run(request, context);
    ASSERT_EQ(result.status, spice::mix::OperationStatus::Success);
    EXPECT_TRUE(std::filesystem::exists(output / "manifest.json"));
    EXPECT_TRUE(std::filesystem::exists(output / "summary.csv"));
    EXPECT_TRUE(std::filesystem::exists(output / "automatic-endian.csv"));
    EXPECT_TRUE(std::filesystem::exists(output / "parser-failures.csv"));
    EXPECT_TRUE(std::filesystem::exists(output / "eu-collapsed.csv"));
    EXPECT_TRUE(std::filesystem::exists(output / "eu-disc-conflicts.csv"));
    EXPECT_TRUE(std::filesystem::exists(output / "formats" / "std" / "files.csv"));
    EXPECT_TRUE(std::filesystem::exists(output / "formats" / "std" / "records.csv"));
    EXPECT_TRUE(std::filesystem::exists(output / "formats" / "mlk" / "files.csv"));
    EXPECT_TRUE(std::filesystem::exists(output / "formats" / "mlk" / "records.csv"));
    std::filesystem::remove_all(root);
}
