#pragma once

#include "SctModel.h"
#include "SctTextCodec.h"

#include <optional>
#include <string>
#include <vector>

namespace spice::sct {

struct SctSourceTextRecordLocation {
    SctTextKind kind = SctTextKind::SctString;
    SctTextStorage storage = SctTextStorage::Footer;
    std::uint32_t decodedPayloadOffset = 0;
    std::optional<std::uint32_t> physicalSectionIndex;
    std::optional<std::uint32_t> footerEntryIndex;
    auto operator<=>(const SctSourceTextRecordLocation&) const = default;
};

struct SctSourceTextAssessmentIssue {
    std::optional<SctSourceTextRecordLocation> record;
    std::string message;
};

struct SctSourceTextRecordAssessment {
    SctSourceTextRecordLocation location;
    std::vector<std::uint8_t> bytes;
    std::vector<SctTextInterpretation> interpretations;
    std::vector<SctKnownTextConvention> viableConventions;
    bool ambiguous = false;
};

struct SctSourceTextAssessment {
    bool sourceUsable = false;
    std::vector<SctSourceTextRecordAssessment> records;
    std::vector<SctKnownTextConvention> viableConventions;
    bool ambiguous = false;
    std::vector<SctSourceTextAssessmentIssue> issues;
};

class SctSourceTextDetector {
public:
    // Returns unranked mechanical evidence. It never selects an encoding and
    // never uses platform, region, or release hints.
    [[nodiscard]] static SctSourceTextAssessment assess(const SctParseResult& parsed);
};

} // namespace spice::sct
