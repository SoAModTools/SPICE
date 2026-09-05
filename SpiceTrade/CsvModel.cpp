#include "CsvModel.h"

#include <algorithm>

namespace spice::trade::alx::detail {

std::optional<std::size_t> CsvDocument::columnIndex(const std::string_view header) const noexcept
{
    const auto found = std::find(headers.begin(), headers.end(), header);
    if (found == headers.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(headers.begin(), found));
}

const char* toString(const DiagnosticSeverity severity) noexcept
{
    switch (severity) {
    case DiagnosticSeverity::Info:
        return "info";
    case DiagnosticSeverity::Warning:
        return "warning";
    case DiagnosticSeverity::Error:
        return "error";
    }
    return "unknown";
}

const char* toString(const CsvLineEnding lineEnding) noexcept
{
    switch (lineEnding) {
    case CsvLineEnding::Lf:
        return "lf";
    case CsvLineEnding::CrLf:
        return "crlf";
    }
    return "unknown";
}

bool hasErrors(const std::vector<CsvDiagnostic>& diagnostics) noexcept
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

} // namespace spice::trade::alx::detail
