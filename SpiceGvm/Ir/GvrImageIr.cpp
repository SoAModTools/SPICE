#include "GvrImageIr.h"

#include "../Encoding/GvrEncoder.h"
#include "../Image/PngCodec.h"
#include "../Parsing/GvmParser.h"
#include "../../Compression/Aklz.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace spice::gvm::ir {
namespace {

std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to read JSON sidecar: " + path.string());
    }
    std::ostringstream buffer{};
    buffer << in.rdbuf();
    return buffer.str();
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to write JSON sidecar: " + path.string());
    }
    out << text;
}

std::string jsonEscape(const std::string& value) {
    std::string escaped{};
    escaped.reserve(value.size() + 8U);
    for (const char c : value) {
        switch (c) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(c); break;
        }
    }
    return escaped;
}

void appendJsonString(std::ostringstream& out, const std::string& key, const std::string& value, const bool trailingComma = true) {
    out << "  \"" << key << "\": \"" << jsonEscape(value) << "\"";
    if (trailingComma) {
        out << ",";
    }
    out << "\n";
}

void appendJsonBool(std::ostringstream& out, const std::string& key, const bool value, const bool trailingComma = true) {
    out << "  \"" << key << "\": " << (value ? "true" : "false");
    if (trailingComma) {
        out << ",";
    }
    out << "\n";
}

void appendJsonU32(std::ostringstream& out, const std::string& key, const std::uint32_t value, const bool trailingComma = true) {
    out << "  \"" << key << "\": " << value;
    if (trailingComma) {
        out << ",";
    }
    out << "\n";
}

std::string requireString(const std::string& json, const std::string& key) {
    const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match{};
    if (!std::regex_search(json, match, pattern)) {
        throw std::runtime_error("missing JSON string field: " + key);
    }
    return match[1].str();
}

std::optional<std::string> optionalString(const std::string& json, const std::string& key) {
    const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match{};
    if (!std::regex_search(json, match, pattern)) {
        return std::nullopt;
    }
    return match[1].str();
}

std::uint32_t requireU32(const std::string& json, const std::string& key) {
    const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*([0-9]+)");
    std::smatch match{};
    if (!std::regex_search(json, match, pattern)) {
        throw std::runtime_error("missing JSON integer field: " + key);
    }
    return static_cast<std::uint32_t>(std::stoul(match[1].str()));
}

bool requireBool(const std::string& json, const std::string& key) {
    const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*(true|false)");
    std::smatch match{};
    if (!std::regex_search(json, match, pattern)) {
        throw std::runtime_error("missing JSON bool field: " + key);
    }
    return match[1].str() == "true";
}

bool optionalBool(const std::string& json, const std::string& key, const bool fallback) {
    const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*(true|false)");
    std::smatch match{};
    if (!std::regex_search(json, match, pattern)) {
        return fallback;
    }
    return match[1].str() == "true";
}

std::string toUpperCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

model::TextureFormat parseTextureFormatOrDefault(const std::optional<std::string>& value,
    const model::TextureFormat fallback) {
    if (!value.has_value()) {
        return fallback;
    }
    const auto upper = toUpperCopy(*value);
    if (upper == "I4") {
        return model::TextureFormat::I4;
    }
    if (upper == "I8") {
        return model::TextureFormat::I8;
    }
    if (upper == "IA4") {
        return model::TextureFormat::IA4;
    }
    if (upper == "IA8") {
        return model::TextureFormat::IA8;
    }
    if (upper == "RGB565") {
        return model::TextureFormat::RGB565;
    }
    if (upper == "RGB5A3") {
        return model::TextureFormat::RGB5A3;
    }
    if (upper == "RGBA8") {
        return model::TextureFormat::RGBA8;
    }
    if (upper == "CI4") {
        return model::TextureFormat::CI4;
    }
    if (upper == "CI8") {
        return model::TextureFormat::CI8;
    }
    if (upper == "CI14X2") {
        return model::TextureFormat::CI14X2;
    }
    if (upper == "CMPR") {
        return model::TextureFormat::CMPR;
    }
    throw std::runtime_error("unsupported GVR import texture format: " + *value);
}

model::PaletteFormat parsePaletteFormatOrDefault(const std::optional<std::string>& value,
    const model::PaletteFormat fallback) {
    if (!value.has_value()) {
        return fallback;
    }
    const auto upper = toUpperCopy(*value);
    if (upper == "NONE") {
        return model::PaletteFormat::None;
    }
    if (upper == "IA8") {
        return model::PaletteFormat::IA8;
    }
    if (upper == "RGB565") {
        return model::PaletteFormat::RGB565;
    }
    if (upper == "RGB5A3") {
        return model::PaletteFormat::RGB5A3;
    }
    throw std::runtime_error("unsupported GVR import palette format: " + *value);
}

model::TextureFormat defaultImportTextureFormat(const model::TextureFormat sourceFormat) {
    switch (sourceFormat) {
    case model::TextureFormat::I4:
    case model::TextureFormat::I8:
    case model::TextureFormat::IA4:
    case model::TextureFormat::IA8:
    case model::TextureFormat::RGB565:
    case model::TextureFormat::CMPR:
    case model::TextureFormat::CI4:
    case model::TextureFormat::CI8:
    case model::TextureFormat::CI14X2:
    case model::TextureFormat::RGB5A3:
    case model::TextureFormat::RGBA8:
        return sourceFormat;
    default:
        return model::TextureFormat::RGBA8;
    }
}

std::vector<std::uint8_t> decodeAklzIfNeeded(std::span<const std::uint8_t> sourceBytes,
    std::vector<std::string>& diagnostics,
    bool& sourceWasAklz) {
    sourceWasAklz = spice::compression::aklz::isAklz(sourceBytes);
    if (!sourceWasAklz) {
        return std::vector<std::uint8_t>(sourceBytes.begin(), sourceBytes.end());
    }

    auto decoded = spice::compression::aklz::decompress(sourceBytes);
    if (!decoded.ok()) {
        throw std::runtime_error("AKLZ decompression failed: " + std::string(spice::compression::aklz::errorToString(decoded.error)));
    }
    diagnostics.push_back("Source GVR was AKLZ-compressed and was decompressed before parsing.");
    return std::move(decoded.bytes);
}

std::filesystem::path resolveImagePath(const std::filesystem::path& jsonPath, const std::string& imageFile) {
    std::filesystem::path imagePath{ imageFile };
    if (imagePath.is_relative()) {
        imagePath = jsonPath.parent_path() / imagePath;
    }
    return imagePath;
}

std::vector<std::uint8_t> applyAklzPolicy(std::vector<std::uint8_t> rawBytes,
    const bool sourceWasAklz,
    const AklzPolicy policy,
    std::vector<std::string>& diagnostics) {
    const bool compress = policy == AklzPolicy::Compressed || (policy == AklzPolicy::Preserve && sourceWasAklz);
    if (!compress) {
        diagnostics.push_back("Imported GVR output is raw/uncompressed.");
        return rawBytes;
    }

    auto encoded = spice::compression::aklz::compress(rawBytes);
    if (!encoded.ok()) {
        throw std::runtime_error("AKLZ compression failed: " + std::string(spice::compression::aklz::errorToString(encoded.error)));
    }
    diagnostics.push_back("Imported GVR output was AKLZ-compressed.");
    return std::move(encoded.bytes);
}

} // namespace

std::string to_string(const AklzPolicy policy) {
    switch (policy) {
    case AklzPolicy::Preserve: return "preserve";
    case AklzPolicy::Compressed: return "compressed";
    case AklzPolicy::Raw: return "raw";
    default: return "preserve";
    }
}

AklzPolicy parseAklzPolicy(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lowered == "preserve") {
        return AklzPolicy::Preserve;
    }
    if (lowered == "compressed") {
        return AklzPolicy::Compressed;
    }
    if (lowered == "raw" || lowered == "uncompressed") {
        return AklzPolicy::Raw;
    }
    throw std::runtime_error("unknown GVR AKLZ policy: " + value);
}

GvrImageIrExportResult exportGvrImageIr(
    std::span<const std::uint8_t> sourceBytes,
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& outputDir) {
    GvrImageIrExportResult result{};
    std::filesystem::create_directories(outputDir);
    const auto stem = sourcePath.stem().string();
    result.pngPath = outputDir / (stem + ".png");
    result.jsonPath = outputDir / (stem + ".gvr.json");

    bool sourceWasAklz = false;
    auto parseBytes = decodeAklzIfNeeded(sourceBytes, result.diagnostics, sourceWasAklz);
    auto texture = parsing::parseGvrTexture(std::span<const std::uint8_t>(parseBytes.data(), parseBytes.size()), 0U);
    result.diagnostics.insert(result.diagnostics.end(), texture.diagnostics.begin(), texture.diagnostics.end());
    if (!texture.decodedBaseLevel.has_value() || texture.decodedBaseLevel->rgba8.empty()) {
        throw std::runtime_error("GVR export failed: decoded RGBA8 image was not available");
    }

    image::writePngRgba8(result.pngPath, *texture.decodedBaseLevel);

    std::ostringstream json{};
    json << "{\n";
    appendJsonU32(json, "schemaVersion", 1U);
    appendJsonString(json, "imageFile", result.pngPath.filename().string());
    appendJsonU32(json, "width", texture.decodedBaseLevel->width);
    appendJsonU32(json, "height", texture.decodedBaseLevel->height);
    appendJsonString(json, "pixelFormat", "rgba8");
    appendJsonBool(json, "sourceWasAklz", sourceWasAklz);
    appendJsonBool(json, "hasGlobalIndex", texture.hasGlobalIndex);
    appendJsonU32(json, "globalIndex", texture.globalIndex);
    appendJsonU32(json, "rawFlags", texture.rawFlags);
    appendJsonU32(json, "rawDataFormat", texture.rawDataFormat);
    appendJsonString(json, "textureFormat", model::to_string(texture.textureFormat));
    appendJsonString(json, "paletteFormat", model::to_string(texture.paletteFormat));
    appendJsonBool(json, "hasMipmaps", texture.hasMipmaps);
    appendJsonString(json, "importTextureFormat", model::to_string(defaultImportTextureFormat(texture.textureFormat)),
        texture.textureFormat == model::TextureFormat::CI4 ||
            texture.textureFormat == model::TextureFormat::CI8 ||
            texture.textureFormat == model::TextureFormat::CI14X2);
    if (texture.textureFormat == model::TextureFormat::CI4 ||
        texture.textureFormat == model::TextureFormat::CI8 ||
        texture.textureFormat == model::TextureFormat::CI14X2) {
        appendJsonString(json, "importPaletteFormat", model::to_string(texture.paletteFormat), false);
    }
    json << "}\n";
    writeText(result.jsonPath, json.str());
    return result;
}

GvrImageIrImportResult importGvrImageIr(const std::filesystem::path& jsonPath, const AklzPolicy aklzPolicy) {
    GvrImageIrImportResult result{};
    const auto json = readText(jsonPath);
    const auto schemaVersion = requireU32(json, "schemaVersion");
    if (schemaVersion != 1U) {
        throw std::runtime_error("unsupported GVR image IR schema version: " + std::to_string(schemaVersion));
    }

    const auto imageFile = requireString(json, "imageFile");
    const auto width = requireU32(json, "width");
    const auto height = requireU32(json, "height");
    const auto pixelFormat = requireString(json, "pixelFormat");
    if (pixelFormat != "rgba8") {
        throw std::runtime_error("unsupported GVR image IR pixel format: " + pixelFormat);
    }

    auto image = image::readPngRgba8(resolveImagePath(jsonPath, imageFile));
    if (image.width != width || image.height != height) {
        throw std::runtime_error("PNG dimensions do not match GVR image IR sidecar");
    }

    const auto importTextureFormatText = optionalString(json, "importTextureFormat");
    const auto importFormat = parseTextureFormatOrDefault(importTextureFormatText, model::TextureFormat::RGBA8);

    encoding::EncodeOptions encodeOptions{};
    encodeOptions.textureFormat = importFormat;
    encodeOptions.paletteFormat = importFormat == model::TextureFormat::CI4 ||
        importFormat == model::TextureFormat::CI8 ||
        importFormat == model::TextureFormat::CI14X2
        ? parsePaletteFormatOrDefault(optionalString(json, "importPaletteFormat"), model::PaletteFormat::RGB5A3)
        : model::PaletteFormat::None;
    encodeOptions.generateMipmaps = importTextureFormatText.has_value()
        ? optionalBool(json, "hasMipmaps", false)
        : false;
    encodeOptions.hasGlobalIndex = requireBool(json, "hasGlobalIndex");
    encodeOptions.globalIndex = requireU32(json, "globalIndex");

    auto rawGvr = encoding::encodeGvr(image, encodeOptions);
    const auto sourceWasAklz = requireBool(json, "sourceWasAklz");
    result.bytes = applyAklzPolicy(std::move(rawGvr), sourceWasAklz, aklzPolicy, result.diagnostics);
    return result;
}

GvrSourceMetadata readGvrSourceMetadata(std::span<const std::uint8_t> sourceBytes) {
    GvrSourceMetadata result{};
    auto parseBytes = decodeAklzIfNeeded(sourceBytes, result.diagnostics, result.sourceWasAklz);
    result.texture = parsing::parseGvrTexture(std::span<const std::uint8_t>(parseBytes.data(), parseBytes.size()), 0U);
    result.diagnostics.insert(result.diagnostics.end(), result.texture.diagnostics.begin(), result.texture.diagnostics.end());
    return result;
}

GvrImageIrImportResult encodeGvrFromPng(
    const std::filesystem::path& pngPath,
    const GvrPngEncodeOptions& options) {
    GvrImageIrImportResult result{};
    const auto image = image::readPngRgba8(pngPath);
    auto rawGvr = encoding::encodeGvr(image, options.encodeOptions);
    result.bytes = applyAklzPolicy(std::move(rawGvr), options.sourceWasAklz, options.aklzPolicy, result.diagnostics);
    return result;
}

} // namespace spice::gvm::ir
