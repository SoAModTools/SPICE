#include "MldInspectionSupport.h"

#include "../../SpiceMLD/Parsing/MldParser.h"
#include "../../SpiceRoot/Binary/Endian.h"

#include <algorithm>

namespace spice::mix::documents {
namespace {

const char* platformName(const spice::mld::MldPlatform platform) {
    return platform == spice::mld::MldPlatform::Dreamcast ? "Dreamcast" : "GameCube";
}

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

MldEntrySnapshot projectEntry(const spice::mld::model::IndexEntry& entry) {
    return MldEntrySnapshot{
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
        .textureListId = entry.texturesPointer,
    };
}

MldU32ListSnapshot projectList(const std::shared_ptr<spice::mld::model::U32List>& list,
    const std::uint32_t rawPointer) {
    (void)rawPointer;
    if (!list) {
        return {};
    }
    return { .values = list->values };
}

} // namespace

MldOverviewSnapshot projectMldOverview(const spice::mld::MldDocument& document,
    const spice::mld::MldImportReceipt& receipt, const bool dirty) {
    return {
        .sourcePath = receipt.path.value_or(std::filesystem::path{}),
        .platform = platformName(receipt.platform),
        .endian = receipt.endian == spice::root::Endian::Little ? "Little" : "Big",
        .parseStatus = "Complete",
        .sourceWasAklz = receipt.wrapper == spice::mld::MldWrapper::Aklz,
        .entryCount = document.entries.size(),
        .textureCount = document.textureArchives.empty() ? 0U : document.textureArchives.front().textures.size(),
        .objectResourceCount = document.objects.size(),
        .groundResourceCount = document.grounds.size(),
        .motionResourceCount = document.motions.size(),
        .dirty = dirty,
    };
}

std::vector<MldEntrySnapshot> projectMldEntries(const spice::mld::MldDocument& document) {
    std::vector<MldEntrySnapshot> out{};
    out.reserve(document.entries.size());
    for (std::size_t index = 0U; index < document.entries.size(); ++index) {
        const auto& entry = document.entries[index];
        out.push_back({
            .tableIndex = index,
            .entryId = entry.entryId,
            .tableId = entry.tableId,
            .functionName = entry.functionName,
            .positionX = entry.transform.position.x,
            .positionY = entry.transform.position.y,
            .positionZ = entry.transform.position.z,
            .rotationX = entry.transform.rotationRaw.x,
            .rotationY = entry.transform.rotationRaw.y,
            .rotationZ = entry.transform.rotationRaw.z,
            .scaleX = entry.transform.scale.x,
            .scaleY = entry.transform.scale.y,
            .scaleZ = entry.transform.scale.z,
            .objectCount = entry.objectSlots.size(),
            .groundCount = entry.groundSlots.size(),
            .motionCount = entry.motionSlots.size(),
            .textureListId = entry.textureList ? static_cast<std::uint32_t>(entry.textureList->value) : 0U,
        });
    }
    return out;
}

std::vector<MldEntryDetailSnapshot> projectMldEntryDetails(const spice::mld::MldDocument& document) {
    const auto summaries = projectMldEntries(document);
    std::vector<MldEntryDetailSnapshot> out{};
    out.reserve(document.entries.size());
    for (std::size_t index = 0U; index < document.entries.size(); ++index) {
        const auto& entry = document.entries[index];
        const auto projectSlots = [](const auto& slots) {
            MldU32ListSnapshot result{};
            for (const auto& slot : slots) result.values.push_back(slot ? static_cast<std::uint32_t>(slot->value) : 0U);
            return result;
        };
        MldStringListSnapshot names{};
        if (entry.textureList) {
            const auto found = std::find_if(document.textureLists.begin(), document.textureLists.end(),
                [&](const auto& value) { return value.id == *entry.textureList; });
            if (found != document.textureLists.end()) {
                names.values = found->names;
            }
        }
        out.push_back({
            .summary = summaries[index],
            .groundLinks = { .values = entry.groundLinks },
            .paramList2 = { .values = entry.parameterList2 },
            .functionParameters = { .values = entry.functionParameters },
            .objectIds = projectSlots(entry.objectSlots),
            .groundIds = projectSlots(entry.groundSlots),
            .motionIds = projectSlots(entry.motionSlots),
            .textureNames = std::move(names),
        });
    }
    return out;
}

std::vector<MldTextureSnapshot> projectMldTextures(const spice::mld::MldDocument& document,
    const std::vector<bool>& dirtyTextures) {
    std::vector<MldTextureSnapshot> out{};
    if (document.textureArchives.empty()) return out;
    const auto& textures = document.textureArchives.front().textures;
    out.reserve(textures.size());
    for (std::size_t index = 0U; index < textures.size(); ++index) {
        const auto& texture = textures[index];
        out.push_back({
            .index = index,
            .name = texture.name,
            .encoding = encodingKind(texture.encoding),
            .format = texture.format,
            .paletteFormat = texture.paletteFormat,
            .width = texture.width,
            .height = texture.height,
            .mipmaps = texture.hasMipmaps,
            .hasGlobalIndex = texture.hasGlobalIndex,
            .globalIndex = texture.globalIndex,
            .encodedSize = texture.encodedBytes.size(),
            .decoded = texture.decoded && !texture.rgba8.empty(),
            .dirty = index < dirtyTextures.size() && dirtyTextures[index],
            .diagnostics = {},
        });
    }
    return out;
}

std::vector<DocumentDiagnostic> projectMldDiagnostics(
    const std::vector<spice::mld::MldDocumentDiagnostic>& diagnostics) {
    std::vector<DocumentDiagnostic> out{};
    for (const auto& item : diagnostics) {
        EventLevel level = EventLevel::Info;
        if (item.severity == spice::mld::MldDiagnosticSeverity::Warning) level = EventLevel::Warning;
        if (item.severity == spice::mld::MldDiagnosticSeverity::Error) level = EventLevel::Error;
        out.push_back({
            .level = level,
            .message = item.message,
            .sourceOffset = item.decodedOffset.has_value()
                ? std::optional<std::uint32_t>{ static_cast<std::uint32_t>(*item.decodedOffset) }
                : std::nullopt,
        });
    }
    return out;
}

std::optional<RgbaImageSnapshot> projectMldTexturePreview(
    const spice::mld::MldDocument& document, const std::size_t index) {
    if (document.textureArchives.empty() || index >= document.textureArchives.front().textures.size()) return std::nullopt;
    const auto& texture = document.textureArchives.front().textures[index];
    if (!texture.decoded || texture.rgba8.empty()) return std::nullopt;
    return RgbaImageSnapshot{ .width = texture.width, .height = texture.height, .rgba8 = texture.rgba8 };
}

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
        out.push_back(projectEntry(record.entry));
    }
    return out;
}

std::vector<MldEntryDetailSnapshot> projectMldEntryDetails(const spice::mld::model::MldFile& file) {
    spice::mld::parsing::ParseOptions options{};
    options.entryListOnly = true;
    options.buildBlenderIntermediateIr = false;
    const auto compatibility = spice::mld::parsing::MldParser{}.project(file, options);

    std::vector<MldEntryDetailSnapshot> out{};
    out.reserve(file.entries.size());
    for (const auto& record : file.entries) {
        const auto projected = std::find_if(compatibility.entryList.begin(), compatibility.entryList.end(),
            [&record](const auto& candidate) { return candidate.tableIndex == record.entry.tableIndex; });
        MldStringListSnapshot textureNames{};
        if (projected != compatibility.entryList.end()) {
            textureNames.values = projected->textureNames;
        }
        out.push_back({
            .summary = projectEntry(record.entry),
            .groundLinks = projectList(record.entry.groundLinks, record.groundLinksPointer),
            .paramList2 = projectList(record.entry.paramList2, record.paramList2Pointer),
            .functionParameters = projectList(record.entry.functionParameters, record.functionParametersPointer),
            .objectIds = projectList(record.entry.objectAddresses, record.objectAddressesPointer),
            .groundIds = projectList(record.entry.groundAddresses, record.groundAddressesPointer),
            .motionIds = projectList(record.entry.motionAddresses, record.motionAddressesPointer),
            .textureNames = std::move(textureNames),
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
        std::vector<std::string> textureDiagnostics{};
        textureDiagnostics.reserve(texture.diagnostics.size());
        for (const auto& diagnostic : texture.diagnostics)
            textureDiagnostics.push_back(diagnostic.message);
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
            .diagnostics = std::move(textureDiagnostics),
        });
    }
    return out;
}

std::vector<DocumentDiagnostic> projectMldDiagnostics(const spice::mld::model::MldFile& file) {
    std::vector<DocumentDiagnostic> out{};
    out.reserve(file.parseDiagnostics.size());
    for (const auto& diagnostic : file.parseDiagnostics) out.push_back(convertDiagnostic(diagnostic));
    const auto appendResources = [&](const auto& resources) {
        for (const auto& [_, resource] : resources)
            for (const auto& diagnostic : resource.diagnostics)
                out.push_back(convertDiagnostic(diagnostic));
    };
    appendResources(file.objectResources);
    appendResources(file.motionResources);
    appendResources(file.groundResources);
    appendResources(file.textureListResources);
    if (file.textureArchive.has_value()) {
        for (const auto& diagnostic : file.textureArchive->diagnostics)
            out.push_back(convertDiagnostic(diagnostic));
        for (const auto& texture : file.textureArchive->entries)
            for (const auto& diagnostic : texture.diagnostics)
                out.push_back(convertDiagnostic(diagnostic));
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
