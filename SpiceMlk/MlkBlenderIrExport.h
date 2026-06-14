#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace spice::mlk {

struct MlkBlenderIrEntryMetadata {
    std::size_t combinedEntryIndex{ 0U };
    std::uint32_t combinedSourceEntryId{ 0U };
    std::uint32_t originalSourceEntryId{ 0U };
    std::size_t originalTableIndex{ 0U };
    std::string originalFxnName{};
    std::string adjustedFxnName{};
};

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
    std::size_t combinedMeshIndexStart{ 0U };
    std::size_t combinedObjectTreeIndexStart{ 0U };
    std::size_t combinedEntryIndexStart{ 0U };
    std::size_t combinedTextureIndexStart{ 0U };
    std::size_t combinedAnimationIndexStart{ 0U };
    std::vector<MlkBlenderIrEntryMetadata> entries{};
    std::vector<std::string> sampleFxnNames{};
};

struct MlkBlenderIrFileExportResult {
    std::string relativePath{};
    std::filesystem::path outputDir{};
    std::filesystem::path combinedBlenderIrPath{};
    std::filesystem::path manifestPath{};
    std::filesystem::path metadataPath{};
    std::filesystem::path recordsCsvPath{};
    std::filesystem::path annotationPath{};
    std::filesystem::path annotationMediaDir{};
    std::filesystem::path annotationCombinedBlenderIrPath{};
    std::size_t recordCount{ 0U };
    std::size_t parsedRecordCount{ 0U };
    std::size_t skippedRecordCount{ 0U };
    bool wroteAnnotation{ false };
    bool preservedExistingAnnotation{ false };
    bool createdAnnotationMediaDir{ false };
    bool copiedAnnotationCombinedBlenderIr{ false };
    std::vector<MlkBlenderIrRecordExportSummary> records{};
};

struct MlkBlenderIrExportResult {
    std::string inputPath{};
    bool inputWasDirectory{ false };
    std::vector<MlkBlenderIrFileExportResult> files{};
};

struct MlkBlenderIrExportOptions {
    std::filesystem::path annotationRepositoryDir{};
    bool overwriteMlkAnnotations{ false };
};

[[nodiscard]] std::string generatedMldNameForKey(std::uint32_t key);

[[nodiscard]] MlkBlenderIrExportResult exportMlkBlenderIr(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputDir);

[[nodiscard]] MlkBlenderIrExportResult exportMlkBlenderIr(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputDir,
    const MlkBlenderIrExportOptions& options);

} // namespace spice::mlk
