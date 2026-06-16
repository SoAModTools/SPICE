#pragma once

#include "MllModel.h"

#include <filesystem>
#include <string>
#include <vector>

namespace spice::mll {

struct MllCorpusMemberSummary {
    MllMember member{};
};

struct MllCorpusFileSummary {
    std::string relativePath{};
    std::string absolutePath{};
    MllFile file{};
    std::vector<MllCorpusMemberSummary> members{};
};

struct MllCorpusScanResult {
    std::string inputPath{};
    bool inputWasDirectory{ false };
    std::vector<MllCorpusFileSummary> files{};
};

struct MllCorpusWriteResult {
    std::filesystem::path jsonPath{};
    std::filesystem::path filesCsvPath{};
    std::filesystem::path membersCsvPath{};
    std::filesystem::path mldObjectListsCsvPath{};
    std::filesystem::path mldBlocksCsvPath{};
    std::filesystem::path gvrTexturesCsvPath{};
    std::filesystem::path textureMembersCsvPath{};
    std::filesystem::path preTextureTableEntriesCsvPath{};
    std::filesystem::path indexedBinTablesCsvPath{};
    std::filesystem::path anomaliesCsvPath{};
    std::filesystem::path payloadKindHistogramCsvPath{};
};

struct MllCorpusFeedbackSummary {
    std::size_t fileCount{ 0U };
    std::size_t supportedFileCount{ 0U };
    std::size_t normalShapeCount{ 0U };
    std::size_t firstMemberCountCandidateCount{ 0U };
    std::size_t malformedShapeCount{ 0U };
    std::size_t totalMemberCount{ 0U };
    std::size_t mldLikeMemberCount{ 0U };
    std::size_t mldIndexShapeMemberCount{ 0U };
    std::size_t mldObjectListProbeCount{ 0U };
    std::size_t nonZeroMldObjectListProbeCount{ 0U };
    std::size_t plausibleMldObjectListProbeCount{ 0U };
    std::size_t mldBlockProbeCount{ 0U };
    std::size_t exactReferencedMldBlockProbeCount{ 0U };
    std::size_t gvrTextureProbeCount{ 0U };
    std::size_t parsedGvrTextureProbeCount{ 0U };
    std::size_t failedGvrTextureProbeCount{ 0U };
    std::size_t decodedGvrTextureProbeCount{ 0U };
    std::size_t textureMemberProbeCount{ 0U };
    std::size_t denseGlobalIndexTextureMemberCount{ 0U };
    std::size_t textureClusterCount{ 0U };
    std::size_t headerTextureTableBeforeFirstTextureCount{ 0U };
    std::size_t indexTexturePointerInsideTextureSpanCount{ 0U };
    std::size_t preTextureTableProbeCount{ 0U };
    std::size_t preTextureTableAlignedProbeCount{ 0U };
    std::size_t preTextureTableEntryCount{ 0U };
    std::size_t preTextureTablePrintableNameCount{ 0U };
    std::size_t preTextureTableTextureNameMatchCount{ 0U };
    std::size_t indexedBinTableProbeCount{ 0U };
    std::size_t plausibleIndexedBinTableProbeCount{ 0U };
    std::size_t outOfBoundsMemberCount{ 0U };
    std::size_t warningCount{ 0U };
    std::size_t errorCount{ 0U };
};

[[nodiscard]] MllCorpusScanResult scanMllCorpus(const std::filesystem::path& inputPath);
[[nodiscard]] MllCorpusFeedbackSummary summarizeMllCorpusFeedback(const MllCorpusScanResult& corpus);

[[nodiscard]] std::string formatMllCorpusJson(const MllCorpusScanResult& corpus);
[[nodiscard]] std::string formatMllCorpusFilesCsv(const MllCorpusScanResult& corpus);
[[nodiscard]] std::string formatMllCorpusMembersCsv(const MllCorpusScanResult& corpus);
[[nodiscard]] std::string formatMllCorpusMldObjectListsCsv(const MllCorpusScanResult& corpus);
[[nodiscard]] std::string formatMllCorpusMldBlocksCsv(const MllCorpusScanResult& corpus);
[[nodiscard]] std::string formatMllCorpusGvrTexturesCsv(const MllCorpusScanResult& corpus);
[[nodiscard]] std::string formatMllCorpusTextureMembersCsv(const MllCorpusScanResult& corpus);
[[nodiscard]] std::string formatMllCorpusPreTextureTableEntriesCsv(const MllCorpusScanResult& corpus);
[[nodiscard]] std::string formatMllCorpusIndexedBinTablesCsv(const MllCorpusScanResult& corpus);
[[nodiscard]] std::string formatMllCorpusAnomaliesCsv(const MllCorpusScanResult& corpus);
[[nodiscard]] std::string formatMllCorpusPayloadKindHistogramCsv(const MllCorpusScanResult& corpus);

[[nodiscard]] MllCorpusWriteResult writeMllCorpusArtifacts(
    const MllCorpusScanResult& corpus,
    const std::filesystem::path& outputDir);

} // namespace spice::mll
