#pragma once

#include "CsvModel.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace spice::trade::alx::detail {

struct CsvReadResult {
    std::optional<CsvDocument> document{};
    CsvFormat format{};
    std::vector<CsvDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept;
};

class CsvReader {
public:
    [[nodiscard]] CsvReadResult parse(
        std::span<const std::uint8_t> bytes,
        const std::filesystem::path& diagnosticPath = {}) const;

    [[nodiscard]] CsvReadResult readFile(const std::filesystem::path& path) const;
};

} // namespace spice::trade::alx::detail
