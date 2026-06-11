#include "../Compression/Aklz.h"
#include "../SpiceGvm/SpiceGvm.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

namespace {

spice::gvm::model::RgbaImage makeTestImage() {
    spice::gvm::model::RgbaImage image{};
    image.width = 4;
    image.height = 4;
    image.rgba8.resize(4U * 4U * 4U);
    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4U;
            image.rgba8[offset + 0U] = static_cast<std::uint8_t>(x * 40U);
            image.rgba8[offset + 1U] = static_cast<std::uint8_t>(y * 50U);
            image.rgba8[offset + 2U] = static_cast<std::uint8_t>(x * 10U + y * 20U);
            image.rgba8[offset + 3U] = static_cast<std::uint8_t>(255U - x * 5U);
        }
    }
    return image;
}

spice::gvm::model::RgbaImage makeEncoderImage(const std::uint32_t width = 8U, const std::uint32_t height = 8U) {
    spice::gvm::model::RgbaImage image{};
    image.width = width;
    image.height = height;
    image.rgba8.resize(static_cast<std::size_t>(width) * height * 4U);
    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4U;
            image.rgba8[offset + 0U] = static_cast<std::uint8_t>((x * 37U + y * 11U) & 0xFFU);
            image.rgba8[offset + 1U] = static_cast<std::uint8_t>((x * 13U + y * 29U) & 0xFFU);
            image.rgba8[offset + 2U] = static_cast<std::uint8_t>((x * 19U + y * 7U) & 0xFFU);
            image.rgba8[offset + 3U] = 255U;
        }
    }
    return image;
}

spice::gvm::model::RgbaImage makeCi4Image() {
    spice::gvm::model::RgbaImage image{};
    image.width = 8;
    image.height = 8;
    image.rgba8.reserve(8U * 8U * 4U);
    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const auto v = static_cast<std::uint8_t>(((y * 8U + x) % 16U) * 17U);
            image.rgba8.push_back(v);
            image.rgba8.push_back(static_cast<std::uint8_t>(255U - v));
            image.rgba8.push_back(static_cast<std::uint8_t>((v / 17U) * 8U));
            image.rgba8.push_back(255U);
        }
    }
    return image;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer{};
    buffer << in.rdbuf();
    return buffer.str();
}

std::filesystem::path testOutDir(const char* name) {
    auto dir = std::filesystem::temp_directory_path() / "spice_gvr_image_ir_tests" / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void writeSidecar(const std::filesystem::path& jsonPath,
    const std::string& imageFile,
    const std::uint32_t width,
    const std::uint32_t height,
    const std::string& importTextureFormat,
    const bool hasMipmaps,
    const std::string& importPaletteFormat = "") {
    std::ofstream out(jsonPath, std::ios::binary);
    out << "{\n"
        << "  \"schemaVersion\": 1,\n"
        << "  \"imageFile\": \"" << imageFile << "\",\n"
        << "  \"width\": " << width << ",\n"
        << "  \"height\": " << height << ",\n"
        << "  \"pixelFormat\": \"rgba8\",\n"
        << "  \"sourceWasAklz\": false,\n"
        << "  \"hasGlobalIndex\": true,\n"
        << "  \"globalIndex\": 11,\n"
        << "  \"hasMipmaps\": " << (hasMipmaps ? "true" : "false") << ",\n"
        << "  \"importTextureFormat\": \"" << importTextureFormat << "\"";
    if (!importPaletteFormat.empty()) {
        out << ",\n"
            << "  \"importPaletteFormat\": \"" << importPaletteFormat << "\"";
    }
    out << "\n}\n";
}

} // namespace

TEST(GvrImageIr, PngCodecRoundTripsRgba8) {
    const auto dir = testOutDir("png_roundtrip");
    const auto image = makeTestImage();
    const auto path = dir / "texture.png";

    spice::gvm::image::writePngRgba8(path, image);
    const auto decoded = spice::gvm::image::readPngRgba8(path);

    EXPECT_EQ(decoded.width, image.width);
    EXPECT_EQ(decoded.height, image.height);
    EXPECT_EQ(decoded.rgba8, image.rgba8);
}

TEST(GvrImageIr, Rgba8GvrEncoderReparsesAndDecodes) {
    const auto image = makeTestImage();
    spice::gvm::encoding::EncodeOptions options{};
    options.textureFormat = spice::gvm::model::TextureFormat::RGBA8;
    options.hasGlobalIndex = true;
    options.globalIndex = 7;

    const auto bytes = spice::gvm::encoding::encodeRgba8Gvr(image, options);
    const auto parsed = spice::gvm::parsing::parseGvrTexture(std::span<const std::uint8_t>(bytes.data(), bytes.size()), 0);

    ASSERT_TRUE(parsed.decodedBaseLevel.has_value());
    EXPECT_TRUE(parsed.hasGlobalIndex);
    EXPECT_EQ(parsed.globalIndex, 7U);
    EXPECT_EQ(parsed.textureFormat, spice::gvm::model::TextureFormat::RGBA8);
    EXPECT_EQ(parsed.decodedBaseLevel->width, image.width);
    EXPECT_EQ(parsed.decodedBaseLevel->height, image.height);
    EXPECT_EQ(parsed.decodedBaseLevel->rgba8, image.rgba8);
}

TEST(GvrImageIr, Rgba8GvrEncoderSupportsMipFlagAndExtraPayload) {
    const auto image = makeEncoderImage(8U, 8U);
    spice::gvm::encoding::EncodeOptions baseOptions{};
    baseOptions.textureFormat = spice::gvm::model::TextureFormat::RGBA8;
    const auto baseBytes = spice::gvm::encoding::encodeGvr(image, baseOptions);

    auto mipOptions = baseOptions;
    mipOptions.generateMipmaps = true;
    const auto mipBytes = spice::gvm::encoding::encodeGvr(image, mipOptions);
    const auto parsed = spice::gvm::parsing::parseGvrTexture(std::span<const std::uint8_t>(mipBytes.data(), mipBytes.size()), 0);

    EXPECT_GT(mipBytes.size(), baseBytes.size());
    EXPECT_EQ(parsed.textureFormat, spice::gvm::model::TextureFormat::RGBA8);
    EXPECT_EQ(parsed.rawFlags, 0x01U);
    EXPECT_TRUE(parsed.hasMipmaps);
    ASSERT_TRUE(parsed.decodedBaseLevel.has_value());
    EXPECT_EQ(parsed.decodedBaseLevel->rgba8, image.rgba8);
}

TEST(GvrImageIr, Rgb5A3GvrEncoderReparsesAndDecodesBaseLevel) {
    const auto image = makeEncoderImage();
    spice::gvm::encoding::EncodeOptions options{};
    options.textureFormat = spice::gvm::model::TextureFormat::RGB5A3;

    const auto bytes = spice::gvm::encoding::encodeGvr(image, options);
    const auto parsed = spice::gvm::parsing::parseGvrTexture(std::span<const std::uint8_t>(bytes.data(), bytes.size()), 0);

    ASSERT_TRUE(parsed.decodedBaseLevel.has_value());
    EXPECT_EQ(parsed.textureFormat, spice::gvm::model::TextureFormat::RGB5A3);
    EXPECT_EQ(parsed.rawFlags, 0x00U);
    EXPECT_FALSE(parsed.hasMipmaps);
    EXPECT_EQ(parsed.imageDataSize, 128U);
    EXPECT_EQ(parsed.decodedBaseLevel->width, image.width);
    EXPECT_EQ(parsed.decodedBaseLevel->height, image.height);
}

TEST(GvrImageIr, Rgb5A3GvrEncoderSupportsMipFlagAndExtraPayload) {
    const auto image = makeEncoderImage(8U, 8U);
    spice::gvm::encoding::EncodeOptions baseOptions{};
    baseOptions.textureFormat = spice::gvm::model::TextureFormat::RGB5A3;
    const auto baseBytes = spice::gvm::encoding::encodeGvr(image, baseOptions);

    auto mipOptions = baseOptions;
    mipOptions.generateMipmaps = true;
    const auto mipBytes = spice::gvm::encoding::encodeGvr(image, mipOptions);
    const auto parsed = spice::gvm::parsing::parseGvrTexture(std::span<const std::uint8_t>(mipBytes.data(), mipBytes.size()), 0);

    EXPECT_GT(mipBytes.size(), baseBytes.size());
    EXPECT_EQ(parsed.textureFormat, spice::gvm::model::TextureFormat::RGB5A3);
    EXPECT_EQ(parsed.rawFlags, 0x01U);
    EXPECT_TRUE(parsed.hasMipmaps);
}

TEST(GvrImageIr, Rgb5A3GvrEncoderPreservesTranslucentAlphaMode) {
    auto image = makeEncoderImage(4U, 4U);
    image.rgba8[3U] = 96U;
    spice::gvm::encoding::EncodeOptions options{};
    options.textureFormat = spice::gvm::model::TextureFormat::RGB5A3;

    const auto bytes = spice::gvm::encoding::encodeGvr(image, options);
    const auto parsed = spice::gvm::parsing::parseGvrTexture(std::span<const std::uint8_t>(bytes.data(), bytes.size()), 0);

    ASSERT_TRUE(parsed.decodedBaseLevel.has_value());
    ASSERT_GE(parsed.decodedBaseLevel->rgba8.size(), 4U);
    EXPECT_LT(parsed.decodedBaseLevel->rgba8[3U], 255U);
}

TEST(GvrImageIr, CmprGvrEncoderReparsesAndDecodesBaseLevel) {
    const auto image = makeEncoderImage();
    spice::gvm::encoding::EncodeOptions options{};
    options.textureFormat = spice::gvm::model::TextureFormat::CMPR;

    const auto bytes = spice::gvm::encoding::encodeGvr(image, options);
    const auto parsed = spice::gvm::parsing::parseGvrTexture(std::span<const std::uint8_t>(bytes.data(), bytes.size()), 0);

    ASSERT_TRUE(parsed.decodedBaseLevel.has_value());
    EXPECT_EQ(parsed.textureFormat, spice::gvm::model::TextureFormat::CMPR);
    EXPECT_EQ(parsed.rawFlags, 0x00U);
    EXPECT_FALSE(parsed.hasMipmaps);
    EXPECT_EQ(parsed.imageDataSize, 32U);
    EXPECT_EQ(parsed.decodedBaseLevel->width, image.width);
    EXPECT_EQ(parsed.decodedBaseLevel->height, image.height);
}

TEST(GvrImageIr, CmprGvrEncoderSupportsMipFlagAndExtraPayload) {
    const auto image = makeEncoderImage(16U, 16U);
    spice::gvm::encoding::EncodeOptions baseOptions{};
    baseOptions.textureFormat = spice::gvm::model::TextureFormat::CMPR;
    const auto baseBytes = spice::gvm::encoding::encodeGvr(image, baseOptions);

    auto mipOptions = baseOptions;
    mipOptions.generateMipmaps = true;
    const auto mipBytes = spice::gvm::encoding::encodeGvr(image, mipOptions);
    const auto parsed = spice::gvm::parsing::parseGvrTexture(std::span<const std::uint8_t>(mipBytes.data(), mipBytes.size()), 0);

    EXPECT_GT(mipBytes.size(), baseBytes.size());
    EXPECT_EQ(parsed.textureFormat, spice::gvm::model::TextureFormat::CMPR);
    EXPECT_EQ(parsed.rawFlags, 0x01U);
    EXPECT_TRUE(parsed.hasMipmaps);
}

TEST(GvrImageIr, CmprGvrEncoderUsesTransparentMode) {
    auto image = makeEncoderImage();
    image.rgba8[3U] = 0U;
    spice::gvm::encoding::EncodeOptions options{};
    options.textureFormat = spice::gvm::model::TextureFormat::CMPR;

    const auto bytes = spice::gvm::encoding::encodeGvr(image, options);
    const auto parsed = spice::gvm::parsing::parseGvrTexture(std::span<const std::uint8_t>(bytes.data(), bytes.size()), 0);

    ASSERT_TRUE(parsed.decodedBaseLevel.has_value());
    ASSERT_GE(parsed.decodedBaseLevel->rgba8.size(), 4U);
    EXPECT_EQ(parsed.decodedBaseLevel->rgba8[3U], 0U);
}

TEST(GvrImageIr, Ci4Rgb5A3GvrEncoderReparsesAndDecodes) {
    const auto image = makeCi4Image();
    spice::gvm::encoding::EncodeOptions options{};
    options.textureFormat = spice::gvm::model::TextureFormat::CI4;
    options.paletteFormat = spice::gvm::model::PaletteFormat::RGB5A3;

    const auto bytes = spice::gvm::encoding::encodeGvr(image, options);
    const auto parsed = spice::gvm::parsing::parseGvrTexture(std::span<const std::uint8_t>(bytes.data(), bytes.size()), 0);

    ASSERT_TRUE(parsed.decodedBaseLevel.has_value());
    EXPECT_EQ(parsed.textureFormat, spice::gvm::model::TextureFormat::CI4);
    EXPECT_EQ(parsed.paletteFormat, spice::gvm::model::PaletteFormat::RGB5A3);
    EXPECT_EQ(parsed.rawFlags, 0x28U);
    EXPECT_TRUE(parsed.hasInternalPalette);
    EXPECT_EQ(parsed.paletteData.size(), 32U);
    EXPECT_EQ(parsed.imageDataSize, 32U);
    EXPECT_EQ(parsed.decodedBaseLevel->width, image.width);
    EXPECT_EQ(parsed.decodedBaseLevel->height, image.height);
}

TEST(GvrImageIr, Ci4Rgb5A3GvrEncoderIsDeterministicAndSupportsMipFlag) {
    const auto image = makeEncoderImage();
    spice::gvm::encoding::EncodeOptions options{};
    options.textureFormat = spice::gvm::model::TextureFormat::CI4;
    options.paletteFormat = spice::gvm::model::PaletteFormat::RGB5A3;
    options.generateMipmaps = true;

    const auto first = spice::gvm::encoding::encodeGvr(image, options);
    const auto second = spice::gvm::encoding::encodeGvr(image, options);
    const auto parsed = spice::gvm::parsing::parseGvrTexture(std::span<const std::uint8_t>(first.data(), first.size()), 0);

    EXPECT_EQ(first, second);
    EXPECT_EQ(parsed.textureFormat, spice::gvm::model::TextureFormat::CI4);
    EXPECT_EQ(parsed.rawFlags, 0x29U);
    EXPECT_TRUE(parsed.hasMipmaps);
}

TEST(GvrImageIr, ExportImportRoundTripsThroughPngSidecar) {
    const auto dir = testOutDir("export_import");
    const auto image = makeTestImage();
    spice::gvm::encoding::EncodeOptions options{};
    options.textureFormat = spice::gvm::model::TextureFormat::RGBA8;
    options.hasGlobalIndex = true;
    options.globalIndex = 99;
    const auto sourceBytes = spice::gvm::encoding::encodeRgba8Gvr(image, options);

    const auto exported = spice::gvm::ir::exportGvrImageIr(
        std::span<const std::uint8_t>(sourceBytes.data(), sourceBytes.size()),
        dir / "sample.gvr",
        dir);
    const auto imported = spice::gvm::ir::importGvrImageIr(exported.jsonPath, spice::gvm::ir::AklzPolicy::Raw);
    const auto reparsed = spice::gvm::parsing::parseGvrTexture(
        std::span<const std::uint8_t>(imported.bytes.data(), imported.bytes.size()),
        0);

    ASSERT_TRUE(reparsed.decodedBaseLevel.has_value());
    EXPECT_TRUE(reparsed.hasGlobalIndex);
    EXPECT_EQ(reparsed.globalIndex, 99U);
    EXPECT_EQ(reparsed.decodedBaseLevel->rgba8, image.rgba8);
}

TEST(GvrImageIr, ExportSidecarDefaultsToSourceImportFormatForCmprAndCi4) {
    const auto dir = testOutDir("source_format_sidecar");
    const auto image = makeEncoderImage();
    spice::gvm::encoding::EncodeOptions cmprOptions{};
    cmprOptions.textureFormat = spice::gvm::model::TextureFormat::CMPR;
    const auto cmprBytes = spice::gvm::encoding::encodeGvr(image, cmprOptions);

    const auto cmprExported = spice::gvm::ir::exportGvrImageIr(
        std::span<const std::uint8_t>(cmprBytes.data(), cmprBytes.size()),
        dir / "cmpr.gvr",
        dir);
    EXPECT_NE(readText(cmprExported.jsonPath).find("\"importTextureFormat\": \"CMPR\""), std::string::npos);

    spice::gvm::encoding::EncodeOptions ci4Options{};
    ci4Options.textureFormat = spice::gvm::model::TextureFormat::CI4;
    ci4Options.paletteFormat = spice::gvm::model::PaletteFormat::RGB5A3;
    const auto ci4Bytes = spice::gvm::encoding::encodeGvr(image, ci4Options);
    const auto ci4Exported = spice::gvm::ir::exportGvrImageIr(
        std::span<const std::uint8_t>(ci4Bytes.data(), ci4Bytes.size()),
        dir / "ci4.gvr",
        dir);
    const auto ci4Json = readText(ci4Exported.jsonPath);
    EXPECT_NE(ci4Json.find("\"importTextureFormat\": \"CI4\""), std::string::npos);
    EXPECT_NE(ci4Json.find("\"importPaletteFormat\": \"RGB5A3\""), std::string::npos);

    spice::gvm::encoding::EncodeOptions rgb5a3Options{};
    rgb5a3Options.textureFormat = spice::gvm::model::TextureFormat::RGB5A3;
    const auto rgb5a3Bytes = spice::gvm::encoding::encodeGvr(image, rgb5a3Options);
    const auto rgb5a3Exported = spice::gvm::ir::exportGvrImageIr(
        std::span<const std::uint8_t>(rgb5a3Bytes.data(), rgb5a3Bytes.size()),
        dir / "rgb5a3.gvr",
        dir);
    EXPECT_NE(readText(rgb5a3Exported.jsonPath).find("\"importTextureFormat\": \"RGB5A3\""), std::string::npos);
}

TEST(GvrImageIr, ImportSidecarCanSelectCmprAndCi4Outputs) {
    const auto dir = testOutDir("format_select_import");
    const auto image = makeEncoderImage();
    const auto pngPath = dir / "texture.png";
    spice::gvm::image::writePngRgba8(pngPath, image);

    const auto cmprJson = dir / "cmpr.gvr.json";
    writeSidecar(cmprJson, "texture.png", image.width, image.height, "CMPR", true);
    const auto cmprImported = spice::gvm::ir::importGvrImageIr(cmprJson, spice::gvm::ir::AklzPolicy::Raw);
    const auto cmprParsed = spice::gvm::parsing::parseGvrTexture(
        std::span<const std::uint8_t>(cmprImported.bytes.data(), cmprImported.bytes.size()),
        0);
    EXPECT_EQ(cmprParsed.textureFormat, spice::gvm::model::TextureFormat::CMPR);
    EXPECT_TRUE(cmprParsed.hasMipmaps);
    EXPECT_TRUE(cmprParsed.hasGlobalIndex);
    EXPECT_EQ(cmprParsed.globalIndex, 11U);

    const auto ci4Json = dir / "ci4.gvr.json";
    writeSidecar(ci4Json, "texture.png", image.width, image.height, "CI4", true, "RGB5A3");
    const auto ci4Imported = spice::gvm::ir::importGvrImageIr(ci4Json, spice::gvm::ir::AklzPolicy::Raw);
    const auto ci4Parsed = spice::gvm::parsing::parseGvrTexture(
        std::span<const std::uint8_t>(ci4Imported.bytes.data(), ci4Imported.bytes.size()),
        0);
    EXPECT_EQ(ci4Parsed.textureFormat, spice::gvm::model::TextureFormat::CI4);
    EXPECT_EQ(ci4Parsed.paletteFormat, spice::gvm::model::PaletteFormat::RGB5A3);
    EXPECT_TRUE(ci4Parsed.hasMipmaps);

    const auto rgb5a3Json = dir / "rgb5a3.gvr.json";
    writeSidecar(rgb5a3Json, "texture.png", image.width, image.height, "RGB5A3", true);
    const auto rgb5a3Imported = spice::gvm::ir::importGvrImageIr(rgb5a3Json, spice::gvm::ir::AklzPolicy::Raw);
    const auto rgb5a3Parsed = spice::gvm::parsing::parseGvrTexture(
        std::span<const std::uint8_t>(rgb5a3Imported.bytes.data(), rgb5a3Imported.bytes.size()),
        0);
    EXPECT_EQ(rgb5a3Parsed.textureFormat, spice::gvm::model::TextureFormat::RGB5A3);
    EXPECT_TRUE(rgb5a3Parsed.hasMipmaps);

    const auto rgba8Json = dir / "rgba8_mip.gvr.json";
    writeSidecar(rgba8Json, "texture.png", image.width, image.height, "RGBA8", true);
    const auto rgba8Imported = spice::gvm::ir::importGvrImageIr(rgba8Json, spice::gvm::ir::AklzPolicy::Raw);
    const auto rgba8Parsed = spice::gvm::parsing::parseGvrTexture(
        std::span<const std::uint8_t>(rgba8Imported.bytes.data(), rgba8Imported.bytes.size()),
        0);
    EXPECT_EQ(rgba8Parsed.textureFormat, spice::gvm::model::TextureFormat::RGBA8);
    EXPECT_TRUE(rgba8Parsed.hasMipmaps);
}

TEST(GvrImageIr, LegacySidecarWithoutImportTextureFormatStillImportsBaseRgba8) {
    const auto dir = testOutDir("legacy_sidecar");
    const auto image = makeTestImage();
    const auto pngPath = dir / "legacy.png";
    const auto jsonPath = dir / "legacy.gvr.json";
    spice::gvm::image::writePngRgba8(pngPath, image);

    std::ofstream out(jsonPath, std::ios::binary);
    out << "{\n"
        << "  \"schemaVersion\": 1,\n"
        << "  \"imageFile\": \"legacy.png\",\n"
        << "  \"width\": 4,\n"
        << "  \"height\": 4,\n"
        << "  \"pixelFormat\": \"rgba8\",\n"
        << "  \"sourceWasAklz\": false,\n"
        << "  \"hasGlobalIndex\": false,\n"
        << "  \"globalIndex\": 0,\n"
        << "  \"hasMipmaps\": true\n"
        << "}\n";
    out.close();

    const auto imported = spice::gvm::ir::importGvrImageIr(jsonPath, spice::gvm::ir::AklzPolicy::Raw);
    const auto parsed = spice::gvm::parsing::parseGvrTexture(
        std::span<const std::uint8_t>(imported.bytes.data(), imported.bytes.size()),
        0);

    ASSERT_TRUE(parsed.decodedBaseLevel.has_value());
    EXPECT_EQ(parsed.textureFormat, spice::gvm::model::TextureFormat::RGBA8);
    EXPECT_FALSE(parsed.hasMipmaps);
}

TEST(GvrImageIr, ImportAklzPolicyPreservesOrOverridesWrapper) {
    const auto dir = testOutDir("aklz_policy");
    const auto image = makeTestImage();
    spice::gvm::encoding::EncodeOptions options{};
    options.textureFormat = spice::gvm::model::TextureFormat::RGBA8;
    const auto sourceBytes = spice::gvm::encoding::encodeRgba8Gvr(image, options);
    const auto compressed = spice::compression::aklz::compress(sourceBytes);
    ASSERT_TRUE(compressed.ok());

    const auto exported = spice::gvm::ir::exportGvrImageIr(
        std::span<const std::uint8_t>(compressed.bytes.data(), compressed.bytes.size()),
        dir / "sample.gvr",
        dir);

    const auto preserved = spice::gvm::ir::importGvrImageIr(exported.jsonPath, spice::gvm::ir::AklzPolicy::Preserve);
    EXPECT_TRUE(spice::compression::aklz::isAklz(preserved.bytes));

    const auto raw = spice::gvm::ir::importGvrImageIr(exported.jsonPath, spice::gvm::ir::AklzPolicy::Raw);
    EXPECT_FALSE(spice::compression::aklz::isAklz(raw.bytes));

    const auto forcedCompressed = spice::gvm::ir::importGvrImageIr(exported.jsonPath, spice::gvm::ir::AklzPolicy::Compressed);
    EXPECT_TRUE(spice::compression::aklz::isAklz(forcedCompressed.bytes));
}

TEST(GvrImageIr, ImportRejectsDimensionMismatch) {
    const auto dir = testOutDir("dimension_mismatch");
    const auto image = makeTestImage();
    const auto pngPath = dir / "bad.png";
    const auto jsonPath = dir / "bad.gvr.json";
    spice::gvm::image::writePngRgba8(pngPath, image);

    std::ofstream out(jsonPath, std::ios::binary);
    out << "{\n"
        << "  \"schemaVersion\": 1,\n"
        << "  \"imageFile\": \"bad.png\",\n"
        << "  \"width\": 8,\n"
        << "  \"height\": 4,\n"
        << "  \"pixelFormat\": \"rgba8\",\n"
        << "  \"sourceWasAklz\": false,\n"
        << "  \"hasGlobalIndex\": false,\n"
        << "  \"globalIndex\": 0\n"
        << "}\n";
    out.close();

    EXPECT_THROW((void)spice::gvm::ir::importGvrImageIr(jsonPath), std::runtime_error);
}
