#include "BinCorpus.h"

#include "BinParser.h"

#include "../Compression/Aklz.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace spice::bin {
namespace {

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool isBinPath(const std::filesystem::path& path) {
    return toLowerCopy(path.extension().string()) == ".bin";
}

std::vector<std::filesystem::path> collectBinPaths(const std::filesystem::path& inputPath, bool& inputWasDirectory) {
    std::error_code ec{};
    inputWasDirectory = std::filesystem::is_directory(inputPath, ec);
    ec.clear();
    const bool inputWasFile = std::filesystem::is_regular_file(inputPath, ec);
    if (!inputWasDirectory && !inputWasFile) {
        std::ostringstream message;
        message << "Input path is not a file or directory: " << inputPath.string();
        throw std::runtime_error(message.str());
    }

    std::vector<std::filesystem::path> paths{};
    if (inputWasFile) {
        if (isBinPath(inputPath)) {
            paths.push_back(inputPath);
        }
        return paths;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             inputPath,
             std::filesystem::directory_options::skip_permission_denied)) {
        std::error_code entryEc{};
        if (!entry.is_regular_file(entryEc) || entryEc) {
            continue;
        }
        if (isBinPath(entry.path())) {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

std::string relativePathString(
    const std::filesystem::path& path,
    const std::filesystem::path& inputPath,
    bool inputWasDirectory) {
    if (!inputWasDirectory) {
        return path.filename().generic_string();
    }

    std::error_code ec{};
    const auto relative = std::filesystem::relative(path, inputPath, ec);
    if (!ec && !relative.empty()) {
        return relative.generic_string();
    }
    return path.filename().generic_string();
}

std::vector<std::uint8_t> readAllBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::ostringstream message;
        message << "Could not open input file: " << path.string();
        throw std::runtime_error(message.str());
    }

    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}

void writeTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::ostringstream message;
        message << "Could not open output file: " << path.string();
        throw std::runtime_error(message.str());
    }
    out << text;
    if (!out.good()) {
        std::ostringstream message;
        message << "Could not write output file: " << path.string();
        throw std::runtime_error(message.str());
    }
}

std::string boolText(bool value) {
    return value ? "true" : "false";
}

std::string csvEscape(const std::string& value) {
    const bool needsQuotes = value.find_first_of(",\"\r\n") != std::string::npos;
    if (!needsQuotes) {
        return value;
    }

    std::string escaped = "\"";
    for (const char c : value) {
        if (c == '"') {
            escaped += "\"\"";
        } else {
            escaped += c;
        }
    }
    escaped += '"';
    return escaped;
}

std::size_t diagnosticCount(const BinFile& file, DiagnosticSeverity severity) {
    return static_cast<std::size_t>(std::count_if(file.diagnostics.begin(), file.diagnostics.end(), [&](const auto& diagnostic) {
        return diagnostic.severity == severity;
    }));
}

bool plausibleIndexedTableProbe(const BinIndexedTableProbe& probe) {
    return probe.present &&
        probe.offsetTableInBounds &&
        probe.offsetsInBounds &&
        probe.offsetsMonotonic;
}

std::uint32_t sizeToU32Saturated(std::size_t size) {
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(size);
}

} // namespace

BinCorpusScanResult scanBinCorpus(const std::filesystem::path& inputPath) {
    BinCorpusScanResult result{};
    result.inputPath = inputPath.string();

    const auto paths = collectBinPaths(inputPath, result.inputWasDirectory);
    result.files.reserve(paths.size());

    for (const auto& path : paths) {
        BinCorpusFileSummary file{};
        file.relativePath = relativePathString(path, inputPath, result.inputWasDirectory);
        file.absolutePath = std::filesystem::absolute(path).string();

        auto rawBytes = readAllBytes(path);
        file.rawSize = sizeToU32Saturated(rawBytes.size());

        std::vector<std::uint8_t> decodedBytes{};
        if (spice::compression::aklz::isAklz(rawBytes)) {
            file.sourceWasCompressedAklz = true;
            const auto decoded = spice::compression::aklz::decompress(rawBytes);
            if (!decoded.ok()) {
                file.decodedOk = false;
                file.decodeError = std::string(spice::compression::aklz::errorToString(decoded.error));
                file.decodedSize = 0U;
                file.file = parseBytes({}, file.relativePath);
                result.files.push_back(std::move(file));
                continue;
            }
            decodedBytes = decoded.bytes;
        } else {
            decodedBytes = std::move(rawBytes);
        }

        file.decodedSize = sizeToU32Saturated(decodedBytes.size());
        file.file = parseBytes(std::move(decodedBytes), file.relativePath);
        result.files.push_back(std::move(file));
    }

    return result;
}

BinCorpusFeedbackSummary summarizeBinCorpusFeedback(const BinCorpusScanResult& corpus) {
    BinCorpusFeedbackSummary summary{};
    summary.fileCount = corpus.files.size();
    for (const auto& file : corpus.files) {
        if (file.sourceWasCompressedAklz) {
            ++summary.aklzCompressedFileCount;
        }
        if (!file.decodedOk) {
            ++summary.decodeErrorCount;
        }
        if (file.file.indexedTableProbe.present) {
            ++summary.indexedTableProbeCount;
        }
        if (plausibleIndexedTableProbe(file.file.indexedTableProbe)) {
            ++summary.plausibleIndexedTableProbeCount;
        }
        summary.warningCount += diagnosticCount(file.file, DiagnosticSeverity::Warning);
        summary.errorCount += diagnosticCount(file.file, DiagnosticSeverity::Error);
    }
    return summary;
}

std::string formatBinCorpusFilesCsv(const BinCorpusScanResult& corpus) {
    std::ostringstream out;
    out << "path,absolutePath,rawSize,decodedSize,sourceWasCompressedAklz,decodedOk,decodeError,"
           "indexedTablePresent,indexedTablePlausible,warningCount,errorCount\n";
    for (const auto& file : corpus.files) {
        out << csvEscape(file.relativePath) << ","
            << csvEscape(file.absolutePath) << ","
            << file.rawSize << ","
            << file.decodedSize << ","
            << boolText(file.sourceWasCompressedAklz) << ","
            << boolText(file.decodedOk) << ","
            << csvEscape(file.decodeError) << ","
            << boolText(file.file.indexedTableProbe.present) << ","
            << boolText(plausibleIndexedTableProbe(file.file.indexedTableProbe)) << ","
            << diagnosticCount(file.file, DiagnosticSeverity::Warning) << ","
            << diagnosticCount(file.file, DiagnosticSeverity::Error) << "\n";
    }
    return out.str();
}

std::string formatBinCorpusIndexedTablesCsv(const BinCorpusScanResult& corpus) {
    std::ostringstream out;
    out << "filePath,rawSize,decodedSize,sourceWasCompressedAklz,decodedOk,decodeError,"
           "present,headerInBounds,count,offsetTableOffset,offsetTableEndOffset,dataBaseOffset,"
           "offsetTableInBounds,offsetsInBounds,offsetsMonotonic,firstRecordOffset,lastRecordOffset,"
           "sampledRecordCount,offsetsPreview,sampleIndex,tableOffset,recordOffset,recordInBounds,"
           "word0,word0EqualsDataBaseOffset,word4,word4TargetInBounds,word8,word12,word16,word20,word24,"
           "bytes16Hex,bytes32Hex\n";
    for (const auto& file : corpus.files) {
        const auto& probe = file.file.indexedTableProbe;
        if (!probe.present) {
            continue;
        }

        if (probe.samples.empty()) {
            out << csvEscape(file.relativePath) << ","
                << file.rawSize << ","
                << file.decodedSize << ","
                << boolText(file.sourceWasCompressedAklz) << ","
                << boolText(file.decodedOk) << ","
                << csvEscape(file.decodeError) << ","
                << boolText(probe.present) << ","
                << boolText(probe.headerInBounds) << ","
                << probe.count << ","
                << probe.offsetTableOffset << ","
                << probe.offsetTableEndOffset << ","
                << probe.dataBaseOffset << ","
                << boolText(probe.offsetTableInBounds) << ","
                << boolText(probe.offsetsInBounds) << ","
                << boolText(probe.offsetsMonotonic) << ","
                << probe.firstRecordOffset << ","
                << probe.lastRecordOffset << ","
                << probe.sampledRecordCount << ","
                << csvEscape(probe.offsetsPreview)
                << ",,,,,,,,,,,,,,,\n";
            continue;
        }

        for (const auto& sample : probe.samples) {
            out << csvEscape(file.relativePath) << ","
                << file.rawSize << ","
                << file.decodedSize << ","
                << boolText(file.sourceWasCompressedAklz) << ","
                << boolText(file.decodedOk) << ","
                << csvEscape(file.decodeError) << ","
                << boolText(probe.present) << ","
                << boolText(probe.headerInBounds) << ","
                << probe.count << ","
                << probe.offsetTableOffset << ","
                << probe.offsetTableEndOffset << ","
                << probe.dataBaseOffset << ","
                << boolText(probe.offsetTableInBounds) << ","
                << boolText(probe.offsetsInBounds) << ","
                << boolText(probe.offsetsMonotonic) << ","
                << probe.firstRecordOffset << ","
                << probe.lastRecordOffset << ","
                << probe.sampledRecordCount << ","
                << csvEscape(probe.offsetsPreview) << ","
                << sample.sampleIndex << ","
                << sample.tableOffset << ","
                << sample.recordOffset << ","
                << boolText(sample.recordInBounds) << ","
                << sample.word0 << ","
                << boolText(sample.word0EqualsDataBaseOffset) << ","
                << sample.word4 << ","
                << boolText(sample.word4TargetInBounds) << ","
                << sample.word8 << ","
                << sample.word12 << ","
                << sample.word16 << ","
                << sample.word20 << ","
                << sample.word24 << ","
                << csvEscape(sample.bytes16Hex) << ","
                << csvEscape(sample.bytes32Hex) << "\n";
        }
    }
    return out.str();
}

BinCorpusWriteResult writeBinCorpusArtifacts(
    const BinCorpusScanResult& corpus,
    const std::filesystem::path& outputDir) {
    std::filesystem::create_directories(outputDir);

    BinCorpusWriteResult result{};
    result.filesCsvPath = outputDir / "bin_corpus_files.csv";
    result.indexedTablesCsvPath = outputDir / "bin_corpus_indexed_tables.csv";

    writeTextFile(result.filesCsvPath, formatBinCorpusFilesCsv(corpus));
    writeTextFile(result.indexedTablesCsvPath, formatBinCorpusIndexedTablesCsv(corpus));
    return result;
}

} // namespace spice::bin
