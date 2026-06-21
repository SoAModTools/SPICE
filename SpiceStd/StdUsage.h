#pragma once

#include "StdModel.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace spice::stdfile {

struct StdUsageScanResult {
    std::string inputPath{};
    bool inputWasDirectory{ false };
    std::vector<StdUsageFile> files{};
};

struct StdUsageSummary {
    std::size_t fileCount{ 0U };
    std::size_t aklzCompressedFileCount{ 0U };
    std::size_t decodeErrorCount{ 0U };
    std::size_t alxKnownCoveredPatternCount{ 0U };
    std::size_t bcharaFileCount{ 0U };
    std::size_t otherDirectoryFileCount{ 0U };
};

struct StdUsageBucketSummary {
    StdUsageBucket bucket{ StdUsageBucket::Unknown };
    std::size_t fileCount{ 0U };
    std::size_t aklzCompressedFileCount{ 0U };
    std::size_t decodeErrorCount{ 0U };
    std::size_t alxKnownCoveredPatternCount{ 0U };
};

struct StdUsageWriteResult {
    std::filesystem::path filesCsvPath{};
    std::filesystem::path bucketsCsvPath{};
};

[[nodiscard]] StdUsageScanResult scanStdUsage(const std::filesystem::path& inputPath);
[[nodiscard]] StdUsageSummary summarizeStdUsage(const StdUsageScanResult& scan);
[[nodiscard]] std::vector<StdUsageBucketSummary> summarizeStdUsageBuckets(const StdUsageScanResult& scan);

[[nodiscard]] std::string formatStdUsageFilesCsv(const StdUsageScanResult& scan);
[[nodiscard]] std::string formatStdUsageBucketsCsv(const StdUsageScanResult& scan);

[[nodiscard]] StdUsageWriteResult writeStdUsageArtifacts(
    const StdUsageScanResult& scan,
    const std::filesystem::path& outputDir);

} // namespace spice::stdfile
