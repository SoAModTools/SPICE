#pragma once

#include "SctModel.h"

#include <string>

namespace spice::sct {

class SctJsonExporter {
public:
    [[nodiscard]] std::string toJson(const SctParseResult& result) const;
};

} // namespace spice::sct
