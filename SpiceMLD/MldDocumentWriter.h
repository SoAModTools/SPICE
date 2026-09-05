#pragma once

#include "MldDocumentValidator.h"

#include <cstdint>
#include <vector>

namespace spice::mld {

struct MldDocumentWriteResult {
    std::vector<std::uint8_t> bytes{};
    std::vector<MldDocumentDiagnostic> diagnostics{};
    [[nodiscard]] bool ok() const noexcept;
};

class MldDocumentWriter {
public:
    [[nodiscard]] static MldDocumentWriteResult write(
        const MldDocument& document,
        const MldWriteTarget& target,
        const MldImportReceipt* receipt = nullptr);
};

} // namespace spice::mld
