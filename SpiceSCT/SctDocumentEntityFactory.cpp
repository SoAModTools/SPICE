#include "SctDocumentEntityFactory.h"

#include "SctDocumentValidator.h"

#include <algorithm>

namespace spice::sct {
namespace {

template <typename Result>
bool invalidName(Result& result, const std::string& name) {
    if (name.size() <= 16u && name.find('\0') == std::string::npos) return false;
    result.diagnostics.push_back({SctDiagnosticSeverity::Error, SctDiagnosticCode::InvalidName,
        "Section names must be zero-free byte strings no longer than 16 bytes."});
    return true;
}

template <typename Result>
void appendValidation(Result& result, SctDocumentValidationResult validation) {
    for (auto& diagnostic : validation.diagnostics) {
        diagnostic.primaryLocation.reset();
        result.diagnostics.push_back(std::move(diagnostic));
    }
}

template <typename Result>
bool hasErrors(const Result& result) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity == SctDiagnosticSeverity::Error;
    });
}

SctSectionFactoryResult createSimpleSection(
    SctDocument& document, std::string nameBytes, SctDocumentSectionContent content) {
    SctSectionFactoryResult result;
    if (invalidName(result, nameBytes)) return result;
    result.section = SctDocumentSection{document.allocateSectionId(), std::move(nameBytes), std::move(content)};
    return result;
}

} // namespace

SctSectionFactoryResult SctDocumentEntityFactory::createScriptSection(
    SctDocument& document, std::string nameBytes) {
    return createSimpleSection(document, std::move(nameBytes), SctScriptSectionContent{});
}

SctSectionFactoryResult SctDocumentEntityFactory::createStringGroupMarkerSection(
    SctDocument& document, std::string nameBytes, std::vector<std::uint32_t> preambleWords) {
    SctSectionFactoryResult result;
    if (invalidName(result, nameBytes)) return result;
    SctDocument validationDocument;
    const auto sectionId = validationDocument.allocateSectionId();
    validationDocument.sections.push_back({sectionId, nameBytes,
        SctStringGroupMarkerSectionContent{preambleWords}});
    appendValidation(result, SctDocumentValidator::validateDocument(validationDocument));
    if (hasErrors(result)) return result;
    result.section = SctDocumentSection{document.allocateSectionId(), std::move(nameBytes),
        SctStringGroupMarkerSectionContent{std::move(preambleWords)}};
    return result;
}

SctSectionFactoryResult SctDocumentEntityFactory::createIndexedStringSection(
    SctDocument& document, std::string nameBytes, SctTextValue value,
    std::vector<std::uint32_t> preambleWords) {
    SctSectionFactoryResult result;
    if (invalidName(result, nameBytes)) return result;
    SctDocument validationDocument;
    const auto sectionId = validationDocument.allocateSectionId();
    const auto stringId = validationDocument.allocateStringId();
    validationDocument.sections.push_back({sectionId, nameBytes,
        SctStringSectionContent{SctDocumentString{stringId, value, SctTextKind::SctString}, preambleWords}});
    appendValidation(result, SctDocumentValidator::validateDocument(validationDocument));
    if (hasErrors(result)) return result;
    const auto realSectionId = document.allocateSectionId();
    const auto realStringId = document.allocateStringId();
    result.section = SctDocumentSection{realSectionId, std::move(nameBytes),
        SctStringSectionContent{SctDocumentString{realStringId, std::move(value), SctTextKind::SctString},
            std::move(preambleWords)}};
    return result;
}

SctSectionFactoryResult SctDocumentEntityFactory::createOpaqueSection(
    SctDocument& document, std::string nameBytes,
    SctOpaqueSectionAttachmentRequest attachment) {
    SctSectionFactoryResult result;
    if (invalidName(result, nameBytes)) return result;
    SctDocument validationDocument;
    const auto sectionId = validationDocument.allocateSectionId();
    const auto attachmentId = validationDocument.allocateOpaqueAttachmentId();
    validationDocument.sections.push_back({sectionId, nameBytes, SctOpaqueSectionContent{}});
    validationDocument.opaqueAttachments.push_back({attachmentId, attachment.bytes, sectionId,
        attachment.placement, attachment.fixedOffset, attachment.alignment,
        attachment.relocation, attachment.reason});
    appendValidation(result, SctDocumentValidator::validateDocument(validationDocument));
    if (hasErrors(result)) return result;
    const auto realSectionId = document.allocateSectionId();
    const auto realAttachmentId = document.allocateOpaqueAttachmentId();
    result.section = SctDocumentSection{realSectionId, std::move(nameBytes), SctOpaqueSectionContent{}};
    result.attachment = SctOpaqueAttachment{realAttachmentId, std::move(attachment.bytes), realSectionId,
        attachment.placement, attachment.fixedOffset, attachment.alignment,
        attachment.relocation, attachment.reason};
    return result;
}

SctSupplementaryTextFactoryResult SctDocumentEntityFactory::createSupplementaryText(
    SctDocument& document, SctTextKind kind, SctTextValue value) {
    SctSupplementaryTextFactoryResult result;
    SctDocument validationDocument;
    const auto id = validationDocument.allocateSupplementaryTextId();
    validationDocument.supplementaryText.push_back({id, kind, value});
    appendValidation(result, SctDocumentValidator::validateDocument(validationDocument));
    if (hasErrors(result)) return result;
    result.text = SctDocumentSupplementaryText{
        document.allocateSupplementaryTextId(), kind, std::move(value)};
    return result;
}

} // namespace spice::sct
