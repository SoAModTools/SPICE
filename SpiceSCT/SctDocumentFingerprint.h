#pragma once

#include "SctDocument.h"
#include "SctDocumentImporter.h"
#include "SctOpcodeMetadata.h"

#include <array>
#include <cstdint>
#include <vector>

namespace spice::sct::detail {

using SctValidationFingerprint = std::array<std::uint8_t, 32>;

struct SctValidationReceiptState {
    SctValidationFingerprint fingerprint{};
    std::vector<SctDocumentDiagnostic> diagnostics;
};

[[nodiscard]] SctValidationFingerprint fingerprintDocument(
    const SctDocument& document) noexcept;

[[nodiscard]] SctValidationFingerprint fingerprintTargetValidation(
    const SctValidationFingerprint& documentFingerprint,
    SctPlatform platform,
    SctTextEncoding textEncoding,
    const SctBoundImportEvidence* evidence) noexcept;

} // namespace spice::sct::detail
