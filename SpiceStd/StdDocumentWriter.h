#pragma once

#include "StdDocumentValidator.h"

namespace spice::stdfile {

struct StdDocumentWriteResult {
    std::vector<std::uint8_t> bytes{};
    std::vector<StdDocumentDiagnostic> diagnostics{};
    [[nodiscard]] bool ok() const noexcept;
};

class StdDocumentWriter {
public:
    [[nodiscard]] static StdDocumentWriteResult write(
        const StdDocument& document,
        const StdWriteTarget& target,
        const StdImportReceipt* receipt = nullptr);
};

} // namespace spice::stdfile
