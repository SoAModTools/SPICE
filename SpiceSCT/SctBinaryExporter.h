#pragma once

#include "SctModel.h"

#include <cstdint>
#include <vector>

namespace spice::sct {

enum class SctExportMode {
    Canonical,
    PreserveBytesForTest,
};

enum class SctExportEndianPolicy {
    PreserveParsed,
    Big,
    Little,
};

struct SctExportOptions {
    SctExportMode mode = SctExportMode::Canonical;
    SctExportEndianPolicy endianPolicy = SctExportEndianPolicy::PreserveParsed;
    bool compressAklz = false;
};

class SctBinaryExporter {
public:
    [[nodiscard]] std::vector<std::uint8_t> exportFile(
        const SctParseResult& parseResult,
        const SctExportOptions& options = {}) const;
};

} // namespace spice::sct
