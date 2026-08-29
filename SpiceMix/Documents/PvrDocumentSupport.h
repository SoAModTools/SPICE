#pragma once

#include "DocumentTypes.h"
#include "../../SpicePvm/Decoding/PvrDecoder.h"
#include "../../SpicePvm/Encoding/PvrEncoder.h"
#include "../../SpicePvm/Parsing/PvmParser.h"

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace spice::mix::documents {

struct PvrEncodingCandidate {
    std::vector<std::uint8_t> bytes{};
    spice::pvm::model::PvrTexture texture{};
    spice::pvm::model::DecodeResult decoded{};
    std::vector<std::string> diagnostics{};
};

struct PvrEncodingResult {
    std::optional<PvrEncodingCandidate> candidate{};
    DocumentResult result{};
};

[[nodiscard]] PvrEncodingResult inspectPvr(std::span<const std::uint8_t> bytes);
[[nodiscard]] PvrEncodingResult encodePvrFromPng(
    const std::filesystem::path& pngPath,
    const PvrEncodingOverrides& overrides,
    std::optional<std::span<const std::uint8_t>> sourceBytes = std::nullopt);
[[nodiscard]] bool pvrLayoutHasMipmaps(PvrDataLayout layout) noexcept;

} // namespace spice::mix::documents
