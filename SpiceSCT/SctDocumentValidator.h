#pragma once

#include "SctDocument.h"
#include "SctDocumentImporter.h"
#include "SctOpcodeMetadata.h"

#include <vector>

namespace spice::sct {

struct SctDocumentValidationResult {
    bool validDocument = false;
    std::vector<SctDocumentDiagnostic> diagnostics;
};

struct SctTargetValidationResult {
    bool validForTarget = false;
    std::vector<SctDocumentDiagnostic> diagnostics;
    std::vector<SctOpaqueAttachmentId> unresolvedOpaqueAttachments;
};

class SctDocumentValidator {
public:
    [[nodiscard]] static SctDocumentValidationResult validateDocument(
        const SctDocument& document);
    [[nodiscard]] static SctTargetValidationResult validateForTarget(
        const SctDocument& document,
        SctPlatform targetPlatform,
        SctTextProfile textProfile,
        const SctDocumentImportReceipt* receipt = nullptr);
};

} // namespace spice::sct
