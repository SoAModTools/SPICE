#include "MllCorpus.h"
#include "StandaloneMldTextureScan.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::cerr << "Usage: SpiceMllCorpusScan [--standalone-mld] <input_file_or_dir> <output_dir>\n";
        return 2;
    }

    try {
        const bool standaloneMldMode = argc == 4 && std::string_view(argv[1]) == "--standalone-mld";
        if (argc == 4 && !standaloneMldMode) {
            std::cerr << "Unknown option: " << argv[1] << "\n";
            return 2;
        }

        const std::filesystem::path inputPath = standaloneMldMode ? argv[2] : argv[1];
        const std::filesystem::path outputDir = standaloneMldMode ? argv[3] : argv[2];
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
