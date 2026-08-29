#include "MldDocumentSession.h"

#include "DocumentSupport.h"
#include "MldInspectionSupport.h"
#include "PvrDocumentSupport.h"
#include "../../SpiceGvm/Image/PngCodec.h"
#include "../../SpiceMLD/Export/BlenderIrJsonExporter.h"
#include "../../SpiceMLD/Export/MldEntryListJsonExporter.h"
#include "../../SpiceMLD/Export/MldFileWriter.h"
#include "../../SpiceMLD/Parsing/MldParser.h"

#include <algorithm>
#include <span>
#include <stdexcept>
#include <utility>

namespace spice::mix {

struct MldDocumentSession::Impl {
    std::filesystem::path protectedSourcePath{};
    spice::mld::model::MldFile file{};
    std::vector<spice::mld::model::MldTextureEntry> savedTextures{};
    std::vector<bool> dirtyTextures{};
};

namespace {

template <typename ImplType>
spice::mld::model::MldTextureEntry* textureAt(ImplType& impl, const std::size_t index) {
    if (!impl.file.textureArchive.has_value() || index >= impl.file.textureArchive->entries.size()) return nullptr;
    return &impl.file.textureArchive->entries[index];
}

template <typename ImplType>
const spice::mld::model::MldTextureEntry* textureAt(const ImplType& impl, const std::size_t index) {
    if (!impl.file.textureArchive.has_value() || index >= impl.file.textureArchive->entries.size()) return nullptr;
    return &impl.file.textureArchive->entries[index];
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
        impl->file = spice::mld::parsing::MldParser{}.parseBytes(bytes);
        if (context.stopToken.stop_requested()) return { .result = documents::cancelled() };
        std::vector<std::string> diagnosticText{};
        for (const auto& diagnostic : impl->file.parseDiagnostics) diagnosticText.push_back(diagnostic.message);
        if (impl->file.parseStatus == spice::mld::model::MldParseStatus::Failed) {
            return { .result = documents::failure("MLD parsing failed.", std::move(diagnosticText)) };
        }
        if (impl->file.textureArchive.has_value()) {
            impl->savedTextures = impl->file.textureArchive->entries;
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
    return documents::projectMldOverview(impl_->file, impl_->protectedSourcePath, dirty());
}

std::vector<MldEntrySnapshot> MldDocumentSession::entries() const {
    return documents::projectMldEntries(impl_->file);
}

std::vector<MldTextureSnapshot> MldDocumentSession::textures() const {
    return documents::projectMldTextures(impl_->file, impl_->dirtyTextures);
}

std::vector<DocumentDiagnostic> MldDocumentSession::diagnostics() const {
    return documents::projectMldDiagnostics(impl_->file);
}

std::optional<RgbaImageSnapshot> MldDocumentSession::texturePreview(const std::size_t index) const {
    return documents::projectMldTexturePreview(impl_->file, index);
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
        const auto source = spice::gvm::ir::readGvrSourceMetadata(texture->encodedData);
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
        texture->encodedData = replacement;
        texture->encodedDataSize = replacement.size();
        texture->hasGlobalIndex = replacementMetadata.texture.hasGlobalIndex;
        texture->globalIndex = replacementMetadata.texture.globalIndex;
        texture->pixelFormat = replacementMetadata.texture.rawFlags;
        texture->dataFormat = replacementMetadata.texture.rawDataFormat;
        texture->sourceFormat = spice::gvm::model::to_string(replacementMetadata.texture.textureFormat);
        texture->sourcePaletteFormat = spice::gvm::model::to_string(replacementMetadata.texture.paletteFormat);
        texture->hasMipmaps = replacementMetadata.texture.hasMipmaps;
        texture->hasInternalPalette = replacementMetadata.texture.hasInternalPalette;
        texture->width = replacementMetadata.texture.width;
        texture->height = replacementMetadata.texture.height;
        texture->imageDataOffset = replacementMetadata.texture.imageDataOffset;
        texture->imageDataSize = replacementMetadata.texture.imageDataSize;
        texture->paletteDataSize = replacementMetadata.texture.paletteData.size();
        texture->decoded = true;
        texture->rgba8 = replacementMetadata.texture.decodedBaseLevel->rgba8;
        texture->diagnostics = replacementMetadata.diagnostics;
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
    auto encoded = documents::encodePvrFromPng(pngPath, overrides, texture->encodedData);
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
    replacement.encodedData = candidate.bytes;
    replacement.encodedDataSize = candidate.bytes.size();
    replacement.hasGlobalIndex = candidate.texture.globalIndex.has_value();
    replacement.globalIndex = candidate.texture.globalIndex.value_or(0U);
    replacement.pixelFormat = candidate.texture.rawPixelFormat;
    replacement.dataFormat = candidate.texture.rawDataLayout;
    replacement.sourceFormat = spice::pvm::model::toString(candidate.texture.pixelFormat);
    replacement.sourcePaletteFormat = spice::pvm::model::toString(candidate.texture.dataLayout);
    using Layout = spice::pvm::model::DataLayout;
    replacement.hasMipmaps = candidate.texture.dataLayout == Layout::TwiddledMipmaps
        || candidate.texture.dataLayout == Layout::VqMipmaps
        || candidate.texture.dataLayout == Layout::SmallVqMipmaps
        || candidate.texture.dataLayout == Layout::TwiddledMipmapsDma;
    replacement.hasInternalPalette = false;
    replacement.width = candidate.texture.width;
    replacement.height = candidate.texture.height;
    replacement.imageDataOffset = candidate.texture.textureDataRange.offset;
    replacement.imageDataSize = candidate.texture.textureDataRange.size;
    replacement.paletteDataSize = 0U;
    replacement.decoded = !candidate.decoded.mipLevels.empty()
        && !candidate.decoded.mipLevels.front().image.pixels.empty();
    replacement.rgba8 = replacement.decoded
        ? candidate.decoded.mipLevels.front().image.pixels : std::vector<std::uint8_t>{};
    replacement.diagnostics = candidate.diagnostics;
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
    if (!impl_->file.textureArchive.has_value()) return { .message = "There are no texture changes to revert." };
    impl_->file.textureArchive->entries = impl_->savedTextures;
    std::fill(impl_->dirtyTextures.begin(), impl_->dirtyTextures.end(), false);
    return { .message = "All staged texture changes reverted." };
}

DocumentResult MldDocumentSession::extractNativeTexture(const std::size_t index,
    const std::filesystem::path& outputPath, const DocumentContext& context) const {
    if (context.stopToken.stop_requested()) return documents::cancelled();
    const auto* texture = textureAt(*impl_, index);
    if (!texture) return documents::failure("The selected MLD texture index is out of range.");
    if (texture->encoding == spice::mld::model::MldTextureEncoding::Unknown || texture->encodedData.empty()) {
        return documents::failure("The selected texture has no native encoded payload.");
    }
    auto result = documents::writeBytesSafely(outputPath, texture->encodedData);
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
        spice::mld::parsing::ParseOptions options{};
        options.buildBlenderIntermediateIr = true;
        const auto projected = spice::mld::parsing::MldParser{}.project(impl_->file, options);
        std::vector<std::string> diagnostics = projected.blenderIrDiagnostics;
        for (const auto& diagnostic : projected.diagnostics) {
            diagnostics.push_back(diagnostic.message);
            if (diagnostic.severity == spice::mld::parsing::ParseDiagnostic::Severity::Warning) {
                documents::emit(context, EventLevel::Warning, diagnostic.message);
            } else if (diagnostic.severity == spice::mld::parsing::ParseDiagnostic::Severity::Error) {
                documents::emit(context, EventLevel::Error, diagnostic.message);
            }
        }
        for (const auto& diagnostic : projected.blenderIrDiagnostics) {
            documents::emit(context, EventLevel::Warning, diagnostic);
        }
        if (!projected.blenderIrScene.has_value()) {
            return documents::failure("MLD Blender IR could not be produced.", std::move(diagnostics));
        }
        if (context.stopToken.stop_requested()) return documents::cancelled();
        documents::emit(context, EventLevel::Progress, "Writing MLD Blender IR JSON.");
        const auto json = spice::mld::exporting::BlenderIrJsonExporter{}.toJson(*projected.blenderIrScene);
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
        spice::mld::parsing::ParseOptions options{};
        options.entryListOnly = true;
        options.buildBlenderIntermediateIr = false;
        const auto projected = spice::mld::parsing::MldParser{}.project(impl_->file, options);
        std::vector<std::string> diagnostics{};
        diagnostics.reserve(projected.diagnostics.size());
        for (const auto& diagnostic : projected.diagnostics) {
            diagnostics.push_back(diagnostic.message);
            if (diagnostic.severity == spice::mld::parsing::ParseDiagnostic::Severity::Warning) {
                documents::emit(context, EventLevel::Warning, diagnostic.message);
            } else if (diagnostic.severity == spice::mld::parsing::ParseDiagnostic::Severity::Error) {
                documents::emit(context, EventLevel::Error, diagnostic.message);
            }
        }
        if (context.stopToken.stop_requested()) return documents::cancelled();
        documents::emit(context, EventLevel::Progress, "Writing detailed MLD entry-list JSON.");
        const auto json = spice::mld::exporting::MldEntryListJsonExporter{}.toJson(
            impl_->protectedSourcePath, projected.entryList);
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
    const auto written = spice::mld::exporting::MldFileWriter{}.write(impl_->file);
    std::vector<std::string> diagnostics{};
    for (const auto& diagnostic : written.diagnostics) diagnostics.push_back(diagnostic.message);
    if (!written.ok() || written.bytes.empty()) {
        return documents::failure("The MLD writer rejected the staged document.", std::move(diagnostics));
    }
    if (context.stopToken.stop_requested()) return documents::cancelled();
    auto result = documents::writeBytesSafely(outputPath, written.bytes);
    result.diagnostics = std::move(diagnostics);
    if (!result.ok()) return result;
    if (impl_->file.textureArchive.has_value()) impl_->savedTextures = impl_->file.textureArchive->entries;
    std::fill(impl_->dirtyTextures.begin(), impl_->dirtyTextures.end(), false);
    documents::emit(context, EventLevel::Info, result.message);
    return result;
}

} // namespace spice::mix
