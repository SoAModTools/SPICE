#pragma once

#include "SctModel.h"
#include "SctTextCodec.h"

#include <cstddef>
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

enum class SctSourceTextEvidenceGroupKind {
    IndexedSctString,
    FooterSctString,
    FooterPlainString,
};

struct SctSourceTextEvidenceGroupAssessment {
    SctSourceTextEvidenceGroupKind kind = SctSourceTextEvidenceGroupKind::IndexedSctString;
    std::vector<std::size_t> recordOrdinals;
    std::vector<SctKnownTextConvention> viableConventions;
};

enum class SctSourceTextRecommendationStatus {
    Unique,
    Ambiguous,
    Conflicting,
    InsufficientEvidence,
};

enum class SctSourceTextRecommendationBasis {
    IndexedSctStrings,
};

struct SctSourceTextRecommendation {
    SctSourceTextRecommendationStatus status =
        SctSourceTextRecommendationStatus::InsufficientEvidence;
    SctSourceTextRecommendationBasis basis =
        SctSourceTextRecommendationBasis::IndexedSctStrings;
    std::optional<SctKnownTextConvention> convention;
};

struct SctSourceTextAssessment {
    bool sourceUsable = false;
    std::vector<SctSourceTextRecordAssessment> records;
    SctSourceTextEvidenceGroupAssessment indexedSctStrings{
        SctSourceTextEvidenceGroupKind::IndexedSctString};
    SctSourceTextEvidenceGroupAssessment footerSctStrings{
        SctSourceTextEvidenceGroupKind::FooterSctString};
    SctSourceTextEvidenceGroupAssessment footerPlainStrings{
        SctSourceTextEvidenceGroupKind::FooterPlainString};
    SctSourceTextRecommendation recommendation;
    std::vector<SctSourceTextAssessmentIssue> issues;
};

class SctSourceTextDetector {
public:
    // Returns mechanical per-record evidence plus an indexed-SctString-only
    // recommendation. It never uses platform, region, or release hints.
    [[nodiscard]] static SctSourceTextAssessment assess(const SctParseResult& parsed);
};

} // namespace spice::sct
