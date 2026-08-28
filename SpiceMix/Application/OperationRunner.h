#pragma once

#include "Operation.h"

namespace spice::mix {

class OperationRunner {
public:
    [[nodiscard]] OperationResult run(const OperationRequest& request, OperationContext& context) const;
};

} // namespace spice::mix
