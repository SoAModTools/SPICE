#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace spice::mll {

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error,
};

struct MllDiagnostic {
    DiagnosticSeverity severity{ DiagnosticSeverity::Info };
    std::string message{};
    std::uint32_t offset{ 0U };
};

enum class MllPayloadKind {
    Empty,
    Unknown,
    AklzCompressed,
    MldFile,
    NinjaChunk,
    Pof0,
};

enum class MllMemberCountSource {
    HeaderU16At04,
    FirstMemberOffset,
    Unresolved,
};

enum class MllTableShape {
    Normal,
    FirstMemberCountCandidate,
    MalformedMemberSpans,
};

struct MllEmbeddedMldHeaderProbe {
    bool plausible{ false };
    std::uint32_t entryCount{ 0U };
    std::uint32_t indexTableOffset{ 0U };
    std::uint32_t functionParametersOffset{ 0U };
    std::uint32_t realDataOffset{ 0U };
    std::uint32_t textureTableOffset{ 0U };
    bool indexEntryShapePlausible{ false };
    std::uint32_t printableFunctionNameCount{ 0U };
    std::uint32_t emptyFunctionNameCount{ 0U };
    std::uint32_t suspiciousFunctionNameCount{ 0U };
    std::uint32_t nonZeroCountedListFieldCount{ 0U };
    std::uint32_t plausibleCountedListFieldCount{ 0U };
};

struct MllEmbeddedMldObjectListTargetSample {
    std::uint32_t sampleIndex{ 0U };
    std::uint32_t pointerOffset{ 0U };
    std::uint32_t targetOffset{ 0U };
    bool pointerNonZero{ false };
    bool targetOffsetAligned{ false };
    bool targetInBounds{ false };
    bool targetLooksPlausible{ false };
    std::uint32_t listBaseTargetOffset{ 0U };
    bool listBaseTargetLooksPlausible{ false };
    std::uint32_t entryBaseTargetOffset{ 0U };
    bool entryBaseTargetLooksPlausible{ false };
    std::uint32_t pointerBaseTargetOffset{ 0U };
    bool pointerBaseTargetLooksPlausible{ false };
    std::uint32_t targetWord0{ 0U };
    std::string targetSignature{};
    std::string targetBytes16Hex{};
};

struct MllEmbeddedMldObjectListProbe {
    std::uint32_t entryIndex{ 0U };
    std::uint32_t entryOffset{ 0U };
    std::uint32_t fieldOffset{ 0U };
    std::uint32_t listOffset{ 0U };
    bool listOffsetNonZero{ false };
    bool listOffsetAligned{ false };
    bool listHeaderInBounds{ false };
    std::uint32_t declaredCount{ 0U };
    bool listEntriesInBounds{ false };
    bool listLooksPlausible{ false };
    std::string listBytes32Hex{};
    std::uint32_t sampledPointerCount{ 0U };
    std::uint32_t nonNullSampledPointerCount{ 0U };
    std::vector<MllEmbeddedMldObjectListTargetSample> targetSamples{};
};

struct MllEmbeddedBlockProbe {
    std::uint32_t blockOffset{ 0U };
    std::string tag{};
    bool offsetAligned{ false };
    std::uint32_t declaredSizeBe{ 0U };
    bool declaredSizeBePlausible{ false };
    std::uint32_t declaredSizeLe{ 0U };
    bool declaredSizeLePlausible{ false };
    bool atHeaderRealDataOffset{ false };
    bool atHeaderTextureTableOffset{ false };
    std::uint32_t exactCountedListReferenceCount{ 0U };
    std::uint32_t plus8CountedListReferenceCount{ 0U };
    std::uint32_t minus8CountedListReferenceCount{ 0U };
    std::string firstExactCountedListReference{};
    std::string bytes32Hex{};
};

struct MllEmbeddedGvrTextureProbe {
    std::uint32_t textureIndex{ 0U };
    std::uint32_t gcixOffset{ 0U };
    std::uint32_t gvrtOffset{ 0U };
    std::uint32_t pairDistance{ 0U };
    std::uint32_t gcixPayloadSizeLe{ 0U };
    std::uint32_t gvrtPayloadSizeLe{ 0U };
    std::uint32_t sourceSize{ 0U };
    bool recordInBounds{ false };
    bool parseAttempted{ false };
    bool parseHasFailureDiagnostics{ false };
    bool hasGlobalIndex{ false };
    std::uint32_t globalIndex{ 0U };
    std::uint32_t rawFlags{ 0U };
    std::uint32_t rawDataFormat{ 0U };
    std::string textureFormat{};
    std::string paletteFormat{};
    bool hasMipmaps{ false };
    bool hasInternalPalette{ false };
    std::uint32_t width{ 0U };
    std::uint32_t height{ 0U };
    std::uint32_t imageDataOffset{ 0U };
    std::uint32_t imageDataSize{ 0U };
    bool decodedBaseLevelPresent{ false };
    std::uint32_t decodedRgba8Size{ 0U };
    std::uint32_t diagnosticCount{ 0U };
    std::string diagnosticsJoined{};
};

struct MllTextureTableProbe {
    bool hasTextures{ false };
    std::uint32_t textureCount{ 0U };
    std::uint32_t firstTextureOffset{ 0U };
    std::uint32_t lastTextureEndOffset{ 0U };
    std::uint32_t textureSpanSize{ 0U };
    std::uint32_t clusterCount{ 0U };
    std::uint32_t largestGapBetweenRecords{ 0U };
    bool allRecordsInBounds{ false };
    bool allTexturesParsed{ false };
    bool allTexturesDecoded{ false };
    bool allTexturesHaveGlobalIndex{ false };
    std::uint32_t globalIndexMin{ 0U };
    std::uint32_t globalIndexMax{ 0U };
    std::uint32_t uniqueGlobalIndexCount{ 0U };
    std::uint32_t duplicateGlobalIndexCount{ 0U };
    std::uint32_t missingGlobalIndexCount{ 0U };
    bool globalIndexSequenceDense{ false };
    bool globalIndexSequenceStartsAtZero{ false };
    std::string globalIndexSequencePreview{};
    bool headerTextureTableOffsetNonZero{ false };
    bool headerTextureTableOffsetAtFirstTexture{ false };
    bool headerTextureTableOffsetInsideTextureSpan{ false };
    bool headerTextureTableOffsetBeforeFirstTexture{ false };
    std::int32_t headerTextureTableOffsetDeltaToFirstTexture{ 0 };
    bool headerRealDataOffsetNonZero{ false };
    bool headerRealDataOffsetAtFirstTexture{ false };
    bool headerRealDataOffsetInsideTextureSpan{ false };
    bool headerRealDataOffsetBeforeFirstTexture{ false };
    std::int32_t headerRealDataOffsetDeltaToFirstTexture{ 0 };
    std::uint32_t indexTexturePointerCount{ 0U };
    std::uint32_t nonZeroIndexTexturePointerCount{ 0U };
    std::uint32_t indexTexturePointerAtFirstTextureCount{ 0U };
    std::uint32_t indexTexturePointerInsideTextureSpanCount{ 0U };
    std::uint32_t indexTexturePointerBeforeFirstTextureCount{ 0U };
    std::uint32_t uniqueIndexTexturePointerCount{ 0U };
    std::string indexTexturePointerValuesPreview{};
    std::string nearbyPrintableStrings{};
};

struct MllPreTextureTableEntryProbe {
    std::uint32_t entryIndex{ 0U };
    std::uint32_t entryOffset{ 0U };
    std::string name{};
    bool namePrintable{ false };
    bool nameEmpty{ false };
    bool nameMatchesKnownTextureGlobalIndex{ false };
    std::uint32_t matchedGlobalIndex{ 0U };
    std::uint32_t word10{ 0U };
    std::uint32_t word14{ 0U };
    std::uint32_t word18{ 0U };
    std::uint32_t word1c{ 0U };
    std::uint32_t word20{ 0U };
    std::uint32_t word24{ 0U };
    std::uint32_t word28{ 0U };
    std::string bytes32Hex{};
    bool orderTexturePresent{ false };
    std::uint32_t orderTextureIndex{ 0U };
    std::uint32_t orderTextureGcixOffset{ 0U };
    std::uint32_t orderTextureGvrtOffset{ 0U };
    std::uint32_t orderTextureSourceSize{ 0U };
    bool orderTextureHasGlobalIndex{ false };
    std::uint32_t orderTextureGlobalIndex{ 0U };
    std::uint32_t orderTextureRawFlags{ 0U };
    std::uint32_t orderTextureRawDataFormat{ 0U };
    std::string orderTextureFormat{};
    std::string orderTexturePaletteFormat{};
    bool orderTextureHasMipmaps{ false };
    bool orderTextureHasInternalPalette{ false };
    std::uint32_t orderTextureWidth{ 0U };
    std::uint32_t orderTextureHeight{ 0U };
    std::uint32_t orderTextureImageDataSize{ 0U };
    bool orderTextureDecoded{ false };
    bool nameSuffixMatchesOrderTextureGlobalIndex{ false };
};

struct MllPreTextureTableProbe {
    bool present{ false };
    bool spanInBounds{ false };
    bool spanAlignedTo20{ false };
    bool recordsFit{ false };
    bool declaredCountMatchesTextureCount{ false };
    std::uint32_t tableOffset{ 0U };
    std::uint32_t tableEndOffset{ 0U };
    std::uint32_t tableSize{ 0U };
    std::uint32_t declaredEntryCount{ 0U };
    std::uint32_t entryStride{ 0x2cU };
    std::uint32_t entryCount{ 0U };
    std::uint32_t trailingPaddingSize{ 0U };
    std::uint32_t printableNameCount{ 0U };
    std::uint32_t emptyNameCount{ 0U };
    std::uint32_t textureNameMatchCount{ 0U };
    std::string entryNamePreview{};
    std::vector<MllPreTextureTableEntryProbe> entries{};
};

struct MllIndexedBinRecordSample {
    std::uint32_t sampleIndex{ 0U };
    std::uint32_t tableOffset{ 0U };
    std::uint32_t recordOffset{ 0U };
    bool recordInBounds{ false };
    std::uint32_t word0{ 0U };
    bool word0EqualsDataBaseOffset{ false };
    std::uint32_t word4{ 0U };
    bool word4TargetInBounds{ false };
    std::uint32_t word8{ 0U };
    std::uint32_t word12{ 0U };
    std::uint32_t word16{ 0U };
    std::uint32_t word20{ 0U };
    std::uint32_t word24{ 0U };
    std::string bytes16Hex{};
    std::string bytes32Hex{};
};

struct MllIndexedBinTableProbe {
    bool present{ false };
    bool headerInBounds{ false };
    std::uint32_t count{ 0U };
    std::uint32_t offsetTableOffset{ 0x04U };
    std::uint32_t offsetTableEndOffset{ 0U };
    std::uint32_t dataBaseOffset{ 0U };
    bool offsetTableInBounds{ false };
    bool offsetsInBounds{ false };
    bool offsetsMonotonic{ false };
    std::uint32_t firstRecordOffset{ 0U };
    std::uint32_t lastRecordOffset{ 0U };
    std::uint32_t sampledRecordCount{ 0U };
    std::string offsetsPreview{};
    std::vector<MllIndexedBinRecordSample> samples{};
};

struct MllMember {
    std::size_t index{ 0U };
    std::uint32_t recordOffset{ 0U };
    std::string name{};
    std::uint32_t payloadOffset{ 0U };
    std::uint32_t payloadSize{ 0U };
    std::uint32_t rawWord1c{ 0U };
    bool payloadInBounds{ false };
    bool payloadOverlapsMemberTable{ false };
    MllPayloadKind payloadKind{ MllPayloadKind::Unknown };
    std::string payloadSignature{};
    MllEmbeddedMldHeaderProbe embeddedMldHeader{};
    std::vector<MllEmbeddedMldObjectListProbe> embeddedMldObjectListProbes{};
    std::vector<MllEmbeddedBlockProbe> embeddedBlockProbes{};
    std::vector<MllEmbeddedGvrTextureProbe> embeddedGvrTextureProbes{};
    MllTextureTableProbe textureTableProbe{};
    MllPreTextureTableProbe preTextureTableProbe{};
    MllIndexedBinTableProbe indexedBinTableProbe{};
};

struct MllFile {
    std::string sourcePath{};
    bool sourceWasCompressedAklz{ false };
    std::uint32_t rawSize{ 0U };
    std::uint32_t decodedSize{ 0U };
    std::uint32_t headerWord0{ 0U };
    std::uint32_t countWord{ 0U };
    std::int16_t signedMemberCountCandidate{ 0 };
    std::uint16_t memberCountCandidate{ 0U };
    std::uint16_t selectedMemberCount{ 0U };
    MllMemberCountSource memberCountSource{ MllMemberCountSource::Unresolved };
    std::uint32_t recordsOffset{ 0x08U };
    std::uint32_t recordStride{ 0x20U };
    std::uint32_t memberTableEndOffset{ 0U };
    std::uint32_t firstMemberOffset{ 0U };
    std::uint16_t memberCountInferredFromFirstMemberOffset{ 0U };
    bool memberCountMatchesFirstMemberOffset{ false };
    bool memberTableInBounds{ false };
    bool supported{ false };
    MllTableShape tableShape{ MllTableShape::Normal };
    std::vector<MllMember> members{};
    std::vector<MllDiagnostic> diagnostics{};
    std::vector<std::uint8_t> originalDecodedBytes{};

    [[nodiscard]] bool ok() const;
};

[[nodiscard]] const char* toString(DiagnosticSeverity severity);
[[nodiscard]] const char* toString(MllPayloadKind kind);
[[nodiscard]] const char* toString(MllMemberCountSource source);
[[nodiscard]] const char* toString(MllTableShape shape);

} // namespace spice::mll
