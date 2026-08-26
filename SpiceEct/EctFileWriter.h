#pragma once

#include "EctModel.h"

#include <cstdint>
#include <vector>

namespace spice::ect {

struct EctWriteResult {
    std::vector<std::uint8_t> bytes{};
    std::vector<EctDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept;
};

class EctFileWriter {
public:
    [[nodiscard]] EctWriteResult write(
        const EctFile& file,
        EctTargetPlatform targetPlatform) const;
};

} // namespace spice::ect
