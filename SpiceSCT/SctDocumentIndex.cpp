#include "SctDocumentIndex.h"

namespace spice::sct {
namespace {

void collectReference(const SctDocumentInstruction& instruction, const SctDocumentParameter& parameter,
    std::optional<std::uint32_t> groupOrdinal, std::vector<SctDocumentReferenceRecord>& references) {
    if (const auto* target = std::get_if<SctInstructionReference>(&parameter.value)) {
        references.push_back({instruction.id, {parameter.schemaIndex, groupOrdinal}, target->target});
    } else if (const auto* target = std::get_if<SctFooterEntryReference>(&parameter.value)) {
        references.push_back({instruction.id, {parameter.schemaIndex, groupOrdinal}, target->target});
    }
}

} // namespace

SctDocumentIndex SctDocumentIndex::build(const SctDocument& document) {
    SctDocumentIndex result;
    for (const auto& section : document.sections) {
        result.sections_.emplace(section.id.value(), &section);
        if (const auto* script = std::get_if<SctScriptSectionContent>(&section.content)) {
            for (const auto& instruction : script->instructions) {
                result.instructions_.emplace(instruction.id.value(), &instruction);
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
    for (const auto& string : document.strings) result.strings_.emplace(string.id.value(), &string);
    for (const auto& footer : document.footerEntries) result.footerEntries_.emplace(footer.id.value(), &footer);
    for (const auto& attachment : document.opaqueAttachments) {
        result.attachments_.emplace(attachment.id.value(), &attachment);
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
