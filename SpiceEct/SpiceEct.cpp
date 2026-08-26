#include "SpiceEct.h"

namespace spice::ect {

EctLayout EctFile::layout() const noexcept {
    return std::holds_alternative<EctFlatContent>(content)
        ? EctLayout::Flat
        : EctLayout::OverworldIndexed;
}

const char* toString(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::Info: return "info";
    case DiagnosticSeverity::Warning: return "warning";
    case DiagnosticSeverity::Error: return "error";
    }
    return "unknown";
}

const char* toString(EctLayout layout) {
    switch (layout) {
    case EctLayout::Flat: return "flat";
    case EctLayout::OverworldIndexed: return "overworld-indexed";
    }
    return "unknown";
}

const char* toString(EctTargetPlatform platform) {
    switch (platform) {
    case EctTargetPlatform::Dreamcast: return "dreamcast";
    case EctTargetPlatform::GameCube: return "gamecube";
    }
    return "unknown";
}

} // namespace spice::ect
