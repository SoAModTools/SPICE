#pragma once

#include "../Application/Operation.h"

namespace spice::fileparsing::detail {

// Internal executable-project boundary used by OperationRunner. Filetype
// libraries remain unaware of application orchestration.
int executeOperation(const OperationRequest& request, OperationContext& context);

} // namespace spice::fileparsing::detail
