#include "WMapAreaModel.h"

#include <algorithm>

namespace spice::wmap {

std::uint8_t& WMapAreaDocument::cell(
    const std::size_t layer,
    const std::size_t row,
    const std::size_t column) {
    return layers.at(layer).at(row).at(column);
}

const std::uint8_t& WMapAreaDocument::cell(
    const std::size_t layer,
    const std::size_t row,
    const std::size_t column) const {
    return layers.at(layer).at(row).at(column);
}

const char* toString(const DiagnosticSeverity severity) noexcept {
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

const char* toString(const WMapAreaStorage storage) noexcept {
    switch (storage) {
    case WMapAreaStorage::Raw:
        return "raw";
    case WMapAreaStorage::Aklz:
        return "aklz";
    }
    return "unknown";
}

bool hasErrors(const std::vector<WMapAreaDiagnostic>& diagnostics) noexcept {
    return std::any_of(
        diagnostics.begin(),
        diagnostics.end(),
        [](const WMapAreaDiagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::Error;
        });
}

} // namespace spice::wmap
