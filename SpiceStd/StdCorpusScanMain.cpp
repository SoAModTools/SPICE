#include "StdDocumentImporter.h"
#include "StdDocumentWriter.h"
#include "StdUsage.h"
#include "../Compression/Aklz.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <map>
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
    std::size_t actionRowCount{ 0U };
    std::size_t entryRecordCount{ 0U };
    std::size_t fileTrailerCount{ 0U };
    std::map<std::uint32_t, std::size_t> typedPayloadCounts{};
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
        const auto parsed = spice::stdfile::StdDocumentImporter::importFile(path);
        if (!parsed.ok()) {
            addRoundTripFailure(result, path,
                parsed.diagnostics.empty() ? "import failed" : parsed.diagnostics.front().message);
            continue;
        }
        ++result.completeParseCount;
        if (const auto* rows = std::get_if<spice::stdfile::StdActionRowsContent>(&parsed.document->content)) {
            result.actionRowCount += rows->rows.size();
        } else if (const auto* table = std::get_if<spice::stdfile::StdEntryTableContent>(&parsed.document->content)) {
            result.entryRecordCount += table->records.size();
            if (table->fileTrailer.has_value()) ++result.fileTrailerCount;
            for (const auto& record : table->records) {
                if (!record.payload.has_value()) continue;
                const auto* payload = spice::stdfile::findEntryPayload(*table, *record.payload);
                if (payload != nullptr && !std::holds_alternative<spice::stdfile::StdOpaquePayload>(payload->content)) {
                    ++result.typedPayloadCounts[record.combinedType()];
                }
            }
        }

        const auto platform = parsed.receipt.byteOrder == spice::root::Endian::Little
            ? spice::stdfile::StdPlatform::Dreamcast : spice::stdfile::StdPlatform::GameCube;
        const auto written = spice::stdfile::StdDocumentWriter::write(
            *parsed.document, { platform, parsed.receipt.compression }, &parsed.receipt);
        if (!written.ok()) {
            addRoundTripFailure(
                result,
                path,
                written.diagnostics.empty() ? "writer failed" : written.diagnostics.front().message);
            continue;
        }
        std::ifstream input(path, std::ios::binary);
        const std::vector<std::uint8_t> original{
            std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        if (parsed.receipt.compression == spice::stdfile::StdCompression::None) {
            if (written.bytes != original) {
                addRoundTripFailure(result, path, "raw writer output did not exactly match the source bytes");
            }
        } else {
            const auto originalDecoded = spice::compression::aklz::decompress(original);
            const auto writtenDecoded = spice::compression::aklz::decompress(written.bytes);
            if (!originalDecoded.ok() || !writtenDecoded.ok() || originalDecoded.bytes != writtenDecoded.bytes) {
                addRoundTripFailure(result, path, "AKLZ writer output did not decode exactly to the source payload");
            }
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
        std::cout << "canonicalActionRows=" << roundTrip.actionRowCount << "\n";
        std::cout << "canonicalEntryRecords=" << roundTrip.entryRecordCount << "\n";
        std::cout << "canonicalFileTrailers=" << roundTrip.fileTrailerCount << "\n";
        for (const auto& [combinedType, count] : roundTrip.typedPayloadCounts) {
            std::cout << "canonicalTypedPayload[0x" << std::hex << combinedType << std::dec << "]=" << count << "\n";
        }
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
