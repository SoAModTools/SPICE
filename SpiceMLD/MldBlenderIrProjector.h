#pragma once

#include "MldDocument.h"
#include "Model/BlenderIrModel.h"

#include <optional>
#include <string>
#include <vector>

namespace spice::mld {

struct MldBlenderIrProjectionResult {
    std::optional<model::BlenderIrScene> scene{};
    std::vector<std::string> diagnostics{};
    [[nodiscard]] bool ok() const noexcept { return scene.has_value(); }
};

class MldBlenderIrProjector {
public:
    [[nodiscard]] static MldBlenderIrProjectionResult project(const MldDocument& document);
};

} // namespace spice::mld
