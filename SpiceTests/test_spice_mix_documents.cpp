#include "../SpiceMix/Documents/GvrDocumentSession.h"
#include "../SpiceMix/Documents/EctDocumentSession.h"
#include "../SpiceMix/Documents/MldDocumentSession.h"
#include "../SpiceMix/Documents/PvrDocumentSession.h"
#include "../SpiceMix/Documents/SstSmlDocumentSession.h"
#include "../SpiceGvm/Encoding/GvrEncoder.h"
#include "../SpiceGvm/Image/PngCodec.h"
#include "../SpicePvm/Encoding/PvrEncoder.h"
#include "../SpicePvm/Parsing/PvmParser.h"
#include "../SpiceEct/EctFileWriter.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace {

void writeU32Be(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 24U);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 16U);
    bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value);
}

void writeU16Be(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value);
}

void writeU32Le(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

void writeFile(const std::filesystem::path& path, const std::span<const std::uint8_t> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), {});
}

std::filesystem::path makeTempDirectory() {
    const auto name = "spice_mix_documents_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::create_directories(path);
    return path;
}

spice::gvm::model::RgbaImage image(const std::uint32_t width, const std::uint32_t height,
    const std::uint8_t red, const std::uint8_t green) {
    spice::gvm::model::RgbaImage out{};
    out.width = width;
    out.height = height;
    out.rgba8.resize(static_cast<std::size_t>(width) * height * 4U);
    for (std::size_t offset = 0; offset < out.rgba8.size(); offset += 4U) {
        out.rgba8[offset] = red;
        out.rgba8[offset + 1U] = green;
        out.rgba8[offset + 2U] = 40U;
        out.rgba8[offset + 3U] = 255U;
    }
    return out;
}

spice::ect::EctEncounterTable ectTable(
    const std::uint16_t stage,
    const std::uint16_t overallRate,
    const std::uint16_t encounterId,
    const std::uint16_t encounterRate) {
    spice::ect::EctEncounterTable table{};
    table.stage = stage;
    table.overallEncounterRate = overallRate;
    table.encounters[0] = { encounterId, encounterRate };
    table.encounters[2] = { static_cast<std::uint16_t>(encounterId + 1U),
        static_cast<std::uint16_t>(encounterRate + 3U) };
    return table;
}

std::vector<std::uint8_t> makeTwoTextureMld() {
    const auto first = spice::gvm::encoding::encodeGvr(image(4, 4, 255, 0));
    const auto second = spice::gvm::encoding::encodeGvr(image(4, 4, 0, 255));
    constexpr std::size_t textureTable = 0x340U;
    constexpr std::size_t recordSize = 44U;
    const std::size_t archiveHeaderSize = 4U + 2U * recordSize;
    std::vector<std::uint8_t> bytes(textureTable + archiveHeaderSize, 0U);
    writeU32Be(bytes, 0x00U, 1U);
    writeU32Be(bytes, 0x04U, 0x20U);
    writeU32Be(bytes, 0x08U, 0x108U);
    writeU32Be(bytes, 0x0CU, 0x180U);
    writeU32Be(bytes, 0x10U, static_cast<std::uint32_t>(textureTable));
    writeU32Be(bytes, 0x20U, 7U);
    writeU32Be(bytes, 0x24U, 9U);
    writeU32Be(bytes, 0x28U, 0x100U);
    writeU32Be(bytes, 0x2CU, 0x108U);
    writeU32Be(bytes, 0x30U, 0x108U);
    writeU32Be(bytes, 0x34U, 0x118U);
    writeU32Be(bytes, 0x38U, 0x120U);
    writeU32Be(bytes, 0x3CU, 0x128U);
    writeU32Be(bytes, 0x40U, static_cast<std::uint32_t>(textureTable));
    const std::string functionName = "textures";
    std::copy(functionName.begin(), functionName.end(), bytes.begin() + 0x44U);
    writeU32Be(bytes, 0x100U, 0U);
    writeU32Be(bytes, 0x108U, 2U);
    writeU32Be(bytes, 0x10CU, 11U);
    writeU32Be(bytes, 0x110U, 22U);
    writeU32Be(bytes, 0x118U, 0U);
    writeU32Be(bytes, 0x120U, 0U);
    writeU32Be(bytes, 0x128U, 0U);
    writeU32Be(bytes, textureTable, 2U);
    const std::string names[] = { "first", "second" };
    for (std::size_t index = 0; index < 2U; ++index) {
        const auto record = textureTable + 4U + index * recordSize;
        std::copy(names[index].begin(), names[index].end(), bytes.begin() + static_cast<std::ptrdiff_t>(record));
    }
    bytes.insert(bytes.end(), first.begin(), first.end());
    bytes.insert(bytes.end(), second.begin(), second.end());
    return bytes;
}

void addDetailedEntryLists(std::vector<std::uint8_t>& bytes, const bool littleEndian) {
    const auto write = [littleEndian, &bytes](const std::size_t offset, const std::uint32_t value) {
        if (littleEndian) writeU32Le(bytes, offset, value);
        else writeU32Be(bytes, offset, value);
    };
    constexpr std::uint32_t groundLinks = 0x200U;
    constexpr std::uint32_t paramList2 = 0x220U;
    constexpr std::uint32_t functionParameters = 0x240U;
    constexpr std::uint32_t objectAddresses = 0x260U;
    constexpr std::uint32_t groundAddresses = 0x280U;
    constexpr std::uint32_t motionAddresses = 0x2A0U;
    constexpr std::uint32_t textureNames = 0x2C0U;
    write(0x28U, groundLinks);
    write(0x2CU, paramList2);
    write(0x30U, functionParameters);
    write(0x34U, objectAddresses);
    write(0x38U, groundAddresses);
    write(0x3CU, motionAddresses);
    write(0x40U, textureNames);

    const auto writeList = [&write](const std::uint32_t pointer,
        const std::initializer_list<std::uint32_t> values) {
        write(pointer, static_cast<std::uint32_t>(values.size()));
        std::size_t index = 0U;
        for (const auto value : values) write(pointer + 4U + index++ * 4U, value);
    };
    writeList(groundLinks, { 0U, 7U });
    writeList(paramList2, { 8U, 9U });
    writeList(functionParameters, { 10U, 0U, 12U });
    writeList(objectAddresses, { 0U, 0x180U });
    writeList(groundAddresses, { 0x180U, 0U });
    writeList(motionAddresses, { 0U, 0x180U, 0U });

    write(textureNames + 4U, 2U);
    write(textureNames + 8U, 0x300U);
    write(textureNames + 20U, 0x310U);
    const std::string names[] = { "detail_first", "detail_second" };
    std::copy(names[0].begin(), names[0].end(), bytes.begin() + 0x300U);
    std::copy(names[1].begin(), names[1].end(), bytes.begin() + 0x310U);
}

std::vector<std::uint8_t> makeSmlPairMember(const std::span<const std::uint8_t> embeddedMld) {
    std::vector<std::uint8_t> bytes(0x20U + embeddedMld.size(), 0U);
    writeU32Be(bytes, 0x00U, 0x0001FFFFU);
    writeU32Be(bytes, 0x04U, 0x0001FFFFU);
    writeU32Be(bytes, 0x08U, 0x00000100U);
    writeU32Be(bytes, 0x0CU, 0x20U);
    writeU32Be(bytes, 0x10U, static_cast<std::uint32_t>(embeddedMld.size()));
    writeU32Be(bytes, 0x14U, 0x11111111U);
    std::copy(embeddedMld.begin(), embeddedMld.end(), bytes.begin() + 0x20U);
    return bytes;
}

std::vector<std::uint8_t> makeSstPairMember() {
    std::vector<std::uint8_t> bytes(0x60U, 0U);
    writeU32Be(bytes, 0x00U, 0x0001FFFFU);
    writeU32Be(bytes, 0x04U, 0x0001FFFFU);
    writeU32Be(bytes, 0x08U, 0xBBBB0000U);
    writeU32Be(bytes, 0x0CU, 0x20U);

    writeU32Be(bytes, 0x20U, 1U);
    writeU16Be(bytes, 0x24U, 3U);
    writeU16Be(bytes, 0x26U, 0U);
    writeU32Be(bytes, 0x28U, 0x30000004U);
    writeU32Be(bytes, 0x2CU, 0x30000008U);
    writeU32Be(bytes, 0x30U, 0x05060708U);
    writeU16Be(bytes, 0x34U, 0xFFFFU);
    writeU16Be(bytes, 0x36U, 0U);

    writeU16Be(bytes, 0x44U, 0U);
    writeU16Be(bytes, 0x46U, 0x2222U);
    writeU16Be(bytes, 0x48U, 0x0033U);
    writeU16Be(bytes, 0x4AU, 0x0044U);
    return bytes;
}

spice::pvm::model::RgbaImage pvrImage(const std::uint32_t width,
    const std::uint32_t height, const std::uint8_t bias) {
    const auto source = image(width, height, bias, static_cast<std::uint8_t>(bias + 31U));
    return {
        .width = source.width,
        .height = source.height,
        .pixels = source.rgba8,
    };
}

std::vector<std::uint8_t> encodePvr(const std::uint32_t size,
    const std::uint32_t globalIndex, const std::uint8_t bias) {
    spice::pvm::encoding::PvrEncodeOptions options{};
    options.pixelFormat = spice::pvm::model::PixelFormat::Rgb565;
    options.dataLayout = spice::pvm::model::DataLayout::Twiddled;
    options.includeGlobalIndex = true;
    options.globalIndex = globalIndex;
    options.gbixTrailingBytes = {0x11U, 0x22U, 0x33U, 0x44U, 0x55U};
    options.pvrtUnknownHeader = {0xA1U, 0xB2U};
    const auto encoded = spice::pvm::encoding::encodePvrTexture(
        pvrImage(size, size, bias), options);
    EXPECT_TRUE(encoded.ok());
    return encoded.bytes;
}

std::vector<std::uint8_t> makeDreamcastTextureMld(
    const std::vector<std::vector<std::uint8_t>>& textures) {
    constexpr std::size_t textureTable = 0x340U;
    constexpr std::size_t recordSize = 44U;
    const std::size_t archiveHeaderSize = 4U + textures.size() * recordSize;
    std::vector<std::uint8_t> bytes(textureTable + archiveHeaderSize, 0U);
    writeU32Le(bytes, 0x00U, 1U);
    writeU32Le(bytes, 0x04U, 0x20U);
    writeU32Le(bytes, 0x08U, 0x108U);
    writeU32Le(bytes, 0x0CU, 0x180U);
    writeU32Le(bytes, 0x10U, static_cast<std::uint32_t>(textureTable));
    writeU32Le(bytes, 0x20U, 7U);
    writeU32Le(bytes, 0x24U, 9U);
    writeU32Le(bytes, 0x28U, 0x100U);
    writeU32Le(bytes, 0x2CU, 0x108U);
    writeU32Le(bytes, 0x30U, 0x108U);
    writeU32Le(bytes, 0x34U, 0x118U);
    writeU32Le(bytes, 0x38U, 0x120U);
    writeU32Le(bytes, 0x3CU, 0x128U);
    writeU32Le(bytes, 0x40U, static_cast<std::uint32_t>(textureTable));
    const std::string functionName = "textures";
    std::copy(functionName.begin(), functionName.end(), bytes.begin() + 0x44U);
    writeU32Le(bytes, 0x100U, 0U);
    writeU32Le(bytes, 0x108U, 2U);
    writeU32Le(bytes, 0x10CU, 11U);
    writeU32Le(bytes, 0x110U, 22U);
    writeU32Le(bytes, 0x118U, 0U);
    writeU32Le(bytes, 0x120U, 0U);
    writeU32Le(bytes, 0x128U, 0U);
    writeU32Le(bytes, textureTable, static_cast<std::uint32_t>(textures.size()));
    for (std::size_t index = 0; index < textures.size(); ++index) {
        const auto record = textureTable + 4U + index * recordSize;
        const auto name = "dc_tex_" + std::to_string(index);
        std::copy(name.begin(), name.end(), bytes.begin() + static_cast<std::ptrdiff_t>(record));
        writeU32Le(bytes, record + 36U, 0x80000000U);
    }
    for (std::size_t index = 0; index < textures.size(); ++index) {
        const auto record = textureTable + 4U + index * recordSize;
        const auto prefixSize = (32U - (bytes.size() & 31U)) & 31U;
        bytes.resize(bytes.size() + prefixSize, 0xCCU);
        bytes.insert(bytes.end(), textures[index].begin(), textures[index].end());
        writeU32Le(bytes, record + 40U, static_cast<std::uint32_t>(textures[index].size()));
    }
    return bytes;
}

class TempDirectory final {
public:
    TempDirectory() : path(makeTempDirectory()) {}
    ~TempDirectory() { std::error_code error{}; std::filesystem::remove_all(path, error); }
    std::filesystem::path path;
};

} // namespace

TEST(SpiceMixDocuments, NewGvrUsesSafeDefaultsAndRoundTripsThroughSaveAs) {
    TempDirectory temp{};
    const auto png = temp.path / "source.png";
    spice::gvm::image::writePngRgba8(png, image(8, 4, 120, 30));

    auto created = spice::mix::GvrDocumentSession::createFromPng(png);
    ASSERT_TRUE(created.result.ok()) << created.result.message;
    ASSERT_TRUE(created.session);
    const auto snapshot = created.session->snapshot();
    EXPECT_EQ(snapshot.format, "RGBA8");
    EXPECT_FALSE(snapshot.mipmaps);
    EXPECT_FALSE(snapshot.hasGlobalIndex);
    EXPECT_FALSE(snapshot.aklzWrapped);
    EXPECT_TRUE(snapshot.dirty);
    ASSERT_TRUE(created.session->preview().has_value());
    EXPECT_EQ(created.session->preview()->width, 8U);

    const auto output = temp.path / "created.gvr";
    EXPECT_TRUE(created.session->saveAs(output).ok());
    EXPECT_FALSE(created.session->dirty());
    ASSERT_TRUE(std::filesystem::is_regular_file(output));

    auto opened = spice::mix::GvrDocumentSession::open(output);
    ASSERT_TRUE(opened.result.ok()) << opened.result.message;
    EXPECT_EQ(opened.session->snapshot().width, 8U);
    EXPECT_FALSE(opened.session->snapshot().aklzWrapped);
}

TEST(SpiceMixDocuments, GvrReplacementPreservesPropertiesAndCanRevert) {
    TempDirectory temp{};
    const auto sourcePng = temp.path / "source.png";
    const auto replacementPng = temp.path / "replacement.png";
    spice::gvm::image::writePngRgba8(sourcePng, image(4, 4, 10, 20));
    spice::gvm::image::writePngRgba8(replacementPng, image(4, 4, 30, 40));
    auto created = spice::mix::GvrDocumentSession::createFromPng(sourcePng);
    ASSERT_TRUE(created.result.ok());
    const auto output = temp.path / "source.gvr";
    ASSERT_TRUE(created.session->saveAs(output).ok());

    auto opened = spice::mix::GvrDocumentSession::open(output);
    ASSERT_TRUE(opened.result.ok());
    const auto before = opened.session->snapshot();
    EXPECT_TRUE(opened.session->replaceImage(replacementPng).ok());
    EXPECT_TRUE(opened.session->dirty());
    const auto after = opened.session->snapshot();
    EXPECT_EQ(after.format, before.format);
    EXPECT_EQ(after.mipmaps, before.mipmaps);
    EXPECT_EQ(after.hasGlobalIndex, before.hasGlobalIndex);
    EXPECT_TRUE(opened.session->revert().ok());
    EXPECT_FALSE(opened.session->dirty());
}

TEST(SpiceMixDocuments, NewPvrUsesSafeDefaultsAndSupportsTypedReplacement) {
    TempDirectory temp{};
    const auto sourcePng = temp.path / "source.png";
    const auto replacementPng = temp.path / "replacement.png";
    spice::gvm::image::writePngRgba8(sourcePng, image(8, 8, 80, 30));
    spice::gvm::image::writePngRgba8(replacementPng, image(16, 16, 25, 140));

    auto created = spice::mix::PvrDocumentSession::createFromPng(sourcePng);
    ASSERT_TRUE(created.result.ok()) << created.result.message;
    ASSERT_TRUE(created.session);
    auto snapshot = created.session->snapshot();
    EXPECT_EQ(snapshot.pixelFormat, "ARGB4444");
    EXPECT_EQ(snapshot.dataLayout, "Twiddled");
    EXPECT_FALSE(snapshot.mipmaps);
    EXPECT_FALSE(snapshot.hasGlobalIndex);
    EXPECT_TRUE(snapshot.dirty);
    ASSERT_TRUE(created.session->preview().has_value());

    const auto sourcePvr = temp.path / "source.pvr";
    ASSERT_TRUE(created.session->saveAs(sourcePvr).ok());
    EXPECT_FALSE(created.session->dirty());
    auto opened = spice::mix::PvrDocumentSession::open(sourcePvr);
    ASSERT_TRUE(opened.result.ok()) << opened.result.message;

    spice::mix::PvrEncodingOverrides overrides{};
    overrides.pixelFormat = spice::mix::PvrPixelFormat::RGB565;
    overrides.dataLayout = spice::mix::PvrDataLayout::VqMipmaps;
    overrides.globalIndex = {
        .kind = spice::mix::PvrGlobalIndexKind::Value,
        .value = 77U,
    };
    const auto rejectedDimensions = opened.session->replaceImage(
        replacementPng, overrides, false);
    EXPECT_EQ(rejectedDimensions.status, spice::mix::OperationStatus::Failure);
    EXPECT_FALSE(opened.session->dirty());
    EXPECT_EQ(opened.session->snapshot().width, 8U);

    ASSERT_TRUE(opened.session->replaceImage(replacementPng, overrides, true).ok());
    snapshot = opened.session->snapshot();
    EXPECT_EQ(snapshot.pixelFormat, "RGB565");
    EXPECT_EQ(snapshot.dataLayout, "VQMipmaps");
    EXPECT_TRUE(snapshot.mipmaps);
    EXPECT_TRUE(snapshot.hasGlobalIndex);
    EXPECT_EQ(snapshot.globalIndex, 77U);
    EXPECT_EQ(snapshot.width, 16U);
    EXPECT_TRUE(snapshot.dirty);
    ASSERT_TRUE(opened.session->revert().ok());
    EXPECT_FALSE(opened.session->dirty());
    EXPECT_EQ(opened.session->snapshot().pixelFormat, "ARGB4444");
}

TEST(SpiceMixDocuments, PvrReplacementFailuresAndCancellationDoNotMutateDocument) {
    TempDirectory temp{};
    const auto sourcePng = temp.path / "source.png";
    spice::gvm::image::writePngRgba8(sourcePng, image(8, 8, 10, 20));
    auto created = spice::mix::PvrDocumentSession::createFromPng(sourcePng);
    ASSERT_TRUE(created.result.ok());
    const auto sourcePvr = temp.path / "source.pvr";
    ASSERT_TRUE(created.session->saveAs(sourcePvr).ok());
    auto opened = spice::mix::PvrDocumentSession::open(sourcePvr);
    ASSERT_TRUE(opened.result.ok());

    spice::mix::PvrEncodingOverrides invalid{};
    invalid.dataLayout = spice::mix::PvrDataLayout::SmallVq;
    const auto invalidResult = opened.session->replaceImage(sourcePng, invalid, true);
    EXPECT_EQ(invalidResult.status, spice::mix::OperationStatus::Failure);
    EXPECT_FALSE(opened.session->dirty());
    EXPECT_EQ(opened.session->snapshot().dataLayout, "Twiddled");

    std::stop_source stop{};
    stop.request_stop();
    const auto cancelled = opened.session->replaceImage(sourcePng, {}, true,
        spice::mix::DocumentContext{ .stopToken = stop.get_token() });
    EXPECT_EQ(cancelled.status, spice::mix::OperationStatus::Cancelled);
    EXPECT_FALSE(opened.session->dirty());

    const auto invalidDestination = temp.path / "existing_directory";
    std::filesystem::create_directories(invalidDestination);
    EXPECT_EQ(opened.session->saveAs(invalidDestination).status,
        spice::mix::OperationStatus::Failure);
    for (const auto& entry : std::filesystem::directory_iterator(temp.path)) {
        EXPECT_EQ(entry.path().filename().string().find(".spicemix-"), std::string::npos);
    }
}

TEST(SpiceMixDocuments, PvrReplacementPreservesAndCanRemoveSourceMetadata) {
    TempDirectory temp{};
    const auto source = temp.path / "indexed.pvr";
    writeFile(source, encodePvr(8U, 55U, 4U));
    const auto replacementPng = temp.path / "replacement.png";
    spice::gvm::image::writePngRgba8(replacementPng, image(8, 8, 70, 90));

    auto opened = spice::mix::PvrDocumentSession::open(source);
    ASSERT_TRUE(opened.result.ok()) << opened.result.message;
    ASSERT_TRUE(opened.session->replaceImage(replacementPng).ok());
    const auto preservedPath = temp.path / "preserved.pvr";
    ASSERT_TRUE(opened.session->saveAs(preservedPath).ok());
    const auto preserved = spice::pvm::parsing::parsePvrTexture(readFile(preservedPath));
    ASSERT_TRUE(preserved.globalIndex.has_value());
    EXPECT_EQ(*preserved.globalIndex, 55U);
    EXPECT_EQ(preserved.gbixTrailingBytes,
        (std::vector<std::uint8_t>{0x11U, 0x22U, 0x33U, 0x44U, 0x55U}));
    EXPECT_EQ(preserved.pvrtUnknownHeader,
        (std::vector<std::uint8_t>{0xA1U, 0xB2U}));
    EXPECT_EQ(preserved.pixelFormat, spice::pvm::model::PixelFormat::Rgb565);
    EXPECT_EQ(preserved.dataLayout, spice::pvm::model::DataLayout::Twiddled);

    spice::mix::PvrEncodingOverrides removeIndex{};
    removeIndex.globalIndex.kind = spice::mix::PvrGlobalIndexKind::None;
    ASSERT_TRUE(opened.session->replaceImage(replacementPng, removeIndex).ok());
    EXPECT_FALSE(opened.session->snapshot().hasGlobalIndex);
}

TEST(SpiceMixDocuments, PvrDocumentMapsEveryPromotedFormatAndLayoutOverride) {
    TempDirectory temp{};
    const auto png = temp.path / "source.png";
    spice::gvm::image::writePngRgba8(png, image(16, 16, 45, 85));
    auto created = spice::mix::PvrDocumentSession::createFromPng(png);
    ASSERT_TRUE(created.result.ok()) << created.result.message;

    const std::pair<spice::mix::PvrPixelFormat, const char*> formats[] = {
        { spice::mix::PvrPixelFormat::ARGB1555, "ARGB1555" },
        { spice::mix::PvrPixelFormat::RGB565, "RGB565" },
        { spice::mix::PvrPixelFormat::ARGB4444, "ARGB4444" },
    };
    const std::pair<spice::mix::PvrDataLayout, const char*> layouts[] = {
        { spice::mix::PvrDataLayout::Twiddled, "Twiddled" },
        { spice::mix::PvrDataLayout::TwiddledMipmaps, "TwiddledMipmaps" },
        { spice::mix::PvrDataLayout::Vq, "VQ" },
        { spice::mix::PvrDataLayout::VqMipmaps, "VQMipmaps" },
        { spice::mix::PvrDataLayout::Rectangle, "Rectangle" },
        { spice::mix::PvrDataLayout::SmallVq, "SmallVQ" },
        { spice::mix::PvrDataLayout::SmallVqMipmaps, "SmallVQMipmaps" },
        { spice::mix::PvrDataLayout::TwiddledMipmapsDma, "TwiddledMipmapsDMA" },
    };
    for (const auto& [format, formatName] : formats) {
        for (const auto& [layout, layoutName] : layouts) {
            spice::mix::PvrEncodingOverrides overrides{};
            overrides.pixelFormat = format;
            overrides.dataLayout = layout;
            const auto replaced = created.session->replaceImage(png, overrides, true);
            ASSERT_TRUE(replaced.ok()) << formatName << '/' << layoutName << ": " << replaced.message;
            const auto snapshot = created.session->snapshot();
            EXPECT_EQ(snapshot.pixelFormat, formatName);
            EXPECT_EQ(snapshot.dataLayout, layoutName);
        }
    }
}

TEST(SpiceMixDocuments, MldStagesMultipleGvrReplacementsAndProtectsOriginal) {
    TempDirectory temp{};
    const auto source = temp.path / "two_textures.mld";
    writeFile(source, makeTwoTextureMld());
    const auto firstPng = temp.path / "first.png";
    const auto secondPng = temp.path / "second.png";
    spice::gvm::image::writePngRgba8(firstPng, image(4, 4, 10, 80));
    spice::gvm::image::writePngRgba8(secondPng, image(4, 4, 70, 20));

    auto opened = spice::mix::MldDocumentSession::open(source);
    ASSERT_TRUE(opened.result.ok()) << opened.result.message;
    ASSERT_TRUE(opened.session);
    ASSERT_EQ(opened.session->textures().size(), 2U);
    ASSERT_TRUE(opened.session->replaceGvrTexture(0U, firstPng).ok());
    ASSERT_TRUE(opened.session->replaceGvrTexture(1U, secondPng).ok());
    EXPECT_TRUE(opened.session->textures()[0].dirty);
    EXPECT_TRUE(opened.session->textures()[1].dirty);
    ASSERT_TRUE(opened.session->revertTexture(0U).ok());
    EXPECT_FALSE(opened.session->textures()[0].dirty);
    EXPECT_TRUE(opened.session->textures()[1].dirty);

    const auto protectedResult = opened.session->saveAs(source);
    EXPECT_EQ(protectedResult.status, spice::mix::OperationStatus::Failure);
    const auto output = temp.path / "saved.mld";
    ASSERT_TRUE(opened.session->saveAs(output).ok());
    EXPECT_FALSE(opened.session->dirty());
    auto reparsed = spice::mix::MldDocumentSession::open(output);
    ASSERT_TRUE(reparsed.result.ok()) << reparsed.result.message;
    EXPECT_EQ(reparsed.session->textures().size(), 2U);
}

TEST(SpiceMixDocuments, MldStagesAndSavesMultiplePvrReplacements) {
    TempDirectory temp{};
    const auto originalBytes = makeDreamcastTextureMld({
        encodePvr(8U, 101U, 3U), encodePvr(8U, 102U, 9U) });
    const auto source = temp.path / "dreamcast.mld";
    writeFile(source, originalBytes);
    const auto firstPng = temp.path / "first.png";
    const auto secondPng = temp.path / "second.png";
    spice::gvm::image::writePngRgba8(firstPng, image(8, 8, 35, 70));
    spice::gvm::image::writePngRgba8(secondPng, image(16, 16, 95, 20));

    auto opened = spice::mix::MldDocumentSession::open(source);
    ASSERT_TRUE(opened.result.ok()) << opened.result.message;
    ASSERT_TRUE(opened.session);
    auto textures = opened.session->textures();
    ASSERT_EQ(textures.size(), 2U);
    EXPECT_EQ(textures[0].encoding, spice::mix::TextureEncodingKind::Pvr);
    EXPECT_EQ(textures[0].format, "RGB565");
    EXPECT_EQ(textures[0].paletteFormat, "Twiddled");
    ASSERT_TRUE(opened.session->replacePvrTexture(0U, firstPng).ok());
    EXPECT_EQ(opened.session->textures()[0].globalIndex, 101U);

    spice::mix::PvrEncodingOverrides overrides{};
    overrides.pixelFormat = spice::mix::PvrPixelFormat::ARGB4444;
    overrides.dataLayout = spice::mix::PvrDataLayout::VqMipmaps;
    overrides.globalIndex = {
        .kind = spice::mix::PvrGlobalIndexKind::Value,
        .value = 202U,
    };
    EXPECT_EQ(opened.session->replacePvrTexture(1U, secondPng, overrides, false).status,
        spice::mix::OperationStatus::Failure);
    EXPECT_FALSE(opened.session->textures()[1].dirty);
    ASSERT_TRUE(opened.session->replacePvrTexture(1U, secondPng, overrides, true).ok());
    textures = opened.session->textures();
    EXPECT_TRUE(textures[0].dirty);
    EXPECT_TRUE(textures[1].dirty);
    EXPECT_EQ(textures[1].format, "ARGB4444");
    EXPECT_EQ(textures[1].paletteFormat, "VQMipmaps");
    EXPECT_EQ(textures[1].globalIndex, 202U);
    EXPECT_EQ(textures[1].width, 16U);

    const auto blenderPath = temp.path / "staged.json";
    ASSERT_TRUE(opened.session->exportBlenderIrJson(blenderPath).ok());
    const auto blenderJson = readText(blenderPath);
    EXPECT_NE(blenderJson.find("\"encodedFormat\":\"pvr\""), std::string::npos);
    EXPECT_NE(blenderJson.find("\"width\":16"), std::string::npos);

    const auto output = temp.path / "saved.mld";
    ASSERT_TRUE(opened.session->saveAs(output).ok());
    EXPECT_EQ(readFile(source), originalBytes);
    auto reparsed = spice::mix::MldDocumentSession::open(output);
    ASSERT_TRUE(reparsed.result.ok()) << reparsed.result.message;
    const auto savedTextures = reparsed.session->textures();
    ASSERT_EQ(savedTextures.size(), 2U);
    EXPECT_EQ(savedTextures[0].globalIndex, 101U);
    EXPECT_EQ(savedTextures[1].globalIndex, 202U);
    EXPECT_EQ(savedTextures[1].width, 16U);
    EXPECT_EQ(savedTextures[1].paletteFormat, "VQMipmaps");
}

TEST(SpiceMixDocuments, MldExportsCurrentDocumentWithoutChangingDirtyState) {
    TempDirectory temp{};
    const auto source = temp.path / "a101b_DC.mld";
    writeFile(source, makeTwoTextureMld());
    auto opened = spice::mix::MldDocumentSession::open(source);
    ASSERT_TRUE(opened.result.ok()) << opened.result.message;
    ASSERT_TRUE(opened.session);

    std::vector<spice::mix::OperationEvent> events{};
    const spice::mix::DocumentContext context{
        .report = [&events](const auto& event) { events.push_back(event); },
    };
    const auto cleanBlenderPath = temp.path / "clean.json";
    const auto entryPath = temp.path / "a101b_DC.mld.entries.json";
    ASSERT_TRUE(opened.session->exportBlenderIrJson(cleanBlenderPath, context).ok());
    ASSERT_TRUE(opened.session->exportEntryListJson(entryPath, context).ok());
    EXPECT_FALSE(opened.session->dirty());
    ASSERT_TRUE(std::filesystem::is_regular_file(cleanBlenderPath));
    ASSERT_TRUE(std::filesystem::is_regular_file(entryPath));

    const auto entryJson = readText(entryPath);
    EXPECT_NE(entryJson.find("\"schema\": \"spice_mld_entry_list_v1\""), std::string::npos);
    EXPECT_NE(entryJson.find("\"source\": \""), std::string::npos);
    EXPECT_NE(entryJson.find("\"entry_count\": 1"), std::string::npos);
    EXPECT_NE(entryJson.find("\"entryID\": 7"), std::string::npos);
    EXPECT_NE(entryJson.find("\"tableID\": 9"), std::string::npos);
    EXPECT_NE(entryJson.find("\"texture_names\": ["), std::string::npos);

    const auto replacementPng = temp.path / "replacement.png";
    spice::gvm::image::writePngRgba8(replacementPng, image(8, 8, 45, 90));
    ASSERT_TRUE(opened.session->replaceGvrTexture(0U, replacementPng, {}, true).ok());
    ASSERT_TRUE(opened.session->dirty());
    const auto stagedBlenderPath = temp.path / "staged.json";
    ASSERT_TRUE(opened.session->exportBlenderIrJson(stagedBlenderPath, context).ok());
    EXPECT_TRUE(opened.session->dirty());
    const auto stagedJson = readText(stagedBlenderPath);
    EXPECT_NE(stagedJson.find("\"textureName\":\"first\""), std::string::npos);
    EXPECT_NE(stagedJson.find("\"width\":8"), std::string::npos);
    EXPECT_NE(stagedJson.find("\"height\":8"), std::string::npos);
    EXPECT_TRUE(std::any_of(events.begin(), events.end(), [](const auto& event) {
        return event.level == spice::mix::EventLevel::Progress;
    }));
    EXPECT_TRUE(std::any_of(events.begin(), events.end(), [](const auto& event) {
        return event.level == spice::mix::EventLevel::Info;
    }));
}

TEST(SpiceMixDocuments, MldExportsHonorCancellationAndRemoveFailedTemporaryFiles) {
    TempDirectory temp{};
    const auto source = temp.path / "source.mld";
    writeFile(source, makeTwoTextureMld());
    auto opened = spice::mix::MldDocumentSession::open(source);
    ASSERT_TRUE(opened.result.ok()) << opened.result.message;

    std::stop_source stop{};
    stop.request_stop();
    const auto cancelledPath = temp.path / "cancelled.json";
    const auto cancelled = opened.session->exportBlenderIrJson(cancelledPath,
        spice::mix::DocumentContext{ .stopToken = stop.get_token() });
    EXPECT_EQ(cancelled.status, spice::mix::OperationStatus::Cancelled);
    EXPECT_FALSE(std::filesystem::exists(cancelledPath));

    const auto invalidTarget = temp.path / "existing_directory";
    std::filesystem::create_directories(invalidTarget);
    const auto failed = opened.session->exportEntryListJson(invalidTarget);
    EXPECT_EQ(failed.status, spice::mix::OperationStatus::Failure);
    for (const auto& entry : std::filesystem::directory_iterator(temp.path)) {
        EXPECT_EQ(entry.path().filename().string().find(".spicemix-"), std::string::npos);
    }
}

TEST(SpiceMixDocuments, CancelledOpenDoesNotCreateSession) {
    std::stop_source stop{};
    stop.request_stop();
    const auto opened = spice::mix::GvrDocumentSession::open("missing.gvr",
        spice::mix::DocumentContext{ .stopToken = stop.get_token() });
    EXPECT_FALSE(opened.session);
    EXPECT_EQ(opened.result.status, spice::mix::OperationStatus::Cancelled);
}

TEST(SpiceMixDocuments, DocumentOperationsDeliverStructuredEvents) {
    TempDirectory temp{};
    const auto png = temp.path / "source.png";
    spice::gvm::image::writePngRgba8(png, image(4, 4, 20, 30));
    std::vector<spice::mix::OperationEvent> events{};
    const auto created = spice::mix::GvrDocumentSession::createFromPng(png,
        spice::mix::DocumentContext{ .report = [&events](const auto& event) { events.push_back(event); } });
    ASSERT_TRUE(created.result.ok());
    EXPECT_TRUE(std::any_of(events.begin(), events.end(), [](const auto& event) {
        return event.level == spice::mix::EventLevel::Progress;
    }));
    EXPECT_TRUE(std::any_of(events.begin(), events.end(), [](const auto& event) {
        return event.level == spice::mix::EventLevel::Info;
    }));
}

TEST(SpiceMixDocuments, EctSessionProjectsFlatTablesAndAllOrderedSlots) {
    TempDirectory temp{};
    spice::ect::EctFlatContent content{};
    content.tables.push_back(ectTable(0x12U, 0x30U, 0x17U, 0x10U));
    content.tables.push_back(ectTable(0x22U, 0x40U, 0x27U, 0x20U));
    const spice::ect::EctFile file{ spice::ect::EctContent{ content } };
    const auto written = spice::ect::EctFileWriter{}.write(
        file, spice::ect::EctTargetPlatform::Dreamcast);
    ASSERT_TRUE(written.ok());
    const auto path = temp.path / "sample.ECT";
    writeFile(path, written.bytes);

    std::vector<spice::mix::OperationEvent> events{};
    const auto opened = spice::mix::EctDocumentSession::open(path,
        spice::mix::DocumentContext{ .report = [&events](const auto& event) { events.push_back(event); } });
    ASSERT_TRUE(opened.result.ok()) << opened.result.message;
    ASSERT_TRUE(opened.session);

    const auto overview = opened.session->overview();
    EXPECT_EQ(overview.sourcePath, path);
    EXPECT_EQ(overview.layout, "Flat encounter tables");
    EXPECT_EQ(overview.platform, "Dreamcast");
    EXPECT_EQ(overview.endian, "Little endian");
    EXPECT_FALSE(overview.sourceWasAklz);
    EXPECT_EQ(overview.sourceSize, 2U * 0x84U);
    EXPECT_EQ(overview.decodedSize, overview.sourceSize);
    EXPECT_EQ(overview.containerEntryCount, 0U);
    EXPECT_EQ(overview.tableCount, 2U);
    EXPECT_EQ(overview.encounterSlotCount, 64U);
    EXPECT_EQ(overview.nonzeroEncounterSlotCount, 4U);
    EXPECT_TRUE(opened.session->containerEntries().empty());
    ASSERT_EQ(opened.session->tables().size(), 2U);
    ASSERT_TRUE(opened.session->table(0U).has_value());
    const auto table = *opened.session->table(0U);
    EXPECT_EQ(table.summary.stage, 0x12U);
    EXPECT_EQ(table.summary.overallEncounterRate, 0x30U);
    EXPECT_EQ(table.summary.nonzeroEncounterSlotCount, 2U);
    EXPECT_EQ(table.summary.encounterRateSum, 0x23U);
    ASSERT_EQ(table.encounters.size(), 32U);
    EXPECT_EQ(table.encounters[0].encounterId, 0x17U);
    EXPECT_EQ(table.encounters[1].encounterId, 0U);
    EXPECT_FALSE(table.encounters[1].nonzero);
    EXPECT_EQ(table.encounters[2].encounterId, 0x18U);
    EXPECT_EQ(opened.session->sourcePaths(), (std::vector<std::filesystem::path>{ path }));
    EXPECT_TRUE(std::any_of(events.begin(), events.end(), [](const auto& event) {
        return event.level == spice::mix::EventLevel::Progress;
    }));
    EXPECT_TRUE(std::any_of(events.begin(), events.end(), [](const auto& event) {
        return event.level == spice::mix::EventLevel::Info;
    }));
}

TEST(SpiceMixDocuments, EctSessionProjectsA099HierarchyAndGameCubeEnvelope) {
    TempDirectory temp{};
    spice::ect::EctOverworldEntry entry{};
    entry.title = "dam_test";
    for (std::size_t index = 0; index < entry.tables.size(); ++index) {
        entry.tables[index] = ectTable(static_cast<std::uint16_t>(0x30U + index),
            static_cast<std::uint16_t>(0x40U + index),
            static_cast<std::uint16_t>(0x50U + index), 5U);
    }
    spice::ect::EctOverworldContent indexed{};
    indexed.entries.push_back(entry);
    const spice::ect::EctFile indexedFile{ spice::ect::EctContent{ indexed } };
    const auto indexedBytes = spice::ect::EctFileWriter{}.write(
        indexedFile, spice::ect::EctTargetPlatform::Dreamcast);
    ASSERT_TRUE(indexedBytes.ok());
    const auto indexedPath = temp.path / "a099a.ect";
    writeFile(indexedPath, indexedBytes.bytes);

    const auto openedIndexed = spice::mix::EctDocumentSession::open(indexedPath);
    ASSERT_TRUE(openedIndexed.result.ok()) << openedIndexed.result.message;
    ASSERT_TRUE(openedIndexed.session);
    const auto overview = openedIndexed.session->overview();
    EXPECT_EQ(overview.layout, "A099 indexed overworld");
    EXPECT_EQ(overview.containerEntryCount, 1U);
    EXPECT_EQ(overview.tableCount, 8U);
    const auto entries = openedIndexed.session->containerEntries();
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries[0].title, "dam_test");
    EXPECT_EQ(entries[0].tableIndexes,
        (std::vector<std::size_t>{ 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U }));
    ASSERT_TRUE(openedIndexed.session->table(7U).has_value());
    EXPECT_EQ(openedIndexed.session->table(7U)->summary.tableIndexWithinEntry, 7U);
    EXPECT_EQ(openedIndexed.session->table(7U)->summary.containerTitle, "dam_test");

    spice::ect::EctFlatContent flat{};
    flat.tables.push_back(ectTable(1U, 2U, 3U, 4U));
    const spice::ect::EctFile flatFile{ spice::ect::EctContent{ flat } };
    const auto gameCubeBytes = spice::ect::EctFileWriter{}.write(
        flatFile, spice::ect::EctTargetPlatform::GameCube);
    ASSERT_TRUE(gameCubeBytes.ok());
    const auto gameCubePath = temp.path / "gamecube.ect";
    writeFile(gameCubePath, gameCubeBytes.bytes);
    const auto openedGameCube = spice::mix::EctDocumentSession::open(gameCubePath);
    ASSERT_TRUE(openedGameCube.result.ok()) << openedGameCube.result.message;
    EXPECT_EQ(openedGameCube.session->overview().platform, "GameCube");
    EXPECT_EQ(openedGameCube.session->overview().endian, "Big endian");
    EXPECT_TRUE(openedGameCube.session->overview().sourceWasAklz);
    EXPECT_EQ(openedGameCube.session->overview().decodedSize, 0x84U);
}

TEST(SpiceMixDocuments, EctSessionHonorsCancellationBeforeReading) {
    std::stop_source stop{};
    stop.request_stop();
    const auto opened = spice::mix::EctDocumentSession::open("missing.ect",
        spice::mix::DocumentContext{ .stopToken = stop.get_token() });
    EXPECT_FALSE(opened.session);
    EXPECT_EQ(opened.result.status, spice::mix::OperationStatus::Cancelled);
}

TEST(SpiceMixDocuments, SstSmlSessionResolvesPairAndProjectsNestedInspection) {
    TempDirectory temp{};
    const auto sml = temp.path / "battle.SML";
    const auto sst = temp.path / "battle.sSt";
    writeFile(sml, makeSmlPairMember(makeTwoTextureMld()));
    writeFile(sst, makeSstPairMember());

    std::vector<spice::mix::OperationEvent> events{};
    const auto opened = spice::mix::SstSmlDocumentSession::open(sst,
        spice::mix::DocumentContext{ .report = [&events](const auto& event) { events.push_back(event); } });
    ASSERT_TRUE(opened.result.ok()) << opened.result.message;
    ASSERT_TRUE(opened.session);

    const auto overview = opened.session->overview();
    EXPECT_EQ(overview.stem, "battle");
    EXPECT_EQ(overview.recordCount, 1U);
    EXPECT_TRUE(overview.recordCountsAgree);
    EXPECT_EQ(overview.smlEndian, "Big endian");
    EXPECT_EQ(overview.sstEndian, "Big endian");
    EXPECT_EQ(overview.platformContext, "Big-endian convention");
    EXPECT_EQ(overview.embeddedMldParsedCount, 1U);
    EXPECT_EQ(overview.embeddedMldFailedCount, 0U);
    ASSERT_EQ(opened.session->sourcePaths().size(), 2U);

    const auto records = opened.session->records();
    ASSERT_EQ(records.size(), 1U);
    EXPECT_TRUE(records.front().embeddedMldParsed);
    EXPECT_EQ(records.front().embeddedMldEntryCount, 1U);
    EXPECT_EQ(records.front().embeddedMldTextureCount, 2U);
    ASSERT_TRUE(opened.session->embeddedMldOverview(0U).has_value());
    EXPECT_EQ(opened.session->embeddedMldEntries(0U).size(), 1U);
    ASSERT_EQ(opened.session->embeddedMldEntryDetails(0U).size(), 1U);
    ASSERT_EQ(opened.session->embeddedMldTextures(0U).size(), 2U);
    const auto preview = opened.session->embeddedMldTexturePreview(0U, 0U);
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->width, 4U);
    EXPECT_EQ(preview->height, 4U);

    const auto commands = opened.session->commands(0U);
    ASSERT_EQ(commands.size(), 1U);
    EXPECT_EQ(commands.front().type, 3);
    EXPECT_EQ(commands.front().typeLabel, "Texture-coordinate adjustment");
    ASSERT_TRUE(opened.session->commandDetail(0U, 0U).has_value());
    EXPECT_FALSE(opened.session->runtimeContext().fields.empty());
    EXPECT_TRUE(std::any_of(events.begin(), events.end(), [](const auto& event) {
        return event.level == spice::mix::EventLevel::Progress;
    }));

    std::size_t regularFileCount = 0U;
    for (const auto& entry : std::filesystem::directory_iterator(temp.path)) {
        if (entry.is_regular_file()) ++regularFileCount;
    }
    EXPECT_EQ(regularFileCount, 2U);
}

TEST(SpiceMixDocuments, MldEntryDetailsExposeNeutralValuesStableIdsAndTextureNames) {
    TempDirectory temp{};
    auto bytes = makeTwoTextureMld();
    addDetailedEntryLists(bytes, false);
    const auto source = temp.path / "detailed.mld";
    writeFile(source, bytes);

    const auto opened = spice::mix::MldDocumentSession::open(source);
    ASSERT_TRUE(opened.result.ok()) << opened.result.message;
    ASSERT_TRUE(opened.session);
    const auto details = opened.session->entryDetails();
    ASSERT_EQ(details.size(), 1U);
    const auto& detail = details.front();
    EXPECT_EQ(detail.summary.entryId, 7U);
    EXPECT_EQ(detail.groundLinks.values, (std::vector<std::uint32_t>{ 0U, 7U }));
    EXPECT_EQ(detail.paramList2.values, (std::vector<std::uint32_t>{ 8U, 9U }));
    EXPECT_EQ(detail.functionParameters.values, (std::vector<std::uint32_t>{ 10U, 0U, 12U }));
    EXPECT_EQ(detail.objectIds.values, (std::vector<std::uint32_t>{ 0U, 1U }));
    EXPECT_EQ(detail.groundIds.values, (std::vector<std::uint32_t>{ 1U, 0U }));
    EXPECT_EQ(detail.motionIds.values, (std::vector<std::uint32_t>{ 0U, 1U, 0U }));
    EXPECT_EQ(detail.textureNames.values,
        (std::vector<std::string>{ "detail_first", "detail_second" }));
    EXPECT_FALSE(opened.session->dirty());

    writeU32Be(bytes, 0x28U, 0xFFFFFFF0U);
    const auto malformed = temp.path / "invalid-list.mld";
    writeFile(malformed, bytes);
    const auto malformedOpen = spice::mix::MldDocumentSession::open(malformed);
    ASSERT_TRUE(malformedOpen.result.ok()) << malformedOpen.result.message;
    const auto malformedDetails = malformedOpen.session->entryDetails();
    ASSERT_EQ(malformedDetails.size(), 1U);
    EXPECT_TRUE(malformedDetails.front().groundLinks.values.empty());
}

TEST(SpiceMixDocuments, MldEntryDetailsRetainEquivalentListsAcrossPlatforms) {
    TempDirectory temp{};
    auto gameCubeBytes = makeTwoTextureMld();
    addDetailedEntryLists(gameCubeBytes, false);
    auto dreamcastBytes = makeDreamcastTextureMld({ encodePvr(4U, 71U, 25U) });
    addDetailedEntryLists(dreamcastBytes, true);
    const auto gameCubePath = temp.path / "details-gc.mld";
    const auto dreamcastPath = temp.path / "details-dc.mld";
    writeFile(gameCubePath, gameCubeBytes);
    writeFile(dreamcastPath, dreamcastBytes);

    const auto gameCube = spice::mix::MldDocumentSession::open(gameCubePath);
    const auto dreamcast = spice::mix::MldDocumentSession::open(dreamcastPath);
    ASSERT_TRUE(gameCube.result.ok()) << gameCube.result.message;
    ASSERT_TRUE(dreamcast.result.ok()) << dreamcast.result.message;
    const auto gameCubeDetails = gameCube.session->entryDetails();
    const auto dreamcastDetails = dreamcast.session->entryDetails();
    ASSERT_EQ(gameCubeDetails.size(), 1U);
    ASSERT_EQ(dreamcastDetails.size(), 1U);
    EXPECT_EQ(gameCubeDetails.front().groundLinks.values, dreamcastDetails.front().groundLinks.values);
    EXPECT_EQ(gameCubeDetails.front().paramList2.values, dreamcastDetails.front().paramList2.values);
    EXPECT_EQ(gameCubeDetails.front().functionParameters.values, dreamcastDetails.front().functionParameters.values);
    EXPECT_EQ(gameCubeDetails.front().objectIds.values, dreamcastDetails.front().objectIds.values);
    EXPECT_EQ(gameCubeDetails.front().groundIds.values, dreamcastDetails.front().groundIds.values);
    EXPECT_EQ(gameCubeDetails.front().motionIds.values, dreamcastDetails.front().motionIds.values);
    EXPECT_EQ(gameCubeDetails.front().textureNames.values, dreamcastDetails.front().textureNames.values);
}

TEST(SpiceMixDocuments, SstSmlSessionRequiresAnUnambiguousCompanion) {
    TempDirectory temp{};
    const auto sml = temp.path / "orphan.sml";
    writeFile(sml, makeSmlPairMember(makeTwoTextureMld()));

    const auto opened = spice::mix::SstSmlDocumentSession::open(sml);
    EXPECT_FALSE(opened.session);
    EXPECT_EQ(opened.result.status, spice::mix::OperationStatus::Failure);
    EXPECT_NE(opened.result.message.find("companion is missing"), std::string::npos);
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(temp.path),
        std::filesystem::directory_iterator{}), 1);
}

TEST(SpiceMixDocuments, SstSmlSessionKeepsEmbeddedMldFailureNonFatal) {
    TempDirectory temp{};
    const auto sml = temp.path / "partial.sml";
    const auto sst = temp.path / "partial.sst";
    const std::vector<std::uint8_t> invalidMld{ 0xDEU, 0xADU, 0xBEU, 0xEFU };
    writeFile(sml, makeSmlPairMember(invalidMld));
    writeFile(sst, makeSstPairMember());

    const auto opened = spice::mix::SstSmlDocumentSession::open(sml);
    ASSERT_TRUE(opened.result.ok()) << opened.result.message;
    ASSERT_TRUE(opened.session);
    const auto records = opened.session->records();
    ASSERT_EQ(records.size(), 1U);
    EXPECT_FALSE(records.front().embeddedMldParsed);
    EXPECT_EQ(records.front().embeddedMldParseStatus, "Opaque");
    EXPECT_EQ(opened.session->overview().embeddedMldFailedCount, 1U);
    EXPECT_FALSE(opened.session->diagnostics().empty());
}

TEST(SpiceMixDocuments, SstSmlSessionHonorsCancellationBeforeReading) {
    std::stop_source stop{};
    stop.request_stop();
    const auto opened = spice::mix::SstSmlDocumentSession::open("missing.sml",
        spice::mix::DocumentContext{ .stopToken = stop.get_token() });
    EXPECT_FALSE(opened.session);
    EXPECT_EQ(opened.result.status, spice::mix::OperationStatus::Cancelled);
}
