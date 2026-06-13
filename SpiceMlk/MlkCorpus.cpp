#include "MlkCorpus.h"

#include "MlkScanner.h"

#include "../Compression/Aklz.h"
#include "../SpiceMLD/Parsing/MldParser.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>

namespace spice::mlk {
namespace {

std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::ostringstream message;
        message << "Could not open file: " << path.string();
        throw std::runtime_error(message.str());
    }

    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0) {
        std::ostringstream message;
        message << "Could not determine file size: " << path.string();
        throw std::runtime_error(message.str());
    }
    in.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!in) {
            std::ostringstream message;
            message << "Could not read full file: " << path.string();
            throw std::runtime_error(message.str());
        }
    }
    return bytes;
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

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool isMlkPath(const std::filesystem::path& path) {
    return toLowerCopy(path.extension().string()) == ".mlk";
}

std::vector<std::filesystem::path> collectMlkPaths(const std::filesystem::path& inputPath, bool& inputWasDirectory) {
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
        if (isMlkPath(inputPath)) {
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
        if (isMlkPath(entry.path())) {
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

std::vector<std::uint8_t> decodeForPayloadAccess(std::span<const std::uint8_t> bytes) {
    if (!spice::compression::aklz::isAklz(bytes)) {
        return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
    }
    const auto decoded = spice::compression::aklz::decompress(bytes);
    if (!decoded.ok()) {
        return {};
    }
    return decoded.bytes;
}

class ScopedCoutSilencer {
public:
    ScopedCoutSilencer() : oldBuffer_(std::cout.rdbuf(sink_.rdbuf())) {}
    ~ScopedCoutSilencer() {
        std::cout.rdbuf(oldBuffer_);
    }

    ScopedCoutSilencer(const ScopedCoutSilencer&) = delete;
    ScopedCoutSilencer& operator=(const ScopedCoutSilencer&) = delete;

private:
    std::ostringstream sink_{};
    std::streambuf* oldBuffer_{ nullptr };
};

MlkEmbeddedMldParseSummary parseEmbeddedMld(
    std::span<const std::uint8_t> decodedBytes,
    const MlkRecordProbe& record) {
    MlkEmbeddedMldParseSummary summary{};
    if (record.payloadKind != MlkPayloadKind::MldFile || !record.payloadInBounds) {
        return summary;
    }
    if (record.payloadOffset > decodedBytes.size() ||
        record.payloadSize > decodedBytes.size() - record.payloadOffset) {
        return summary;
    }

    summary.attempted = true;
    const auto payload = decodedBytes.subspan(record.payloadOffset, record.payloadSize);

    spice::mld::parsing::ParseOptions options{};
    options.entryListOnly = true;
    options.buildBlenderIntermediateIr = false;
    options.exportBlenderIrJson = false;
    options.extractGrndGobjBlocks = false;

    try {
        ScopedCoutSilencer silence{};
        spice::mld::parsing::MldParser parser{};
        const auto parsed = parser.parse(payload, options);
        summary.entryCount = parsed.entryList.size();
        summary.diagnosticCount = parsed.diagnostics.size();
        for (const auto& diagnostic : parsed.diagnostics) {
            if (diagnostic.severity == spice::mld::parsing::ParseDiagnostic::Severity::Warning) {
                ++summary.warningCount;
            } else if (diagnostic.severity == spice::mld::parsing::ParseDiagnostic::Severity::Error) {
                ++summary.errorCount;
            }
        }
        summary.parseOk = summary.errorCount == 0U && summary.entryCount > 0U;
    } catch (const std::exception&) {
        summary.errorCount = 1U;
        summary.diagnosticCount = 1U;
        summary.parseOk = false;
    }
    return summary;
}

MlkCorpusFileSummary scanCorpusFile(
    const std::filesystem::path& path,
    const std::filesystem::path& inputPath,
    bool inputWasDirectory) {
    const auto rawBytes = readFileBytes(path);
    const auto relativePath = relativePathString(path, inputPath, inputWasDirectory);

    MlkCorpusFileSummary file{};
    file.relativePath = relativePath;
    file.absolutePath = std::filesystem::absolute(path).string();
    file.scan = MlkScanner::scan(rawBytes, relativePath);

    const auto decodedBytes = decodeForPayloadAccess(rawBytes);
    const auto decodedSpan = std::span<const std::uint8_t>(decodedBytes.data(), decodedBytes.size());
    file.records.reserve(file.scan.records.size());
    for (const auto& record : file.scan.records) {
        MlkCorpusRecordSummary recordSummary{};
        recordSummary.record = record;
        recordSummary.embeddedMldParse = parseEmbeddedMld(decodedSpan, record);
        file.records.push_back(std::move(recordSummary));
    }
    return file;
}

std::size_t diagnosticCount(const MlkScanResult& scan, DiagnosticSeverity severity) {
    return static_cast<std::size_t>(std::count_if(scan.diagnostics.begin(), scan.diagnostics.end(), [&](const auto& diagnostic) {
        return diagnostic.severity == severity;
    }));
}

std::size_t plausibleEmbeddedMldRecordCount(const MlkCorpusFileSummary& file) {
    return static_cast<std::size_t>(std::count_if(file.records.begin(), file.records.end(), [](const auto& record) {
        return record.record.embeddedMldHeader.plausible;
    }));
}

std::size_t parsedEmbeddedMldRecordCount(const MlkCorpusFileSummary& file) {
    return static_cast<std::size_t>(std::count_if(file.records.begin(), file.records.end(), [](const auto& record) {
        return record.embeddedMldParse.parseOk;
    }));
}

std::size_t outOfBoundsPayloadCount(const MlkCorpusFileSummary& file) {
    return static_cast<std::size_t>(std::count_if(file.records.begin(), file.records.end(), [](const auto& record) {
        return !record.record.payloadInBounds;
    }));
}

std::size_t duplicateKeyCount(const MlkCorpusFileSummary& file) {
    return static_cast<std::size_t>(std::count_if(file.records.begin(), file.records.end(), [](const auto& record) {
        return record.record.duplicateKey;
    }));
}

void appendJsonEscaped(std::ostream& out, std::string_view value) {
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20U) {
                out << "\\u00";
                constexpr char kHex[] = "0123456789abcdef";
                out << kHex[(static_cast<unsigned char>(ch) >> 4U) & 0x0fU];
                out << kHex[static_cast<unsigned char>(ch) & 0x0fU];
            } else {
                out << ch;
            }
            break;
        }
    }
}

void writeJsonString(std::ostream& out, std::string_view value) {
    out << '"';
    appendJsonEscaped(out, value);
    out << '"';
}

std::string csvEscape(std::string_view value) {
    const bool needsQuotes = value.find_first_of(",\"\r\n") != std::string_view::npos;
    if (!needsQuotes) {
        return std::string(value);
    }

    std::string escaped;
    escaped.reserve(value.size() + 2U);
    escaped.push_back('"');
    for (const char ch : value) {
        if (ch == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

std::string boolText(bool value) {
    return value ? "true" : "false";
}

std::string parentDirectoryForHistogram(const std::string& relativePath) {
    const auto slash = relativePath.find_last_of("/\\");
    if (slash == std::string::npos) {
        return {};
    }
    return relativePath.substr(0U, slash);
}

} // namespace

MlkCorpusScanResult scanMlkCorpus(const std::filesystem::path& inputPath) {
    MlkCorpusScanResult corpus{};
    corpus.inputPath = inputPath.string();
    const auto paths = collectMlkPaths(inputPath, corpus.inputWasDirectory);
    corpus.files.reserve(paths.size());
    for (const auto& path : paths) {
        corpus.files.push_back(scanCorpusFile(path, inputPath, corpus.inputWasDirectory));
    }
    return corpus;
}

std::string formatMlkCorpusJson(const MlkCorpusScanResult& corpus) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"inputPath\": ";
    writeJsonString(out, corpus.inputPath);
    out << ",\n";
    out << "  \"inputWasDirectory\": " << boolText(corpus.inputWasDirectory) << ",\n";
    out << "  \"fileCount\": " << corpus.files.size() << ",\n";
    out << "  \"files\": [\n";
    for (std::size_t fileIndex = 0; fileIndex < corpus.files.size(); ++fileIndex) {
        const auto& file = corpus.files[fileIndex];
        const auto& scan = file.scan;
        out << "    {\n";
        out << "      \"path\": ";
        writeJsonString(out, file.relativePath);
        out << ",\n";
        out << "      \"absolutePath\": ";
        writeJsonString(out, file.absolutePath);
        out << ",\n";
        out << "      \"rawSize\": " << scan.rawSize << ",\n";
        out << "      \"decodedSize\": " << scan.decodedSize << ",\n";
        out << "      \"sourceWasCompressedAklz\": " << boolText(scan.sourceWasCompressedAklz) << ",\n";
        out << "      \"headerWords\": [";
        for (std::size_t i = 0; i < scan.headerWords.size(); ++i) {
            if (i != 0U) {
                out << ", ";
            }
            out << scan.headerWords[i];
        }
        out << "],\n";
        out << "      \"recordCountCandidate\": " << scan.recordCountCandidate << ",\n";
        out << "      \"selectedRecordCount\": " << scan.selectedRecordCount << ",\n";
        out << "      \"recordCountSource\": ";
        writeJsonString(out, toString(scan.recordCountSource));
        out << ",\n";
        out << "      \"firstPayloadOffset\": " << scan.firstPayloadOffset << ",\n";
        out << "      \"recordCountInferredFromFirstPayloadOffset\": "
            << scan.recordCountInferredFromFirstPayloadOffset << ",\n";
        out << "      \"recordCountMatchesFirstPayloadOffset\": "
            << boolText(scan.recordCountMatchesFirstPayloadOffset) << ",\n";
        out << "      \"recordTableEndOffset\": " << scan.recordTableEndOffset << ",\n";
        out << "      \"recordTableInBounds\": " << boolText(scan.recordTableInBounds) << ",\n";
        out << "      \"infoCount\": " << diagnosticCount(scan, DiagnosticSeverity::Info) << ",\n";
        out << "      \"warningCount\": " << diagnosticCount(scan, DiagnosticSeverity::Warning) << ",\n";
        out << "      \"errorCount\": " << diagnosticCount(scan, DiagnosticSeverity::Error) << ",\n";
        out << "      \"embeddedMldPlausibleRecordCount\": " << plausibleEmbeddedMldRecordCount(file) << ",\n";
        out << "      \"embeddedMldEntryListAcceptedCount\": " << parsedEmbeddedMldRecordCount(file) << ",\n";
        out << "      \"payloadOutOfBoundsCount\": " << outOfBoundsPayloadCount(file) << ",\n";
        out << "      \"duplicateKeyCount\": " << duplicateKeyCount(file) << ",\n";
        out << "      \"records\": [\n";
        for (std::size_t recordIndex = 0; recordIndex < file.records.size(); ++recordIndex) {
            const auto& recordSummary = file.records[recordIndex];
            const auto& record = recordSummary.record;
            const auto& embedded = record.embeddedMldHeader;
            const auto& parse = recordSummary.embeddedMldParse;
            out << "        {\n";
            out << "          \"recordIndex\": " << record.index << ",\n";
            out << "          \"recordOffset\": " << record.recordOffset << ",\n";
            out << "          \"key\": " << record.key << ",\n";
            out << "          \"payloadOffset\": " << record.payloadOffset << ",\n";
            out << "          \"payloadSize\": " << record.payloadSize << ",\n";
            out << "          \"rawWord12\": " << record.rawWord12 << ",\n";
            out << "          \"payloadInBounds\": " << boolText(record.payloadInBounds) << ",\n";
            out << "          \"payloadOverlapsRecordTable\": " << boolText(record.payloadOverlapsRecordTable) << ",\n";
            out << "          \"duplicateKey\": " << boolText(record.duplicateKey) << ",\n";
            out << "          \"payloadKind\": ";
            writeJsonString(out, toString(record.payloadKind));
            out << ",\n";
            out << "          \"payloadSignature\": ";
            writeJsonString(out, record.payloadSignature);
            out << ",\n";
            out << "          \"embeddedMldHeaderPlausible\": " << boolText(embedded.plausible) << ",\n";
            out << "          \"embeddedMldEntryCount\": " << embedded.entryCount << ",\n";
            out << "          \"embeddedMldIndexTableOffset\": " << embedded.indexTableOffset << ",\n";
            out << "          \"embeddedMldFunctionParametersOffset\": " << embedded.functionParametersOffset << ",\n";
            out << "          \"embeddedMldRealDataOffset\": " << embedded.realDataOffset << ",\n";
            out << "          \"embeddedMldTextureTableOffset\": " << embedded.textureTableOffset << ",\n";
            out << "          \"embeddedMldParseAttempted\": " << boolText(parse.attempted) << ",\n";
            out << "          \"embeddedMldEntryListParseOk\": " << boolText(parse.parseOk) << ",\n";
            out << "          \"embeddedMldEntryCountParsed\": " << parse.entryCount << ",\n";
            out << "          \"embeddedMldDiagnosticsCount\": " << parse.diagnosticCount << "\n";
            out << "        }";
            if (recordIndex + 1U != file.records.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "      ],\n";
        out << "      \"diagnostics\": [\n";
        for (std::size_t diagnosticIndex = 0; diagnosticIndex < scan.diagnostics.size(); ++diagnosticIndex) {
            const auto& diagnostic = scan.diagnostics[diagnosticIndex];
            out << "        { \"severity\": ";
            writeJsonString(out, toString(diagnostic.severity));
            out << ", \"offset\": " << diagnostic.offset << ", \"message\": ";
            writeJsonString(out, diagnostic.message);
            out << " }";
            if (diagnosticIndex + 1U != scan.diagnostics.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "      ]\n";
        out << "    }";
        if (fileIndex + 1U != corpus.files.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

std::string formatMlkCorpusFilesCsv(const MlkCorpusScanResult& corpus) {
    std::ostringstream out;
    out << "path,rawSize,decodedSize,sourceWasCompressedAklz,headerWord0,headerWord1,headerWord2,headerWord3,"
           "recordCountCandidate,selectedRecordCount,recordCountSource,firstPayloadOffset,"
           "recordCountInferredFromFirstPayloadOffset,recordCountMatchesFirstPayloadOffset,recordTableEndOffset,"
           "recordTableInBounds,infoCount,warningCount,errorCount,embeddedMldPlausibleRecordCount,"
           "embeddedMldEntryListAcceptedCount,payloadOutOfBoundsCount,duplicateKeyCount\n";
    for (const auto& file : corpus.files) {
        const auto& scan = file.scan;
        out << csvEscape(file.relativePath) << ","
            << scan.rawSize << ","
            << scan.decodedSize << ","
            << boolText(scan.sourceWasCompressedAklz) << ","
            << scan.headerWords[0] << ","
            << scan.headerWords[1] << ","
            << scan.headerWords[2] << ","
            << scan.headerWords[3] << ","
            << scan.recordCountCandidate << ","
            << scan.selectedRecordCount << ","
            << csvEscape(toString(scan.recordCountSource)) << ","
            << scan.firstPayloadOffset << ","
            << scan.recordCountInferredFromFirstPayloadOffset << ","
            << boolText(scan.recordCountMatchesFirstPayloadOffset) << ","
            << scan.recordTableEndOffset << ","
            << boolText(scan.recordTableInBounds) << ","
            << diagnosticCount(scan, DiagnosticSeverity::Info) << ","
            << diagnosticCount(scan, DiagnosticSeverity::Warning) << ","
            << diagnosticCount(scan, DiagnosticSeverity::Error) << ","
            << plausibleEmbeddedMldRecordCount(file) << ","
            << parsedEmbeddedMldRecordCount(file) << ","
            << outOfBoundsPayloadCount(file) << ","
            << duplicateKeyCount(file) << "\n";
    }
    return out.str();
}

std::string formatMlkCorpusRecordsCsv(const MlkCorpusScanResult& corpus) {
    std::ostringstream out;
    out << "filePath,recordIndex,recordOffset,key,payloadOffset,payloadSize,rawWord12,payloadInBounds,"
           "payloadOverlapsRecordTable,duplicateKey,payloadKind,payloadSignature,embeddedMldHeaderPlausible,"
           "embeddedMldEntryCount,embeddedMldIndexTableOffset,embeddedMldFunctionParametersOffset,"
           "embeddedMldRealDataOffset,embeddedMldTextureTableOffset,embeddedMldParseAttempted,"
           "embeddedMldEntryListParseOk,embeddedMldEntryCountParsed,embeddedMldDiagnosticsCount\n";
    for (const auto& file : corpus.files) {
        for (const auto& recordSummary : file.records) {
            const auto& record = recordSummary.record;
            const auto& embedded = record.embeddedMldHeader;
            const auto& parse = recordSummary.embeddedMldParse;
            out << csvEscape(file.relativePath) << ","
                << record.index << ","
                << record.recordOffset << ","
                << record.key << ","
                << record.payloadOffset << ","
                << record.payloadSize << ","
                << record.rawWord12 << ","
                << boolText(record.payloadInBounds) << ","
                << boolText(record.payloadOverlapsRecordTable) << ","
                << boolText(record.duplicateKey) << ","
                << csvEscape(toString(record.payloadKind)) << ","
                << csvEscape(record.payloadSignature) << ","
                << boolText(embedded.plausible) << ","
                << embedded.entryCount << ","
                << embedded.indexTableOffset << ","
                << embedded.functionParametersOffset << ","
                << embedded.realDataOffset << ","
                << embedded.textureTableOffset << ","
                << boolText(parse.attempted) << ","
                << boolText(parse.parseOk) << ","
                << parse.entryCount << ","
                << parse.diagnosticCount << "\n";
        }
    }
    return out.str();
}

std::string formatMlkCorpusWord12HistogramCsv(const MlkCorpusScanResult& corpus) {
    std::map<std::tuple<std::string, std::string, std::uint32_t>, std::size_t> histogram{};
    for (const auto& file : corpus.files) {
        const auto directory = parentDirectoryForHistogram(file.relativePath);
        const auto source = std::string(toString(file.scan.recordCountSource));
        for (const auto& record : file.records) {
            ++histogram[std::make_tuple(directory, source, record.record.rawWord12)];
        }
    }

    std::ostringstream out;
    out << "directory,recordCountSource,rawWord12,count\n";
    for (const auto& [key, count] : histogram) {
        const auto& [directory, source, rawWord12] = key;
        out << csvEscape(directory) << ","
            << csvEscape(source) << ","
            << rawWord12 << ","
            << count << "\n";
    }
    return out.str();
}

MlkCorpusWriteResult writeMlkCorpusArtifacts(
    const MlkCorpusScanResult& corpus,
    const std::filesystem::path& outputDir) {
    std::filesystem::create_directories(outputDir);

    MlkCorpusWriteResult result{};
    result.jsonPath = outputDir / "mlk_corpus.json";
    result.filesCsvPath = outputDir / "mlk_corpus_files.csv";
    result.recordsCsvPath = outputDir / "mlk_corpus_records.csv";
    result.word12HistogramCsvPath = outputDir / "mlk_corpus_word12_histogram.csv";

    writeTextFile(result.jsonPath, formatMlkCorpusJson(corpus));
    writeTextFile(result.filesCsvPath, formatMlkCorpusFilesCsv(corpus));
    writeTextFile(result.recordsCsvPath, formatMlkCorpusRecordsCsv(corpus));
    writeTextFile(result.word12HistogramCsvPath, formatMlkCorpusWord12HistogramCsv(corpus));
    return result;
}

} // namespace spice::mlk
