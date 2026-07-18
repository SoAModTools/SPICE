#pragma once

#include "MldGroundModel.h"

#include <string>
#include <vector>

namespace spice::mld::model {

struct GrndGridAssignmentOptions {
    float originX = 0.0F;
    float originZ = 0.0F;
};

[[nodiscard]] bool assignGrndTrianglesToIntersectingCells(
    GrndData& data,
    const GrndGridAssignmentOptions& options,
    std::vector<std::string>* diagnostics = nullptr);

[[nodiscard]] bool synchronizeGrndSourceView(
    GrndData& data,
    std::vector<std::string>* diagnostics = nullptr);

[[nodiscard]] bool synchronizeGobjSourceView(
    GobjData& data,
    std::vector<std::string>* diagnostics = nullptr);

} // namespace spice::mld::model
