#pragma once

#include "SctModel.h"

#include <string>

namespace spice::sct {

[[nodiscard]] SctParseResult runSctParseProbe(const std::string& sctPath = "<PATH_TO_SCT_FILE>");
[[nodiscard]] std::string formatParseSummary(const SctParseResult& parseResult);

} // namespace spice::sct
