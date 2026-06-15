#pragma once

#include "../SpiceMLD/Model/BlenderIrModel.h"

#include <cstddef>
#include <optional>
#include <string>

namespace spice::sstsml::exporting {

[[nodiscard]] std::string makeSmlEntryBlenderIrPrefix(const std::string& stem, std::size_t recordIndex);

void namespaceSmlEntryBlenderIrScene(
    spice::mld::model::BlenderIrScene& scene,
    const std::string& stem,
    std::size_t recordIndex);

struct SmlBlenderIrSstPlacementOverlay {
    bool hasPosition{ false };
    spice::mld::model::Vec3 position{};
    bool hasScale{ false };
    spice::mld::model::Vec3 scale{ 1.0F, 1.0F, 1.0F };
    bool hasRotationRaw{ false };
    spice::mld::model::Vec3 rotationRaw{};
    std::string sourceDescription{};
};

class SmlBlenderIrCombiner {
public:
    void appendEntryScene(
        spice::mld::model::BlenderIrScene entryScene,
        const std::string& stem,
        std::size_t recordIndex);

    void appendEntryScene(
        spice::mld::model::BlenderIrScene entryScene,
        const std::string& stem,
        std::size_t recordIndex,
        const std::optional<SmlBlenderIrSstPlacementOverlay>& sstPlacementOverlay);

    [[nodiscard]] const spice::mld::model::BlenderIrScene& scene() const noexcept;
    [[nodiscard]] spice::mld::model::BlenderIrScene&& takeScene() noexcept;

private:
    spice::mld::model::BlenderIrScene m_scene{};
};

} // namespace spice::sstsml::exporting
