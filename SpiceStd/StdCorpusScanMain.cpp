#include "StdUsage.h"

#include <exception>
#include <filesystem>
#include <iostream>

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

        std::cout << "SpiceStdCorpusScan complete\n";
        std::cout << "input=" << inputPath.string() << "\n";
        std::cout << "output=" << outputDir.string() << "\n";
        std::cout << "files=" << summary.fileCount << "\n";
        std::cout << "aklzCompressedFiles=" << summary.aklzCompressedFileCount << "\n";
        std::cout << "decodeErrors=" << summary.decodeErrorCount << "\n";
        std::cout << "alxKnownCoveredPatternFiles=" << summary.alxKnownCoveredPatternCount << "\n";
        std::cout << "bcharaFiles=" << summary.bcharaFileCount << "\n";
        std::cout << "otherDirectoryFiles=" << summary.otherDirectoryFileCount << "\n";
        std::cout << "filesCsv=" << written.filesCsvPath.string() << "\n";
        std::cout << "bucketsCsv=" << written.bucketsCsvPath.string() << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "SpiceStdCorpusScan failed: " << ex.what() << "\n";
        return 1;
    }
}
