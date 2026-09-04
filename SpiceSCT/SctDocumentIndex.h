#pragma once

#include "SctDocument.h"

#include <cstddef>
#include <optional>
#include <span>
#include <unordered_map>
#include <variant>
#include <vector>

namespace spice::sct {

struct SctInstructionDocumentLocation {
    SctSectionId sectionId;
    std::size_t sectionOrdinal = 0;
    std::size_t instructionOrdinal = 0;
};

struct SctStringDocumentLocation {
    SctSectionId sectionId;
    std::size_t sectionOrdinal = 0;
};

// A value-owned, revision-scoped view. It contains no pointers into the source
// document. Rebuild after mutation; resolution verifies the supplied document.
class SctDocumentIndex {
public:
    [[nodiscard]] static SctDocumentIndex build(const SctDocument& document);

    [[nodiscard]] const SctDocumentSection* find(const SctDocument& document, SctSectionId id) const noexcept;
    [[nodiscard]] const SctDocumentInstruction* find(const SctDocument& document, SctInstructionId id) const noexcept;
    [[nodiscard]] const SctDocumentString* find(const SctDocument& document, SctStringId id) const noexcept;
    [[nodiscard]] const SctDocumentSupplementaryText* find(const SctDocument& document, SctSupplementaryTextId id) const noexcept;
    [[nodiscard]] const SctOpaqueAttachment* find(const SctDocument& document, SctOpaqueAttachmentId id) const noexcept;

    [[nodiscard]] std::optional<std::size_t> sectionOrdinal(SctSectionId id) const noexcept;
    [[nodiscard]] std::optional<SctInstructionDocumentLocation> instructionLocation(
        SctInstructionId id) const noexcept;
    [[nodiscard]] const SctDocumentSection* owningSection(
        const SctDocument& document, SctInstructionId id) const noexcept;
    [[nodiscard]] std::optional<SctStringDocumentLocation> stringLocation(SctStringId id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> supplementaryTextOrdinal(SctSupplementaryTextId id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> opaqueAttachmentOrdinal(
        SctOpaqueAttachmentId id) const noexcept;

    [[nodiscard]] std::vector<SctOpaqueAttachmentId> attachmentsFor(const SctOpaqueAnchor& anchor) const;

private:
    std::unordered_map<std::uint64_t, std::size_t> sectionOrdinals_;
    std::unordered_map<std::uint64_t, SctInstructionDocumentLocation> instructionLocations_;
    std::unordered_map<std::uint64_t, SctStringDocumentLocation> stringLocations_;
    std::unordered_map<std::uint64_t, std::size_t> supplementaryTextOrdinals_;
    std::unordered_map<std::uint64_t, std::size_t> attachmentOrdinals_;
    std::vector<std::pair<SctOpaqueAttachmentId, SctOpaqueAnchor>> attachmentOrder_;
};

} // namespace spice::sct
