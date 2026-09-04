#pragma once

#include "SctDocument.h"

#include <optional>
#include <string>
#include <vector>

namespace spice::sct {

struct SctSectionFactoryResult {
    std::optional<SctDocumentSection> section;
    std::optional<SctOpaqueAttachment> attachment;
    std::vector<SctDocumentDiagnostic> diagnostics;
};

struct SctSupplementaryTextFactoryResult {
    std::optional<SctDocumentSupplementaryText> text;
    std::vector<SctDocumentDiagnostic> diagnostics;
};

struct SctOpaqueSectionAttachmentRequest {
    std::vector<std::uint8_t> bytes;
    SctOpaquePlacement placement = SctOpaquePlacement::After;
    std::optional<std::uint32_t> fixedOffset;
    std::uint32_t alignment = 1;
    SctOpaqueRelocationSupport relocation = SctOpaqueRelocationSupport::Relocatable;
    SctOpaqueReason reason = SctOpaqueReason::UnknownEncoding;
};

class SctDocumentEntityFactory {
public:
    [[nodiscard]] static SctSectionFactoryResult createScriptSection(
        SctDocument& document, std::string nameBytes);
    [[nodiscard]] static SctSectionFactoryResult createStringGroupMarkerSection(
        SctDocument& document, std::string nameBytes,
        std::vector<std::uint32_t> preambleWords = {9u, 0x0000001du});
    [[nodiscard]] static SctSectionFactoryResult createIndexedStringSection(
        SctDocument& document, std::string nameBytes, SctTextValue value,
        std::vector<std::uint32_t> preambleWords = {9u, 0x0000001du});
    [[nodiscard]] static SctSectionFactoryResult createOpaqueSection(
        SctDocument& document, std::string nameBytes,
        SctOpaqueSectionAttachmentRequest attachment);
    [[nodiscard]] static SctSupplementaryTextFactoryResult createSupplementaryText(
        SctDocument& document, SctTextKind kind, SctTextValue value);
};

} // namespace spice::sct
