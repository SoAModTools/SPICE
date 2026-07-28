#include "StdFileWriter.h"
#include "StdParser.h"
#include "StdUsage.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool hasStdExtension(const std::filesystem::path& path)
{
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension == ".std";
}

std::vector<std::filesystem::path> collectStdFiles(const std::filesystem::path& inputPath)
{
    std::vector<std::filesystem::path> paths{};
    if (std::filesystem::is_regular_file(inputPath)) {
        if (hasStdExtension(inputPath)) {
            paths.push_back(inputPath);
        }
        return paths;
    }

    if (!std::filesystem::is_directory(inputPath)) {
        return paths;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(inputPath)) {
        if (entry.is_regular_file() && hasStdExtension(entry.path())) {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

struct RoundTripSmokeResult {
    std::size_t fileCount{ 0U };
    std::size_t completeParseCount{ 0U };
    std::size_t failureCount{ 0U };
    std::vector<std::string> firstFailures{};
};

void addRoundTripFailure(RoundTripSmokeResult& result, const std::filesystem::path& path, const std::string& reason)
{
    ++result.failureCount;
    if (result.firstFailures.size() < 5U) {
        result.firstFailures.push_back(path.string() + ": " + reason);
    }
}

RoundTripSmokeResult runRoundTripSmoke(const std::filesystem::path& inputPath)
{
    RoundTripSmokeResult result{};
    const auto paths = collectStdFiles(inputPath);
    result.fileCount = paths.size();

    for (const auto& path : paths) {
        const auto parsed = spice::stdfile::parseFile(path);
        if (parsed.parseStatus != spice::stdfile::StdParseStatus::Complete) {
            addRoundTripFailure(result, path, std::string("parseStatus=") + spice::stdfile::toString(parsed.parseStatus));
            continue;
        }
        ++result.completeParseCount;

        const auto written = spice::stdfile::StdFileWriter{}.write(parsed);
        if (!written.ok()) {
            addRoundTripFailure(
                result,
                path,
                written.diagnostics.empty() ? "writer failed" : written.diagnostics.front().message);
            continue;
        }
        if (written.bytes != parsed.rawBytes) {
            addRoundTripFailure(result, path, "writer output did not exactly match original source bytes");
        }
    }

    return result;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: SpiceStdCorpusScan <input_file_or_dir> <output_dir>\n";
        return 2;
    }

    try {
        const std::filesystem::path inputPath = argv[1];
        const std::filesystem::path outputDir = argv[2];

        const auto scan = spice::stdfile::scanStdUsage(inputPath);
        const auto written = spice::stdfile::writeStdUsageArtifacts(scan, outputDir);
        const auto summary = spice::stdfile::summarizeStdUsage(scan);
        const auto roundTrip = runRoundTripSmoke(inputPath);

        std::cout << "SpiceStdCorpusScan complete\n";
        std::cout << "input=" << inputPath.string() << "\n";
        std::cout << "output=" << outputDir.string() << "\n";
        std::cout << "files=" << summary.fileCount << "\n";
        std::cout << "aklzCompressedFiles=" << summary.aklzCompressedFileCount << "\n";
        std::cout << "decodeErrors=" << summary.decodeErrorCount << "\n";
        std::cout << "alxKnownCoveredPatternFiles=" << summary.alxKnownCoveredPatternCount << "\n";
        std::cout << "bcharaFiles=" << summary.bcharaFileCount << "\n";
        std::cout << "otherDirectoryFiles=" << summary.otherDirectoryFileCount << "\n";
        std::cout << "canonicalRoundTripFiles=" << roundTrip.fileCount << "\n";
        std::cout << "canonicalRoundTripCompleteParses=" << roundTrip.completeParseCount << "\n";
        std::cout << "canonicalRoundTripFailures=" << roundTrip.failureCount << "\n";
        for (const auto& failure : roundTrip.firstFailures) {
            std::cout << "canonicalRoundTripFailure=" << failure << "\n";
        }
        std::cout << "filesCsv=" << written.filesCsvPath.string() << "\n";
        std::cout << "bucketsCsv=" << written.bucketsCsvPath.string() << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "SpiceStdCorpusScan failed: " << ex.what() << "\n";
        return 1;
    }
}
