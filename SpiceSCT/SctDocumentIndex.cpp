#include "SctDocumentIndex.h"

namespace spice::sct {
namespace {

void collectReference(const SctDocumentInstruction& instruction, const SctDocumentParameter& parameter,
    std::optional<std::uint32_t> groupOrdinal, std::vector<SctDocumentReferenceRecord>& references) {
    if (const auto* target = std::get_if<SctInstructionReference>(&parameter.value)) {
        references.push_back({instruction.id, {parameter.schemaIndex, groupOrdinal}, target->target});
    } else if (const auto* target = std::get_if<SctStringReference>(&parameter.value)) {
        references.push_back({instruction.id, {parameter.schemaIndex, groupOrdinal}, target->target});
    } else if (const auto* target = std::get_if<SctFooterEntryReference>(&parameter.value)) {
        references.push_back({instruction.id, {parameter.schemaIndex, groupOrdinal}, target->target});
    }
}

} // namespace

SctDocumentIndex SctDocumentIndex::build(const SctDocument& document) {
    SctDocumentIndex result;
    for (std::size_t sectionOrdinal = 0; sectionOrdinal < document.sections.size(); ++sectionOrdinal) {
        const auto& section = document.sections[sectionOrdinal];
        result.sections_.emplace(section.id.value(), &section);
        result.sectionOrdinals_.emplace(section.id.value(), sectionOrdinal);
        if (const auto* script = std::get_if<SctScriptSectionContent>(&section.content)) {
            for (std::size_t instructionOrdinal = 0;
                instructionOrdinal < script->instructions.size(); ++instructionOrdinal) {
                const auto& instruction = script->instructions[instructionOrdinal];
                result.instructions_.emplace(instruction.id.value(), &instruction);
                result.instructionLocations_.emplace(instruction.id.value(),
                    SctInstructionDocumentLocation{section.id, sectionOrdinal, instructionOrdinal});
                for (const auto& parameter : instruction.fixedParameters) {
                    collectReference(instruction, parameter, std::nullopt, result.references_);
                }
                for (std::size_t ordinal = 0; ordinal < instruction.repeatedParameterGroups.size(); ++ordinal) {
                    for (const auto& parameter : instruction.repeatedParameterGroups[ordinal].parameters) {
                        collectReference(instruction, parameter, static_cast<std::uint32_t>(ordinal), result.references_);
                    }
                }
            }
        }
    }
    for (std::size_t ordinal = 0; ordinal < document.strings.size(); ++ordinal) {
        const auto& string = document.strings[ordinal];
        result.strings_.emplace(string.id.value(), &string);
        result.stringLocations_.emplace(string.id.value(), SctStringDocumentLocation{ordinal});
    }
    for (std::size_t sectionOrdinal = 0; sectionOrdinal < document.sections.size(); ++sectionOrdinal) {
        const auto& section = document.sections[sectionOrdinal];
        const auto* stringContent = std::get_if<SctStringSectionContent>(&section.content);
        if (stringContent == nullptr) continue;
        const auto found = result.stringLocations_.find(stringContent->stringId.value());
        if (found != result.stringLocations_.end() && !found->second.sectionId.has_value()) {
            found->second.sectionId = section.id;
            found->second.sectionOrdinal = sectionOrdinal;
        }
    }
    for (std::size_t ordinal = 0; ordinal < document.footerEntries.size(); ++ordinal) {
        const auto& footer = document.footerEntries[ordinal];
        result.footerEntries_.emplace(footer.id.value(), &footer);
        result.footerEntryOrdinals_.emplace(footer.id.value(), ordinal);
    }
    for (std::size_t ordinal = 0; ordinal < document.opaqueAttachments.size(); ++ordinal) {
        const auto& attachment = document.opaqueAttachments[ordinal];
        result.attachments_.emplace(attachment.id.value(), &attachment);
        result.attachmentOrdinals_.emplace(attachment.id.value(), ordinal);
        result.attachmentOrder_.push_back(&attachment);
    }
    return result;
}

const SctDocumentSection* SctDocumentIndex::find(SctSectionId id) const noexcept {
    const auto found = sections_.find(id.value());
    return found == sections_.end() ? nullptr : found->second;
}
const SctDocumentInstruction* SctDocumentIndex::find(SctInstructionId id) const noexcept {
    const auto found = instructions_.find(id.value());
    return found == instructions_.end() ? nullptr : found->second;
}
const SctDocumentString* SctDocumentIndex::find(SctStringId id) const noexcept {
    const auto found = strings_.find(id.value());
    return found == strings_.end() ? nullptr : found->second;
}
const SctDocumentFooterEntry* SctDocumentIndex::find(SctFooterEntryId id) const noexcept {
    const auto found = footerEntries_.find(id.value());
    return found == footerEntries_.end() ? nullptr : found->second;
}
const SctOpaqueAttachment* SctDocumentIndex::find(SctOpaqueAttachmentId id) const noexcept {
    const auto found = attachments_.find(id.value());
    return found == attachments_.end() ? nullptr : found->second;
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

const SctDocumentSection* SctDocumentIndex::owningSection(SctInstructionId id) const noexcept {
    const auto location = instructionLocation(id);
    return location.has_value() ? find(location->sectionId) : nullptr;
}

std::optional<SctStringDocumentLocation> SctDocumentIndex::stringLocation(SctStringId id) const noexcept {
    const auto found = stringLocations_.find(id.value());
    return found == stringLocations_.end()
        ? std::nullopt : std::optional<SctStringDocumentLocation>{found->second};
}

std::optional<std::size_t> SctDocumentIndex::footerEntryOrdinal(SctFooterEntryId id) const noexcept {
    const auto found = footerEntryOrdinals_.find(id.value());
    return found == footerEntryOrdinals_.end() ? std::nullopt
                                               : std::optional<std::size_t>{found->second};
}

std::optional<std::size_t> SctDocumentIndex::opaqueAttachmentOrdinal(
    SctOpaqueAttachmentId id) const noexcept {
    const auto found = attachmentOrdinals_.find(id.value());
    return found == attachmentOrdinals_.end() ? std::nullopt
                                              : std::optional<std::size_t>{found->second};
}

std::vector<const SctOpaqueAttachment*> SctDocumentIndex::attachmentsFor(const SctOpaqueAnchor& anchor) const {
    std::vector<const SctOpaqueAttachment*> result;
    for (const auto* attachment : attachmentOrder_) {
        if (attachment->anchor == anchor) result.push_back(attachment);
    }
    return result;
}

std::vector<SctDocumentReferenceRecord> SctDocumentIndex::outboundReferences(SctInstructionId source) const {
    std::vector<SctDocumentReferenceRecord> result;
    for (const auto& reference : references_) if (reference.sourceInstruction == source) result.push_back(reference);
    return result;
}

std::vector<SctDocumentReferenceRecord> SctDocumentIndex::inboundReferences(
    const SctDocumentReferenceTarget& target) const {
    std::vector<SctDocumentReferenceRecord> result;
    for (const auto& reference : references_) if (reference.target == target) result.push_back(reference);
    return result;
}

} // namespace spice::sct
