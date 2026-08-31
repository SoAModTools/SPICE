#pragma once

#include "SctDocument.h"
#include "SctDocumentImporter.h"
#include "SctOpcodeMetadata.h"

#include <vector>

namespace spice::sct {

struct SctValidationResult {
    bool validForLayout = false;
    std::vector<SctDocumentDiagnostic> diagnostics;
    std::vector<SctOpaqueAttachmentId> unresolvedOpaqueAttachments;
};

struct SctDocumentValidationOptions {
    SctPlatform targetPlatform = SctPlatform::GameCube;
};

class SctDocumentValidator {
public:
    [[nodiscard]] static SctValidationResult validate(
        const SctDocument& document,
        const SctDocumentValidationOptions& options,
        const SctDocumentImportReceipt* receipt = nullptr);
};

} // namespace spice::sct
