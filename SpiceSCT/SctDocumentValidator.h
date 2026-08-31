#pragma once

#include "SctDocument.h"

#include <vector>

namespace spice::sct {

struct SctValidationResult {
    bool validForLayout = false;
    std::vector<SctDocumentDiagnostic> diagnostics;
    std::vector<SctOpaqueAttachmentId> unresolvedOpaqueAttachments;
};

class SctDocumentValidator {
public:
    [[nodiscard]] static SctValidationResult validate(const SctDocument& document, SctPlatform platform);
};

} // namespace spice::sct
