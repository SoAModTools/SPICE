#pragma once

#include "DocumentTypes.h"
#include "../../Compression/Aklz.h"
#include "../../SpiceGvm/Encoding/GvrEncoder.h"
#include "../../SpiceGvm/Ir/GvrImageIr.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace spice::mix::documents {

inline void emit(const DocumentContext& context, const EventLevel level, std::string message) {
    if (context.report) {
        context.report(OperationEvent{ .level = level, .message = std::move(message) });
    }
}

inline DocumentResult cancelled() {
    return { .status = OperationStatus::Cancelled, .message = "Operation cancelled." };
}

inline DocumentResult failure(std::string message, std::vector<std::string> diagnostics = {}) {
    return { .status = OperationStatus::Failure, .message = std::move(message),
        .diagnostics = std::move(diagnostics) };
}

inline std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open input file: " + path.string());
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), {});
}

inline bool samePath(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code error{};
    if (std::filesystem::exists(left, error) && !error && std::filesystem::exists(right, error) && !error) {
        if (std::filesystem::equivalent(left, right, error) && !error) {
            return true;
        }
    }
    error.clear();
    const auto normalizedLeft = std::filesystem::weakly_canonical(left, error);
    if (error) return left.lexically_normal() == right.lexically_normal();
    error.clear();
    const auto normalizedRight = std::filesystem::weakly_canonical(right, error);
    return !error && normalizedLeft == normalizedRight;
}

inline DocumentResult writeBytesSafely(const std::filesystem::path& outputPath,
    const std::span<const std::uint8_t> bytes) {
    if (outputPath.empty() || outputPath.filename().empty()) {
        return failure("An output file path is required.");
    }
    std::error_code error{};
    if (outputPath.has_parent_path()) {
        std::filesystem::create_directories(outputPath.parent_path(), error);
        if (error) {
            return failure("Could not create output directory: " + error.message());
        }
    }
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto temporaryPath = outputPath;
    temporaryPath += ".spicemix-" + suffix + ".tmp";
    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            return failure("Could not create temporary output file: " + temporaryPath.string());
        }
        if (!bytes.empty()) {
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(temporaryPath, error);
            return failure("Failed while writing temporary output file.");
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(temporaryPath.c_str(), outputPath.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto code = GetLastError();
        std::filesystem::remove(temporaryPath, error);
        return failure("Could not replace output file (Windows error " + std::to_string(code) + ").");
    }
#else
    std::filesystem::rename(temporaryPath, outputPath, error);
    if (error) {
        std::filesystem::remove(temporaryPath, error);
        return failure("Could not replace output file: " + error.message());
    }
#endif
    return { .message = "Saved " + outputPath.string() };
}

inline spice::gvm::model::TextureFormat runtimeFormat(const GvrTextureFormat format) {
    using Source = GvrTextureFormat;
    using Target = spice::gvm::model::TextureFormat;
    switch (format) {
    case Source::I4: return Target::I4;
    case Source::I8: return Target::I8;
    case Source::IA4: return Target::IA4;
    case Source::IA8: return Target::IA8;
    case Source::RGB565: return Target::RGB565;
    case Source::RGB5A3: return Target::RGB5A3;
    case Source::RGBA8: return Target::RGBA8;
    case Source::CI4: return Target::CI4;
    case Source::CI8: return Target::CI8;
    case Source::CI14X2: return Target::CI14X2;
    case Source::CMPR: return Target::CMPR;
    }
    return Target::RGBA8;
}

inline spice::gvm::model::PaletteFormat runtimePalette(const GvrPaletteFormat format) {
    switch (format) {
    case GvrPaletteFormat::IA8: return spice::gvm::model::PaletteFormat::IA8;
    case GvrPaletteFormat::RGB565: return spice::gvm::model::PaletteFormat::RGB565;
    case GvrPaletteFormat::RGB5A3: return spice::gvm::model::PaletteFormat::RGB5A3;
    }
    return spice::gvm::model::PaletteFormat::RGB5A3;
}

inline bool indexed(const spice::gvm::model::TextureFormat format) {
    return format == spice::gvm::model::TextureFormat::CI4
        || format == spice::gvm::model::TextureFormat::CI8
        || format == spice::gvm::model::TextureFormat::CI14X2;
}

inline bool supported(const spice::gvm::model::TextureFormat format) {
    return format != spice::gvm::model::TextureFormat::Unknown;
}

inline spice::gvm::encoding::EncodeOptions makeEncoding(
    const GvrEncodingOverrides& overrides,
    const std::optional<spice::gvm::model::GvrTexture>& source) {
    spice::gvm::encoding::EncodeOptions out{};
    out.textureFormat = overrides.format.has_value()
        ? runtimeFormat(*overrides.format)
        : source.has_value() ? source->textureFormat : spice::gvm::model::TextureFormat::RGBA8;
    if (!supported(out.textureFormat)) {
        throw std::runtime_error("The source GVR texture format cannot be preserved by the encoder.");
    }
    out.paletteFormat = indexed(out.textureFormat)
        ? overrides.paletteFormat.has_value()
            ? runtimePalette(*overrides.paletteFormat)
            : source.has_value() && indexed(source->textureFormat)
                ? source->paletteFormat : spice::gvm::model::PaletteFormat::RGB5A3
        : spice::gvm::model::PaletteFormat::None;
    out.generateMipmaps = overrides.mipmaps.value_or(source.has_value() && source->hasMipmaps);
    switch (overrides.globalIndex.kind) {
    case GvrGlobalIndexKind::Preserve:
        out.hasGlobalIndex = source.has_value() && source->hasGlobalIndex;
        out.globalIndex = source.has_value() ? source->globalIndex : 0;
        break;
    case GvrGlobalIndexKind::None:
        break;
    case GvrGlobalIndexKind::Value:
        out.hasGlobalIndex = true;
        out.globalIndex = overrides.globalIndex.value;
        break;
    }
    return out;
}

inline spice::gvm::ir::AklzPolicy runtimeAklz(const AklzPolicy policy) {
    switch (policy) {
    case AklzPolicy::Preserve: return spice::gvm::ir::AklzPolicy::Preserve;
    case AklzPolicy::Raw: return spice::gvm::ir::AklzPolicy::Raw;
    case AklzPolicy::Compressed: return spice::gvm::ir::AklzPolicy::Compressed;
    }
    return spice::gvm::ir::AklzPolicy::Preserve;
}

} // namespace spice::mix::documents
