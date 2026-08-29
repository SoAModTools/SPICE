#include "GvrDocumentSession.h"

#include "DocumentSupport.h"
#include "../../SpiceGvm/Image/PngCodec.h"

#include <span>
#include <stdexcept>
#include <utility>

namespace spice::mix {

struct GvrDocumentSession::Impl {
    std::optional<std::filesystem::path> sourcePath{};
    std::string displayName{};
    std::vector<std::uint8_t> originalBytes{};
    std::vector<std::uint8_t> workingBytes{};
    spice::gvm::ir::GvrSourceMetadata metadata{};
    bool dirty = false;
};

namespace {

template <typename ImplType>
DocumentResult refreshMetadata(ImplType& impl) {
    impl.metadata = spice::gvm::ir::readGvrSourceMetadata(impl.workingBytes);
    const auto& texture = impl.metadata.texture;
    if (texture.textureFormat == spice::gvm::model::TextureFormat::Unknown
        || texture.width == 0 || texture.height == 0
        || !texture.decodedBaseLevel.has_value() || texture.decodedBaseLevel->rgba8.empty()) {
        return documents::failure("The file does not contain a decodable GVR texture.", impl.metadata.diagnostics);
    }
    return { .message = "GVR texture is ready.", .diagnostics = impl.metadata.diagnostics };
}

} // namespace

GvrDocumentSession::GvrDocumentSession(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

GvrDocumentSession::~GvrDocumentSession() = default;
GvrDocumentSession::GvrDocumentSession(GvrDocumentSession&&) noexcept = default;
GvrDocumentSession& GvrDocumentSession::operator=(GvrDocumentSession&&) noexcept = default;

GvrDocumentSession::OpenResult GvrDocumentSession::open(
    const std::filesystem::path& path, const DocumentContext& context) {
    if (context.stopToken.stop_requested()) return { .result = documents::cancelled() };
    try {
        if (path.empty() || !std::filesystem::is_regular_file(path)) {
            return { .result = documents::failure("A readable GVR input file is required.") };
        }
        documents::emit(context, EventLevel::Progress, "Opening GVR " + path.string());
        auto impl = std::make_unique<Impl>();
        impl->sourcePath = path;
        impl->displayName = path.filename().string();
        impl->workingBytes = documents::readBytes(path);
        impl->originalBytes = impl->workingBytes;
        if (context.stopToken.stop_requested()) return { .result = documents::cancelled() };
        auto result = refreshMetadata(*impl);
        if (context.stopToken.stop_requested()) return { .result = documents::cancelled() };
        if (!result.ok()) return { .result = std::move(result) };
        auto session = std::shared_ptr<GvrDocumentSession>(new GvrDocumentSession(std::move(impl)));
        documents::emit(context, EventLevel::Info, "Opened GVR " + path.filename().string());
        return { .session = std::move(session), .result = std::move(result) };
    } catch (const std::exception& error) {
        documents::emit(context, EventLevel::Error, error.what());
        return { .result = documents::failure(error.what()) };
    }
}

GvrDocumentSession::OpenResult GvrDocumentSession::createFromPng(
    const std::filesystem::path& path, const DocumentContext& context) {
    if (context.stopToken.stop_requested()) return { .result = documents::cancelled() };
    try {
        if (path.empty() || !std::filesystem::is_regular_file(path)) {
            return { .result = documents::failure("A readable PNG input file is required.") };
        }
        documents::emit(context, EventLevel::Progress, "Creating GVR document from " + path.string());
        spice::gvm::ir::GvrPngEncodeOptions options{};
        options.encodeOptions = documents::makeEncoding({}, std::nullopt);
        options.aklzPolicy = spice::gvm::ir::AklzPolicy::Raw;
        const auto encoded = spice::gvm::ir::encodeGvrFromPng(path, options);
        auto impl = std::make_unique<Impl>();
        impl->displayName = path.stem().string() + ".gvr";
        impl->workingBytes = encoded.bytes;
        impl->dirty = true;
        auto result = refreshMetadata(*impl);
        if (context.stopToken.stop_requested()) return { .result = documents::cancelled() };
        result.diagnostics.insert(result.diagnostics.begin(), encoded.diagnostics.begin(), encoded.diagnostics.end());
        if (!result.ok()) return { .result = std::move(result) };
        auto session = std::shared_ptr<GvrDocumentSession>(new GvrDocumentSession(std::move(impl)));
        documents::emit(context, EventLevel::Info, "Created unsaved GVR document.");
        return { .session = std::move(session), .result = std::move(result) };
    } catch (const std::exception& error) {
        documents::emit(context, EventLevel::Error, error.what());
        return { .result = documents::failure(error.what()) };
    }
}

GvrDocumentSnapshot GvrDocumentSession::snapshot() const {
    GvrDocumentSnapshot out{};
    out.sourcePath = impl_->sourcePath;
    out.displayName = impl_->displayName;
    out.format = spice::gvm::model::to_string(impl_->metadata.texture.textureFormat);
    out.paletteFormat = spice::gvm::model::to_string(impl_->metadata.texture.paletteFormat);
    out.width = impl_->metadata.texture.width;
    out.height = impl_->metadata.texture.height;
    out.mipmaps = impl_->metadata.texture.hasMipmaps;
    out.hasGlobalIndex = impl_->metadata.texture.hasGlobalIndex;
    out.globalIndex = impl_->metadata.texture.globalIndex;
    out.aklzWrapped = impl_->metadata.sourceWasAklz;
    out.dirty = impl_->dirty;
    out.diagnostics = impl_->metadata.diagnostics;
    return out;
}

std::optional<RgbaImageSnapshot> GvrDocumentSession::preview() const {
    if (!impl_->metadata.texture.decodedBaseLevel.has_value()) return std::nullopt;
    const auto& image = *impl_->metadata.texture.decodedBaseLevel;
    return RgbaImageSnapshot{ .width = image.width, .height = image.height, .rgba8 = image.rgba8 };
}

bool GvrDocumentSession::dirty() const noexcept { return impl_->dirty; }

DocumentResult GvrDocumentSession::replaceImage(const std::filesystem::path& pngPath,
    const GvrSaveOptions& options, const bool allowDimensionChange, const DocumentContext& context) {
    if (context.stopToken.stop_requested()) return documents::cancelled();
    try {
        if (pngPath.empty() || !std::filesystem::is_regular_file(pngPath)) {
            return documents::failure("A readable replacement PNG is required.");
        }
        const auto image = spice::gvm::image::readPngRgba8(pngPath);
        if (!allowDimensionChange && (image.width != impl_->metadata.texture.width
            || image.height != impl_->metadata.texture.height)) {
            return documents::failure("Replacement PNG dimensions do not match the current texture.");
        }
        if (context.stopToken.stop_requested()) return documents::cancelled();
        spice::gvm::ir::GvrPngEncodeOptions encode{};
        encode.encodeOptions = documents::makeEncoding(options.encoding, impl_->metadata.texture);
        encode.aklzPolicy = documents::runtimeAklz(options.aklz);
        encode.sourceWasAklz = impl_->metadata.sourceWasAklz;
        const auto encoded = spice::gvm::ir::encodeGvrFromPng(pngPath, encode);
        auto candidate = *impl_;
        candidate.workingBytes = encoded.bytes;
        candidate.dirty = true;
        auto result = refreshMetadata(candidate);
        result.diagnostics.insert(result.diagnostics.begin(), encoded.diagnostics.begin(), encoded.diagnostics.end());
        if (!result.ok()) return result;
        *impl_ = std::move(candidate);
        documents::emit(context, EventLevel::Info, "Staged GVR image replacement.");
        return result;
    } catch (const std::exception& error) {
        documents::emit(context, EventLevel::Error, error.what());
        return documents::failure(error.what());
    }
}

DocumentResult GvrDocumentSession::revert() {
    if (impl_->originalBytes.empty()) {
        return documents::failure("This new GVR document has no saved version to revert to.");
    }
    impl_->workingBytes = impl_->originalBytes;
    impl_->dirty = false;
    auto result = refreshMetadata(*impl_);
    result.message = "Reverted GVR changes.";
    return result;
}

DocumentResult GvrDocumentSession::exportPng(
    const std::filesystem::path& outputPath, const DocumentContext& context) const {
    if (context.stopToken.stop_requested()) return documents::cancelled();
    try {
        if (outputPath.empty() || outputPath.filename().empty()) {
            return documents::failure("A PNG output file is required.");
        }
        const auto exported = spice::gvm::ir::exportGvrPng(impl_->workingBytes, outputPath);
        documents::emit(context, EventLevel::Info, "Exported PNG " + outputPath.string());
        return { .message = "Exported PNG.", .diagnostics = exported.diagnostics };
    } catch (const std::exception& error) {
        documents::emit(context, EventLevel::Error, error.what());
        return documents::failure(error.what());
    }
}

DocumentResult GvrDocumentSession::saveAs(
    const std::filesystem::path& outputPath, const DocumentContext& context) {
    if (context.stopToken.stop_requested()) return documents::cancelled();
    auto result = documents::writeBytesSafely(outputPath, impl_->workingBytes);
    if (!result.ok()) return result;
    impl_->sourcePath = outputPath;
    impl_->displayName = outputPath.filename().string();
    impl_->originalBytes = impl_->workingBytes;
    impl_->dirty = false;
    documents::emit(context, EventLevel::Info, result.message);
    return result;
}

} // namespace spice::mix
