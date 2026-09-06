#pragma once

#include "WMapAreaModel.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace spice::wmap {

struct WMapAreaParseResult {
    std::optional<WMapAreaDocument> document{};
    std::vector<WMapAreaDiagnostic> diagnostics{};
    std::size_t rawSize{ 0U };
    std::size_t decodedSize{ 0U };
    bool sourceWasCompressedAklz{ false };

    [[nodiscard]] bool ok() const noexcept;
};

class WMapAreaParser {
public:
    [[nodiscard]] static WMapAreaParseResult parse(
        std::span<const std::uint8_t> bytes);
    [[nodiscard]] static WMapAreaParseResult parseFile(
        const std::filesystem::path& path);
};

} // namespace spice::wmap
