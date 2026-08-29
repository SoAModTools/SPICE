#include "PvrDocumentSession.h"

#include "DocumentSupport.h"
#include "PvrDocumentSupport.h"
#include "../../SpiceGvm/Image/PngCodec.h"

#include <stdexcept>
#include <utility>

namespace spice::mix {

struct PvrDocumentSession::Impl {
    std::optional<std::filesystem::path> sourcePath{};
    std::string displayName{};
    std::vector<std::uint8_t> originalBytes{};
    std::vector<std::uint8_t> workingBytes{};
    documents::PvrEncodingCandidate current{};
    bool dirty = false;
};

namespace {

template <typename ImplType>
DocumentResult refreshMetadata(ImplType& impl) {
    auto inspected = documents::inspectPvr(impl.workingBytes);
    if (!inspected.result.ok() || !inspected.candidate.has_value()) return inspected.result;
    impl.current = std::move(*inspected.candidate);
    return inspected.result;
}

bool isMipmapped(const spice::pvm::model::DataLayout layout) {
    using Layout = spice::pvm::model::DataLayout;
    return layout == Layout::TwiddledMipmaps || layout == Layout::VqMipmaps
        || layout == Layout::SmallVqMipmaps || layout == Layout::TwiddledMipmapsDma;
}

} // namespace

PvrDocumentSession::PvrDocumentSession(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

PvrDocumentSession::~PvrDocumentSession() = default;
PvrDocumentSession::PvrDocumentSession(PvrDocumentSession&&) noexcept = default;
PvrDocumentSession& PvrDocumentSession::operator=(PvrDocumentSession&&) noexcept = default;

PvrDocumentSession::OpenResult PvrDocumentSession::open(
    const std::filesystem::path& path, const DocumentContext& context) {
    if (context.stopToken.stop_requested()) return { .result = documents::cancelled() };
    try {
        if (path.empty() || !std::filesystem::is_regular_file(path)) {
            return { .result = documents::failure("A readable PVR input file is required.") };
        }
        documents::emit(context, EventLevel::Progress, "Opening PVR " + path.string());
        auto impl = std::make_unique<Impl>();
        impl->sourcePath = path;
        impl->displayName = path.filename().string();
        impl->workingBytes = documents::readBytes(path);
        impl->originalBytes = impl->workingBytes;
        if (context.stopToken.stop_requested()) return { .result = documents::cancelled() };
        auto result = refreshMetadata(*impl);
        if (!result.ok()) return { .result = std::move(result) };
        auto session = std::shared_ptr<PvrDocumentSession>(new PvrDocumentSession(std::move(impl)));
        documents::emit(context, EventLevel::Info, "Opened PVR " + path.filename().string());
        return { .session = std::move(session), .result = std::move(result) };
    } catch (const std::exception& error) {
        documents::emit(context, EventLevel::Error, error.what());
        return { .result = documents::failure(error.what()) };
    }
}

PvrDocumentSession::OpenResult PvrDocumentSession::createFromPng(
    const std::filesystem::path& path, const DocumentContext& context) {
    if (context.stopToken.stop_requested()) return { .result = documents::cancelled() };
    documents::emit(context, EventLevel::Progress, "Creating PVR document from " + path.string());
    auto encoded = documents::encodePvrFromPng(path, {});
    if (context.stopToken.stop_requested()) return { .result = documents::cancelled() };
    if (!encoded.result.ok() || !encoded.candidate.has_value()) return { .result = std::move(encoded.result) };
    auto impl = std::make_unique<Impl>();
    impl->displayName = path.stem().string() + ".pvr";
    impl->workingBytes = encoded.candidate->bytes;
    impl->current = std::move(*encoded.candidate);
    impl->dirty = true;
    auto session = std::shared_ptr<PvrDocumentSession>(new PvrDocumentSession(std::move(impl)));
    documents::emit(context, EventLevel::Info, "Created unsaved PVR document.");
    return { .session = std::move(session), .result = std::move(encoded.result) };
}

PvrDocumentSnapshot PvrDocumentSession::snapshot() const {
    const auto& texture = impl_->current.texture;
    return {
        .sourcePath = impl_->sourcePath,
        .displayName = impl_->displayName,
        .pixelFormat = spice::pvm::model::toString(texture.pixelFormat),
        .dataLayout = spice::pvm::model::toString(texture.dataLayout),
        .width = texture.width,
        .height = texture.height,
        .mipmaps = isMipmapped(texture.dataLayout),
        .hasGlobalIndex = texture.globalIndex.has_value(),
        .globalIndex = texture.globalIndex.value_or(0U),
        .dirty = impl_->dirty,
        .diagnostics = impl_->current.diagnostics,
    };
}

std::optional<RgbaImageSnapshot> PvrDocumentSession::preview() const {
    if (impl_->current.decoded.mipLevels.empty()) return std::nullopt;
    const auto& image = impl_->current.decoded.mipLevels.front().image;
    return RgbaImageSnapshot{ .width = image.width, .height = image.height, .rgba8 = image.pixels };
}

bool PvrDocumentSession::dirty() const noexcept { return impl_->dirty; }

DocumentResult PvrDocumentSession::replaceImage(const std::filesystem::path& pngPath,
    const PvrEncodingOverrides& overrides, const bool allowDimensionChange,
    const DocumentContext& context) {
    if (context.stopToken.stop_requested()) return documents::cancelled();
    documents::emit(context, EventLevel::Progress, "Encoding replacement PVR image.");
    auto encoded = documents::encodePvrFromPng(pngPath, overrides, impl_->workingBytes);
    if (!encoded.result.ok() || !encoded.candidate.has_value()) {
        documents::emit(context, EventLevel::Error, encoded.result.message);
        return encoded.result;
    }
    if (!allowDimensionChange
        && (encoded.candidate->texture.width != impl_->current.texture.width
            || encoded.candidate->texture.height != impl_->current.texture.height)) {
        return documents::failure("Replacement PNG dimensions do not match the current PVR texture.");
    }
    if (context.stopToken.stop_requested()) return documents::cancelled();
    impl_->workingBytes = encoded.candidate->bytes;
    impl_->current = std::move(*encoded.candidate);
    impl_->dirty = true;
    documents::emit(context, EventLevel::Info, "Staged PVR image replacement.");
    encoded.result.message = "PVR image replacement staged.";
    return encoded.result;
}

DocumentResult PvrDocumentSession::revert() {
    if (impl_->originalBytes.empty()) {
        return documents::failure("This new PVR document has no saved version to revert to.");
    }
    impl_->workingBytes = impl_->originalBytes;
    impl_->dirty = false;
    auto result = refreshMetadata(*impl_);
    result.message = "Reverted PVR changes.";
    return result;
}

DocumentResult PvrDocumentSession::exportPng(
    const std::filesystem::path& outputPath, const DocumentContext& context) const {
    if (context.stopToken.stop_requested()) return documents::cancelled();
    if (outputPath.empty() || outputPath.filename().empty()) {
        return documents::failure("A PNG output file is required.");
    }
    try {
        const auto previewImage = preview();
        if (!previewImage.has_value()) return documents::failure("The PVR texture has no decoded image.");
        spice::gvm::model::RgbaImage image{
            .width = previewImage->width,
            .height = previewImage->height,
            .rgba8 = previewImage->rgba8,
        };
        if (outputPath.has_parent_path()) std::filesystem::create_directories(outputPath.parent_path());
        spice::gvm::image::writePngRgba8(outputPath, image);
        documents::emit(context, EventLevel::Info, "Exported PNG " + outputPath.string());
        return { .message = "Exported PNG." };
    } catch (const std::exception& error) {
        documents::emit(context, EventLevel::Error, error.what());
        return documents::failure(error.what());
    }
}

DocumentResult PvrDocumentSession::saveAs(
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
