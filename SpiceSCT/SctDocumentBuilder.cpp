#include "SctDocumentBuilder.h"

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>

namespace spice::sct {
namespace {

template <typename Id>
bool observeId(Id id, std::string_view label, std::unordered_set<std::uint64_t>& seen,
    std::uint64_t& maximum, std::vector<SctDocumentDiagnostic>& diagnostics) {
    if (!id) {
        diagnostics.push_back({SctDiagnosticSeverity::Error, SctDiagnosticCode::InvalidId,
            std::string(label) + " ID is zero.", SctDiagnosticLocation{SctDocumentEntityId{id}}});
        return false;
    }
    if (!seen.insert(id.value()).second) {
        diagnostics.push_back({SctDiagnosticSeverity::Error, SctDiagnosticCode::DuplicateId,
            std::string(label) + " ID is duplicated.", SctDiagnosticLocation{SctDocumentEntityId{id}}});
        return false;
    }
    if (id.value() == std::numeric_limits<std::uint64_t>::max()) {
        diagnostics.push_back({SctDiagnosticSeverity::Error, SctDiagnosticCode::AllocatorDiscontinuity,
            std::string(label) + " ID leaves no monotonic successor.",
            SctDiagnosticLocation{SctDocumentEntityId{id}}});
        return false;
    }
    maximum = std::max(maximum, id.value());
    return true;
}

} // namespace

SctDocumentBuildResult SctDocumentBuilder::reconstitute(SctDocument document) {
    SctDocumentBuildResult result;
    std::unordered_set<std::uint64_t> sectionIds;
    std::unordered_set<std::uint64_t> instructionIds;
    std::unordered_set<std::uint64_t> stringIds;
    std::unordered_set<std::uint64_t> footerIds;
    std::unordered_set<std::uint64_t> attachmentIds;
    std::uint64_t maxSection = 0;
    std::uint64_t maxInstruction = 0;
    std::uint64_t maxString = 0;
    std::uint64_t maxFooter = 0;
    std::uint64_t maxAttachment = 0;

    for (const auto& section : document.sections) {
        observeId(section.id, "Section", sectionIds, maxSection, result.diagnostics);
        if (const auto* script = std::get_if<SctScriptSectionContent>(&section.content)) {
            for (const auto& instruction : script->instructions) {
                observeId(instruction.id, "Instruction", instructionIds, maxInstruction, result.diagnostics);
            }
        } else if (const auto* string = std::get_if<SctStringSectionContent>(&section.content)) {
            observeId(string->string.id, "String", stringIds, maxString, result.diagnostics);
        }
    }
    for (const auto& footer : document.footerEntries) {
        observeId(footer.id, "Footer entry", footerIds, maxFooter, result.diagnostics);
    }
    for (const auto& attachment : document.opaqueAttachments) {
        observeId(attachment.id, "Opaque attachment", attachmentIds, maxAttachment, result.diagnostics);
    }

    if (!result.diagnostics.empty()) return result;
    document.restoreAllocatorState(maxSection + 1, maxInstruction + 1, maxString + 1,
        maxFooter + 1, maxAttachment + 1);
    result.document = std::move(document);
    return result;
}

} // namespace spice::sct
