#include "MldDocumentSession.h"

#include "DocumentSupport.h"
#include "MldInspectionSupport.h"
#include "PvrDocumentSupport.h"
#include "../../SpiceGvm/Image/PngCodec.h"
#include "../../SpiceMLD/Export/BlenderIrJsonExporter.h"
#include "../../SpiceMLD/Export/MldEntryListJsonExporter.h"
#include "../../SpiceMLD/MldBlenderIrProjector.h"
#include "../../SpiceMLD/MldDocumentImporter.h"
#include "../../SpiceMLD/MldDocumentWriter.h"

#include <algorithm>
#include <span>
#include <stdexcept>
#include <utility>

namespace spice::mix {

struct MldDocumentSession::Impl {
    std::filesystem::path protectedSourcePath{};
    spice::mld::MldDocument document{};
    spice::mld::MldImportReceipt receipt{};
    std::vector<spice::mld::MldDocumentDiagnostic> importDiagnostics{};
    std::vector<spice::mld::MldTexture> savedTextures{};
    std::vector<bool> dirtyTextures{};
};

namespace {

template <typename ImplType>
spice::mld::MldTexture* textureAt(ImplType& impl, const std::size_t index) {
    if (impl.document.textureArchives.empty() || index >= impl.document.textureArchives.front().textures.size()) return nullptr;
    return &impl.document.textureArchives.front().textures[index];
}

template <typename ImplType>
const spice::mld::MldTexture* textureAt(const ImplType& impl, const std::size_t index) {
    if (impl.document.textureArchives.empty() || index >= impl.document.textureArchives.front().textures.size()) return nullptr;
    return &impl.document.textureArchives.front().textures[index];
}

} // namespace

MldDocumentSession::MldDocumentSession(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

MldDocumentSession::~MldDocumentSession() = default;
MldDocumentSession::MldDocumentSession(MldDocumentSession&&) noexcept = default;
MldDocumentSession& MldDocumentSession::operator=(MldDocumentSession&&) noexcept = default;

MldDocumentSession::OpenResult MldDocumentSession::open(
    const std::filesystem::path& path, const DocumentContext& context) {
    if (context.stopToken.stop_requested()) return { .result = documents::cancelled() };
    try {
        if (path.empty() || !std::filesystem::is_regular_file(path)) {
            return { .result = documents::failure("A readable MLD input file is required.") };
        }
        documents::emit(context, EventLevel::Progress, "Opening MLD " + path.string());
        const auto bytes = documents::readBytes(path);
        if (bytes.empty()) return { .result = documents::failure("The MLD input file is empty.") };
        if (context.stopToken.stop_requested()) return { .result = documents::cancelled() };
        auto impl = std::make_unique<Impl>();
        impl->protectedSourcePath = path;
        auto imported = spice::mld::MldDocumentImporter::importBytes(bytes);
        if (context.stopToken.stop_requested()) return { .result = documents::cancelled() };
        std::vector<std::string> diagnosticText{};
        for (const auto& diagnostic : documents::projectMldDiagnostics(imported.diagnostics))
            diagnosticText.push_back(diagnostic.message);
        if (!imported.ok() || !imported.document.has_value()) {
            return { .result = documents::failure("MLD parsing failed.", std::move(diagnosticText)) };
        }
        impl->document = std::move(*imported.document);
        impl->receipt = std::move(imported.receipt);
        impl->receipt.path = path;
        impl->importDiagnostics = std::move(imported.diagnostics);
        if (!impl->document.textureArchives.empty()) {
            impl->savedTextures = impl->document.textureArchives.front().textures;
            impl->dirtyTextures.resize(impl->savedTextures.size(), false);
        }
        auto session = std::shared_ptr<MldDocumentSession>(new MldDocumentSession(std::move(impl)));
        documents::emit(context, EventLevel::Info, "Opened MLD " + path.filename().string());
        return { .session = std::move(session),
            .result = { .message = "MLD document is ready.", .diagnostics = std::move(diagnosticText) } };
    } catch (const std::exception& error) {
        documents::emit(context, EventLevel::Error, error.what());
        return { .result = documents::failure(error.what()) };
    }
}

MldOverviewSnapshot MldDocumentSession::overview() const {
    return documents::projectMldOverview(impl_->document, impl_->receipt, dirty());
}

std::vector<MldEntrySnapshot> MldDocumentSession::entries() const {
    return documents::projectMldEntries(impl_->document);
}

std::vector<MldEntryDetailSnapshot> MldDocumentSession::entryDetails() const {
    return documents::projectMldEntryDetails(impl_->document);
}

std::vector<MldTextureSnapshot> MldDocumentSession::textures() const {
    return documents::projectMldTextures(impl_->document, impl_->dirtyTextures);
}

std::vector<DocumentDiagnostic> MldDocumentSession::diagnostics() const {
    return documents::projectMldDiagnostics(impl_->importDiagnostics);
}

std::optional<RgbaImageSnapshot> MldDocumentSession::texturePreview(const std::size_t index) const {
    return documents::projectMldTexturePreview(impl_->document, index);
}

bool MldDocumentSession::dirty() const noexcept {
    return std::any_of(impl_->dirtyTextures.begin(), impl_->dirtyTextures.end(), [](const bool value) { return value; });
}

DocumentResult MldDocumentSession::replaceGvrTexture(const std::size_t index,
    const std::filesystem::path& pngPath, const GvrEncodingOverrides& overrides,
    const bool allowDimensionChange, const DocumentContext& context) {
    if (context.stopToken.stop_requested()) return documents::cancelled();
    auto* texture = textureAt(*impl_, index);
    if (!texture) return documents::failure("The selected MLD texture index is out of range.");
    if (texture->encoding != spice::mld::model::MldTextureEncoding::Gvr) {
        return documents::failure("The selected MLD texture is not a GVR texture.");
    }
    try {
        if (pngPath.empty() || !std::filesystem::is_regular_file(pngPath)) {
            return documents::failure("A readable replacement PNG is required.");
        }
        const auto source = spice::gvm::ir::readGvrSourceMetadata(texture->encodedBytes);
        const auto image = spice::gvm::image::readPngRgba8(pngPath);
        if (!allowDimensionChange && (image.width != source.texture.width || image.height != source.texture.height)) {
            return documents::failure("Replacement PNG dimensions do not match the selected MLD texture.");
        }
        if (context.stopToken.stop_requested()) return documents::cancelled();
        const auto encodeOptions = documents::makeEncoding(overrides, source.texture);
        const auto replacement = spice::gvm::encoding::encodeGvr(image, encodeOptions);
        const auto replacementMetadata = spice::gvm::ir::readGvrSourceMetadata(replacement);
        if (replacementMetadata.texture.textureFormat == spice::gvm::model::TextureFormat::Unknown
            || !replacementMetadata.texture.decodedBaseLevel.has_value()) {
            return documents::failure("The staged replacement could not be decoded.", replacementMetadata.diagnostics);
        }
        texture->encodedBytes = replacement;
        texture->hasGlobalIndex = replacementMetadata.texture.hasGlobalIndex;
        texture->globalIndex = replacementMetadata.texture.globalIndex;
        texture->pixelFormat = replacementMetadata.texture.rawFlags;
        texture->dataFormat = replacementMetadata.texture.rawDataFormat;
        texture->format = spice::gvm::model::to_string(replacementMetadata.texture.textureFormat);
        texture->paletteFormat = spice::gvm::model::to_string(replacementMetadata.texture.paletteFormat);
        texture->hasMipmaps = replacementMetadata.texture.hasMipmaps;
        texture->hasInternalPalette = replacementMetadata.texture.hasInternalPalette;
        texture->width = replacementMetadata.texture.width;
        texture->height = replacementMetadata.texture.height;
        texture->decoded = true;
        texture->rgba8 = replacementMetadata.texture.decodedBaseLevel->rgba8;
        impl_->dirtyTextures[index] = true;
        documents::emit(context, EventLevel::Info, "Staged replacement for MLD texture " + std::to_string(index) + ".");
        return { .message = "Texture replacement staged.", .diagnostics = replacementMetadata.diagnostics };
    } catch (const std::exception& error) {
        documents::emit(context, EventLevel::Error, error.what());
        return documents::failure(error.what());
    }
}

DocumentResult MldDocumentSession::replacePvrTexture(const std::size_t index,
    const std::filesystem::path& pngPath, const PvrEncodingOverrides& overrides,
    const bool allowDimensionChange, const DocumentContext& context) {
    if (context.stopToken.stop_requested()) return documents::cancelled();
    auto* texture = textureAt(*impl_, index);
    if (!texture) return documents::failure("The selected MLD texture index is out of range.");
    if (texture->encoding != spice::mld::model::MldTextureEncoding::Pvr) {
        return documents::failure("The selected MLD texture is not a PVR texture.");
    }
    documents::emit(context, EventLevel::Progress,
        "Encoding replacement for MLD PVR texture " + std::to_string(index) + ".");
    auto encoded = documents::encodePvrFromPng(pngPath, overrides, texture->encodedBytes);
    if (!encoded.result.ok() || !encoded.candidate.has_value()) {
        documents::emit(context, EventLevel::Error, encoded.result.message);
        return encoded.result;
    }
    const auto& candidate = *encoded.candidate;
    if (!allowDimensionChange
        && (candidate.texture.width != texture->width || candidate.texture.height != texture->height)) {
        return documents::failure("Replacement PNG dimensions do not match the selected MLD texture.");
    }
    if (context.stopToken.stop_requested()) return documents::cancelled();

    auto replacement = *texture;
    replacement.encodedBytes = candidate.bytes;
    replacement.hasGlobalIndex = candidate.texture.globalIndex.has_value();
    replacement.globalIndex = candidate.texture.globalIndex.value_or(0U);
    replacement.pixelFormat = candidate.texture.rawPixelFormat;
    replacement.dataFormat = candidate.texture.rawDataLayout;
    replacement.format = spice::pvm::model::toString(candidate.texture.pixelFormat);
    replacement.paletteFormat = spice::pvm::model::toString(candidate.texture.dataLayout);
    using Layout = spice::pvm::model::DataLayout;
    replacement.hasMipmaps = candidate.texture.dataLayout == Layout::TwiddledMipmaps
        || candidate.texture.dataLayout == Layout::VqMipmaps
        || candidate.texture.dataLayout == Layout::SmallVqMipmaps
        || candidate.texture.dataLayout == Layout::TwiddledMipmapsDma;
    replacement.hasInternalPalette = false;
    replacement.width = candidate.texture.width;
    replacement.height = candidate.texture.height;
    replacement.decoded = !candidate.decoded.mipLevels.empty()
        && !candidate.decoded.mipLevels.front().image.pixels.empty();
    replacement.rgba8 = replacement.decoded
        ? candidate.decoded.mipLevels.front().image.pixels : std::vector<std::uint8_t>{};
    *texture = std::move(replacement);
    impl_->dirtyTextures[index] = true;
    documents::emit(context, EventLevel::Info,
        "Staged replacement for MLD PVR texture " + std::to_string(index) + ".");
    encoded.result.message = "PVR texture replacement staged.";
    return encoded.result;
}

DocumentResult MldDocumentSession::revertTexture(const std::size_t index) {
    auto* texture = textureAt(*impl_, index);
    if (!texture || index >= impl_->savedTextures.size()) {
        return documents::failure("The selected MLD texture index is out of range.");
    }
    *texture = impl_->savedTextures[index];
    impl_->dirtyTextures[index] = false;
    return { .message = "Texture changes reverted." };
}

DocumentResult MldDocumentSession::revertAll() {
    if (impl_->document.textureArchives.empty()) return { .message = "There are no texture changes to revert." };
    impl_->document.textureArchives.front().textures = impl_->savedTextures;
    std::fill(impl_->dirtyTextures.begin(), impl_->dirtyTextures.end(), false);
    return { .message = "All staged texture changes reverted." };
}

DocumentResult MldDocumentSession::extractNativeTexture(const std::size_t index,
    const std::filesystem::path& outputPath, const DocumentContext& context) const {
    if (context.stopToken.stop_requested()) return documents::cancelled();
    const auto* texture = textureAt(*impl_, index);
    if (!texture) return documents::failure("The selected MLD texture index is out of range.");
    if (texture->encoding == spice::mld::model::MldTextureEncoding::Unknown || texture->encodedBytes.empty()) {
        return documents::failure("The selected texture has no native encoded payload.");
    }
    auto result = documents::writeBytesSafely(outputPath, texture->encodedBytes);
    if (result.ok()) documents::emit(context, EventLevel::Info, result.message);
    return result;
}

DocumentResult MldDocumentSession::exportTexturePng(const std::size_t index,
    const std::filesystem::path& outputPath, const DocumentContext& context) const {
    if (context.stopToken.stop_requested()) return documents::cancelled();
    const auto preview = texturePreview(index);
    if (!preview.has_value()) return documents::failure("The selected texture has no decoded image to export.");
    if (outputPath.empty() || outputPath.filename().empty()) return documents::failure("A PNG output file is required.");
    try {
        spice::gvm::model::RgbaImage image{
            .width = preview->width,
            .height = preview->height,
            .rgba8 = preview->rgba8,
        };
        if (outputPath.has_parent_path()) std::filesystem::create_directories(outputPath.parent_path());
        spice::gvm::image::writePngRgba8(outputPath, image);
        documents::emit(context, EventLevel::Info, "Exported PNG " + outputPath.string());
        return { .message = "Exported texture PNG." };
    } catch (const std::exception& error) {
        documents::emit(context, EventLevel::Error, error.what());
        return documents::failure(error.what());
    }
}

DocumentResult MldDocumentSession::exportBlenderIrJson(
    const std::filesystem::path& outputPath, const DocumentContext& context) const {
    if (context.stopToken.stop_requested()) return documents::cancelled();
    if (outputPath.empty() || outputPath.filename().empty()) {
        return documents::failure("A Blender IR JSON output file is required.");
    }
    try {
        documents::emit(context, EventLevel::Progress, "Building MLD Blender IR.");
        const auto projected = spice::mld::MldBlenderIrProjector::project(impl_->document);
        std::vector<std::string> diagnostics = projected.diagnostics;
        for (const auto& diagnostic : projected.diagnostics) {
            documents::emit(context, EventLevel::Warning, diagnostic);
        }
        if (!projected.scene.has_value()) {
            return documents::failure("MLD Blender IR could not be produced.", std::move(diagnostics));
        }
        if (context.stopToken.stop_requested()) return documents::cancelled();
        documents::emit(context, EventLevel::Progress, "Writing MLD Blender IR JSON.");
        const auto json = spice::mld::exporting::BlenderIrJsonExporter{}.toJson(*projected.scene);
        auto result = documents::writeTextSafely(outputPath, json);
        result.diagnostics = std::move(diagnostics);
        if (result.ok()) {
            result.message = "Exported MLD Blender IR JSON.";
            documents::emit(context, EventLevel::Info, result.message + " " + outputPath.string());
        } else {
            documents::emit(context, EventLevel::Error, result.message);
        }
        return result;
    } catch (const std::exception& error) {
        documents::emit(context, EventLevel::Error, error.what());
        return documents::failure(error.what());
    }
}

DocumentResult MldDocumentSession::exportEntryListJson(
    const std::filesystem::path& outputPath, const DocumentContext& context) const {
    if (context.stopToken.stop_requested()) return documents::cancelled();
    if (outputPath.empty() || outputPath.filename().empty()) {
        return documents::failure("An MLD entry-list JSON output file is required.");
    }
    try {
        documents::emit(context, EventLevel::Progress, "Building detailed MLD entry list.");
        std::vector<std::string> diagnostics{};
        std::vector<spice::mld::parsing::ParsedEntryListItem> entries{};
        entries.reserve(impl_->document.entries.size());
        for (std::size_t index = 0U; index < impl_->document.entries.size(); ++index) {
            const auto& entry = impl_->document.entries[index];
            spice::mld::parsing::ParsedEntryListItem item{
                .tableIndex = index,
                .entryId = entry.entryId,
                .tblId = entry.tableId,
                .fxnName = entry.functionName,
                .objectCount = entry.objectSlots.size(),
                .groundCount = entry.groundSlots.size(),
                .motionCount = entry.motionSlots.size(),
                .groundLinks = entry.groundLinks,
                .paramList2 = entry.parameterList2,
                .functionParameters = entry.functionParameters,
            };
            for (const auto& slot : entry.objectSlots) item.objectAddresses.push_back(slot ? static_cast<std::uint32_t>(slot->value) : 0U);
            for (const auto& slot : entry.groundSlots) item.groundAddresses.push_back(slot ? static_cast<std::uint32_t>(slot->value) : 0U);
            for (const auto& slot : entry.motionSlots) item.motionAddresses.push_back(slot ? static_cast<std::uint32_t>(slot->value) : 0U);
            if (entry.textureList) {
                item.texturesPointer = static_cast<std::uint32_t>(entry.textureList->value);
                const auto found = std::find_if(impl_->document.textureLists.begin(), impl_->document.textureLists.end(),
                    [&](const auto& list) { return list.id == *entry.textureList; });
                if (found != impl_->document.textureLists.end()) item.textureNames = found->names;
            }
            item.textureCount = item.textureNames.size();
            entries.push_back(std::move(item));
        }
        if (context.stopToken.stop_requested()) return documents::cancelled();
        documents::emit(context, EventLevel::Progress, "Writing detailed MLD entry-list JSON.");
        const auto json = spice::mld::exporting::MldEntryListJsonExporter{}.toJson(
            impl_->protectedSourcePath, entries);
        auto result = documents::writeTextSafely(outputPath, json);
        result.diagnostics = std::move(diagnostics);
        if (result.ok()) {
            result.message = "Exported detailed MLD entry-list JSON.";
            documents::emit(context, EventLevel::Info, result.message + " " + outputPath.string());
        } else {
            documents::emit(context, EventLevel::Error, result.message);
        }
        return result;
    } catch (const std::exception& error) {
        documents::emit(context, EventLevel::Error, error.what());
        return documents::failure(error.what());
    }
}

DocumentResult MldDocumentSession::saveAs(
    const std::filesystem::path& outputPath, const DocumentContext& context) {
    if (context.stopToken.stop_requested()) return documents::cancelled();
    if (outputPath.empty() || outputPath.filename().empty()) return documents::failure("An MLD output file is required.");
    if (documents::samePath(outputPath, impl_->protectedSourcePath)) {
        return documents::failure("Save As cannot overwrite the original MLD source file.");
    }
    const auto written = spice::mld::MldDocumentWriter::write(
        impl_->document,
        { .platform = impl_->receipt.platform, .wrapper = impl_->receipt.wrapper },
        &impl_->receipt);
    std::vector<std::string> diagnostics{};
    for (const auto& diagnostic : written.diagnostics) diagnostics.push_back(diagnostic.message);
    if (!written.ok() || written.bytes.empty()) {
        return documents::failure("The MLD writer rejected the staged document.", std::move(diagnostics));
    }
    if (context.stopToken.stop_requested()) return documents::cancelled();
    auto result = documents::writeBytesSafely(outputPath, written.bytes);
    result.diagnostics = std::move(diagnostics);
    if (!result.ok()) return result;
    if (!impl_->document.textureArchives.empty()) impl_->savedTextures = impl_->document.textureArchives.front().textures;
    std::fill(impl_->dirtyTextures.begin(), impl_->dirtyTextures.end(), false);
    documents::emit(context, EventLevel::Info, result.message);
    return result;
}

} // namespace spice::mix
