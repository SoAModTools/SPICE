#pragma once

#include "../SpiceMLD/Model/BlenderIrModel.h"

#include <cstddef>
#include <string>

namespace spice::sstsml::exporting {

[[nodiscard]] std::string makeSmlEntryBlenderIrPrefix(const std::string& stem, std::size_t recordIndex);

void namespaceSmlEntryBlenderIrScene(
    spice::mld::model::BlenderIrScene& scene,
    const std::string& stem,
    std::size_t recordIndex);

class SmlBlenderIrCombiner {
public:
    void appendEntryScene(
        spice::mld::model::BlenderIrScene entryScene,
        const std::string& stem,
        std::size_t recordIndex);

    [[nodiscard]] const spice::mld::model::BlenderIrScene& scene() const noexcept;
    [[nodiscard]] spice::mld::model::BlenderIrScene&& takeScene() noexcept;

private:
    spice::mld::model::BlenderIrScene m_scene{};
};

} // namespace spice::sstsml::exporting

