#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <variant>

namespace spice::mix {

enum class AklzPolicy {
    Preserve,
    Raw,
    Compressed,
};

enum class GvrTextureFormat {
    I4,
    I8,
    IA4,
    IA8,
    RGB565,
    RGB5A3,
    RGBA8,
    CI4,
    CI8,
    CI14X2,
    CMPR,
};

enum class GvrPaletteFormat {
    IA8,
    RGB565,
    RGB5A3,
};

enum class GvrGlobalIndexKind {
    Preserve,
    None,
    Value,
};

struct GvrGlobalIndex {
    GvrGlobalIndexKind kind = GvrGlobalIndexKind::None;
    std::uint32_t value = 0;
};

struct GvrEncodingSettings {
    std::optional<GvrTextureFormat> format{};
    bool preserveFormat = false;
    std::optional<GvrPaletteFormat> paletteFormat{};
    bool preservePaletteFormat = false;
    std::optional<bool> mipmaps{};
    bool preserveMipmaps = false;
    GvrGlobalIndex globalIndex{};
    AklzPolicy aklz = AklzPolicy::Raw;
};

struct DirectoryPaths {
    std::filesystem::path input{};
    std::filesystem::path output{};
    std::optional<std::filesystem::path> decompressedOutput{};
};

struct TextureIndex {
    std::size_t value = 0;
};

struct TextureName {
    std::string value{};
};

using MldTextureSelector = std::variant<TextureIndex, TextureName>;

struct ParseMldRequest {
    DirectoryPaths paths{};
    bool extractGrndGobjBlocks = false;
};

struct ExportMldEntryListRequest {
    DirectoryPaths paths{};
};

struct InventoryMldGvrFormatsRequest {
    DirectoryPaths paths{};
};

struct ReplaceMldTextureRequest {
    std::filesystem::path source{};
    std::filesystem::path replacement{};
    std::filesystem::path output{};
    MldTextureSelector selector{};
    GvrEncodingSettings encoding{};
    bool allowDimensionChange = false;
    bool allowPostArchiveShift = false;
};

struct ExtractMldTextureGvrRequest {
    std::filesystem::path input{};
    std::filesystem::path output{};
    MldTextureSelector selector{};
};

struct ExtractMldTexturePngRequest {
    std::filesystem::path input{};
    std::filesystem::path output{};
    MldTextureSelector selector{};
    std::optional<std::filesystem::path> gvrOutput{};
};

struct ParseSctRequest {
    DirectoryPaths paths{};
    bool decodeUnreachedCode = false;
};

struct ExportSctRequest {
    DirectoryPaths paths{};
    bool compressAklz = false;
    bool decodeUnreachedCode = false;
};

enum class CombinedPlacement {
    Sst,
    Raw,
};

struct ExportSmlResearchRequest {
    std::filesystem::path input{};
    std::filesystem::path output{};
    std::filesystem::path annotationRepository{};
    bool embeddedMld = false;
    bool embeddedMldBlenderIr = false;
    bool combinedBlenderIr = false;
    bool commandMap = false;
    CombinedPlacement combinedPlacement = CombinedPlacement::Sst;
};

struct ExportStdJsonRequest {
    std::filesystem::path input{};
    std::filesystem::path output{};
};

struct ExportMlkCorpusRequest {
    std::filesystem::path input{};
    std::filesystem::path output{};
};

struct ExportMlkBlenderIrRequest {
    std::filesystem::path input{};
    std::filesystem::path output{};
    std::filesystem::path annotationRepository{};
    bool overwriteAnnotations = false;
};

enum class ContentGraphProjection {
    Full,
    Sections,
    World,
};

struct ExportContentGraphRequest {
    DirectoryPaths paths{};
    ContentGraphProjection projection = ContentGraphProjection::Full;
};

struct ExportAlxEnemyEventsRequest {
    std::filesystem::path input{};
    std::filesystem::path output{};
};

struct AuditDreamcastParityRequest {
    std::filesystem::path dreamcastUs{};
    std::filesystem::path gameCubeUs{};
    std::optional<std::filesystem::path> dreamcastEuDisc1{};
    std::optional<std::filesystem::path> dreamcastEuDisc2{};
    std::optional<std::filesystem::path> gameCubeEu{};
    std::filesystem::path output{};
};

struct CreateGvrRequest {
    std::filesystem::path input{};
    std::filesystem::path output{};
    GvrEncodingSettings encoding{};
};

struct ReplaceGvrRequest {
    std::filesystem::path source{};
    std::filesystem::path replacement{};
    std::filesystem::path output{};
    GvrEncodingSettings encoding{};
};

struct GvrToPngRequest {
    std::filesystem::path input{};
    std::filesystem::path output{};
};

struct CreateGvrBatchRequest {
    std::filesystem::path input{};
    std::filesystem::path output{};
    GvrEncodingSettings encoding{};
};

struct ReplaceGvrBatchRequest {
    std::filesystem::path input{};
    std::filesystem::path sourceGvrDirectory{};
    std::filesystem::path output{};
    GvrEncodingSettings encoding{};
};

struct ExportGvrImageIrRequest {
    std::filesystem::path input{};
    std::filesystem::path output{};
};

struct ImportGvrImageIrRequest {
    std::filesystem::path input{};
    std::filesystem::path output{};
    AklzPolicy aklz = AklzPolicy::Preserve;
};

struct CompressAklzRequest {
    std::filesystem::path input{};
    std::filesystem::path output{};
};

struct DecompressAklzRequest {
    std::filesystem::path input{};
    std::filesystem::path output{};
};

using OperationRequest = std::variant<
    ParseMldRequest,
    ExportMldEntryListRequest,
    InventoryMldGvrFormatsRequest,
    ReplaceMldTextureRequest,
    ExtractMldTextureGvrRequest,
    ExtractMldTexturePngRequest,
    ParseSctRequest,
    ExportSctRequest,
    ExportSmlResearchRequest,
    ExportStdJsonRequest,
    ExportMlkCorpusRequest,
    ExportMlkBlenderIrRequest,
    ExportContentGraphRequest,
    ExportAlxEnemyEventsRequest,
    AuditDreamcastParityRequest,
    CreateGvrRequest,
    ReplaceGvrRequest,
    GvrToPngRequest,
    CreateGvrBatchRequest,
    ReplaceGvrBatchRequest,
    ExportGvrImageIrRequest,
    ImportGvrImageIrRequest,
    CompressAklzRequest,
    DecompressAklzRequest>;

enum class EventLevel {
    Info,
    Warning,
    Error,
    Progress,
};

struct OperationEvent {
    EventLevel level = EventLevel::Info;
    std::string message{};
};

struct OperationContext {
    std::filesystem::path executableDirectory{};
    std::function<void(const OperationEvent&)> report{};
    std::stop_token stopToken{};
};

enum class OperationStatus {
    Success,
    Failure,
    Cancelled,
};

struct OperationResult {
    OperationStatus status = OperationStatus::Success;
    std::size_t filesProcessed = 0;
};

} // namespace spice::mix
