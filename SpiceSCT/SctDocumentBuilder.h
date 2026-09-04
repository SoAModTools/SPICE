#pragma once

#include "SctDocument.h"

#include <optional>
#include <vector>

namespace spice::sct {

struct SctDocumentBuildResult {
    std::optional<SctDocument> document;
    std::vector<SctDocumentDiagnostic> diagnostics;
};

// Low-level construction support for callers that keep document mutation and
// transaction policy outside SpiceSCT. Reconstitution is platform-neutral and
// validates only identity state; format validation remains a separate step.
class SctDocumentBuilder {
public:
    SctDocumentBuilder() = default;

    [[nodiscard]] SctDocument& document() noexcept { return document_; }
    [[nodiscard]] const SctDocument& document() const noexcept { return document_; }

    [[nodiscard]] SctSectionId allocateSectionId() noexcept { return document_.allocateSectionId(); }
    [[nodiscard]] SctInstructionId allocateInstructionId() noexcept { return document_.allocateInstructionId(); }
    [[nodiscard]] SctStringId allocateStringId() noexcept { return document_.allocateStringId(); }
    [[nodiscard]] SctSupplementaryTextId allocateSupplementaryTextId() noexcept { return document_.allocateSupplementaryTextId(); }
    [[nodiscard]] SctOpaqueAttachmentId allocateOpaqueAttachmentId() noexcept {
        return document_.allocateOpaqueAttachmentId();
    }

    [[nodiscard]] SctDocument finish() && noexcept { return std::move(document_); }

    [[nodiscard]] static SctDocumentBuildResult reconstitute(SctDocument document);

private:
    SctDocument document_;
};

} // namespace spice::sct
