#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace spice::stdfile {

enum class StdUsageBucket {
    Unknown,
    BcharaMFamily,
    BcharaCommon,
    BcharaDamage,
    BcharaCharacterResource,
    BcharaOther,
    OtherDirectory,
};

struct StdUsageFile {
    std::string relativePath{};
    std::string absolutePath{};
    std::string directory{};
    std::string stem{};
    bool sourceWasCompressedAklz{ false };
    std::uint32_t rawSize{ 0U };
    std::uint32_t decodedSize{ 0U };
    bool decodedOk{ true };
    std::string decodeError{};
    StdUsageBucket usageBucket{ StdUsageBucket::Unknown };
    bool alxKnownCoveredPattern{ false };
    std::string decodedHeader16Hex{};
    std::string decodedHeader32Hex{};
    std::vector<std::string> printableStrings{};
};

[[nodiscard]] const char* toString(StdUsageBucket bucket);

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
