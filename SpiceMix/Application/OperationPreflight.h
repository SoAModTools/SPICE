#pragma once

#include "Operation.h"

#include <optional>
#include <string>

namespace spice::mix::preflight {

[[nodiscard]] std::optional<std::string> validate(const OperationRequest& request);

} // namespace spice::mix::preflight
