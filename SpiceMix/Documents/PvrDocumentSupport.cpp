#include "PvrDocumentSupport.h"

#include "DocumentSupport.h"
#include "../../SpiceGvm/Image/PngCodec.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace spice::mix::documents {
namespace {

spice::pvm::model::PixelFormat runtimePixelFormat(const PvrPixelFormat format) {
    using Source = PvrPixelFormat;
    using Target = spice::pvm::model::PixelFormat;
    switch (format) {
    case Source::ARGB1555: return Target::Argb1555;
    case Source::RGB565: return Target::Rgb565;
    case Source::ARGB4444: return Target::Argb4444;
    }
    throw std::runtime_error("Unsupported PVR pixel format.");
}

spice::pvm::model::DataLayout runtimeDataLayout(const PvrDataLayout layout) {
    using Source = PvrDataLayout;
    using Target = spice::pvm::model::DataLayout;
    switch (layout) {
    case Source::Twiddled: return Target::Twiddled;
    case Source::TwiddledMipmaps: return Target::TwiddledMipmaps;
    case Source::Vq: return Target::Vq;
    case Source::VqMipmaps: return Target::VqMipmaps;
    case Source::Rectangle: return Target::Rectangle;
    case Source::SmallVq: return Target::SmallVq;
    case Source::SmallVqMipmaps: return Target::SmallVqMipmaps;
    case Source::TwiddledMipmapsDma: return Target::TwiddledMipmapsDma;
    }
    throw std::runtime_error("Unsupported PVR data layout.");
}

void appendDiagnostics(std::vector<std::string>& destination,
    const std::vector<spice::pvm::model::Diagnostic>& source) {
    destination.reserve(destination.size() + source.size());
    for (const auto& diagnostic : source) destination.push_back(diagnostic.message);
}

PvrEncodingResult failed(std::string message, std::vector<std::string> diagnostics = {}) {
    return { .result = documents::failure(std::move(message), std::move(diagnostics)) };
}

} // namespace

bool pvrLayoutHasMipmaps(const PvrDataLayout layout) noexcept {
    return layout == PvrDataLayout::TwiddledMipmaps
        || layout == PvrDataLayout::VqMipmaps
        || layout == PvrDataLayout::SmallVqMipmaps
        || layout == PvrDataLayout::TwiddledMipmapsDma;
}

PvrEncodingResult inspectPvr(const std::span<const std::uint8_t> bytes) {
    auto parsed = spice::pvm::parsing::parsePvrTexture(bytes);
    std::vector<std::string> diagnostics{};
    appendDiagnostics(diagnostics, parsed.diagnostics);
    if (parsed.status == spice::pvm::model::ParseStatus::Failed
        || parsed.sourceRange.offset != 0U || parsed.sourceRange.size != bytes.size()) {
        return failed("The file is not exactly one structurally valid PVR texture.", std::move(diagnostics));
    }
    auto decoded = spice::pvm::decoding::decodePvrTexture(parsed);
    appendDiagnostics(diagnostics, decoded.diagnostics);
    if (decoded.status == spice::pvm::model::ParseStatus::Failed
        || decoded.mipLevels.empty() || decoded.mipLevels.front().image.pixels.empty()) {
        return failed("The PVR texture could not be decoded.", std::move(diagnostics));
    }
    PvrEncodingCandidate candidate{
        .bytes = std::vector<std::uint8_t>(bytes.begin(), bytes.end()),
        .texture = std::move(parsed),
        .decoded = std::move(decoded),
        .diagnostics = diagnostics,
    };
    return {
        .candidate = std::move(candidate),
        .result = { .message = "PVR texture is ready.", .diagnostics = std::move(diagnostics) },
    };
}

PvrEncodingResult encodePvrFromPng(const std::filesystem::path& pngPath,
    const PvrEncodingOverrides& overrides,
    const std::optional<std::span<const std::uint8_t>> sourceBytes) {
    try {
        if (pngPath.empty() || !std::filesystem::is_regular_file(pngPath)) {
            return failed("A readable replacement PNG is required.");
        }

        std::optional<PvrEncodingCandidate> source{};
        if (sourceBytes.has_value()) {
            auto inspected = inspectPvr(*sourceBytes);
            if (!inspected.result.ok() || !inspected.candidate.has_value()) return inspected;
            source = std::move(*inspected.candidate);
        }

        spice::pvm::encoding::PvrEncodeOptions options{};
        if (overrides.pixelFormat.has_value()) {
            options.pixelFormat = runtimePixelFormat(*overrides.pixelFormat);
        } else if (source.has_value()) {
            if (source->texture.pixelFormat == spice::pvm::model::PixelFormat::Unknown) {
                return failed("The source PVR pixel format cannot be preserved; select a supported format.");
            }
            options.pixelFormat = source->texture.pixelFormat;
        }
        if (overrides.dataLayout.has_value()) {
            options.dataLayout = runtimeDataLayout(*overrides.dataLayout);
        } else if (source.has_value()) {
            if (source->texture.dataLayout == spice::pvm::model::DataLayout::Unknown) {
                return failed("The source PVR data layout cannot be preserved; select a supported layout.");
            }
            options.dataLayout = source->texture.dataLayout;
        }

        if (source.has_value()) {
            if (source->texture.pvrtUnknownHeader.size() == options.pvrtUnknownHeader.size()) {
                std::copy(source->texture.pvrtUnknownHeader.begin(),
                    source->texture.pvrtUnknownHeader.end(), options.pvrtUnknownHeader.begin());
            }
            options.gbixTrailingBytes = source->texture.gbixTrailingBytes;
            const bool preservesLayout = !overrides.dataLayout.has_value()
                || options.dataLayout == source->texture.dataLayout;
            options.alignVqMipmapsTo32Bytes = preservesLayout
                ? source->decoded.trailingPaddingRange.has_value() : true;
        }
        switch (overrides.globalIndex.kind) {
        case PvrGlobalIndexKind::Preserve:
            options.includeGlobalIndex = source.has_value() && source->texture.globalIndex.has_value();
            options.globalIndex = source.has_value() ? source->texture.globalIndex.value_or(0U) : 0U;
            break;
        case PvrGlobalIndexKind::None:
            options.includeGlobalIndex = false;
            break;
        case PvrGlobalIndexKind::Value:
            options.includeGlobalIndex = true;
            options.globalIndex = overrides.globalIndex.value;
            if (!source.has_value() || !source->texture.gbixRange.has_value()) {
                options.gbixTrailingBytes.assign(4U, 0U);
            }
            break;
        }

        const auto png = spice::gvm::image::readPngRgba8(pngPath);
        spice::pvm::model::RgbaImage image{
            .width = png.width,
            .height = png.height,
            .pixels = png.rgba8,
        };
        options.generateMipmaps = options.dataLayout == spice::pvm::model::DataLayout::TwiddledMipmaps
            || options.dataLayout == spice::pvm::model::DataLayout::VqMipmaps
            || options.dataLayout == spice::pvm::model::DataLayout::SmallVqMipmaps
            || options.dataLayout == spice::pvm::model::DataLayout::TwiddledMipmapsDma;
        const auto encoded = spice::pvm::encoding::encodePvrTexture(image, options);
        std::vector<std::string> diagnostics{};
        appendDiagnostics(diagnostics, encoded.diagnostics);
        if (!encoded.ok() || encoded.bytes.empty()) {
            std::string message = "The replacement PNG cannot be encoded with the selected PVR settings.";
            if (!diagnostics.empty()) message += " " + diagnostics.front();
            return failed(std::move(message), std::move(diagnostics));
        }
        auto inspected = inspectPvr(encoded.bytes);
        inspected.result.diagnostics.insert(inspected.result.diagnostics.begin(),
            diagnostics.begin(), diagnostics.end());
        if (inspected.candidate.has_value()) {
            inspected.candidate->diagnostics.insert(inspected.candidate->diagnostics.begin(),
                diagnostics.begin(), diagnostics.end());
        }
        return inspected;
    } catch (const std::exception& error) {
        return failed(error.what());
    }
}

} // namespace spice::mix::documents
