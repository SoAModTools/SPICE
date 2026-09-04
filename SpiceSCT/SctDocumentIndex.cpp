#include "SctDocumentIndex.h"

namespace spice::sct {
SctDocumentIndex SctDocumentIndex::build(const SctDocument& document) {
    SctDocumentIndex result;
    for (std::size_t sectionOrdinal = 0; sectionOrdinal < document.sections.size(); ++sectionOrdinal) {
        const auto& section = document.sections[sectionOrdinal];
        result.sectionOrdinals_.emplace(section.id.value(), sectionOrdinal);
        if (const auto* script = std::get_if<SctScriptSectionContent>(&section.content)) {
            for (std::size_t instructionOrdinal = 0;
                instructionOrdinal < script->instructions.size(); ++instructionOrdinal) {
                const auto& instruction = script->instructions[instructionOrdinal];
                result.instructionLocations_.emplace(instruction.id.value(),
                    SctInstructionDocumentLocation{section.id, sectionOrdinal, instructionOrdinal});
            }
        } else if (const auto* stringContent = std::get_if<SctStringSectionContent>(&section.content)) {
            const auto& string = stringContent->string;
            result.stringLocations_.emplace(string.id.value(),
                SctStringDocumentLocation{section.id, sectionOrdinal});
        }
    }
    for (std::size_t ordinal = 0; ordinal < document.supplementaryText.size(); ++ordinal) {
        const auto& text = document.supplementaryText[ordinal];
        result.supplementaryTextOrdinals_.emplace(text.id.value(), ordinal);
    }
    for (std::size_t ordinal = 0; ordinal < document.opaqueAttachments.size(); ++ordinal) {
        const auto& attachment = document.opaqueAttachments[ordinal];
        result.attachmentOrdinals_.emplace(attachment.id.value(), ordinal);
        result.attachmentOrder_.emplace_back(attachment.id, attachment.anchor);
    }
    return result;
}

const SctDocumentSection* SctDocumentIndex::find(
    const SctDocument& document, SctSectionId id) const noexcept {
    const auto ordinal = sectionOrdinal(id);
    if (!ordinal || *ordinal >= document.sections.size()) return nullptr;
    return document.sections[*ordinal].id == id ? &document.sections[*ordinal] : nullptr;
}
const SctDocumentInstruction* SctDocumentIndex::find(
    const SctDocument& document, SctInstructionId id) const noexcept {
    const auto location = instructionLocation(id);
    if (!location || location->sectionOrdinal >= document.sections.size()) return nullptr;
    const auto& section = document.sections[location->sectionOrdinal];
    if (section.id != location->sectionId) return nullptr;
    const auto* script = std::get_if<SctScriptSectionContent>(&section.content);
    if (script == nullptr || location->instructionOrdinal >= script->instructions.size()) return nullptr;
    const auto& instruction = script->instructions[location->instructionOrdinal];
    return instruction.id == id ? &instruction : nullptr;
}
const SctDocumentString* SctDocumentIndex::find(
    const SctDocument& document, SctStringId id) const noexcept {
    const auto found = stringLocations_.find(id.value());
    if (found == stringLocations_.end() || found->second.sectionOrdinal >= document.sections.size()) return nullptr;
    const auto& section = document.sections[found->second.sectionOrdinal];
    if (section.id != found->second.sectionId) return nullptr;
    const auto* content = std::get_if<SctStringSectionContent>(&section.content);
    return content != nullptr && content->string.id == id ? &content->string : nullptr;
}
const SctDocumentSupplementaryText* SctDocumentIndex::find(
    const SctDocument& document, SctSupplementaryTextId id) const noexcept {
    const auto ordinal = supplementaryTextOrdinal(id);
    if (!ordinal || *ordinal >= document.supplementaryText.size()) return nullptr;
    return document.supplementaryText[*ordinal].id == id ? &document.supplementaryText[*ordinal] : nullptr;
}
const SctOpaqueAttachment* SctDocumentIndex::find(
    const SctDocument& document, SctOpaqueAttachmentId id) const noexcept {
    const auto ordinal = opaqueAttachmentOrdinal(id);
    if (!ordinal || *ordinal >= document.opaqueAttachments.size()) return nullptr;
    return document.opaqueAttachments[*ordinal].id == id ? &document.opaqueAttachments[*ordinal] : nullptr;
}

std::optional<std::size_t> SctDocumentIndex::sectionOrdinal(SctSectionId id) const noexcept {
    const auto found = sectionOrdinals_.find(id.value());
    return found == sectionOrdinals_.end() ? std::nullopt
                                           : std::optional<std::size_t>{found->second};
}

std::optional<SctInstructionDocumentLocation> SctDocumentIndex::instructionLocation(
    SctInstructionId id) const noexcept {
    const auto found = instructionLocations_.find(id.value());
    return found == instructionLocations_.end()
        ? std::nullopt : std::optional<SctInstructionDocumentLocation>{found->second};
}

const SctDocumentSection* SctDocumentIndex::owningSection(
    const SctDocument& document, SctInstructionId id) const noexcept {
    const auto location = instructionLocation(id);
    return location.has_value() ? find(document, location->sectionId) : nullptr;
}

std::optional<SctStringDocumentLocation> SctDocumentIndex::stringLocation(SctStringId id) const noexcept {
    const auto found = stringLocations_.find(id.value());
    return found == stringLocations_.end()
        ? std::nullopt : std::optional<SctStringDocumentLocation>{found->second};
}

std::optional<std::size_t> SctDocumentIndex::supplementaryTextOrdinal(SctSupplementaryTextId id) const noexcept {
    const auto found = supplementaryTextOrdinals_.find(id.value());
    return found == supplementaryTextOrdinals_.end() ? std::nullopt
                                               : std::optional<std::size_t>{found->second};
}

std::optional<std::size_t> SctDocumentIndex::opaqueAttachmentOrdinal(
    SctOpaqueAttachmentId id) const noexcept {
    const auto found = attachmentOrdinals_.find(id.value());
    return found == attachmentOrdinals_.end() ? std::nullopt
                                              : std::optional<std::size_t>{found->second};
}

std::vector<SctOpaqueAttachmentId> SctDocumentIndex::attachmentsFor(const SctOpaqueAnchor& anchor) const {
    std::vector<SctOpaqueAttachmentId> result;
    for (const auto& [id, candidate] : attachmentOrder_) {
        if (candidate == anchor) result.push_back(id);
    }
    return result;
}

} // namespace spice::sct
