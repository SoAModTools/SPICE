#pragma once

#include "Operation.h"

namespace spice::fileparsing {

class OperationRunner {
public:
    [[nodiscard]] OperationResult run(const OperationRequest& request, OperationContext& context) const;
};

} // namespace spice::fileparsing
