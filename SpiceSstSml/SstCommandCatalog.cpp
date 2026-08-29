#include "SstCommandCatalog.h"

#include <array>

namespace spice::sstsml {
namespace {

constexpr std::array<SstCommandCatalogEntry, 12> kEntries{{
    { 0, "Base resource setup", "Creates the base local model/object slots and supplies transform data for the same-index SML resource." },
    { 1, "Lighting and environment", "Supplies stage lighting and render-environment rows." },
    { 2, "Coordinate deformation", "Applies weighted model-coordinate deformation used by moving liquid-like surfaces." },
    { 3, "Texture-coordinate adjustment", "Applies a one-time texture-coordinate adjustment to selected mesh strips." },
    { 4, "Transform delta", "Applies a per-frame object transform delta." },
    { 5, "No-payload command", "Recognized by the command walker with no structural payload; its runtime role remains unresolved." },
    { 6, "Scalar object adjustment", "Code-supported scalar object adjustment not observed in the known stage corpus." },
    { 7, "Sine object adjustment", "Code-supported sine-driven object adjustment not observed in the known stage corpus." },
    { 8, "Presentation animation", "Sets up node-oriented texture or presentation animation." },
    { 9, "Object orientation", "Sets model or object orientation state." },
    { 10, "Vector interpolation", "Sets up vector interpolation or oscillation." },
    { 11, "Vector-motion controller", "Controls rare vector motion with a separate nearby ramp or hold consumer window." },
}};

} // namespace

std::optional<SstCommandCatalogEntry> commandCatalogEntry(const std::int16_t type) {
    if (type < 0 || static_cast<std::size_t>(type) >= kEntries.size()) {
        return std::nullopt;
    }
    return kEntries[static_cast<std::size_t>(type)];
}

} // namespace spice::sstsml
