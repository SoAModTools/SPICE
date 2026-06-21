#pragma once

#include "../Encoding/GvrEncoder.h"
#include "../Model/GvmTextureModel.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace spice::gvm::ir {

enum class AklzPolicy {
    Preserve,
    Compressed,
    Raw,
};

struct GvrImageIrExportResult {
    std::filesystem::path pngPath{};
    std::filesystem::path jsonPath{};
    std::vector<std::string> diagnostics{};
};

struct GvrImageIrImportResult {
    std::vector<std::uint8_t> bytes{};
    std::vector<std::string> diagnostics{};
};

struct GvrPngExportResult {
    std::filesystem::path pngPath{};
    bool sourceWasAklz = false;
    model::GvrTexture texture{};
    std::vector<std::string> diagnostics{};
};

struct GvrPngEncodeOptions {
    encoding::EncodeOptions encodeOptions{};
    AklzPolicy aklzPolicy = AklzPolicy::Raw;
    bool sourceWasAklz = false;
};

struct GvrSourceMetadata {
    bool sourceWasAklz = false;
    model::GvrTexture texture{};
    std::vector<std::string> diagnostics{};
};

[[nodiscard]] GvrImageIrExportResult exportGvrImageIr(
    std::span<const std::uint8_t> sourceBytes,
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& outputDir);

[[nodiscard]] GvrPngExportResult exportGvrPng(
    std::span<const std::uint8_t> sourceBytes,
    const std::filesystem::path& outputPath);

[[nodiscard]] GvrImageIrImportResult importGvrImageIr(
    const std::filesystem::path& jsonPath,
    AklzPolicy aklzPolicy = AklzPolicy::Preserve);

[[nodiscard]] GvrSourceMetadata readGvrSourceMetadata(std::span<const std::uint8_t> sourceBytes);

[[nodiscard]] GvrImageIrImportResult encodeGvrFromPng(
    const std::filesystem::path& pngPath,
    const GvrPngEncodeOptions& options);

[[nodiscard]] std::string to_string(AklzPolicy policy);
[[nodiscard]] AklzPolicy parseAklzPolicy(const std::string& value);

} // namespace spice::gvm::ir
