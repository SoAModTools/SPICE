#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace spice::mlk {

struct MlkBlenderIrRecordExportSummary {
    std::string filePath{};
    std::size_t recordIndex{ 0U };
    std::uint32_t recordOffset{ 0U };
    std::uint32_t key{ 0U };
    std::string generatedMldName{};
    std::uint32_t rawWord12{ 0U };
    std::uint32_t payloadOffset{ 0U };
    std::uint32_t payloadSize{ 0U };
    std::string payloadKind{};
    bool payloadInBounds{ false };
    bool parseAttempted{ false };
    bool parseOk{ false };
    std::string status{};
    std::string skipReason{};
    std::size_t diagnosticCount{ 0U };
    std::size_t warningCount{ 0U };
    std::size_t errorCount{ 0U };
    std::size_t meshCount{ 0U };
    std::size_t objectTreeCount{ 0U };
    std::size_t indexEntryCount{ 0U };
    std::size_t textureCount{ 0U };
    std::size_t animationCount{ 0U };
    std::vector<std::string> sampleFxnNames{};
};

struct MlkBlenderIrFileExportResult {
    std::string relativePath{};
    std::filesystem::path outputDir{};
    std::filesystem::path combinedBlenderIrPath{};
    std::filesystem::path manifestPath{};
    std::filesystem::path recordsCsvPath{};
    std::size_t recordCount{ 0U };
    std::size_t parsedRecordCount{ 0U };
    std::size_t skippedRecordCount{ 0U };
    std::vector<MlkBlenderIrRecordExportSummary> records{};
};

struct MlkBlenderIrExportResult {
    std::string inputPath{};
    bool inputWasDirectory{ false };
    std::vector<MlkBlenderIrFileExportResult> files{};
};

[[nodiscard]] std::string generatedMldNameForKey(std::uint32_t key);

[[nodiscard]] MlkBlenderIrExportResult exportMlkBlenderIr(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputDir);

} // namespace spice::mlk
