#pragma once

#include "../Application/Operation.h"

namespace spice::mix::detail {

int executeDreamcastParityAudit(
    const AuditDreamcastParityRequest& request,
    OperationContext& context);

} // namespace spice::mix::detail
