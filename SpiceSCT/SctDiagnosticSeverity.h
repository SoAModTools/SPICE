#pragma once

#include <string_view>

namespace spice::sct {

enum class SctDiagnosticSeverity {
    Info,
    Warning,
    Error,
};

[[nodiscard]] constexpr std::string_view sctDiagnosticSeverityName(
    const SctDiagnosticSeverity severity) noexcept {
    switch (severity) {
    case SctDiagnosticSeverity::Info: return "info";
    case SctDiagnosticSeverity::Warning: return "warning";
    case SctDiagnosticSeverity::Error: return "error";
    }
    return "error";
}

} // namespace spice::sct
