#pragma once

#include "SctDocumentExporter.h"

namespace spice::sct {

enum class SctDocumentReadiness {
    Unavailable,
    Inspectable,
    StructurallyValid,
    ExportReady,
};

struct SctDocumentImportAssessment {
    SctDocumentImportResult import;
    SctDocumentValidationResult documentValidation;
    SctDocumentReadiness readiness = SctDocumentReadiness::Unavailable;
};

struct SctDocumentExportAssessment {
    SctDocumentReadiness readiness = SctDocumentReadiness::Inspectable;
    SctDocumentValidationResult documentValidation;
    SctTargetValidationResult targetValidation;
    SctDocumentLayoutResult layout;
};

class SctDocumentWorkflow {
public:
    [[nodiscard]] static SctDocumentImportAssessment importForEditing(
        const SctParseResult& parsed, const SctDocumentImportOptions& options = {});
    [[nodiscard]] static SctDocumentExportAssessment assessForExport(
        const SctDocument& document, const SctDocumentExportOptions& options,
        const SctBoundImportEvidence* evidence = nullptr);
};

} // namespace spice::sct
