#pragma once

#include "CsvModel.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace spice::trade::alx {

struct CsvWriteResult {
    std::vector<std::uint8_t> bytes{};
    std::vector<CsvDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept;
};

class CsvWriter {
public:
    [[nodiscard]] CsvWriteResult write(
        const CsvDocument& document,
        const CsvFormat& format = {}) const;

    [[nodiscard]] CsvWriteResult writeFile(
        const CsvDocument& document,
        const std::filesystem::path& path,
        const CsvFormat& format = {}) const;
};

} // namespace spice::trade::alx
