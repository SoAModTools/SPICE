#pragma once

#include "SctDocument.h"

#include <cstddef>
#include <optional>
#include <span>
#include <unordered_map>
#include <variant>
#include <vector>

namespace spice::sct {

using SctDocumentReferenceTarget = std::variant<SctInstructionId, SctStringId, SctFooterEntryId>;

struct SctDocumentReferenceRecord {
    SctInstructionId sourceInstruction;
    SctParameterAddress parameter;
    SctDocumentReferenceTarget target;
};

struct SctInstructionDocumentLocation {
    SctSectionId sectionId;
    std::size_t sectionOrdinal = 0;
    std::size_t instructionOrdinal = 0;
};

struct SctStringDocumentLocation {
    SctSectionId sectionId;
    std::size_t sectionOrdinal = 0;
};

// A derived, revision-scoped view. Mutating the source document invalidates the
// pointers and reference lists in this object; rebuild it after each mutation.
class SctDocumentIndex {
public:
    [[nodiscard]] static SctDocumentIndex build(const SctDocument& document);

    [[nodiscard]] const SctDocumentSection* find(SctSectionId id) const noexcept;
    [[nodiscard]] const SctDocumentInstruction* find(SctInstructionId id) const noexcept;
    [[nodiscard]] const SctDocumentString* find(SctStringId id) const noexcept;
    [[nodiscard]] const SctDocumentFooterEntry* find(SctFooterEntryId id) const noexcept;
    [[nodiscard]] const SctOpaqueAttachment* find(SctOpaqueAttachmentId id) const noexcept;

    [[nodiscard]] std::optional<std::size_t> sectionOrdinal(SctSectionId id) const noexcept;
    [[nodiscard]] std::optional<SctInstructionDocumentLocation> instructionLocation(
        SctInstructionId id) const noexcept;
    [[nodiscard]] const SctDocumentSection* owningSection(SctInstructionId id) const noexcept;
    [[nodiscard]] std::optional<SctStringDocumentLocation> stringLocation(SctStringId id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> footerEntryOrdinal(SctFooterEntryId id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> opaqueAttachmentOrdinal(
        SctOpaqueAttachmentId id) const noexcept;

    [[nodiscard]] std::vector<const SctOpaqueAttachment*> attachmentsFor(const SctOpaqueAnchor& anchor) const;
    [[nodiscard]] std::vector<SctDocumentReferenceRecord> outboundReferences(SctInstructionId source) const;
    [[nodiscard]] std::vector<SctDocumentReferenceRecord> inboundReferences(
        const SctDocumentReferenceTarget& target) const;

private:
    std::unordered_map<std::uint64_t, const SctDocumentSection*> sections_;
    std::unordered_map<std::uint64_t, const SctDocumentInstruction*> instructions_;
    std::unordered_map<std::uint64_t, const SctDocumentString*> strings_;
    std::unordered_map<std::uint64_t, const SctDocumentFooterEntry*> footerEntries_;
    std::unordered_map<std::uint64_t, const SctOpaqueAttachment*> attachments_;
    std::unordered_map<std::uint64_t, std::size_t> sectionOrdinals_;
    std::unordered_map<std::uint64_t, SctInstructionDocumentLocation> instructionLocations_;
    std::unordered_map<std::uint64_t, SctStringDocumentLocation> stringLocations_;
    std::unordered_map<std::uint64_t, std::size_t> footerEntryOrdinals_;
    std::unordered_map<std::uint64_t, std::size_t> attachmentOrdinals_;
    std::vector<const SctOpaqueAttachment*> attachmentOrder_;
    std::vector<SctDocumentReferenceRecord> references_;
};

} // namespace spice::sct
