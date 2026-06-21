#include "MllCorpus.h"
#include "MllParser.h"
#include "StandaloneMldTextureScan.h"

#include "../SpiceGvm/Ir/GvrImageIr.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <sstream>
#include <string_view>
#include <vector>

namespace {

std::string sanitizeName(std::string value) {
    for (char& ch : value) {
        const auto isSafe =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_' ||
            ch == '-' ||
            ch == '.';
        if (!isSafe) {
            ch = '_';
        }
    }
    if (value.empty()) {
        return "unnamed";
    }
    return value;
}

void writeBytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Unable to open output file: " + path.string());
    }
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            throw std::runtime_error("Unable to write output file: " + path.string());
        }
    }
}

std::string joinDiagnostics(const std::vector<std::string>& diagnostics) {
    std::ostringstream out;
    for (std::size_t i = 0U; i < diagnostics.size(); ++i) {
        if (i != 0U) {
            out << " | ";
        }
        out << diagnostics[i];
    }
    return out.str();
}

std::size_t exportMllGvrTextures(const std::filesystem::path& inputPath, const std::filesystem::path& outputDir) {
    const auto parsed = spice::mll::MllParser::parseFile(inputPath);
    if (!parsed.ok()) {
        throw std::runtime_error("MLL parse failed for " + inputPath.string());
    }
    if (!parsed.supported) {
        throw std::runtime_error("MLL file is not in the supported table shape: " + inputPath.string());
    }

    std::filesystem::create_directories(outputDir);
    std::ofstream manifest(outputDir / "manifest.tsv");
    if (!manifest) {
        throw std::runtime_error("Unable to open texture export manifest");
    }

    manifest << "memberIndex\tmemberName\ttextureIndex\tglobalIndex\tformat\tpalette\twidth\theight\t"
                "memberPayloadOffset\ttextureGcixOffset\ttextureGvrtOffset\tsourceSize\trawPath\tpngPath\tjsonPath\tdiagnostics\n";

    std::size_t exportedCount = 0U;
    for (const auto& member : parsed.members) {
        if (!member.payloadInBounds) {
            continue;
        }

        for (const auto& texture : member.embeddedGvrTextureProbes) {
            const auto absoluteTextureOffset64 =
                static_cast<std::uint64_t>(member.payloadOffset) + texture.gcixOffset;
            const auto absoluteTextureEnd64 = absoluteTextureOffset64 + texture.sourceSize;
            if (!texture.recordInBounds ||
                texture.sourceSize == 0U ||
                absoluteTextureEnd64 > parsed.originalDecodedBytes.size()) {
                continue;
            }

            const auto stem =
                "m" + std::to_string(member.index) +
                "_t" + std::to_string(texture.textureIndex) +
                "_" + sanitizeName(member.name) +
                "_gi" + (texture.hasGlobalIndex ? std::to_string(texture.globalIndex) : std::string("none"));
            const auto rawPath = outputDir / (stem + ".gvr");
            const auto absoluteTextureOffset = static_cast<std::size_t>(absoluteTextureOffset64);
            const auto sourceSize = static_cast<std::size_t>(texture.sourceSize);
            const auto textureBytes = std::span<const std::uint8_t>(
                parsed.originalDecodedBytes.data() + absoluteTextureOffset,
                sourceSize);
            writeBytes(rawPath, textureBytes);

            std::filesystem::path pngPath{};
            std::filesystem::path jsonPath{};
            std::vector<std::string> diagnostics{};
            try {
                const auto exported = spice::gvm::ir::exportGvrImageIr(textureBytes, rawPath, outputDir);
                pngPath = exported.pngPath;
                jsonPath = exported.jsonPath;
                diagnostics = exported.diagnostics;
            } catch (const std::exception& ex) {
                diagnostics.push_back(ex.what());
            }

            manifest << member.index << "\t"
                << member.name << "\t"
                << texture.textureIndex << "\t"
                << (texture.hasGlobalIndex ? std::to_string(texture.globalIndex) : std::string("")) << "\t"
                << texture.textureFormat << "\t"
                << texture.paletteFormat << "\t"
                << texture.width << "\t"
                << texture.height << "\t"
                << member.payloadOffset << "\t"
                << texture.gcixOffset << "\t"
                << texture.gvrtOffset << "\t"
                << texture.sourceSize << "\t"
                << rawPath.string() << "\t"
                << pngPath.string() << "\t"
                << jsonPath.string() << "\t"
                << joinDiagnostics(diagnostics) << "\n";
            ++exportedCount;
        }
    }

    return exportedCount;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::cerr << "Usage: SpiceMllCorpusScan [--standalone-mld|--export-mll-gvr-textures] <input_file_or_dir> <output_dir>\n";
        return 2;
    }

    try {
        const bool standaloneMldMode = argc == 4 && std::string_view(argv[1]) == "--standalone-mld";
        const bool exportMllGvrTexturesMode = argc == 4 && std::string_view(argv[1]) == "--export-mll-gvr-textures";
        if (argc == 4 && !standaloneMldMode && !exportMllGvrTexturesMode) {
            std::cerr << "Unknown option: " << argv[1] << "\n";
            return 2;
        }

        const std::filesystem::path inputPath = (standaloneMldMode || exportMllGvrTexturesMode) ? argv[2] : argv[1];
        const std::filesystem::path outputDir = (standaloneMldMode || exportMllGvrTexturesMode) ? argv[3] : argv[2];
        if (standaloneMldMode) {
            const auto corpus = spice::mll::scanStandaloneMldTextures(inputPath);
            const auto written = spice::mll::writeStandaloneMldTextureArtifacts(corpus, outputDir);
            const auto summary = spice::mll::summarizeStandaloneMldTextureFeedback(corpus);

            std::cout << "SpiceMllCorpusScan standalone MLD texture scan complete\n";
            std::cout << "input=" << inputPath.string() << "\n";
            std::cout << "output=" << outputDir.string() << "\n";
            std::cout << "files=" << summary.fileCount << "\n";
            std::cout << "compressedFiles=" << summary.compressedFileCount << "\n";
            std::cout << "plausibleHeaderFiles=" << summary.plausibleHeaderFileCount << "\n";
            std::cout << "textureChunkFiles=" << summary.textureChunkFileCount << "\n";
            std::cout << "textureTableFiles=" << summary.textureTableFileCount << "\n";
            std::cout << "recordsFitFiles=" << summary.recordsFitFileCount << "\n";
            std::cout << "countMatchesTextureCountFiles=" << summary.countMatchesTextureCountFileCount << "\n";
            std::cout << "paddingMatchesFiles=" << summary.paddingMatchesFileCount << "\n";
            std::cout << "textureChunks=" << summary.totalTextureChunks << "\n";
            std::cout << "textureTableEntries=" << summary.totalTextureTableEntries << "\n";
            std::cout << "printableNames=" << summary.totalPrintableNames << "\n";
            std::cout << "emptyNames=" << summary.totalEmptyNames << "\n";
            std::cout << "word10NonZero=" << summary.totalWord10NonZero << "\n";
            std::cout << "word14NonZero=" << summary.totalWord14NonZero << "\n";
            std::cout << "word18NonZero=" << summary.totalWord18NonZero << "\n";
            std::cout << "word1cNonZero=" << summary.totalWord1cNonZero << "\n";
            std::cout << "word20NonZero=" << summary.totalWord20NonZero << "\n";
            std::cout << "word24Not80000000=" << summary.totalWord24Not80000000 << "\n";
            std::cout << "word28SourceSizeMismatches=" << summary.totalWord28SourceSizeMismatches << "\n";
            std::cout << "parsedTextures=" << summary.totalParsedTextures << "\n";
            std::cout << "decodedTextures=" << summary.totalDecodedTextures << "\n";
            std::cout << "diagnostics=" << summary.totalDiagnostics << "\n";
            std::cout << "json=" << written.jsonPath.string() << "\n";
            std::cout << "filesCsv=" << written.filesCsvPath.string() << "\n";
            std::cout << "entriesCsv=" << written.entriesCsvPath.string() << "\n";
            return 0;
        }
        if (exportMllGvrTexturesMode) {
            const auto exportedCount = exportMllGvrTextures(inputPath, outputDir);
            std::cout << "SpiceMllCorpusScan MLL GVR texture export complete\n";
            std::cout << "input=" << inputPath.string() << "\n";
            std::cout << "output=" << outputDir.string() << "\n";
            std::cout << "exportedTextures=" << exportedCount << "\n";
            std::cout << "manifest=" << (outputDir / "manifest.tsv").string() << "\n";
            return 0;
        }

        const auto corpus = spice::mll::scanMllCorpus(inputPath);
        const auto written = spice::mll::writeMllCorpusArtifacts(corpus, outputDir);
        const auto summary = spice::mll::summarizeMllCorpusFeedback(corpus);

        std::cout << "SpiceMllCorpusScan complete\n";
        std::cout << "input=" << inputPath.string() << "\n";
        std::cout << "output=" << outputDir.string() << "\n";
        std::cout << "files=" << summary.fileCount << "\n";
        std::cout << "supportedFiles=" << summary.supportedFileCount << "\n";
        std::cout << "normalShapes=" << summary.normalShapeCount << "\n";
        std::cout << "firstMemberCountCandidates=" << summary.firstMemberCountCandidateCount << "\n";
        std::cout << "malformedShapes=" << summary.malformedShapeCount << "\n";
        std::cout << "members=" << summary.totalMemberCount << "\n";
        std::cout << "mldLikeMembers=" << summary.mldLikeMemberCount << "\n";
        std::cout << "mldIndexShapeMembers=" << summary.mldIndexShapeMemberCount << "\n";
        std::cout << "mldObjectListProbes=" << summary.mldObjectListProbeCount << "\n";
        std::cout << "nonZeroMldObjectListProbes=" << summary.nonZeroMldObjectListProbeCount << "\n";
        std::cout << "plausibleMldObjectListProbes=" << summary.plausibleMldObjectListProbeCount << "\n";
        std::cout << "mldBlockProbes=" << summary.mldBlockProbeCount << "\n";
        std::cout << "exactReferencedMldBlockProbes=" << summary.exactReferencedMldBlockProbeCount << "\n";
        std::cout << "gvrTextureProbes=" << summary.gvrTextureProbeCount << "\n";
        std::cout << "parsedGvrTextureProbes=" << summary.parsedGvrTextureProbeCount << "\n";
        std::cout << "failedGvrTextureProbes=" << summary.failedGvrTextureProbeCount << "\n";
        std::cout << "decodedGvrTextureProbes=" << summary.decodedGvrTextureProbeCount << "\n";
        std::cout << "textureMemberProbes=" << summary.textureMemberProbeCount << "\n";
        std::cout << "denseGlobalIndexTextureMembers=" << summary.denseGlobalIndexTextureMemberCount << "\n";
        std::cout << "textureClusters=" << summary.textureClusterCount << "\n";
        std::cout << "headerTextureTableBeforeFirstTexture=" << summary.headerTextureTableBeforeFirstTextureCount << "\n";
        std::cout << "indexTexturePointersInsideTextureSpan=" << summary.indexTexturePointerInsideTextureSpanCount << "\n";
        std::cout << "preTextureTableProbes=" << summary.preTextureTableProbeCount << "\n";
        std::cout << "preTextureTableAlignedProbes=" << summary.preTextureTableAlignedProbeCount << "\n";
        std::cout << "preTextureTableEntries=" << summary.preTextureTableEntryCount << "\n";
        std::cout << "preTextureTablePrintableNames=" << summary.preTextureTablePrintableNameCount << "\n";
        std::cout << "preTextureTableTextureNameMatches=" << summary.preTextureTableTextureNameMatchCount << "\n";
        std::cout << "indexedBinTableProbes=" << summary.indexedBinTableProbeCount << "\n";
        std::cout << "plausibleIndexedBinTableProbes=" << summary.plausibleIndexedBinTableProbeCount << "\n";
        std::cout << "outOfBoundsMembers=" << summary.outOfBoundsMemberCount << "\n";
        std::cout << "warnings=" << summary.warningCount << "\n";
        std::cout << "errors=" << summary.errorCount << "\n";
        std::cout << "json=" << written.jsonPath.string() << "\n";
        std::cout << "filesCsv=" << written.filesCsvPath.string() << "\n";
        std::cout << "membersCsv=" << written.membersCsvPath.string() << "\n";
        std::cout << "mldObjectListsCsv=" << written.mldObjectListsCsvPath.string() << "\n";
        std::cout << "mldBlocksCsv=" << written.mldBlocksCsvPath.string() << "\n";
        std::cout << "gvrTexturesCsv=" << written.gvrTexturesCsvPath.string() << "\n";
        std::cout << "textureMembersCsv=" << written.textureMembersCsvPath.string() << "\n";
        std::cout << "preTextureTableEntriesCsv=" << written.preTextureTableEntriesCsvPath.string() << "\n";
        std::cout << "indexedBinTablesCsv=" << written.indexedBinTablesCsvPath.string() << "\n";
        std::cout << "anomaliesCsv=" << written.anomaliesCsvPath.string() << "\n";
        std::cout << "payloadKindHistogramCsv=" << written.payloadKindHistogramCsvPath.string() << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "SpiceMllCorpusScan failed: " << ex.what() << "\n";
        return 1;
    }
}
