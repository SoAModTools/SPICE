#pragma once

#include "StdModel.h"

#include <string>

namespace spice::stdfile {

class StdJsonExporter {
public:
    [[nodiscard]] std::string toJson(const StdFile& file) const;
};

} // namespace spice::stdfile
