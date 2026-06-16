#include "MllCorpus.h"

#include "MllParser.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>

namespace spice::mll {
namespace {

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool isMllPath(const std::filesystem::path& path) {
    return toLowerCopy(path.extension().string()) == ".mll";
}

std::vector<std::filesystem::path> collectMllPaths(const std::filesystem::path& inputPath, bool& inputWasDirectory) {
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
        if (isMllPath(inputPath)) {
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
        if (isMllPath(entry.path())) {
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

std::size_t diagnosticCount(const MllFile& file, DiagnosticSeverity severity) {
    return static_cast<std::size_t>(std::count_if(file.diagnostics.begin(), file.diagnostics.end(), [&](const auto& diagnostic) {
        return diagnostic.severity == severity;
    }));
}

std::size_t mldLikeMemberCount(const MllCorpusFileSummary& file) {
    return static_cast<std::size_t>(std::count_if(file.members.begin(), file.members.end(), [](const auto& member) {
        return member.member.embeddedMldHeader.plausible || member.member.payloadKind == MllPayloadKind::MldFile;
    }));
}

std::size_t mldIndexShapeMemberCount(const MllCorpusFileSummary& file) {
    return static_cast<std::size_t>(std::count_if(file.members.begin(), file.members.end(), [](const auto& member) {
        return member.member.embeddedMldHeader.indexEntryShapePlausible;
    }));
}

std::size_t outOfBoundsMemberCount(const MllCorpusFileSummary& file) {
    return static_cast<std::size_t>(std::count_if(file.members.begin(), file.members.end(), [](const auto& member) {
        return !member.member.payloadInBounds;
    }));
}

std::size_t mldObjectListProbeCount(const MllCorpusFileSummary& file) {
    std::size_t count = 0U;
    for (const auto& member : file.members) {
        count += member.member.embeddedMldObjectListProbes.size();
    }
    return count;
}

std::size_t nonZeroMldObjectListProbeCount(const MllCorpusFileSummary& file) {
    std::size_t count = 0U;
    for (const auto& member : file.members) {
        count += static_cast<std::size_t>(std::count_if(
            member.member.embeddedMldObjectListProbes.begin(),
            member.member.embeddedMldObjectListProbes.end(),
            [](const auto& probe) {
                return probe.listOffsetNonZero;
            }));
    }
    return count;
}

std::size_t plausibleMldObjectListProbeCount(const MllCorpusFileSummary& file) {
    std::size_t count = 0U;
    for (const auto& member : file.members) {
        count += static_cast<std::size_t>(std::count_if(
            member.member.embeddedMldObjectListProbes.begin(),
            member.member.embeddedMldObjectListProbes.end(),
            [](const auto& probe) {
                return probe.listLooksPlausible;
            }));
    }
    return count;
}

std::size_t mldBlockProbeCount(const MllCorpusFileSummary& file) {
    std::size_t count = 0U;
    for (const auto& member : file.members) {
        count += member.member.embeddedBlockProbes.size();
    }
    return count;
}

std::size_t exactReferencedMldBlockProbeCount(const MllCorpusFileSummary& file) {
    std::size_t count = 0U;
    for (const auto& member : file.members) {
        count += static_cast<std::size_t>(std::count_if(
            member.member.embeddedBlockProbes.begin(),
            member.member.embeddedBlockProbes.end(),
            [](const auto& probe) {
                return probe.exactCountedListReferenceCount != 0U;
            }));
    }
    return count;
}

std::size_t gvrTextureProbeCount(const MllCorpusFileSummary& file) {
    std::size_t count = 0U;
    for (const auto& member : file.members) {
        count += member.member.embeddedGvrTextureProbes.size();
    }
    return count;
}

std::size_t parsedGvrTextureProbeCount(const MllCorpusFileSummary& file) {
    std::size_t count = 0U;
    for (const auto& member : file.members) {
        count += static_cast<std::size_t>(std::count_if(
            member.member.embeddedGvrTextureProbes.begin(),
            member.member.embeddedGvrTextureProbes.end(),
            [](const auto& probe) {
                return probe.parseAttempted &&
                    probe.recordInBounds &&
                    !probe.parseHasFailureDiagnostics &&
                    probe.width != 0U &&
                    probe.height != 0U &&
                    probe.textureFormat != "Unknown";
            }));
    }
    return count;
}

std::size_t failedGvrTextureProbeCount(const MllCorpusFileSummary& file) {
    std::size_t count = 0U;
    for (const auto& member : file.members) {
        count += static_cast<std::size_t>(std::count_if(
            member.member.embeddedGvrTextureProbes.begin(),
            member.member.embeddedGvrTextureProbes.end(),
            [](const auto& probe) {
                return !probe.parseAttempted || probe.parseHasFailureDiagnostics;
            }));
    }
    return count;
}

std::size_t decodedGvrTextureProbeCount(const MllCorpusFileSummary& file) {
    std::size_t count = 0U;
    for (const auto& member : file.members) {
        count += static_cast<std::size_t>(std::count_if(
            member.member.embeddedGvrTextureProbes.begin(),
            member.member.embeddedGvrTextureProbes.end(),
            [](const auto& probe) {
                return probe.decodedBaseLevelPresent;
            }));
    }
    return count;
}

std::size_t textureMemberProbeCount(const MllCorpusFileSummary& file) {
    return static_cast<std::size_t>(std::count_if(file.members.begin(), file.members.end(), [](const auto& member) {
        return member.member.textureTableProbe.hasTextures;
    }));
}

std::size_t denseGlobalIndexTextureMemberCount(const MllCorpusFileSummary& file) {
    return static_cast<std::size_t>(std::count_if(file.members.begin(), file.members.end(), [](const auto& member) {
        return member.member.textureTableProbe.hasTextures &&
            member.member.textureTableProbe.globalIndexSequenceDense;
    }));
}

std::size_t textureClusterCount(const MllCorpusFileSummary& file) {
    std::size_t count = 0U;
    for (const auto& member : file.members) {
        count += member.member.textureTableProbe.clusterCount;
    }
    return count;
}

std::size_t headerTextureTableBeforeFirstTextureCount(const MllCorpusFileSummary& file) {
    return static_cast<std::size_t>(std::count_if(file.members.begin(), file.members.end(), [](const auto& member) {
        return member.member.textureTableProbe.headerTextureTableOffsetBeforeFirstTexture;
    }));
}

std::size_t indexTexturePointerInsideTextureSpanCount(const MllCorpusFileSummary& file) {
    std::size_t count = 0U;
    for (const auto& member : file.members) {
        count += member.member.textureTableProbe.indexTexturePointerInsideTextureSpanCount;
    }
    return count;
}

std::size_t preTextureTableProbeCount(const MllCorpusFileSummary& file) {
    return static_cast<std::size_t>(std::count_if(file.members.begin(), file.members.end(), [](const auto& member) {
        return member.member.preTextureTableProbe.present;
    }));
}

std::size_t preTextureTableAlignedProbeCount(const MllCorpusFileSummary& file) {
    return static_cast<std::size_t>(std::count_if(file.members.begin(), file.members.end(), [](const auto& member) {
        return member.member.preTextureTableProbe.present &&
            member.member.preTextureTableProbe.spanInBounds &&
            member.member.preTextureTableProbe.spanAlignedTo20;
    }));
}

std::size_t preTextureTableEntryCount(const MllCorpusFileSummary& file) {
    std::size_t count = 0U;
    for (const auto& member : file.members) {
        count += member.member.preTextureTableProbe.entryCount;
    }
    return count;
}

std::size_t preTextureTablePrintableNameCount(const MllCorpusFileSummary& file) {
    std::size_t count = 0U;
    for (const auto& member : file.members) {
        count += member.member.preTextureTableProbe.printableNameCount;
    }
    return count;
}

std::size_t preTextureTableTextureNameMatchCount(const MllCorpusFileSummary& file) {
    std::size_t count = 0U;
    for (const auto& member : file.members) {
        count += member.member.preTextureTableProbe.textureNameMatchCount;
    }
    return count;
}

std::size_t indexedBinTableProbeCount(const MllCorpusFileSummary& file) {
    return static_cast<std::size_t>(std::count_if(file.members.begin(), file.members.end(), [](const auto& member) {
        return member.member.indexedBinTableProbe.present;
    }));
}

std::size_t plausibleIndexedBinTableProbeCount(const MllCorpusFileSummary& file) {
    return static_cast<std::size_t>(std::count_if(file.members.begin(), file.members.end(), [](const auto& member) {
        const auto& probe = member.member.indexedBinTableProbe;
        return probe.present &&
            probe.offsetTableInBounds &&
            probe.offsetsInBounds &&
            probe.offsetsMonotonic;
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

MllCorpusScanResult scanMllCorpus(const std::filesystem::path& inputPath) {
    MllCorpusScanResult corpus{};
    corpus.inputPath = inputPath.string();
    const auto paths = collectMllPaths(inputPath, corpus.inputWasDirectory);
    corpus.files.reserve(paths.size());
    for (const auto& path : paths) {
        MllCorpusFileSummary file{};
        file.relativePath = relativePathString(path, inputPath, corpus.inputWasDirectory);
        file.absolutePath = std::filesystem::absolute(path).string();
        file.file = MllParser::parseFile(path);
        file.file.sourcePath = file.relativePath;
        file.members.reserve(file.file.members.size());
        for (const auto& member : file.file.members) {
            file.members.push_back(MllCorpusMemberSummary{ member });
        }
        corpus.files.push_back(std::move(file));
    }
    return corpus;
}

MllCorpusFeedbackSummary summarizeMllCorpusFeedback(const MllCorpusScanResult& corpus) {
    MllCorpusFeedbackSummary summary{};
    summary.fileCount = corpus.files.size();
    for (const auto& file : corpus.files) {
        if (file.file.supported) {
            ++summary.supportedFileCount;
        }
        switch (file.file.tableShape) {
        case MllTableShape::Normal:
            ++summary.normalShapeCount;
            break;
        case MllTableShape::FirstMemberCountCandidate:
            ++summary.firstMemberCountCandidateCount;
            break;
        case MllTableShape::MalformedMemberSpans:
            ++summary.malformedShapeCount;
            break;
        }
        summary.totalMemberCount += file.members.size();
        summary.mldLikeMemberCount += mldLikeMemberCount(file);
        summary.mldIndexShapeMemberCount += mldIndexShapeMemberCount(file);
        summary.mldObjectListProbeCount += mldObjectListProbeCount(file);
        summary.nonZeroMldObjectListProbeCount += nonZeroMldObjectListProbeCount(file);
        summary.plausibleMldObjectListProbeCount += plausibleMldObjectListProbeCount(file);
        summary.mldBlockProbeCount += mldBlockProbeCount(file);
        summary.exactReferencedMldBlockProbeCount += exactReferencedMldBlockProbeCount(file);
        summary.gvrTextureProbeCount += gvrTextureProbeCount(file);
        summary.parsedGvrTextureProbeCount += parsedGvrTextureProbeCount(file);
        summary.failedGvrTextureProbeCount += failedGvrTextureProbeCount(file);
        summary.decodedGvrTextureProbeCount += decodedGvrTextureProbeCount(file);
        summary.textureMemberProbeCount += textureMemberProbeCount(file);
        summary.denseGlobalIndexTextureMemberCount += denseGlobalIndexTextureMemberCount(file);
        summary.textureClusterCount += textureClusterCount(file);
        summary.headerTextureTableBeforeFirstTextureCount += headerTextureTableBeforeFirstTextureCount(file);
        summary.indexTexturePointerInsideTextureSpanCount += indexTexturePointerInsideTextureSpanCount(file);
        summary.preTextureTableProbeCount += preTextureTableProbeCount(file);
        summary.preTextureTableAlignedProbeCount += preTextureTableAlignedProbeCount(file);
        summary.preTextureTableEntryCount += preTextureTableEntryCount(file);
        summary.preTextureTablePrintableNameCount += preTextureTablePrintableNameCount(file);
        summary.preTextureTableTextureNameMatchCount += preTextureTableTextureNameMatchCount(file);
        summary.indexedBinTableProbeCount += indexedBinTableProbeCount(file);
        summary.plausibleIndexedBinTableProbeCount += plausibleIndexedBinTableProbeCount(file);
        summary.outOfBoundsMemberCount += outOfBoundsMemberCount(file);
        summary.warningCount += diagnosticCount(file.file, DiagnosticSeverity::Warning);
        summary.errorCount += diagnosticCount(file.file, DiagnosticSeverity::Error);
    }
    return summary;
}

std::string formatMllCorpusJson(const MllCorpusScanResult& corpus) {
    std::ostringstream out;
    const auto summary = summarizeMllCorpusFeedback(corpus);
    out << "{\n";
    out << "  \"inputPath\": ";
    writeJsonString(out, corpus.inputPath);
    out << ",\n";
    out << "  \"inputWasDirectory\": " << boolText(corpus.inputWasDirectory) << ",\n";
    out << "  \"fileCount\": " << summary.fileCount << ",\n";
    out << "  \"supportedFileCount\": " << summary.supportedFileCount << ",\n";
    out << "  \"normalShapeCount\": " << summary.normalShapeCount << ",\n";
    out << "  \"firstMemberCountCandidateCount\": " << summary.firstMemberCountCandidateCount << ",\n";
    out << "  \"malformedShapeCount\": " << summary.malformedShapeCount << ",\n";
    out << "  \"totalMemberCount\": " << summary.totalMemberCount << ",\n";
    out << "  \"mldLikeMemberCount\": " << summary.mldLikeMemberCount << ",\n";
    out << "  \"mldIndexShapeMemberCount\": " << summary.mldIndexShapeMemberCount << ",\n";
    out << "  \"mldObjectListProbeCount\": " << summary.mldObjectListProbeCount << ",\n";
    out << "  \"nonZeroMldObjectListProbeCount\": " << summary.nonZeroMldObjectListProbeCount << ",\n";
    out << "  \"plausibleMldObjectListProbeCount\": " << summary.plausibleMldObjectListProbeCount << ",\n";
    out << "  \"mldBlockProbeCount\": " << summary.mldBlockProbeCount << ",\n";
    out << "  \"exactReferencedMldBlockProbeCount\": " << summary.exactReferencedMldBlockProbeCount << ",\n";
    out << "  \"gvrTextureProbeCount\": " << summary.gvrTextureProbeCount << ",\n";
    out << "  \"parsedGvrTextureProbeCount\": " << summary.parsedGvrTextureProbeCount << ",\n";
    out << "  \"failedGvrTextureProbeCount\": " << summary.failedGvrTextureProbeCount << ",\n";
    out << "  \"decodedGvrTextureProbeCount\": " << summary.decodedGvrTextureProbeCount << ",\n";
    out << "  \"textureMemberProbeCount\": " << summary.textureMemberProbeCount << ",\n";
    out << "  \"denseGlobalIndexTextureMemberCount\": " << summary.denseGlobalIndexTextureMemberCount << ",\n";
    out << "  \"textureClusterCount\": " << summary.textureClusterCount << ",\n";
    out << "  \"headerTextureTableBeforeFirstTextureCount\": " << summary.headerTextureTableBeforeFirstTextureCount << ",\n";
    out << "  \"indexTexturePointerInsideTextureSpanCount\": " << summary.indexTexturePointerInsideTextureSpanCount << ",\n";
    out << "  \"preTextureTableProbeCount\": " << summary.preTextureTableProbeCount << ",\n";
    out << "  \"preTextureTableAlignedProbeCount\": " << summary.preTextureTableAlignedProbeCount << ",\n";
    out << "  \"preTextureTableEntryCount\": " << summary.preTextureTableEntryCount << ",\n";
    out << "  \"preTextureTablePrintableNameCount\": " << summary.preTextureTablePrintableNameCount << ",\n";
    out << "  \"preTextureTableTextureNameMatchCount\": " << summary.preTextureTableTextureNameMatchCount << ",\n";
    out << "  \"indexedBinTableProbeCount\": " << summary.indexedBinTableProbeCount << ",\n";
    out << "  \"plausibleIndexedBinTableProbeCount\": " << summary.plausibleIndexedBinTableProbeCount << ",\n";
    out << "  \"outOfBoundsMemberCount\": " << summary.outOfBoundsMemberCount << ",\n";
    out << "  \"warningCount\": " << summary.warningCount << ",\n";
    out << "  \"errorCount\": " << summary.errorCount << ",\n";
    out << "  \"files\": [\n";
    for (std::size_t fileIndex = 0; fileIndex < corpus.files.size(); ++fileIndex) {
        const auto& file = corpus.files[fileIndex];
        out << "    {\n";
        out << "      \"path\": ";
        writeJsonString(out, file.relativePath);
        out << ",\n";
        out << "      \"absolutePath\": ";
        writeJsonString(out, file.absolutePath);
        out << ",\n";
        out << "      \"rawSize\": " << file.file.rawSize << ",\n";
        out << "      \"decodedSize\": " << file.file.decodedSize << ",\n";
        out << "      \"sourceWasCompressedAklz\": " << boolText(file.file.sourceWasCompressedAklz) << ",\n";
        out << "      \"headerWord0\": " << file.file.headerWord0 << ",\n";
        out << "      \"countWord\": " << file.file.countWord << ",\n";
        out << "      \"memberCountCandidate\": " << file.file.memberCountCandidate << ",\n";
        out << "      \"selectedMemberCount\": " << file.file.selectedMemberCount << ",\n";
        out << "      \"memberCountSource\": ";
        writeJsonString(out, toString(file.file.memberCountSource));
        out << ",\n";
        out << "      \"tableShape\": ";
        writeJsonString(out, toString(file.file.tableShape));
        out << ",\n";
        out << "      \"firstMemberOffset\": " << file.file.firstMemberOffset << ",\n";
        out << "      \"memberCountInferredFromFirstMemberOffset\": " << file.file.memberCountInferredFromFirstMemberOffset << ",\n";
        out << "      \"memberCountMatchesFirstMemberOffset\": " << boolText(file.file.memberCountMatchesFirstMemberOffset) << ",\n";
        out << "      \"memberTableEndOffset\": " << file.file.memberTableEndOffset << ",\n";
        out << "      \"memberTableInBounds\": " << boolText(file.file.memberTableInBounds) << ",\n";
        out << "      \"supported\": " << boolText(file.file.supported) << ",\n";
        out << "      \"warningCount\": " << diagnosticCount(file.file, DiagnosticSeverity::Warning) << ",\n";
        out << "      \"errorCount\": " << diagnosticCount(file.file, DiagnosticSeverity::Error) << ",\n";
        out << "      \"mldLikeMemberCount\": " << mldLikeMemberCount(file) << ",\n";
        out << "      \"mldIndexShapeMemberCount\": " << mldIndexShapeMemberCount(file) << ",\n";
        out << "      \"mldObjectListProbeCount\": " << mldObjectListProbeCount(file) << ",\n";
        out << "      \"nonZeroMldObjectListProbeCount\": " << nonZeroMldObjectListProbeCount(file) << ",\n";
        out << "      \"plausibleMldObjectListProbeCount\": " << plausibleMldObjectListProbeCount(file) << ",\n";
        out << "      \"mldBlockProbeCount\": " << mldBlockProbeCount(file) << ",\n";
        out << "      \"exactReferencedMldBlockProbeCount\": " << exactReferencedMldBlockProbeCount(file) << ",\n";
        out << "      \"gvrTextureProbeCount\": " << gvrTextureProbeCount(file) << ",\n";
        out << "      \"parsedGvrTextureProbeCount\": " << parsedGvrTextureProbeCount(file) << ",\n";
        out << "      \"failedGvrTextureProbeCount\": " << failedGvrTextureProbeCount(file) << ",\n";
        out << "      \"decodedGvrTextureProbeCount\": " << decodedGvrTextureProbeCount(file) << ",\n";
        out << "      \"textureMemberProbeCount\": " << textureMemberProbeCount(file) << ",\n";
        out << "      \"denseGlobalIndexTextureMemberCount\": " << denseGlobalIndexTextureMemberCount(file) << ",\n";
        out << "      \"textureClusterCount\": " << textureClusterCount(file) << ",\n";
        out << "      \"headerTextureTableBeforeFirstTextureCount\": " << headerTextureTableBeforeFirstTextureCount(file) << ",\n";
        out << "      \"indexTexturePointerInsideTextureSpanCount\": " << indexTexturePointerInsideTextureSpanCount(file) << ",\n";
        out << "      \"preTextureTableProbeCount\": " << preTextureTableProbeCount(file) << ",\n";
        out << "      \"preTextureTableAlignedProbeCount\": " << preTextureTableAlignedProbeCount(file) << ",\n";
        out << "      \"preTextureTableEntryCount\": " << preTextureTableEntryCount(file) << ",\n";
        out << "      \"preTextureTablePrintableNameCount\": " << preTextureTablePrintableNameCount(file) << ",\n";
        out << "      \"preTextureTableTextureNameMatchCount\": " << preTextureTableTextureNameMatchCount(file) << ",\n";
        out << "      \"indexedBinTableProbeCount\": " << indexedBinTableProbeCount(file) << ",\n";
        out << "      \"plausibleIndexedBinTableProbeCount\": " << plausibleIndexedBinTableProbeCount(file) << ",\n";
        out << "      \"outOfBoundsMemberCount\": " << outOfBoundsMemberCount(file) << ",\n";
        out << "      \"members\": [\n";
        for (std::size_t memberIndex = 0; memberIndex < file.members.size(); ++memberIndex) {
            const auto& member = file.members[memberIndex].member;
            out << "        {\n";
            out << "          \"memberIndex\": " << member.index << ",\n";
            out << "          \"recordOffset\": " << member.recordOffset << ",\n";
            out << "          \"name\": ";
            writeJsonString(out, member.name);
            out << ",\n";
            out << "          \"payloadOffset\": " << member.payloadOffset << ",\n";
            out << "          \"payloadSize\": " << member.payloadSize << ",\n";
            out << "          \"rawWord1c\": " << member.rawWord1c << ",\n";
            out << "          \"payloadInBounds\": " << boolText(member.payloadInBounds) << ",\n";
            out << "          \"payloadOverlapsMemberTable\": " << boolText(member.payloadOverlapsMemberTable) << ",\n";
            out << "          \"payloadKind\": ";
            writeJsonString(out, toString(member.payloadKind));
            out << ",\n";
            out << "          \"payloadSignature\": ";
            writeJsonString(out, member.payloadSignature);
            out << ",\n";
            out << "          \"embeddedMldHeaderPlausible\": " << boolText(member.embeddedMldHeader.plausible) << ",\n";
            out << "          \"embeddedMldEntryCount\": " << member.embeddedMldHeader.entryCount << ",\n";
            out << "          \"embeddedMldIndexTableOffset\": " << member.embeddedMldHeader.indexTableOffset << ",\n";
            out << "          \"embeddedMldFunctionParametersOffset\": " << member.embeddedMldHeader.functionParametersOffset << ",\n";
            out << "          \"embeddedMldRealDataOffset\": " << member.embeddedMldHeader.realDataOffset << ",\n";
            out << "          \"embeddedMldTextureTableOffset\": " << member.embeddedMldHeader.textureTableOffset << ",\n";
            out << "          \"embeddedMldIndexEntryShapePlausible\": " << boolText(member.embeddedMldHeader.indexEntryShapePlausible) << ",\n";
            out << "          \"embeddedMldPrintableFunctionNameCount\": " << member.embeddedMldHeader.printableFunctionNameCount << ",\n";
            out << "          \"embeddedMldEmptyFunctionNameCount\": " << member.embeddedMldHeader.emptyFunctionNameCount << ",\n";
            out << "          \"embeddedMldSuspiciousFunctionNameCount\": " << member.embeddedMldHeader.suspiciousFunctionNameCount << ",\n";
            out << "          \"embeddedMldNonZeroCountedListFieldCount\": " << member.embeddedMldHeader.nonZeroCountedListFieldCount << ",\n";
            out << "          \"embeddedMldPlausibleCountedListFieldCount\": " << member.embeddedMldHeader.plausibleCountedListFieldCount << ",\n";
            out << "          \"embeddedMldObjectListProbes\": [\n";
            for (std::size_t probeIndex = 0; probeIndex < member.embeddedMldObjectListProbes.size(); ++probeIndex) {
                const auto& probe = member.embeddedMldObjectListProbes[probeIndex];
                out << "            {\n";
                out << "              \"entryIndex\": " << probe.entryIndex << ",\n";
                out << "              \"entryOffset\": " << probe.entryOffset << ",\n";
                out << "              \"fieldOffset\": " << probe.fieldOffset << ",\n";
                out << "              \"listOffset\": " << probe.listOffset << ",\n";
                out << "              \"listOffsetNonZero\": " << boolText(probe.listOffsetNonZero) << ",\n";
                out << "              \"listOffsetAligned\": " << boolText(probe.listOffsetAligned) << ",\n";
                out << "              \"listHeaderInBounds\": " << boolText(probe.listHeaderInBounds) << ",\n";
                out << "              \"declaredCount\": " << probe.declaredCount << ",\n";
                out << "              \"listEntriesInBounds\": " << boolText(probe.listEntriesInBounds) << ",\n";
                out << "              \"listLooksPlausible\": " << boolText(probe.listLooksPlausible) << ",\n";
                out << "              \"listBytes32Hex\": ";
                writeJsonString(out, probe.listBytes32Hex);
                out << ",\n";
                out << "              \"sampledPointerCount\": " << probe.sampledPointerCount << ",\n";
                out << "              \"nonNullSampledPointerCount\": " << probe.nonNullSampledPointerCount << ",\n";
                out << "              \"targetSamples\": [\n";
                for (std::size_t sampleIndex = 0; sampleIndex < probe.targetSamples.size(); ++sampleIndex) {
                    const auto& sample = probe.targetSamples[sampleIndex];
                    out << "                { \"sampleIndex\": " << sample.sampleIndex
                        << ", \"pointerOffset\": " << sample.pointerOffset
                        << ", \"targetOffset\": " << sample.targetOffset
                        << ", \"pointerNonZero\": " << boolText(sample.pointerNonZero)
                        << ", \"targetOffsetAligned\": " << boolText(sample.targetOffsetAligned)
                        << ", \"targetInBounds\": " << boolText(sample.targetInBounds)
                        << ", \"targetLooksPlausible\": " << boolText(sample.targetLooksPlausible)
                        << ", \"listBaseTargetOffset\": " << sample.listBaseTargetOffset
                        << ", \"listBaseTargetLooksPlausible\": " << boolText(sample.listBaseTargetLooksPlausible)
                        << ", \"entryBaseTargetOffset\": " << sample.entryBaseTargetOffset
                        << ", \"entryBaseTargetLooksPlausible\": " << boolText(sample.entryBaseTargetLooksPlausible)
                        << ", \"pointerBaseTargetOffset\": " << sample.pointerBaseTargetOffset
                        << ", \"pointerBaseTargetLooksPlausible\": " << boolText(sample.pointerBaseTargetLooksPlausible)
                        << ", \"targetWord0\": " << sample.targetWord0
                        << ", \"targetSignature\": ";
                    writeJsonString(out, sample.targetSignature);
                    out << ", \"targetBytes16Hex\": ";
                    writeJsonString(out, sample.targetBytes16Hex);
                    out << " }";
                    if (sampleIndex + 1U != probe.targetSamples.size()) {
                        out << ",";
                    }
                    out << "\n";
                }
                out << "              ]\n";
                out << "            }";
                if (probeIndex + 1U != member.embeddedMldObjectListProbes.size()) {
                    out << ",";
                }
                out << "\n";
            }
            out << "          ],\n";
            out << "          \"embeddedBlockProbes\": [\n";
            for (std::size_t blockIndex = 0; blockIndex < member.embeddedBlockProbes.size(); ++blockIndex) {
                const auto& block = member.embeddedBlockProbes[blockIndex];
                out << "            { \"blockOffset\": " << block.blockOffset
                    << ", \"tag\": ";
                writeJsonString(out, block.tag);
                out << ", \"offsetAligned\": " << boolText(block.offsetAligned)
                    << ", \"declaredSizeBe\": " << block.declaredSizeBe
                    << ", \"declaredSizeBePlausible\": " << boolText(block.declaredSizeBePlausible)
                    << ", \"declaredSizeLe\": " << block.declaredSizeLe
                    << ", \"declaredSizeLePlausible\": " << boolText(block.declaredSizeLePlausible)
                    << ", \"atHeaderRealDataOffset\": " << boolText(block.atHeaderRealDataOffset)
                    << ", \"atHeaderTextureTableOffset\": " << boolText(block.atHeaderTextureTableOffset)
                    << ", \"exactCountedListReferenceCount\": " << block.exactCountedListReferenceCount
                    << ", \"plus8CountedListReferenceCount\": " << block.plus8CountedListReferenceCount
                    << ", \"minus8CountedListReferenceCount\": " << block.minus8CountedListReferenceCount
                    << ", \"firstExactCountedListReference\": ";
                writeJsonString(out, block.firstExactCountedListReference);
                out << ", \"bytes32Hex\": ";
                writeJsonString(out, block.bytes32Hex);
                out << " }";
                if (blockIndex + 1U != member.embeddedBlockProbes.size()) {
                    out << ",";
                }
                out << "\n";
            }
            out << "          ],\n";
            out << "          \"embeddedGvrTextureProbes\": [\n";
            for (std::size_t textureIndex = 0; textureIndex < member.embeddedGvrTextureProbes.size(); ++textureIndex) {
                const auto& texture = member.embeddedGvrTextureProbes[textureIndex];
                out << "            { \"textureIndex\": " << texture.textureIndex
                    << ", \"gcixOffset\": " << texture.gcixOffset
                    << ", \"gvrtOffset\": " << texture.gvrtOffset
                    << ", \"pairDistance\": " << texture.pairDistance
                    << ", \"gcixPayloadSizeLe\": " << texture.gcixPayloadSizeLe
                    << ", \"gvrtPayloadSizeLe\": " << texture.gvrtPayloadSizeLe
                    << ", \"sourceSize\": " << texture.sourceSize
                    << ", \"recordInBounds\": " << boolText(texture.recordInBounds)
                    << ", \"parseAttempted\": " << boolText(texture.parseAttempted)
                    << ", \"parseHasFailureDiagnostics\": " << boolText(texture.parseHasFailureDiagnostics)
                    << ", \"hasGlobalIndex\": " << boolText(texture.hasGlobalIndex)
                    << ", \"globalIndex\": " << texture.globalIndex
                    << ", \"rawFlags\": " << texture.rawFlags
                    << ", \"rawDataFormat\": " << texture.rawDataFormat
                    << ", \"textureFormat\": ";
                writeJsonString(out, texture.textureFormat);
                out << ", \"paletteFormat\": ";
                writeJsonString(out, texture.paletteFormat);
                out << ", \"hasMipmaps\": " << boolText(texture.hasMipmaps)
                    << ", \"hasInternalPalette\": " << boolText(texture.hasInternalPalette)
                    << ", \"width\": " << texture.width
                    << ", \"height\": " << texture.height
                    << ", \"imageDataOffset\": " << texture.imageDataOffset
                    << ", \"imageDataSize\": " << texture.imageDataSize
                    << ", \"decodedBaseLevelPresent\": " << boolText(texture.decodedBaseLevelPresent)
                    << ", \"decodedRgba8Size\": " << texture.decodedRgba8Size
                    << ", \"diagnosticCount\": " << texture.diagnosticCount
                    << ", \"diagnostics\": ";
                writeJsonString(out, texture.diagnosticsJoined);
                out << " }";
                if (textureIndex + 1U != member.embeddedGvrTextureProbes.size()) {
                    out << ",";
                }
                out << "\n";
            }
            out << "          ],\n";
            const auto& textureProbe = member.textureTableProbe;
            out << "          \"textureTableProbe\": {\n";
            out << "            \"hasTextures\": " << boolText(textureProbe.hasTextures) << ",\n";
            out << "            \"textureCount\": " << textureProbe.textureCount << ",\n";
            out << "            \"firstTextureOffset\": " << textureProbe.firstTextureOffset << ",\n";
            out << "            \"lastTextureEndOffset\": " << textureProbe.lastTextureEndOffset << ",\n";
            out << "            \"textureSpanSize\": " << textureProbe.textureSpanSize << ",\n";
            out << "            \"clusterCount\": " << textureProbe.clusterCount << ",\n";
            out << "            \"largestGapBetweenRecords\": " << textureProbe.largestGapBetweenRecords << ",\n";
            out << "            \"allRecordsInBounds\": " << boolText(textureProbe.allRecordsInBounds) << ",\n";
            out << "            \"allTexturesParsed\": " << boolText(textureProbe.allTexturesParsed) << ",\n";
            out << "            \"allTexturesDecoded\": " << boolText(textureProbe.allTexturesDecoded) << ",\n";
            out << "            \"allTexturesHaveGlobalIndex\": " << boolText(textureProbe.allTexturesHaveGlobalIndex) << ",\n";
            out << "            \"globalIndexMin\": " << textureProbe.globalIndexMin << ",\n";
            out << "            \"globalIndexMax\": " << textureProbe.globalIndexMax << ",\n";
            out << "            \"uniqueGlobalIndexCount\": " << textureProbe.uniqueGlobalIndexCount << ",\n";
            out << "            \"duplicateGlobalIndexCount\": " << textureProbe.duplicateGlobalIndexCount << ",\n";
            out << "            \"missingGlobalIndexCount\": " << textureProbe.missingGlobalIndexCount << ",\n";
            out << "            \"globalIndexSequenceDense\": " << boolText(textureProbe.globalIndexSequenceDense) << ",\n";
            out << "            \"globalIndexSequenceStartsAtZero\": " << boolText(textureProbe.globalIndexSequenceStartsAtZero) << ",\n";
            out << "            \"globalIndexSequencePreview\": ";
            writeJsonString(out, textureProbe.globalIndexSequencePreview);
            out << ",\n";
            out << "            \"headerTextureTableOffsetNonZero\": " << boolText(textureProbe.headerTextureTableOffsetNonZero) << ",\n";
            out << "            \"headerTextureTableOffsetAtFirstTexture\": " << boolText(textureProbe.headerTextureTableOffsetAtFirstTexture) << ",\n";
            out << "            \"headerTextureTableOffsetInsideTextureSpan\": " << boolText(textureProbe.headerTextureTableOffsetInsideTextureSpan) << ",\n";
            out << "            \"headerTextureTableOffsetBeforeFirstTexture\": " << boolText(textureProbe.headerTextureTableOffsetBeforeFirstTexture) << ",\n";
            out << "            \"headerTextureTableOffsetDeltaToFirstTexture\": " << textureProbe.headerTextureTableOffsetDeltaToFirstTexture << ",\n";
            out << "            \"headerRealDataOffsetNonZero\": " << boolText(textureProbe.headerRealDataOffsetNonZero) << ",\n";
            out << "            \"headerRealDataOffsetAtFirstTexture\": " << boolText(textureProbe.headerRealDataOffsetAtFirstTexture) << ",\n";
            out << "            \"headerRealDataOffsetInsideTextureSpan\": " << boolText(textureProbe.headerRealDataOffsetInsideTextureSpan) << ",\n";
            out << "            \"headerRealDataOffsetBeforeFirstTexture\": " << boolText(textureProbe.headerRealDataOffsetBeforeFirstTexture) << ",\n";
            out << "            \"headerRealDataOffsetDeltaToFirstTexture\": " << textureProbe.headerRealDataOffsetDeltaToFirstTexture << ",\n";
            out << "            \"indexTexturePointerCount\": " << textureProbe.indexTexturePointerCount << ",\n";
            out << "            \"nonZeroIndexTexturePointerCount\": " << textureProbe.nonZeroIndexTexturePointerCount << ",\n";
            out << "            \"indexTexturePointerAtFirstTextureCount\": " << textureProbe.indexTexturePointerAtFirstTextureCount << ",\n";
            out << "            \"indexTexturePointerInsideTextureSpanCount\": " << textureProbe.indexTexturePointerInsideTextureSpanCount << ",\n";
            out << "            \"indexTexturePointerBeforeFirstTextureCount\": " << textureProbe.indexTexturePointerBeforeFirstTextureCount << ",\n";
            out << "            \"uniqueIndexTexturePointerCount\": " << textureProbe.uniqueIndexTexturePointerCount << ",\n";
            out << "            \"indexTexturePointerValuesPreview\": ";
            writeJsonString(out, textureProbe.indexTexturePointerValuesPreview);
            out << ",\n";
            out << "            \"nearbyPrintableStrings\": ";
            writeJsonString(out, textureProbe.nearbyPrintableStrings);
            out << "\n";
            out << "          },\n";
            const auto& preTextureTable = member.preTextureTableProbe;
            out << "          \"preTextureTableProbe\": {\n";
            out << "            \"present\": " << boolText(preTextureTable.present) << ",\n";
            out << "            \"spanInBounds\": " << boolText(preTextureTable.spanInBounds) << ",\n";
            out << "            \"spanAlignedTo20\": " << boolText(preTextureTable.spanAlignedTo20) << ",\n";
            out << "            \"recordsFit\": " << boolText(preTextureTable.recordsFit) << ",\n";
            out << "            \"declaredCountMatchesTextureCount\": " << boolText(preTextureTable.declaredCountMatchesTextureCount) << ",\n";
            out << "            \"tableOffset\": " << preTextureTable.tableOffset << ",\n";
            out << "            \"tableEndOffset\": " << preTextureTable.tableEndOffset << ",\n";
            out << "            \"tableSize\": " << preTextureTable.tableSize << ",\n";
            out << "            \"declaredEntryCount\": " << preTextureTable.declaredEntryCount << ",\n";
            out << "            \"entryStride\": " << preTextureTable.entryStride << ",\n";
            out << "            \"entryCount\": " << preTextureTable.entryCount << ",\n";
            out << "            \"trailingPaddingSize\": " << preTextureTable.trailingPaddingSize << ",\n";
            out << "            \"printableNameCount\": " << preTextureTable.printableNameCount << ",\n";
            out << "            \"emptyNameCount\": " << preTextureTable.emptyNameCount << ",\n";
            out << "            \"textureNameMatchCount\": " << preTextureTable.textureNameMatchCount << ",\n";
            out << "            \"entryNamePreview\": ";
            writeJsonString(out, preTextureTable.entryNamePreview);
            out << ",\n";
            out << "            \"entries\": [\n";
            for (std::size_t entryIndex = 0; entryIndex < preTextureTable.entries.size(); ++entryIndex) {
                const auto& entry = preTextureTable.entries[entryIndex];
                out << "              { \"entryIndex\": " << entry.entryIndex
                    << ", \"entryOffset\": " << entry.entryOffset
                    << ", \"name\": ";
                writeJsonString(out, entry.name);
                out << ", \"namePrintable\": " << boolText(entry.namePrintable)
                    << ", \"nameEmpty\": " << boolText(entry.nameEmpty)
                    << ", \"nameMatchesKnownTextureGlobalIndex\": " << boolText(entry.nameMatchesKnownTextureGlobalIndex)
                    << ", \"matchedGlobalIndex\": " << entry.matchedGlobalIndex
                    << ", \"word10\": " << entry.word10
                    << ", \"word14\": " << entry.word14
                    << ", \"word18\": " << entry.word18
                    << ", \"word1c\": " << entry.word1c
                    << ", \"word20\": " << entry.word20
                    << ", \"word24\": " << entry.word24
                    << ", \"word28\": " << entry.word28
                    << ", \"orderTexturePresent\": " << boolText(entry.orderTexturePresent)
                    << ", \"orderTextureIndex\": " << entry.orderTextureIndex
                    << ", \"orderTextureGcixOffset\": " << entry.orderTextureGcixOffset
                    << ", \"orderTextureGvrtOffset\": " << entry.orderTextureGvrtOffset
                    << ", \"orderTextureSourceSize\": " << entry.orderTextureSourceSize
                    << ", \"orderTextureHasGlobalIndex\": " << boolText(entry.orderTextureHasGlobalIndex)
                    << ", \"orderTextureGlobalIndex\": " << entry.orderTextureGlobalIndex
                    << ", \"orderTextureRawFlags\": " << entry.orderTextureRawFlags
                    << ", \"orderTextureRawDataFormat\": " << entry.orderTextureRawDataFormat
                    << ", \"orderTextureFormat\": ";
                writeJsonString(out, entry.orderTextureFormat);
                out << ", \"orderTexturePaletteFormat\": ";
                writeJsonString(out, entry.orderTexturePaletteFormat);
                out << ", \"orderTextureHasMipmaps\": " << boolText(entry.orderTextureHasMipmaps)
                    << ", \"orderTextureHasInternalPalette\": " << boolText(entry.orderTextureHasInternalPalette)
                    << ", \"orderTextureWidth\": " << entry.orderTextureWidth
                    << ", \"orderTextureHeight\": " << entry.orderTextureHeight
                    << ", \"orderTextureImageDataSize\": " << entry.orderTextureImageDataSize
                    << ", \"orderTextureDecoded\": " << boolText(entry.orderTextureDecoded)
                    << ", \"nameSuffixMatchesOrderTextureGlobalIndex\": " << boolText(entry.nameSuffixMatchesOrderTextureGlobalIndex)
                    << ", \"bytes32Hex\": ";
                writeJsonString(out, entry.bytes32Hex);
                out << " }";
                if (entryIndex + 1U != preTextureTable.entries.size()) {
                    out << ",";
                }
                out << "\n";
            }
            out << "            ]\n";
            out << "          },\n";
            const auto& indexedBin = member.indexedBinTableProbe;
            out << "          \"indexedBinTableProbe\": {\n";
            out << "            \"present\": " << boolText(indexedBin.present) << ",\n";
            out << "            \"headerInBounds\": " << boolText(indexedBin.headerInBounds) << ",\n";
            out << "            \"count\": " << indexedBin.count << ",\n";
            out << "            \"offsetTableOffset\": " << indexedBin.offsetTableOffset << ",\n";
            out << "            \"offsetTableEndOffset\": " << indexedBin.offsetTableEndOffset << ",\n";
            out << "            \"dataBaseOffset\": " << indexedBin.dataBaseOffset << ",\n";
            out << "            \"offsetTableInBounds\": " << boolText(indexedBin.offsetTableInBounds) << ",\n";
            out << "            \"offsetsInBounds\": " << boolText(indexedBin.offsetsInBounds) << ",\n";
            out << "            \"offsetsMonotonic\": " << boolText(indexedBin.offsetsMonotonic) << ",\n";
            out << "            \"firstRecordOffset\": " << indexedBin.firstRecordOffset << ",\n";
            out << "            \"lastRecordOffset\": " << indexedBin.lastRecordOffset << ",\n";
            out << "            \"sampledRecordCount\": " << indexedBin.sampledRecordCount << ",\n";
            out << "            \"offsetsPreview\": ";
            writeJsonString(out, indexedBin.offsetsPreview);
            out << ",\n";
            out << "            \"samples\": [\n";
            for (std::size_t sampleIndex = 0; sampleIndex < indexedBin.samples.size(); ++sampleIndex) {
                const auto& sample = indexedBin.samples[sampleIndex];
                out << "              { \"sampleIndex\": " << sample.sampleIndex
                    << ", \"tableOffset\": " << sample.tableOffset
                    << ", \"recordOffset\": " << sample.recordOffset
                    << ", \"recordInBounds\": " << boolText(sample.recordInBounds)
                    << ", \"word0\": " << sample.word0
                    << ", \"word0EqualsDataBaseOffset\": " << boolText(sample.word0EqualsDataBaseOffset)
                    << ", \"word4\": " << sample.word4
                    << ", \"word4TargetInBounds\": " << boolText(sample.word4TargetInBounds)
                    << ", \"word8\": " << sample.word8
                    << ", \"word12\": " << sample.word12
                    << ", \"word16\": " << sample.word16
                    << ", \"word20\": " << sample.word20
                    << ", \"word24\": " << sample.word24
                    << ", \"bytes16Hex\": ";
                writeJsonString(out, sample.bytes16Hex);
                out << ", \"bytes32Hex\": ";
                writeJsonString(out, sample.bytes32Hex);
                out << " }";
                if (sampleIndex + 1U != indexedBin.samples.size()) {
                    out << ",";
                }
                out << "\n";
            }
            out << "            ]\n";
            out << "          }\n";
            out << "        }";
            if (memberIndex + 1U != file.members.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "      ],\n";
        out << "      \"diagnostics\": [\n";
        for (std::size_t diagnosticIndex = 0; diagnosticIndex < file.file.diagnostics.size(); ++diagnosticIndex) {
            const auto& diagnostic = file.file.diagnostics[diagnosticIndex];
            out << "        { \"severity\": ";
            writeJsonString(out, toString(diagnostic.severity));
            out << ", \"offset\": " << diagnostic.offset << ", \"message\": ";
            writeJsonString(out, diagnostic.message);
            out << " }";
            if (diagnosticIndex + 1U != file.file.diagnostics.size()) {
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

std::string formatMllCorpusFilesCsv(const MllCorpusScanResult& corpus) {
    std::ostringstream out;
    out << "path,rawSize,decodedSize,sourceWasCompressedAklz,headerWord0,countWord,memberCountCandidate,"
           "selectedMemberCount,memberCountSource,tableShape,firstMemberOffset,memberCountInferredFromFirstMemberOffset,"
           "memberCountMatchesFirstMemberOffset,memberTableEndOffset,memberTableInBounds,supported,warningCount,"
           "errorCount,mldLikeMemberCount,mldIndexShapeMemberCount,mldObjectListProbeCount,nonZeroMldObjectListProbeCount,"
           "plausibleMldObjectListProbeCount,mldBlockProbeCount,exactReferencedMldBlockProbeCount,gvrTextureProbeCount,"
           "parsedGvrTextureProbeCount,failedGvrTextureProbeCount,decodedGvrTextureProbeCount,textureMemberProbeCount,"
           "denseGlobalIndexTextureMemberCount,textureClusterCount,headerTextureTableBeforeFirstTextureCount,"
           "indexTexturePointerInsideTextureSpanCount,preTextureTableProbeCount,preTextureTableAlignedProbeCount,"
           "preTextureTableEntryCount,preTextureTablePrintableNameCount,preTextureTableTextureNameMatchCount,"
           "indexedBinTableProbeCount,plausibleIndexedBinTableProbeCount,outOfBoundsMemberCount\n";
    for (const auto& file : corpus.files) {
        out << csvEscape(file.relativePath) << ","
            << file.file.rawSize << ","
            << file.file.decodedSize << ","
            << boolText(file.file.sourceWasCompressedAklz) << ","
            << file.file.headerWord0 << ","
            << file.file.countWord << ","
            << file.file.memberCountCandidate << ","
            << file.file.selectedMemberCount << ","
            << csvEscape(toString(file.file.memberCountSource)) << ","
            << csvEscape(toString(file.file.tableShape)) << ","
            << file.file.firstMemberOffset << ","
            << file.file.memberCountInferredFromFirstMemberOffset << ","
            << boolText(file.file.memberCountMatchesFirstMemberOffset) << ","
            << file.file.memberTableEndOffset << ","
            << boolText(file.file.memberTableInBounds) << ","
            << boolText(file.file.supported) << ","
            << diagnosticCount(file.file, DiagnosticSeverity::Warning) << ","
            << diagnosticCount(file.file, DiagnosticSeverity::Error) << ","
            << mldLikeMemberCount(file) << ","
            << mldIndexShapeMemberCount(file) << ","
            << mldObjectListProbeCount(file) << ","
            << nonZeroMldObjectListProbeCount(file) << ","
            << plausibleMldObjectListProbeCount(file) << ","
            << mldBlockProbeCount(file) << ","
            << exactReferencedMldBlockProbeCount(file) << ","
            << gvrTextureProbeCount(file) << ","
            << parsedGvrTextureProbeCount(file) << ","
            << failedGvrTextureProbeCount(file) << ","
            << decodedGvrTextureProbeCount(file) << ","
            << textureMemberProbeCount(file) << ","
            << denseGlobalIndexTextureMemberCount(file) << ","
            << textureClusterCount(file) << ","
            << headerTextureTableBeforeFirstTextureCount(file) << ","
            << indexTexturePointerInsideTextureSpanCount(file) << ","
            << preTextureTableProbeCount(file) << ","
            << preTextureTableAlignedProbeCount(file) << ","
            << preTextureTableEntryCount(file) << ","
            << preTextureTablePrintableNameCount(file) << ","
            << preTextureTableTextureNameMatchCount(file) << ","
            << indexedBinTableProbeCount(file) << ","
            << plausibleIndexedBinTableProbeCount(file) << ","
            << outOfBoundsMemberCount(file) << "\n";
    }
    return out.str();
}

std::string formatMllCorpusMembersCsv(const MllCorpusScanResult& corpus) {
    std::ostringstream out;
    out << "filePath,memberIndex,recordOffset,name,payloadOffset,payloadSize,rawWord1c,payloadInBounds,payloadOverlapsMemberTable,"
           "payloadKind,payloadSignature,embeddedMldHeaderPlausible,embeddedMldEntryCount,embeddedMldIndexTableOffset,"
           "embeddedMldFunctionParametersOffset,embeddedMldRealDataOffset,embeddedMldTextureTableOffset,"
           "embeddedMldIndexEntryShapePlausible,embeddedMldPrintableFunctionNameCount,embeddedMldEmptyFunctionNameCount,"
           "embeddedMldSuspiciousFunctionNameCount,embeddedMldNonZeroCountedListFieldCount,"
           "embeddedMldPlausibleCountedListFieldCount\n";
    for (const auto& file : corpus.files) {
        for (const auto& memberSummary : file.members) {
            const auto& member = memberSummary.member;
            out << csvEscape(file.relativePath) << ","
                << member.index << ","
                << member.recordOffset << ","
                << csvEscape(member.name) << ","
                << member.payloadOffset << ","
                << member.payloadSize << ","
                << member.rawWord1c << ","
                << boolText(member.payloadInBounds) << ","
                << boolText(member.payloadOverlapsMemberTable) << ","
                << csvEscape(toString(member.payloadKind)) << ","
                << csvEscape(member.payloadSignature) << ","
                << boolText(member.embeddedMldHeader.plausible) << ","
                << member.embeddedMldHeader.entryCount << ","
                << member.embeddedMldHeader.indexTableOffset << ","
                << member.embeddedMldHeader.functionParametersOffset << ","
                << member.embeddedMldHeader.realDataOffset << ","
                << member.embeddedMldHeader.textureTableOffset << ","
                << boolText(member.embeddedMldHeader.indexEntryShapePlausible) << ","
                << member.embeddedMldHeader.printableFunctionNameCount << ","
                << member.embeddedMldHeader.emptyFunctionNameCount << ","
                << member.embeddedMldHeader.suspiciousFunctionNameCount << ","
                << member.embeddedMldHeader.nonZeroCountedListFieldCount << ","
                << member.embeddedMldHeader.plausibleCountedListFieldCount << "\n";
        }
    }
    return out.str();
}

std::string formatMllCorpusIndexedBinTablesCsv(const MllCorpusScanResult& corpus) {
    std::ostringstream out;
    out << "filePath,memberIndex,memberName,payloadOffset,payloadSize,payloadKind,payloadSignature,"
           "present,headerInBounds,count,offsetTableOffset,offsetTableEndOffset,dataBaseOffset,"
           "offsetTableInBounds,offsetsInBounds,offsetsMonotonic,firstRecordOffset,lastRecordOffset,"
           "sampledRecordCount,offsetsPreview,sampleIndex,tableOffset,recordOffset,recordInBounds,"
           "word0,word0EqualsDataBaseOffset,word4,word4TargetInBounds,word8,word12,word16,word20,word24,"
           "bytes16Hex,bytes32Hex\n";
    for (const auto& file : corpus.files) {
        for (const auto& memberSummary : file.members) {
            const auto& member = memberSummary.member;
            const auto& probe = member.indexedBinTableProbe;
            if (!probe.present) {
                continue;
            }

            if (probe.samples.empty()) {
                out << csvEscape(file.relativePath) << ","
                    << member.index << ","
                    << csvEscape(member.name) << ","
                    << member.payloadOffset << ","
                    << member.payloadSize << ","
                    << csvEscape(toString(member.payloadKind)) << ","
                    << csvEscape(member.payloadSignature) << ","
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
                    << member.index << ","
                    << csvEscape(member.name) << ","
                    << member.payloadOffset << ","
                    << member.payloadSize << ","
                    << csvEscape(toString(member.payloadKind)) << ","
                    << csvEscape(member.payloadSignature) << ","
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
    }
    return out.str();
}

std::string formatMllCorpusMldObjectListsCsv(const MllCorpusScanResult& corpus) {
    std::ostringstream out;
    out << "filePath,memberIndex,memberName,payloadOffset,payloadSize,entryIndex,entryOffset,fieldOffset,listOffset,"
           "listOffsetNonZero,listOffsetAligned,listHeaderInBounds,declaredCount,listEntriesInBounds,listLooksPlausible,sampledPointerCount,"
           "listBytes32Hex,nonNullSampledPointerCount,sampleIndex,pointerOffset,targetOffset,pointerNonZero,targetInBounds,"
           "targetOffsetAligned,targetLooksPlausible,listBaseTargetOffset,listBaseTargetLooksPlausible,"
           "entryBaseTargetOffset,entryBaseTargetLooksPlausible,pointerBaseTargetOffset,pointerBaseTargetLooksPlausible,"
           "targetWord0,targetSignature,targetBytes16Hex\n";
    for (const auto& file : corpus.files) {
        for (const auto& memberSummary : file.members) {
            const auto& member = memberSummary.member;
            for (const auto& probe : member.embeddedMldObjectListProbes) {
                if (probe.targetSamples.empty()) {
                    out << csvEscape(file.relativePath) << ","
                        << member.index << ","
                        << csvEscape(member.name) << ","
                        << member.payloadOffset << ","
                        << member.payloadSize << ","
                        << probe.entryIndex << ","
                        << probe.entryOffset << ","
                        << probe.fieldOffset << ","
                        << probe.listOffset << ","
                        << boolText(probe.listOffsetNonZero) << ","
                        << boolText(probe.listOffsetAligned) << ","
                        << boolText(probe.listHeaderInBounds) << ","
                        << probe.declaredCount << ","
                        << boolText(probe.listEntriesInBounds) << ","
                        << boolText(probe.listLooksPlausible) << ","
                        << probe.sampledPointerCount << ","
                        << csvEscape(probe.listBytes32Hex) << ","
                        << probe.nonNullSampledPointerCount
                        << ",,,,,,,,,,,,,,,,\n";
                    continue;
                }

                for (const auto& sample : probe.targetSamples) {
                    out << csvEscape(file.relativePath) << ","
                        << member.index << ","
                        << csvEscape(member.name) << ","
                        << member.payloadOffset << ","
                        << member.payloadSize << ","
                        << probe.entryIndex << ","
                        << probe.entryOffset << ","
                        << probe.fieldOffset << ","
                        << probe.listOffset << ","
                        << boolText(probe.listOffsetNonZero) << ","
                        << boolText(probe.listOffsetAligned) << ","
                        << boolText(probe.listHeaderInBounds) << ","
                        << probe.declaredCount << ","
                        << boolText(probe.listEntriesInBounds) << ","
                        << boolText(probe.listLooksPlausible) << ","
                        << probe.sampledPointerCount << ","
                        << csvEscape(probe.listBytes32Hex) << ","
                        << probe.nonNullSampledPointerCount << ","
                        << sample.sampleIndex << ","
                        << sample.pointerOffset << ","
                        << sample.targetOffset << ","
                        << boolText(sample.pointerNonZero) << ","
                        << boolText(sample.targetInBounds) << ","
                        << boolText(sample.targetOffsetAligned) << ","
                        << boolText(sample.targetLooksPlausible) << ","
                        << sample.listBaseTargetOffset << ","
                        << boolText(sample.listBaseTargetLooksPlausible) << ","
                        << sample.entryBaseTargetOffset << ","
                        << boolText(sample.entryBaseTargetLooksPlausible) << ","
                        << sample.pointerBaseTargetOffset << ","
                        << boolText(sample.pointerBaseTargetLooksPlausible) << ","
                        << sample.targetWord0 << ","
                        << csvEscape(sample.targetSignature) << ","
                        << csvEscape(sample.targetBytes16Hex) << "\n";
                }
            }
        }
    }
    return out.str();
}

std::string formatMllCorpusMldBlocksCsv(const MllCorpusScanResult& corpus) {
    std::ostringstream out;
    out << "filePath,memberIndex,memberName,payloadOffset,payloadSize,payloadKind,embeddedMldHeaderPlausible,"
           "embeddedMldIndexEntryShapePlausible,blockOffset,tag,offsetAligned,declaredSizeBe,"
           "declaredSizeBePlausible,declaredSizeLe,declaredSizeLePlausible,atHeaderRealDataOffset,"
           "atHeaderTextureTableOffset,exactCountedListReferenceCount,plus8CountedListReferenceCount,"
           "minus8CountedListReferenceCount,firstExactCountedListReference,bytes32Hex\n";
    for (const auto& file : corpus.files) {
        for (const auto& memberSummary : file.members) {
            const auto& member = memberSummary.member;
            for (const auto& block : member.embeddedBlockProbes) {
                out << csvEscape(file.relativePath) << ","
                    << member.index << ","
                    << csvEscape(member.name) << ","
                    << member.payloadOffset << ","
                    << member.payloadSize << ","
                    << csvEscape(toString(member.payloadKind)) << ","
                    << boolText(member.embeddedMldHeader.plausible) << ","
                    << boolText(member.embeddedMldHeader.indexEntryShapePlausible) << ","
                    << block.blockOffset << ","
                    << csvEscape(block.tag) << ","
                    << boolText(block.offsetAligned) << ","
                    << block.declaredSizeBe << ","
                    << boolText(block.declaredSizeBePlausible) << ","
                    << block.declaredSizeLe << ","
                    << boolText(block.declaredSizeLePlausible) << ","
                    << boolText(block.atHeaderRealDataOffset) << ","
                    << boolText(block.atHeaderTextureTableOffset) << ","
                    << block.exactCountedListReferenceCount << ","
                    << block.plus8CountedListReferenceCount << ","
                    << block.minus8CountedListReferenceCount << ","
                    << csvEscape(block.firstExactCountedListReference) << ","
                    << csvEscape(block.bytes32Hex) << "\n";
            }
        }
    }
    return out.str();
}

std::string formatMllCorpusGvrTexturesCsv(const MllCorpusScanResult& corpus) {
    std::ostringstream out;
    out << "filePath,memberIndex,memberName,payloadOffset,payloadSize,payloadKind,embeddedMldHeaderPlausible,"
           "embeddedMldIndexEntryShapePlausible,textureIndex,gcixOffset,gvrtOffset,pairDistance,gcixPayloadSizeLe,"
           "gvrtPayloadSizeLe,sourceSize,recordInBounds,parseAttempted,parseHasFailureDiagnostics,hasGlobalIndex,"
           "globalIndex,rawFlags,rawDataFormat,textureFormat,paletteFormat,hasMipmaps,hasInternalPalette,width,height,"
           "imageDataOffset,imageDataSize,decodedBaseLevelPresent,decodedRgba8Size,diagnosticCount,diagnostics\n";
    for (const auto& file : corpus.files) {
        for (const auto& memberSummary : file.members) {
            const auto& member = memberSummary.member;
            for (const auto& texture : member.embeddedGvrTextureProbes) {
                out << csvEscape(file.relativePath) << ","
                    << member.index << ","
                    << csvEscape(member.name) << ","
                    << member.payloadOffset << ","
                    << member.payloadSize << ","
                    << csvEscape(toString(member.payloadKind)) << ","
                    << boolText(member.embeddedMldHeader.plausible) << ","
                    << boolText(member.embeddedMldHeader.indexEntryShapePlausible) << ","
                    << texture.textureIndex << ","
                    << texture.gcixOffset << ","
                    << texture.gvrtOffset << ","
                    << texture.pairDistance << ","
                    << texture.gcixPayloadSizeLe << ","
                    << texture.gvrtPayloadSizeLe << ","
                    << texture.sourceSize << ","
                    << boolText(texture.recordInBounds) << ","
                    << boolText(texture.parseAttempted) << ","
                    << boolText(texture.parseHasFailureDiagnostics) << ","
                    << boolText(texture.hasGlobalIndex) << ","
                    << texture.globalIndex << ","
                    << texture.rawFlags << ","
                    << texture.rawDataFormat << ","
                    << csvEscape(texture.textureFormat) << ","
                    << csvEscape(texture.paletteFormat) << ","
                    << boolText(texture.hasMipmaps) << ","
                    << boolText(texture.hasInternalPalette) << ","
                    << texture.width << ","
                    << texture.height << ","
                    << texture.imageDataOffset << ","
                    << texture.imageDataSize << ","
                    << boolText(texture.decodedBaseLevelPresent) << ","
                    << texture.decodedRgba8Size << ","
                    << texture.diagnosticCount << ","
                    << csvEscape(texture.diagnosticsJoined) << "\n";
            }
        }
    }
    return out.str();
}

std::string formatMllCorpusTextureMembersCsv(const MllCorpusScanResult& corpus) {
    std::ostringstream out;
    out << "filePath,memberIndex,memberName,payloadOffset,payloadSize,payloadKind,embeddedMldHeaderPlausible,"
           "embeddedMldIndexEntryShapePlausible,textureCount,firstTextureOffset,lastTextureEndOffset,textureSpanSize,"
           "clusterCount,largestGapBetweenRecords,allRecordsInBounds,allTexturesParsed,allTexturesDecoded,"
           "allTexturesHaveGlobalIndex,globalIndexMin,globalIndexMax,uniqueGlobalIndexCount,duplicateGlobalIndexCount,"
           "missingGlobalIndexCount,globalIndexSequenceDense,globalIndexSequenceStartsAtZero,globalIndexSequencePreview,"
           "headerTextureTableOffsetNonZero,headerTextureTableOffsetAtFirstTexture,headerTextureTableOffsetInsideTextureSpan,"
           "headerTextureTableOffsetBeforeFirstTexture,headerTextureTableOffsetDeltaToFirstTexture,headerRealDataOffsetNonZero,"
           "headerRealDataOffsetAtFirstTexture,headerRealDataOffsetInsideTextureSpan,headerRealDataOffsetBeforeFirstTexture,"
           "headerRealDataOffsetDeltaToFirstTexture,indexTexturePointerCount,nonZeroIndexTexturePointerCount,"
           "indexTexturePointerAtFirstTextureCount,indexTexturePointerInsideTextureSpanCount,indexTexturePointerBeforeFirstTextureCount,"
           "uniqueIndexTexturePointerCount,indexTexturePointerValuesPreview,nearbyPrintableStrings\n";
    for (const auto& file : corpus.files) {
        for (const auto& memberSummary : file.members) {
            const auto& member = memberSummary.member;
            const auto& probe = member.textureTableProbe;
            if (!probe.hasTextures) {
                continue;
            }
            out << csvEscape(file.relativePath) << ","
                << member.index << ","
                << csvEscape(member.name) << ","
                << member.payloadOffset << ","
                << member.payloadSize << ","
                << csvEscape(toString(member.payloadKind)) << ","
                << boolText(member.embeddedMldHeader.plausible) << ","
                << boolText(member.embeddedMldHeader.indexEntryShapePlausible) << ","
                << probe.textureCount << ","
                << probe.firstTextureOffset << ","
                << probe.lastTextureEndOffset << ","
                << probe.textureSpanSize << ","
                << probe.clusterCount << ","
                << probe.largestGapBetweenRecords << ","
                << boolText(probe.allRecordsInBounds) << ","
                << boolText(probe.allTexturesParsed) << ","
                << boolText(probe.allTexturesDecoded) << ","
                << boolText(probe.allTexturesHaveGlobalIndex) << ","
                << probe.globalIndexMin << ","
                << probe.globalIndexMax << ","
                << probe.uniqueGlobalIndexCount << ","
                << probe.duplicateGlobalIndexCount << ","
                << probe.missingGlobalIndexCount << ","
                << boolText(probe.globalIndexSequenceDense) << ","
                << boolText(probe.globalIndexSequenceStartsAtZero) << ","
                << csvEscape(probe.globalIndexSequencePreview) << ","
                << boolText(probe.headerTextureTableOffsetNonZero) << ","
                << boolText(probe.headerTextureTableOffsetAtFirstTexture) << ","
                << boolText(probe.headerTextureTableOffsetInsideTextureSpan) << ","
                << boolText(probe.headerTextureTableOffsetBeforeFirstTexture) << ","
                << probe.headerTextureTableOffsetDeltaToFirstTexture << ","
                << boolText(probe.headerRealDataOffsetNonZero) << ","
                << boolText(probe.headerRealDataOffsetAtFirstTexture) << ","
                << boolText(probe.headerRealDataOffsetInsideTextureSpan) << ","
                << boolText(probe.headerRealDataOffsetBeforeFirstTexture) << ","
                << probe.headerRealDataOffsetDeltaToFirstTexture << ","
                << probe.indexTexturePointerCount << ","
                << probe.nonZeroIndexTexturePointerCount << ","
                << probe.indexTexturePointerAtFirstTextureCount << ","
                << probe.indexTexturePointerInsideTextureSpanCount << ","
                << probe.indexTexturePointerBeforeFirstTextureCount << ","
                << probe.uniqueIndexTexturePointerCount << ","
                << csvEscape(probe.indexTexturePointerValuesPreview) << ","
                << csvEscape(probe.nearbyPrintableStrings) << "\n";
        }
    }
    return out.str();
}

std::string formatMllCorpusPreTextureTableEntriesCsv(const MllCorpusScanResult& corpus) {
    std::ostringstream out;
    out << "filePath,memberIndex,memberName,payloadOffset,payloadSize,payloadKind,textureCount,firstTextureOffset,"
           "tableOffset,tableEndOffset,tableSize,spanInBounds,spanAlignedTo20,recordsFit,declaredCountMatchesTextureCount,"
           "declaredEntryCount,entryStride,tableEntryCount,trailingPaddingSize,printableNameCount,emptyNameCount,"
           "textureNameMatchCount,entryIndex,entryOffset,name,namePrintable,nameEmpty,nameMatchesKnownTextureGlobalIndex,"
           "matchedGlobalIndex,word10,word14,word18,word1c,word20,word24,word28,orderTexturePresent,"
           "orderTextureIndex,orderTextureGcixOffset,orderTextureGvrtOffset,orderTextureSourceSize,"
           "orderTextureHasGlobalIndex,orderTextureGlobalIndex,orderTextureRawFlags,orderTextureRawDataFormat,"
           "orderTextureFormat,orderTexturePaletteFormat,orderTextureHasMipmaps,orderTextureHasInternalPalette,"
           "orderTextureWidth,orderTextureHeight,orderTextureImageDataSize,orderTextureDecoded,"
           "nameSuffixMatchesOrderTextureGlobalIndex,bytesHex\n";
    for (const auto& file : corpus.files) {
        for (const auto& memberSummary : file.members) {
            const auto& member = memberSummary.member;
            const auto& table = member.preTextureTableProbe;
            if (!table.present) {
                continue;
            }
            for (const auto& entry : table.entries) {
                out << csvEscape(file.relativePath) << ","
                    << member.index << ","
                    << csvEscape(member.name) << ","
                    << member.payloadOffset << ","
                    << member.payloadSize << ","
                    << csvEscape(toString(member.payloadKind)) << ","
                    << member.textureTableProbe.textureCount << ","
                    << member.textureTableProbe.firstTextureOffset << ","
                    << table.tableOffset << ","
                    << table.tableEndOffset << ","
                    << table.tableSize << ","
                    << boolText(table.spanInBounds) << ","
                    << boolText(table.spanAlignedTo20) << ","
                    << boolText(table.recordsFit) << ","
                    << boolText(table.declaredCountMatchesTextureCount) << ","
                    << table.declaredEntryCount << ","
                    << table.entryStride << ","
                    << table.entryCount << ","
                    << table.trailingPaddingSize << ","
                    << table.printableNameCount << ","
                    << table.emptyNameCount << ","
                    << table.textureNameMatchCount << ","
                    << entry.entryIndex << ","
                    << entry.entryOffset << ","
                    << csvEscape(entry.name) << ","
                    << boolText(entry.namePrintable) << ","
                    << boolText(entry.nameEmpty) << ","
                    << boolText(entry.nameMatchesKnownTextureGlobalIndex) << ","
                    << entry.matchedGlobalIndex << ","
                    << entry.word10 << ","
                    << entry.word14 << ","
                    << entry.word18 << ","
                    << entry.word1c << ","
                    << entry.word20 << ","
                    << entry.word24 << ","
                    << entry.word28 << ","
                    << boolText(entry.orderTexturePresent) << ","
                    << entry.orderTextureIndex << ","
                    << entry.orderTextureGcixOffset << ","
                    << entry.orderTextureGvrtOffset << ","
                    << entry.orderTextureSourceSize << ","
                    << boolText(entry.orderTextureHasGlobalIndex) << ","
                    << entry.orderTextureGlobalIndex << ","
                    << entry.orderTextureRawFlags << ","
                    << entry.orderTextureRawDataFormat << ","
                    << csvEscape(entry.orderTextureFormat) << ","
                    << csvEscape(entry.orderTexturePaletteFormat) << ","
                    << boolText(entry.orderTextureHasMipmaps) << ","
                    << boolText(entry.orderTextureHasInternalPalette) << ","
                    << entry.orderTextureWidth << ","
                    << entry.orderTextureHeight << ","
                    << entry.orderTextureImageDataSize << ","
                    << boolText(entry.orderTextureDecoded) << ","
                    << boolText(entry.nameSuffixMatchesOrderTextureGlobalIndex) << ","
                    << csvEscape(entry.bytes32Hex) << "\n";
            }
        }
    }
    return out.str();
}

std::string formatMllCorpusAnomaliesCsv(const MllCorpusScanResult& corpus) {
    std::ostringstream out;
    out << "path,tableShape,memberCountCandidate,selectedMemberCount,memberCountSource,firstMemberOffset,"
           "memberCountInferredFromFirstMemberOffset,memberCountMatchesFirstMemberOffset,memberTableEndOffset,"
           "memberTableInBounds,supported,warningCount,errorCount,mldLikeMemberCount,mldIndexShapeMemberCount,"
           "outOfBoundsMemberCount\n";
    for (const auto& file : corpus.files) {
        if (file.file.tableShape == MllTableShape::Normal &&
            diagnosticCount(file.file, DiagnosticSeverity::Warning) == 0U &&
            diagnosticCount(file.file, DiagnosticSeverity::Error) == 0U) {
            continue;
        }
        out << csvEscape(file.relativePath) << ","
            << csvEscape(toString(file.file.tableShape)) << ","
            << file.file.memberCountCandidate << ","
            << file.file.selectedMemberCount << ","
            << csvEscape(toString(file.file.memberCountSource)) << ","
            << file.file.firstMemberOffset << ","
            << file.file.memberCountInferredFromFirstMemberOffset << ","
            << boolText(file.file.memberCountMatchesFirstMemberOffset) << ","
            << file.file.memberTableEndOffset << ","
            << boolText(file.file.memberTableInBounds) << ","
            << boolText(file.file.supported) << ","
            << diagnosticCount(file.file, DiagnosticSeverity::Warning) << ","
            << diagnosticCount(file.file, DiagnosticSeverity::Error) << ","
            << mldLikeMemberCount(file) << ","
            << mldIndexShapeMemberCount(file) << ","
            << outOfBoundsMemberCount(file) << "\n";
    }
    return out.str();
}

std::string formatMllCorpusPayloadKindHistogramCsv(const MllCorpusScanResult& corpus) {
    std::map<std::tuple<std::string, std::string, std::string, std::string>, std::size_t> histogram{};
    for (const auto& file : corpus.files) {
        const auto directory = parentDirectoryForHistogram(file.relativePath);
        const auto tableShape = std::string(toString(file.file.tableShape));
        for (const auto& member : file.members) {
            ++histogram[std::make_tuple(
                directory,
                tableShape,
                std::string(toString(member.member.payloadKind)),
                member.member.payloadSignature)];
        }
    }

    std::ostringstream out;
    out << "directory,tableShape,payloadKind,payloadSignature,count\n";
    for (const auto& [key, count] : histogram) {
        const auto& [directory, tableShape, payloadKind, payloadSignature] = key;
        out << csvEscape(directory) << ","
            << csvEscape(tableShape) << ","
            << csvEscape(payloadKind) << ","
            << csvEscape(payloadSignature) << ","
            << count << "\n";
    }
    return out.str();
}

MllCorpusWriteResult writeMllCorpusArtifacts(
    const MllCorpusScanResult& corpus,
    const std::filesystem::path& outputDir) {
    std::filesystem::create_directories(outputDir);

    MllCorpusWriteResult result{};
    result.jsonPath = outputDir / "mll_corpus.json";
    result.filesCsvPath = outputDir / "mll_corpus_files.csv";
    result.membersCsvPath = outputDir / "mll_corpus_members.csv";
    result.mldObjectListsCsvPath = outputDir / "mll_corpus_mld_object_lists.csv";
    result.mldBlocksCsvPath = outputDir / "mll_corpus_mld_blocks.csv";
    result.gvrTexturesCsvPath = outputDir / "mll_corpus_gvr_textures.csv";
    result.textureMembersCsvPath = outputDir / "mll_corpus_texture_members.csv";
    result.preTextureTableEntriesCsvPath = outputDir / "mll_corpus_pre_texture_table_entries.csv";
    result.indexedBinTablesCsvPath = outputDir / "mll_corpus_indexed_bin_tables.csv";
    result.anomaliesCsvPath = outputDir / "mll_corpus_anomalies.csv";
    result.payloadKindHistogramCsvPath = outputDir / "mll_corpus_payload_kind_histogram.csv";

    writeTextFile(result.jsonPath, formatMllCorpusJson(corpus));
    writeTextFile(result.filesCsvPath, formatMllCorpusFilesCsv(corpus));
    writeTextFile(result.membersCsvPath, formatMllCorpusMembersCsv(corpus));
    writeTextFile(result.mldObjectListsCsvPath, formatMllCorpusMldObjectListsCsv(corpus));
    writeTextFile(result.mldBlocksCsvPath, formatMllCorpusMldBlocksCsv(corpus));
    writeTextFile(result.gvrTexturesCsvPath, formatMllCorpusGvrTexturesCsv(corpus));
    writeTextFile(result.textureMembersCsvPath, formatMllCorpusTextureMembersCsv(corpus));
    writeTextFile(result.preTextureTableEntriesCsvPath, formatMllCorpusPreTextureTableEntriesCsv(corpus));
    writeTextFile(result.indexedBinTablesCsvPath, formatMllCorpusIndexedBinTablesCsv(corpus));
    writeTextFile(result.anomaliesCsvPath, formatMllCorpusAnomaliesCsv(corpus));
    writeTextFile(result.payloadKindHistogramCsvPath, formatMllCorpusPayloadKindHistogramCsv(corpus));
    return result;
}

} // namespace spice::mll
