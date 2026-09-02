#include "SctDocumentWorkflow.h"

namespace spice::sct {

SctDocumentImportAssessment SctDocumentWorkflow::importForEditing(
    const SctParseResult& parsed, const SctDocumentImportOptions& options) {
    SctDocumentImportAssessment result;
    result.import = SctDocumentImporter::import(parsed, options);
    if (!result.import.document) return result;
    result.readiness = SctDocumentReadiness::Inspectable;
    result.documentValidation = SctDocumentValidator::validateDocument(*result.import.document);
    if (result.documentValidation.validDocument) {
        result.readiness = SctDocumentReadiness::StructurallyValid;
    }
    return result;
}

SctDocumentExportAssessment SctDocumentWorkflow::assessForExport(
    const SctDocument& document, const SctDocumentExportOptions& options,
    const SctBoundImportEvidence* evidence) {
    SctDocumentExportAssessment result;
    result.documentValidation = SctDocumentValidator::validateDocument(document);
    if (!result.documentValidation.validDocument) return result;
    result.readiness = SctDocumentReadiness::StructurallyValid;
    result.targetValidation = SctDocumentValidator::validateForTarget(
        document, options.targetPlatform, options.textEncoding, evidence);
    if (!result.targetValidation.validForTarget) return result;
    result.layout = SctDocumentLayoutEngine::layout(document, options, evidence);
    if (result.layout.success) result.readiness = SctDocumentReadiness::ExportReady;
    return result;
}

} // namespace spice::sct
