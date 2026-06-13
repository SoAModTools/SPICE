#pragma once

#include "MlkModel.h"

#include <filesystem>
#include <string>
#include <vector>

namespace spice::mlk {

enum class MlkTableShape {
    Normal,
    FirstPayloadCountCandidate,
    MalformedRecordSpans,
};

struct MlkEmbeddedMldParseSummary {
    bool attempted{ false };
    bool parseOk{ false };
    std::size_t entryCount{ 0U };
    std::size_t diagnosticCount{ 0U };
    std::size_t warningCount{ 0U };
    std::size_t errorCount{ 0U };
    bool textureArchivePresent{ false };
    std::size_t objectReferenceCount{ 0U };
    std::size_t groundReferenceCount{ 0U };
    std::size_t motionReferenceCount{ 0U };
    std::size_t textureReferenceCount{ 0U };
    std::vector<std::string> sampleFxnNames{};
};

struct MlkCorpusRecordSummary {
    MlkRecordProbe record{};
    MlkEmbeddedMldParseSummary embeddedMldParse{};
};

struct MlkCorpusFileSummary {
    std::string relativePath{};
    std::string absolutePath{};
    MlkScanResult scan{};
    std::vector<MlkCorpusRecordSummary> records{};
};

struct MlkCorpusScanResult {
    std::string inputPath{};
    bool inputWasDirectory{ false };
    std::vector<MlkCorpusFileSummary> files{};
};

struct MlkCorpusWriteResult {
    std::filesystem::path jsonPath{};
    std::filesystem::path filesCsvPath{};
    std::filesystem::path recordsCsvPath{};
    std::filesystem::path word12HistogramCsvPath{};
    std::filesystem::path anomaliesCsvPath{};
    std::filesystem::path word12ByKindCsvPath{};
    std::filesystem::path embeddedMldSummaryCsvPath{};
};

[[nodiscard]] const char* toString(MlkTableShape shape);

[[nodiscard]] MlkCorpusScanResult scanMlkCorpus(const std::filesystem::path& inputPath);

[[nodiscard]] std::string formatMlkCorpusJson(const MlkCorpusScanResult& corpus);
[[nodiscard]] std::string formatMlkCorpusFilesCsv(const MlkCorpusScanResult& corpus);
[[nodiscard]] std::string formatMlkCorpusRecordsCsv(const MlkCorpusScanResult& corpus);
[[nodiscard]] std::string formatMlkCorpusWord12HistogramCsv(const MlkCorpusScanResult& corpus);
[[nodiscard]] std::string formatMlkCorpusAnomaliesCsv(const MlkCorpusScanResult& corpus);
[[nodiscard]] std::string formatMlkCorpusRawWord12ByKindCsv(const MlkCorpusScanResult& corpus);
[[nodiscard]] std::string formatMlkCorpusEmbeddedMldSummaryCsv(const MlkCorpusScanResult& corpus);

[[nodiscard]] MlkCorpusWriteResult writeMlkCorpusArtifacts(
    const MlkCorpusScanResult& corpus,
    const std::filesystem::path& outputDir);

} // namespace spice::mlk
