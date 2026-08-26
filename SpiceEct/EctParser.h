#pragma once

#include "EctModel.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace spice::ect {

struct EctParseResult {
    std::optional<EctFile> file{};
    std::vector<EctDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept;
};

class EctParser {
public:
    [[nodiscard]] static EctParseResult parse(
        std::span<const std::uint8_t> bytes,
        EctLayout layout);

    [[nodiscard]] static EctParseResult parseFile(const std::filesystem::path& path);
};

} // namespace spice::ect
