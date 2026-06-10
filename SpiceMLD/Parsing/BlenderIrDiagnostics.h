#pragma once

#include "../Model/BlenderIrModel.h"

namespace spice::mld::parsing {

class BlenderIrDiagnostics {
public:
    static void finalizeMesh(model::BlenderIrMesh& mesh);
};

} // namespace spice::mld::parsing
