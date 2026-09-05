#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace spice::trade::alx::detail {

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error,
};

enum class CsvLineEnding {
    Lf,
    CrLf,
};

struct CsvDiagnostic {
    DiagnosticSeverity severity{ DiagnosticSeverity::Info };
    std::string message{};
    std::filesystem::path relativePath{};
    std::optional<std::size_t> row{};
    std::optional<std::size_t> column{};
};

struct CsvFormat {
    bool utf8Bom{ false };
    CsvLineEnding lineEnding{ CsvLineEnding::CrLf };
    bool finalLineEnding{ true };

    bool operator==(const CsvFormat&) const = default;
};

using CsvRow = std::vector<std::string>;

struct CsvDocument {
    std::vector<std::string> headers{};
    std::vector<CsvRow> rows{};

    [[nodiscard]] std::optional<std::size_t> columnIndex(std::string_view header) const noexcept;

    bool operator==(const CsvDocument&) const = default;
};

[[nodiscard]] const char* toString(DiagnosticSeverity severity) noexcept;
[[nodiscard]] const char* toString(CsvLineEnding lineEnding) noexcept;
[[nodiscard]] bool hasErrors(const std::vector<CsvDiagnostic>& diagnostics) noexcept;

} // namespace spice::trade::alx::detail
