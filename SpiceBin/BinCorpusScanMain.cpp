#include "BinCorpus.h"

#include <exception>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: SpiceBinCorpusScan <input_file_or_dir> <output_dir>\n";
        return 2;
    }

    try {
        const std::filesystem::path inputPath = argv[1];
        const std::filesystem::path outputDir = argv[2];

        const auto corpus = spice::bin::scanBinCorpus(inputPath);
        const auto written = spice::bin::writeBinCorpusArtifacts(corpus, outputDir);
        const auto summary = spice::bin::summarizeBinCorpusFeedback(corpus);

        std::cout << "SpiceBinCorpusScan complete\n";
        std::cout << "input=" << inputPath.string() << "\n";
        std::cout << "output=" << outputDir.string() << "\n";
        std::cout << "files=" << summary.fileCount << "\n";
        std::cout << "aklzCompressedFiles=" << summary.aklzCompressedFileCount << "\n";
        std::cout << "decodeErrors=" << summary.decodeErrorCount << "\n";
        std::cout << "indexedTableProbes=" << summary.indexedTableProbeCount << "\n";
        std::cout << "plausibleIndexedTableProbes=" << summary.plausibleIndexedTableProbeCount << "\n";
        std::cout << "warnings=" << summary.warningCount << "\n";
        std::cout << "errors=" << summary.errorCount << "\n";
        std::cout << "filesCsv=" << written.filesCsvPath.string() << "\n";
        std::cout << "indexedTablesCsv=" << written.indexedTablesCsvPath.string() << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "SpiceBinCorpusScan failed: " << ex.what() << "\n";
        return 1;
    }
}
