#pragma once

#include "SstSmlDocumentImporter.h"

#include <vector>

namespace spice::sstsml {

enum class SstSmlDocumentReadiness { Invalid, Valid };

struct SstSmlDocumentValidationResult {
    SstSmlDocumentReadiness readiness{ SstSmlDocumentReadiness::Invalid };
    std::vector<SstSmlDocumentDiagnostic> diagnostics{};
    [[nodiscard]] bool ok() const noexcept { return readiness == SstSmlDocumentReadiness::Valid; }
};

class SstSmlDocumentValidator {
public:
    [[nodiscard]] static SstSmlDocumentValidationResult validate(
        const SstSmlDocument& document,
        const SstSmlDocumentImportReceipt* receipt = nullptr);
};

[[nodiscard]] const char* toString(SstSmlDocumentReadiness readiness) noexcept;

} // namespace spice::sstsml
