#pragma once

#include "BinModel.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace spice::bin {

struct BinCorpusFileSummary {
    std::string relativePath{};
    std::string absolutePath{};
    bool sourceWasCompressedAklz{ false };
    std::uint32_t rawSize{ 0U };
    std::uint32_t decodedSize{ 0U };
    bool decodedOk{ true };
    std::string decodeError{};
    BinFile file{};
};

struct BinCorpusScanResult {
    std::string inputPath{};
    bool inputWasDirectory{ false };
    std::vector<BinCorpusFileSummary> files{};
};

struct BinCorpusFeedbackSummary {
    std::size_t fileCount{ 0U };
    std::size_t aklzCompressedFileCount{ 0U };
    std::size_t decodeErrorCount{ 0U };
    std::size_t indexedTableProbeCount{ 0U };
    std::size_t plausibleIndexedTableProbeCount{ 0U };
    std::size_t warningCount{ 0U };
    std::size_t errorCount{ 0U };
};

struct BinCorpusWriteResult {
    std::filesystem::path filesCsvPath{};
    std::filesystem::path indexedTablesCsvPath{};
};

[[nodiscard]] BinCorpusScanResult scanBinCorpus(const std::filesystem::path& inputPath);
[[nodiscard]] BinCorpusFeedbackSummary summarizeBinCorpusFeedback(const BinCorpusScanResult& corpus);

[[nodiscard]] std::string formatBinCorpusFilesCsv(const BinCorpusScanResult& corpus);
[[nodiscard]] std::string formatBinCorpusIndexedTablesCsv(const BinCorpusScanResult& corpus);

[[nodiscard]] BinCorpusWriteResult writeBinCorpusArtifacts(
    const BinCorpusScanResult& corpus,
    const std::filesystem::path& outputDir);

} // namespace spice::bin
