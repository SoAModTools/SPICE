#include "MldInspectionSupport.h"

#include "../../SpiceRoot/Binary/Endian.h"

namespace spice::mix::documents {
namespace {

const char* platformName(const spice::mld::model::TargetPlatform platform) {
    switch (platform) {
    case spice::mld::model::TargetPlatform::Dreamcast: return "Dreamcast";
    case spice::mld::model::TargetPlatform::GameCube: return "GameCube";
    default: return "Unknown";
    }
}

const char* statusName(const spice::mld::model::MldParseStatus status) {
    switch (status) {
    case spice::mld::model::MldParseStatus::Empty: return "Empty";
    case spice::mld::model::MldParseStatus::Partial: return "Partial";
    case spice::mld::model::MldParseStatus::Complete: return "Complete";
    case spice::mld::model::MldParseStatus::Failed: return "Failed";
    }
    return "Unknown";
}

TextureEncodingKind encodingKind(const spice::mld::model::MldTextureEncoding encoding) {
    switch (encoding) {
    case spice::mld::model::MldTextureEncoding::Gvr: return TextureEncodingKind::Gvr;
    case spice::mld::model::MldTextureEncoding::Pvr: return TextureEncodingKind::Pvr;
    default: return TextureEncodingKind::Unknown;
    }
}

DocumentDiagnostic convertDiagnostic(const spice::mld::model::MldDiagnostic& diagnostic) {
    EventLevel level = EventLevel::Info;
    if (diagnostic.severity == spice::mld::model::MldDiagnostic::Severity::Warning) level = EventLevel::Warning;
    if (diagnostic.severity == spice::mld::model::MldDiagnostic::Severity::Error) level = EventLevel::Error;
    return { .level = level, .message = diagnostic.message, .sourceOffset = diagnostic.sourceOffset };
}

} // namespace

MldOverviewSnapshot projectMldOverview(const spice::mld::model::MldFile& file,
    const std::filesystem::path& sourcePath, const bool dirty) {
    MldOverviewSnapshot out{};
    out.sourcePath = sourcePath;
    out.platform = platformName(file.sourcePlatform);
    out.endian = file.endian == spice::root::Endian::Little ? "Little" : "Big";
    out.parseStatus = statusName(file.parseStatus);
    out.sourceWasAklz = file.sourceWasCompressedAklz;
    out.entryCount = file.entries.size();
    out.textureCount = file.textureArchive.has_value() ? file.textureArchive->entries.size() : 0;
    out.objectResourceCount = file.objectResources.size();
    out.groundResourceCount = file.groundResources.size();
    out.motionResourceCount = file.motionResources.size();
    out.dirty = dirty;
    return out;
}

std::vector<MldEntrySnapshot> projectMldEntries(const spice::mld::model::MldFile& file) {
    std::vector<MldEntrySnapshot> out{};
    out.reserve(file.entries.size());
    for (const auto& record : file.entries) {
        const auto& entry = record.entry;
        out.push_back(MldEntrySnapshot{
            .tableIndex = entry.tableIndex,
            .entryId = entry.entryId,
            .tableId = entry.tblId,
            .functionName = entry.fxnName,
            .positionX = entry.transform.position.x,
            .positionY = entry.transform.position.y,
            .positionZ = entry.transform.position.z,
            .rotationX = entry.transform.rotationRaw.x,
            .rotationY = entry.transform.rotationRaw.y,
            .rotationZ = entry.transform.rotationRaw.z,
            .scaleX = entry.transform.scale.x,
            .scaleY = entry.transform.scale.y,
            .scaleZ = entry.transform.scale.z,
            .objectCount = entry.objectCount,
            .groundCount = entry.groundCount,
            .motionCount = entry.motionCount,
            .texturesPointer = entry.texturesPointer,
        });
    }
    return out;
}

std::vector<MldTextureSnapshot> projectMldTextures(const spice::mld::model::MldFile& file,
    const std::vector<bool>& dirtyTextures) {
    std::vector<MldTextureSnapshot> out{};
    if (!file.textureArchive.has_value()) return out;
    out.reserve(file.textureArchive->entries.size());
    for (std::size_t index = 0; index < file.textureArchive->entries.size(); ++index) {
        const auto& texture = file.textureArchive->entries[index];
        out.push_back(MldTextureSnapshot{
            .index = index,
            .name = texture.textureName,
            .encoding = encodingKind(texture.encoding),
            .format = texture.sourceFormat,
            .paletteFormat = texture.sourcePaletteFormat,
            .width = texture.width,
            .height = texture.height,
            .mipmaps = texture.hasMipmaps,
            .hasGlobalIndex = texture.hasGlobalIndex,
            .globalIndex = texture.globalIndex,
            .encodedSize = texture.encodedData.size(),
            .decoded = texture.decoded && !texture.rgba8.empty(),
            .dirty = index < dirtyTextures.size() && dirtyTextures[index],
            .diagnostics = texture.diagnostics,
        });
    }
    return out;
}

std::vector<DocumentDiagnostic> projectMldDiagnostics(const spice::mld::model::MldFile& file) {
    std::vector<DocumentDiagnostic> out{};
    out.reserve(file.parseDiagnostics.size());
    for (const auto& diagnostic : file.parseDiagnostics) out.push_back(convertDiagnostic(diagnostic));
    if (file.textureArchive.has_value()) {
        for (const auto& message : file.textureArchive->diagnostics) {
            out.push_back({ .level = EventLevel::Warning, .message = message });
        }
    }
    return out;
}

std::optional<RgbaImageSnapshot> projectMldTexturePreview(
    const spice::mld::model::MldFile& file, const std::size_t index) {
    if (!file.textureArchive.has_value() || index >= file.textureArchive->entries.size()) return std::nullopt;
    const auto& texture = file.textureArchive->entries[index];
    if (!texture.decoded || texture.rgba8.empty()) return std::nullopt;
    return RgbaImageSnapshot{ .width = texture.width, .height = texture.height, .rgba8 = texture.rgba8 };
}

} // namespace spice::mix::documents
