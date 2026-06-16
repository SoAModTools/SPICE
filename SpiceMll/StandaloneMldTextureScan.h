#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace spice::mll {

struct StandaloneMldTextureEntryScan {
    std::uint32_t entryIndex{ 0U };
    std::uint32_t entryOffset{ 0U };
    std::string name{};
    bool namePrintable{ false };
    bool nameEmpty{ false };
    std::uint32_t word10{ 0U };
    std::uint32_t word14{ 0U };
    std::uint32_t word18{ 0U };
    std::uint32_t word1c{ 0U };
    std::uint32_t word20{ 0U };
    std::uint32_t word24{ 0U };
    std::uint32_t word28{ 0U };
    bool orderTexturePresent{ false };
    std::uint32_t orderTextureIndex{ 0U };
    std::uint32_t orderTextureOffset{ 0U };
    std::string orderTextureStartTag{};
    std::uint32_t orderTextureSourceSize{ 0U };
    bool orderTextureParsed{ false };
    bool orderTextureDecoded{ false };
    bool orderTextureHasGlobalIndex{ false };
    std::uint32_t orderTextureGlobalIndex{ 0U };
    std::uint32_t orderTextureRawFlags{ 0U };
    std::uint32_t orderTextureRawDataFormat{ 0U };
    std::string orderTextureFormat{};
    std::string orderTexturePaletteFormat{};
    std::uint32_t orderTextureWidth{ 0U };
    std::uint32_t orderTextureHeight{ 0U };
};

struct StandaloneMldTextureFileScan {
    std::string relativePath{};
    std::string absolutePath{};
    bool sourceWasCompressedAklz{ false };
    std::uint32_t rawSize{ 0U };
    std::uint32_t decodedSize{ 0U };
    bool headerPlausible{ false };
    std::string endian{};
    std::uint32_t entryCount{ 0U };
    std::uint32_t indexTableOffset{ 0U };
    std::uint32_t functionParametersOffset{ 0U };
    std::uint32_t realDataOffset{ 0U };
    std::uint32_t textureTableOffset{ 0U };
    bool textureTableOffsetInBounds{ false };
    bool hasTextureChunks{ false };
    std::uint32_t textureChunkCount{ 0U };
    std::uint32_t firstTextureOffset{ 0U };
    std::uint32_t textureTableDeclaredCount{ 0U };
    bool textureTablePresent{ false };
    bool textureTableRecordsFit{ false };
    bool textureTableCountMatchesTextureCount{ false };
    std::uint32_t expectedPaddingSize{ 0U };
    std::uint32_t trailingPaddingSize{ 0U };
    bool trailingPaddingMatchesExpected{ false };
    std::uint32_t printableNameCount{ 0U };
    std::uint32_t emptyNameCount{ 0U };
    std::uint32_t word10NonZeroCount{ 0U };
    std::uint32_t word14NonZeroCount{ 0U };
    std::uint32_t word18NonZeroCount{ 0U };
    std::uint32_t word1cNonZeroCount{ 0U };
    std::uint32_t word20NonZeroCount{ 0U };
    std::uint32_t word24Not80000000Count{ 0U };
    std::uint32_t word28SourceSizeMismatchCount{ 0U };
    std::uint32_t parsedTextureCount{ 0U };
    std::uint32_t decodedTextureCount{ 0U };
    std::vector<std::string> diagnostics{};
    std::vector<StandaloneMldTextureEntryScan> entries{};
};

struct StandaloneMldTextureCorpusScan {
    std::string inputPath{};
    bool inputWasDirectory{ false };
    std::vector<StandaloneMldTextureFileScan> files{};
};

struct StandaloneMldTextureFeedbackSummary {
    std::size_t fileCount{ 0U };
    std::size_t compressedFileCount{ 0U };
    std::size_t plausibleHeaderFileCount{ 0U };
    std::size_t textureChunkFileCount{ 0U };
    std::size_t textureTableFileCount{ 0U };
    std::size_t recordsFitFileCount{ 0U };
    std::size_t countMatchesTextureCountFileCount{ 0U };
    std::size_t paddingMatchesFileCount{ 0U };
    std::size_t totalTextureChunks{ 0U };
    std::size_t totalTextureTableEntries{ 0U };
    std::size_t totalPrintableNames{ 0U };
    std::size_t totalEmptyNames{ 0U };
    std::size_t totalWord10NonZero{ 0U };
    std::size_t totalWord14NonZero{ 0U };
    std::size_t totalWord18NonZero{ 0U };
    std::size_t totalWord1cNonZero{ 0U };
    std::size_t totalWord20NonZero{ 0U };
    std::size_t totalWord24Not80000000{ 0U };
    std::size_t totalWord28SourceSizeMismatches{ 0U };
    std::size_t totalParsedTextures{ 0U };
    std::size_t totalDecodedTextures{ 0U };
    std::size_t totalDiagnostics{ 0U };
};

struct StandaloneMldTextureWriteResult {
    std::filesystem::path jsonPath{};
    std::filesystem::path filesCsvPath{};
    std::filesystem::path entriesCsvPath{};
};

[[nodiscard]] StandaloneMldTextureCorpusScan scanStandaloneMldTextures(
    const std::filesystem::path& inputPath);

[[nodiscard]] StandaloneMldTextureFeedbackSummary summarizeStandaloneMldTextureFeedback(
    const StandaloneMldTextureCorpusScan& corpus);

[[nodiscard]] std::string formatStandaloneMldTextureJson(
    const StandaloneMldTextureCorpusScan& corpus);

[[nodiscard]] std::string formatStandaloneMldTextureFilesCsv(
    const StandaloneMldTextureCorpusScan& corpus);

[[nodiscard]] std::string formatStandaloneMldTextureEntriesCsv(
    const StandaloneMldTextureCorpusScan& corpus);

[[nodiscard]] StandaloneMldTextureWriteResult writeStandaloneMldTextureArtifacts(
    const StandaloneMldTextureCorpusScan& corpus,
    const std::filesystem::path& outputDir);

} // namespace spice::mll
