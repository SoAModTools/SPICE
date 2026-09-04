#pragma once

#include "SstSmlDocumentImporter.h"

#include <vector>

namespace spice::sstsml {

enum class SstSmlDocumentReadiness { Invalid, ReadOnly };

struct SstSmlDocumentValidationResult {
    SstSmlDocumentReadiness readiness{ SstSmlDocumentReadiness::Invalid };
    std::vector<SstSmlDocumentDiagnostic> diagnostics{};
    [[nodiscard]] bool ok() const noexcept { return readiness == SstSmlDocumentReadiness::ReadOnly; }
};

class SstSmlDocumentValidator {
public:
    [[nodiscard]] static SstSmlDocumentValidationResult validate(const SstSmlDocument& document);
};

[[nodiscard]] const char* toString(SstSmlDocumentReadiness readiness) noexcept;

} // namespace spice::sstsml
