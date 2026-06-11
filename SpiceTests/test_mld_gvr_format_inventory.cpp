#include "../SpiceMLD/SpiceMLD.h"

#include <gtest/gtest.h>

namespace {

spice::mld::model::MldTextureEntry makeTexture(
    const std::string& format,
    const std::string& palette,
    const std::uint8_t rawFlags,
    const std::uint8_t rawDataFormat,
    const bool decoded = true) {
    spice::mld::model::MldTextureEntry entry{};
    entry.archiveTextureIndex = 4;
    entry.textureName = "sample";
    entry.hasGlobalIndex = true;
    entry.globalIndex = 3;
    entry.pixelFormat = rawFlags;
    entry.dataFormat = rawDataFormat;
    entry.sourceFormat = format;
    entry.sourcePaletteFormat = palette;
    entry.width = 32;
    entry.height = 16;
    entry.imageDataSize = 2048;
    entry.decoded = decoded;
    return entry;
}

spice::mld::model::MldTextureArchive makeArchive(std::vector<spice::mld::model::MldTextureEntry> entries) {
    spice::mld::model::MldTextureArchive archive{};
    archive.entries = std::move(entries);
    return archive;
}

} // namespace

TEST(MldGvrFormatInventory, ClassifiesCurrentRgba8Coverage) {
    spice::mld::analysis::MldGvrTextureSample covered{};
    covered.sourceFormat = "RGBA8";
    covered.sourcePaletteFormat = "None";

    EXPECT_TRUE(spice::mld::analysis::isGvrEncoderCovered(covered));

    covered.hasMipmaps = true;
    EXPECT_TRUE(spice::mld::analysis::isGvrEncoderCovered(covered));

    spice::mld::analysis::MldGvrTextureSample indexed{};
    indexed.sourceFormat = "CI8";
    indexed.sourcePaletteFormat = "RGB5A3";
    indexed.paletteDataSize = 512;
    EXPECT_FALSE(spice::mld::analysis::isGvrEncoderCovered(indexed));

    spice::mld::analysis::MldGvrTextureSample cmpr{};
    cmpr.sourceFormat = "CMPR";
    cmpr.sourcePaletteFormat = "None";
    cmpr.hasMipmaps = true;
    EXPECT_TRUE(spice::mld::analysis::isGvrEncoderCovered(cmpr));

    spice::mld::analysis::MldGvrTextureSample rgb5a3{};
    rgb5a3.sourceFormat = "RGB5A3";
    rgb5a3.sourcePaletteFormat = "None";
    rgb5a3.hasMipmaps = true;
    EXPECT_TRUE(spice::mld::analysis::isGvrEncoderCovered(rgb5a3));

    spice::mld::analysis::MldGvrTextureSample ci4{};
    ci4.sourceFormat = "CI4";
    ci4.sourcePaletteFormat = "RGB5A3";
    ci4.hasInternalPalette = true;
    ci4.paletteDataSize = 32;
    EXPECT_TRUE(spice::mld::analysis::isGvrEncoderCovered(ci4));
}

TEST(MldGvrFormatInventory, AggregatesByFormatTupleAndRanksUnsupported) {
    spice::mld::analysis::MldGvrFormatInventoryBuilder builder{};
    builder.noteFileScanned();
    builder.addParsedMld("a.mld", makeArchive({
        makeTexture("RGBA8", "None", 0x00, 0x06),
        makeTexture("CI8", "RGB5A3", 0x20, 0x23),
        makeTexture("CI8", "RGB5A3", 0x20, 0x23, false),
    }));
    builder.noteFileScanned();
    builder.addParsedMld("b.mld", makeArchive({
        makeTexture("CMPR", "None", 0x00, 0x0E),
    }));

    const auto inventory = builder.build();

    EXPECT_EQ(inventory.filesScanned, 2U);
    EXPECT_EQ(inventory.filesParsed, 2U);
    EXPECT_EQ(inventory.textureCount, 4U);
    EXPECT_EQ(inventory.decodedTextureCount, 3U);
    ASSERT_FALSE(inventory.samples.empty());
    EXPECT_EQ(inventory.samples.front().archiveTextureIndex, 4U);
    EXPECT_TRUE(inventory.samples.front().hasGlobalIndex);
    EXPECT_EQ(inventory.samples.front().globalIndex, 3U);
    ASSERT_EQ(inventory.formatGroups.size(), 3U);
    ASSERT_EQ(inventory.priorityGroups.size(), 1U);
    EXPECT_EQ(inventory.priorityGroups[0].sourceFormat, "CI8");
    EXPECT_EQ(inventory.priorityGroups[0].textureCount, 2U);
    EXPECT_EQ(inventory.priorityGroups[0].decodedCount, 1U);
}

TEST(MldGvrFormatInventory, CapsRepresentativeSamplesDeterministically) {
    spice::mld::analysis::MldGvrFormatInventoryBuilder builder{};
    for (int i = 0; i < 7; ++i) {
        builder.noteFileScanned();
        builder.addParsedMld("sample_" + std::to_string(i) + ".mld", makeArchive({
            makeTexture("RGB5A3", "None", 0x00, 0x05),
        }));
    }

    const auto inventory = builder.build();

    ASSERT_EQ(inventory.formatGroups.size(), 1U);
    EXPECT_EQ(inventory.formatGroups[0].textureCount, 7U);
    EXPECT_EQ(inventory.formatGroups[0].representativeSamples.size(), 5U);
    EXPECT_EQ(inventory.formatGroups[0].representativeSamples.front().sourcePath, "sample_0.mld");
    EXPECT_EQ(inventory.formatGroups[0].representativeSamples.back().sourcePath, "sample_4.mld");
}

TEST(MldGvrFormatInventory, FormatsJsonAndMarkdownReports) {
    spice::mld::analysis::MldGvrFormatInventoryBuilder builder{};
    builder.noteFileScanned();
    auto texture = makeTexture("RGB565", "None", 0x00, 0x04);
    texture.diagnostics.push_back("test diagnostic");
    builder.addParsedMld("level.mld", makeArchive({ texture }));
    builder.addParseFailure("bad.mld", "parse failed");

    const auto inventory = builder.build();
    const auto json = spice::mld::analysis::formatMldGvrFormatInventoryJson(inventory);
    const auto markdown = spice::mld::analysis::formatMldGvrFormatInventoryMarkdown(inventory);

    EXPECT_NE(json.find("\"sourceFormat\": \"RGB565\""), std::string::npos);
    EXPECT_NE(json.find("\"filesFailed\": 1"), std::string::npos);
    EXPECT_NE(markdown.find("Priority Encoder Targets"), std::string::npos);
    EXPECT_NE(markdown.find("RGB565"), std::string::npos);
}
