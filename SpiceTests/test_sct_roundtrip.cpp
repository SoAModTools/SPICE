#include "../SpiceSCT/SpiceSCT.h"
#include "../Compression/Aklz.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kHeaderSize = 12;
constexpr std::size_t kIndexEntrySize = 0x14;
constexpr std::size_t kIndexNameOffset = 4;

void writeU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
{
    bytes[offset + 0u] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[offset + 3u] = static_cast<std::uint8_t>(value & 0xffu);
}

void appendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    const auto offset = bytes.size();
    bytes.resize(offset + 4u);
    writeU32(bytes, offset, value);
}

void writeName(std::vector<std::uint8_t>& bytes, std::size_t offset, std::string name)
{
    for (std::size_t i = 0; i < 16u; ++i) {
        bytes[offset + i] = 0u;
    }
    for (std::size_t i = 0; i < name.size() && i < 16u; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>(name[i]);
    }
}

std::vector<std::uint8_t> makeScriptWithGarbage()
{
    std::vector<std::uint8_t> section{};
    appendU32(section, 10u);
    appendU32(section, 12u);
    appendU32(section, 0xdeadbeefu);
    appendU32(section, 0xcafebabeu);
    appendU32(section, 12u);
    return section;
}

std::vector<std::uint8_t> makeReturnOnlyScript()
{
    std::vector<std::uint8_t> section{};
    appendU32(section, 12u);
    return section;
}

std::vector<std::uint8_t> makeStringSection()
{
    std::vector<std::uint8_t> section{};
    appendU32(section, 9u);
    appendU32(section, 0x1du);
    appendU32(section, 999u);
    return section;
}

std::vector<std::uint8_t> makeSct(std::span<const std::vector<std::uint8_t>> sections)
{
    std::vector<std::uint8_t> out(kHeaderSize + (sections.size() * kIndexEntrySize), 0u);
    writeU32(out, 8u, static_cast<std::uint32_t>(sections.size()));

    std::uint32_t sectionStart = 0;
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const auto rowOffset = kHeaderSize + (i * kIndexEntrySize);
        writeU32(out, rowOffset, sectionStart);
        writeName(out, rowOffset + kIndexNameOffset, "M0000" + std::to_string(i + 1u));
        sectionStart += static_cast<std::uint32_t>(sections[i].size());
    }

    for (const auto& section : sections) {
        out.insert(out.end(), section.begin(), section.end());
    }
    return out;
}

std::vector<std::uint8_t> makeRoundTripFixture()
{
    const std::vector<std::vector<std::uint8_t>> sections = {
        makeScriptWithGarbage(),
        makeReturnOnlyScript(),
        makeStringSection(),
    };
    return makeSct(sections);
}

} // namespace

TEST(SctRoundTrip, CanonicalExportDropsUnreachableGarbageButPreservesSemantics)
{
    const auto originalBytes = makeRoundTripFixture();
    const auto original = spice::sct::SctParser{}.parse(originalBytes, "fixture.sct");
    ASSERT_TRUE(original.parseOk);
    ASSERT_EQ(3u, original.file.sections.size());
    EXPECT_EQ(2u, original.file.sections[0].instructions.size());
    EXPECT_FALSE(original.file.sections[0].rawSpans.empty());

    const auto exported = spice::sct::SctBinaryExporter{}.exportFile(original);
    EXPECT_NE(originalBytes, exported);

    const auto reparsed = spice::sct::SctParser{}.parse(exported, "fixture.exported.sct");
    ASSERT_TRUE(reparsed.parseOk);
    ASSERT_EQ(3u, reparsed.file.sections.size());
    EXPECT_EQ(2u, reparsed.file.sections[0].instructions.size());
    EXPECT_TRUE(reparsed.file.sections[0].rawSpans.empty());

    const auto comparison = spice::sct::SctSemanticComparer{}.compare(original, reparsed);
    EXPECT_TRUE(comparison.equivalent);
}

TEST(SctRoundTrip, EveryScriptSectionIsExportedAsPotentialEntryPoint)
{
    const auto original = spice::sct::SctParser{}.parse(makeRoundTripFixture(), "fixture.sct");
    const auto exported = spice::sct::SctBinaryExporter{}.exportFile(original);
    const auto reparsed = spice::sct::SctParser{}.parse(exported, "fixture.exported.sct");

    ASSERT_TRUE(reparsed.parseOk);
    ASSERT_EQ(3u, reparsed.file.sections.size());
    EXPECT_EQ(spice::sct::SctSectionKind::Script, reparsed.file.sections[0].kind);
    EXPECT_EQ(spice::sct::SctSectionKind::Script, reparsed.file.sections[1].kind);
    EXPECT_EQ(1u, reparsed.file.sections[1].instructions.size());
    EXPECT_EQ(12u, reparsed.file.sections[1].instructions.front().opcode);
}

TEST(SctRoundTrip, PreserveModeReturnsOriginalBytesForDiagnostics)
{
    const auto originalBytes = makeRoundTripFixture();
    const auto original = spice::sct::SctParser{}.parse(originalBytes, "fixture.sct");

    spice::sct::SctExportOptions options{};
    options.mode = spice::sct::SctExportMode::PreserveBytesForTest;
    const auto preserved = spice::sct::SctBinaryExporter{}.exportFile(original, options);

    EXPECT_EQ(originalBytes, preserved);
}

TEST(SctRoundTrip, CompressedInputCanExportCanonicalUncompressedBytes)
{
    const auto originalBytes = makeRoundTripFixture();
    const auto compressed = spice::compression::aklz::compress(originalBytes);
    ASSERT_TRUE(compressed.ok()) << spice::compression::aklz::errorToString(compressed.error);

    const auto original = spice::sct::SctParser{}.parse(compressed.bytes, "fixture.sct");
    ASSERT_TRUE(original.parseOk);
    EXPECT_TRUE(original.file.originalCompressedAklz);

    const auto exported = spice::sct::SctBinaryExporter{}.exportFile(original);
    EXPECT_FALSE(spice::compression::aklz::isAklz(exported));

    const auto reparsed = spice::sct::SctParser{}.parse(exported, "fixture.exported.sct");
    const auto comparison = spice::sct::SctSemanticComparer{}.compare(original, reparsed);
    EXPECT_TRUE(comparison.equivalent);
}
