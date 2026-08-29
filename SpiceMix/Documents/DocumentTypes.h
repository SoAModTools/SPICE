#pragma once

#include "../Application/Operation.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace spice::mix {

struct DocumentContext {
    std::function<void(const OperationEvent&)> report{};
    std::stop_token stopToken{};
};

struct DocumentResult {
    OperationStatus status = OperationStatus::Success;
    std::string message{};
    std::vector<std::string> diagnostics{};

    [[nodiscard]] bool ok() const noexcept { return status == OperationStatus::Success; }
};

struct RgbaImageSnapshot {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba8{};

    [[nodiscard]] bool empty() const noexcept {
        return width == 0 || height == 0 || rgba8.empty();
    }
};

// Encoding overrides are intentionally independent from the outer AKLZ wrapper.
// Unset fields preserve an opened texture and use safe defaults for a new texture.
struct GvrEncodingOverrides {
    std::optional<GvrTextureFormat> format{};
    std::optional<GvrPaletteFormat> paletteFormat{};
    std::optional<bool> mipmaps{};
    GvrGlobalIndex globalIndex{ .kind = GvrGlobalIndexKind::Preserve };
};

struct GvrSaveOptions {
    GvrEncodingOverrides encoding{};
    AklzPolicy aklz = AklzPolicy::Preserve;
};

enum class PvrPixelFormat {
    ARGB1555,
    RGB565,
    ARGB4444,
};

enum class PvrDataLayout {
    Twiddled,
    TwiddledMipmaps,
    Vq,
    VqMipmaps,
    Rectangle,
    SmallVq,
    SmallVqMipmaps,
    TwiddledMipmapsDma,
};

enum class PvrGlobalIndexKind {
    Preserve,
    None,
    Value,
};

struct PvrGlobalIndexOverride {
    PvrGlobalIndexKind kind = PvrGlobalIndexKind::Preserve;
    std::uint32_t value = 0;
};

struct PvrEncodingOverrides {
    std::optional<PvrPixelFormat> pixelFormat{};
    std::optional<PvrDataLayout> dataLayout{};
    PvrGlobalIndexOverride globalIndex{};
};

enum class TextureEncodingKind {
    Unknown,
    Gvr,
    Pvr,
};

struct DocumentDiagnostic {
    EventLevel level = EventLevel::Info;
    std::string message{};
    std::optional<std::uint32_t> sourceOffset{};
};

} // namespace spice::mix
