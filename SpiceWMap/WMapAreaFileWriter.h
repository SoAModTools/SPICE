#pragma once

#include "WMapAreaModel.h"

#include <cstdint>
#include <vector>

namespace spice::wmap {

struct WMapAreaWriteResult {
    std::vector<std::uint8_t> bytes{};
    std::vector<WMapAreaDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept;
};

class WMapAreaFileWriter {
public:
    [[nodiscard]] WMapAreaWriteResult write(
        const WMapAreaDocument& document,
        WMapAreaStorage storage) const;
};

} // namespace spice::wmap
