#pragma once

#include "SctDocumentIndex.h"
#include "SctDocumentImporter.h"

#include <optional>
#include <vector>

namespace spice::sct {

struct SctReferenceRepairCandidate {
    SctDocumentReferenceTarget target;
};

struct SctReferenceRepairIssue {
    SctInstructionId sourceInstruction;
    SctParameterAddress parameter;
    SctExpectedReferenceTarget expectedTarget;
    std::vector<std::uint32_t> encodedWords;
    std::optional<SctUnresolvedReferenceObservation> sourceObservation;
    std::vector<SctReferenceRepairCandidate> candidates;
};

struct SctReferenceRepairAnalysis {
    std::vector<SctReferenceRepairIssue> issues;
    std::vector<SctDocumentDiagnostic> diagnostics;
};

struct SctReferenceValueResult {
    std::optional<SctDocumentParameterValue> value;
    std::vector<SctDocumentDiagnostic> diagnostics;
};

class SctReferenceRepair {
public:
    [[nodiscard]] static SctReferenceRepairAnalysis analyze(
        const SctDocument& document, const SctBoundImportEvidence* evidence = nullptr);
    [[nodiscard]] static SctReferenceValueResult createReferenceValue(
        const SctDocument& document, SctInstructionId sourceInstruction,
        SctParameterAddress parameter, const SctDocumentReferenceTarget& target);
    [[nodiscard]] static SctReferenceValueResult createReferenceValue(
        const SctDocument& document, std::uint16_t sourceOpcode,
        SctParameterAddress parameter, const SctDocumentReferenceTarget& target);
    [[nodiscard]] static SctReferenceValueResult resolve(
        const SctDocument& document, SctInstructionId sourceInstruction,
        SctParameterAddress parameter, const SctDocumentReferenceTarget& target);
};

} // namespace spice::sct
