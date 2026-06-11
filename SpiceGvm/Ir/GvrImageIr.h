#pragma once

#include <cstdint>
#include <filesystem>
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

[[nodiscard]] GvrImageIrExportResult exportGvrImageIr(
    std::span<const std::uint8_t> sourceBytes,
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& outputDir);

[[nodiscard]] GvrImageIrImportResult importGvrImageIr(
    const std::filesystem::path& jsonPath,
    AklzPolicy aklzPolicy = AklzPolicy::Preserve);

[[nodiscard]] std::string to_string(AklzPolicy policy);
[[nodiscard]] AklzPolicy parseAklzPolicy(const std::string& value);

} // namespace spice::gvm::ir
