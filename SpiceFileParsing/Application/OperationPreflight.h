#pragma once

#include "Operation.h"

#include <optional>
#include <string>

namespace spice::fileparsing::preflight {

[[nodiscard]] std::optional<std::string> validate(const OperationRequest& request);

} // namespace spice::fileparsing::preflight
