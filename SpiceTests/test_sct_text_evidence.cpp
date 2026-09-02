#include "../SpiceSCT/SpiceSCT.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace {
using namespace spice::sct;

SctParseResult parsedTextSources(std::vector<std::vector<std::uint8_t>> indexedRecords,
    std::vector<std::uint8_t> footerRecord = {},
    SctFooterEntryKind footerKind = SctFooterEntryKind::String) {
    SctParseResult parsed;
    parsed.parseOk = true;
    parsed.file.detectedEndian = "big";
    const auto dataStart = static_cast<std::uint32_t>(12u + 20u * indexedRecords.size());
    std::uint32_t dataSize = 0u;
    for (const auto& indexedRecord : indexedRecords) {
        const auto indexedSize = 8u + indexedRecord.size();
        dataSize += static_cast<std::uint32_t>((indexedSize + 3u) & ~std::size_t{3u});
    }
    parsed.file.originalPayloadBytes.resize(dataStart + dataSize + footerRecord.size(), 0u);

    std::uint32_t sectionDataOffset = 0u;
    for (std::size_t ordinal = 0; ordinal < indexedRecords.size(); ++ordinal) {
        const auto& indexedRecord = indexedRecords[ordinal];
        const auto indexedSize = 8u + indexedRecord.size();
        const auto alignedIndexedSize = static_cast<std::uint32_t>(
            (indexedSize + 3u) & ~std::size_t{3u});
        const auto sectionStart = dataStart + sectionDataOffset;
        std::copy(indexedRecord.begin(), indexedRecord.end(),
            parsed.file.originalPayloadBytes.begin() + sectionStart + 8u);

        SctSection section;
        section.id.name = "M" + std::to_string(ordinal);
        section.startOffset = sectionStart;
        section.endOffset = sectionStart + alignedIndexedSize;
        section.kind = SctSectionKind::String;
        SctStringEntry entry;
        entry.hasPreamble = true;
        entry.textStartOffset = 8u;
        entry.preambleEndOffset = 8u;
        entry.preambleWords = {9u, 0x1du};
        const auto terminator = std::find(indexedRecord.begin(), indexedRecord.end(), 0u);
        if (terminator != indexedRecord.end()) {
            entry.rawTextBytes.assign(indexedRecord.begin(), terminator + 1);
        } else {
            entry.rawTextBytes = indexedRecord;
        }
        section.stringEntry = std::move(entry);
        parsed.file.sections.push_back(std::move(section));
        sectionDataOffset += alignedIndexedSize;
    }

    std::copy(footerRecord.begin(), footerRecord.end(),
        parsed.file.originalPayloadBytes.begin() + dataStart + dataSize);

    if (!footerRecord.empty()) {
        SctFooter footer;
        footer.present = true;
        footer.payloadStartOffset = dataSize;
        footer.payloadEndOffset = dataSize + static_cast<std::uint32_t>(footerRecord.size());
        SctFooterEntry footerEntry;
        footerEntry.id = "FOOTER_0";
        footerEntry.kind = footerKind;
        footerEntry.payloadOffset = dataSize;
        footerEntry.rawBytes = std::move(footerRecord);
        footer.entries.push_back(std::move(footerEntry));
        parsed.file.footer = std::move(footer);
    }
    return parsed;
}

SctParseResult parsedTextSource(std::vector<std::uint8_t> indexedRecord,
    std::vector<std::uint8_t> footerRecord = {},
    SctFooterEntryKind footerKind = SctFooterEntryKind::String) {
    return parsedTextSources({std::move(indexedRecord)}, std::move(footerRecord), footerKind);
}

const SctTextInterpretation& interpretationFor(
    const SctOpaqueTextInspectionResult& result, SctKnownTextConvention convention) {
    const auto found = std::find_if(result.interpretations.begin(), result.interpretations.end(),
        [&](const auto& item) { return item.knownConvention == convention; });
    EXPECT_NE(found, result.interpretations.end());
    return *found;
}

} // namespace

TEST(SctTextEvidence, KnownConventionsNameEvidenceBackedCombinationsWithoutRestrictingAxes) {
    const auto conventions = sctKnownTextConventions();
    ASSERT_EQ(conventions.size(), 3u);
    EXPECT_EQ(conventions[0].stableName, "windows-1252-byte-7f");
    EXPECT_EQ(conventions[1].stableName, "shift-jis-byte-7f");
    EXPECT_EQ(conventions[2].stableName, "shift-jis-8140");
    EXPECT_EQ(findSctKnownTextConvention(kSctWindows1252Byte7FEncoding),
        SctKnownTextConvention::Windows1252Byte7F);
    EXPECT_EQ(sctTextEncodingFor(SctKnownTextConvention::ShiftJis8140),
        kSctShiftJis8140Encoding);

    constexpr SctTextEncoding mechanicallyAvailableButUnnamed{
        SctCharacterEncoding::Windows1252, SctMessageSpaceEncoding::ShiftJis8140};
    EXPECT_FALSE(findSctKnownTextConvention(mechanicallyAvailableButUnnamed).has_value());
    const SctTextValue message = SctMessage{std::nullopt,
        SctFormattedText{{SctTextChunk{"caf\xc3\xa9 au lait"}}}};
    const auto encoded = encodeSctTextRecord(message, SctTextKind::SctString,
        SctTextStorage::Footer, mechanicallyAvailableButUnnamed);
    EXPECT_TRUE(encoded.bytes.has_value()) << encoded.error;
}

TEST(SctTextEvidence, InspectionIsReadOnlyUnrankedAndReportsExactInvalidRanges) {
    const SctOpaqueText opaque{{'A', '\\', 'p', '(', '1', ',', 'x', ')', 'B', 0u}};
    const auto inspected = SctTextInspectionService::inspectKnownConventions(
        opaque, SctTextKind::SctString, SctTextStorage::Footer);
    EXPECT_EQ(inspected.bytes, opaque.bytes);
    ASSERT_EQ(inspected.interpretations.size(), 3u);
    EXPECT_FALSE(inspected.ambiguous);

    for (const auto& interpretation : inspected.interpretations) {
        EXPECT_FALSE(interpretation.complete);
        EXPECT_FALSE(interpretation.semanticValue.has_value());
        const auto malformed = std::find_if(interpretation.issues.begin(), interpretation.issues.end(),
            [](const auto& issue) {
                return issue.code == SctTextInspectionIssueCode::MalformedCommand;
            });
        ASSERT_NE(malformed, interpretation.issues.end());
        EXPECT_EQ(malformed->source, (SctTextByteRange{1u, 7u}));
        EXPECT_TRUE(std::any_of(interpretation.spans.begin(), interpretation.spans.end(),
            [](const auto& span) {
                return span.kind == SctTextInspectionSpanKind::Text
                    && span.source.offset == 8u && span.utf8 == "B";
            }));
    }
}

TEST(SctTextEvidence, InspectionKeepsCompleteAmbiguousInterpretationsSeparate) {
    const std::vector<std::uint8_t> ascii{'H', 'e', 'l', 'l', 'o', 0u};
    const auto inspected = SctTextInspectionService::inspectKnownConventions(
        ascii, SctTextKind::SctString, SctTextStorage::Footer);
    EXPECT_TRUE(inspected.ambiguous);
    ASSERT_EQ(inspected.interpretations.size(), 3u);
    for (const auto& interpretation : inspected.interpretations) {
        EXPECT_TRUE(interpretation.complete);
        EXPECT_TRUE(interpretation.semanticValue.has_value());
        EXPECT_TRUE(interpretation.issues.empty());
    }

    const std::vector<std::uint8_t> invalidCp1252{0x81u, 0x40u, 0u};
    const auto differentiated = SctTextInspectionService::inspectKnownConventions(
        invalidCp1252, SctTextKind::PlainString, SctTextStorage::Footer);
    const auto& western = interpretationFor(
        differentiated, SctKnownTextConvention::Windows1252Byte7F);
    ASSERT_FALSE(western.complete);
    ASSERT_FALSE(western.issues.empty());
    EXPECT_EQ(western.issues.front().source, (SctTextByteRange{0u, 1u}));
    EXPECT_TRUE(interpretationFor(
        differentiated, SctKnownTextConvention::ShiftJisByte7F).complete);
    EXPECT_TRUE(interpretationFor(
        differentiated, SctKnownTextConvention::ShiftJis8140).complete);
}

TEST(SctTextEvidence, SourceDetectionSeparatesIndexedAndFooterEvidence) {
    const auto parsed = parsedTextSource({'H', 'i', 0u}, {0x81u, 0x40u, 0u});
    const auto assessment = SctSourceTextDetector::assess(parsed);
    EXPECT_TRUE(assessment.sourceUsable);
    ASSERT_EQ(assessment.records.size(), 2u);
    EXPECT_TRUE(assessment.records[0].ambiguous);
    EXPECT_EQ(assessment.records[0].viableConventions.size(), 3u);
    EXPECT_EQ(assessment.records[0].location.storage, SctTextStorage::IndexedSection);
    EXPECT_EQ(assessment.records[0].location.decodedPayloadOffset, 40u);
    EXPECT_TRUE(assessment.records[1].ambiguous);
    EXPECT_EQ(assessment.records[1].viableConventions,
        (std::vector<SctKnownTextConvention>{SctKnownTextConvention::ShiftJisByte7F,
            SctKnownTextConvention::ShiftJis8140}));
    EXPECT_EQ(assessment.records[1].location.storage, SctTextStorage::Footer);
    EXPECT_EQ(assessment.indexedSctStrings.recordOrdinals, (std::vector<std::size_t>{0u}));
    EXPECT_EQ(assessment.footerPlainStrings.recordOrdinals, (std::vector<std::size_t>{1u}));
    EXPECT_TRUE(assessment.footerSctStrings.recordOrdinals.empty());
    EXPECT_EQ(assessment.indexedSctStrings.viableConventions,
        assessment.records[0].viableConventions);
    EXPECT_EQ(assessment.footerPlainStrings.viableConventions,
        assessment.records[1].viableConventions);
    EXPECT_EQ(assessment.recommendation.status,
        SctSourceTextRecommendationStatus::InsufficientEvidence);
    EXPECT_FALSE(assessment.recommendation.convention.has_value());
}

TEST(SctTextEvidence, IndexedRecommendationIsNotChangedByConflictingFooterEvidence) {
    const auto parsed = parsedTextSource({'c', 'a', 'f', 0xe9u, 0u},
        {0x81u, 0x40u, 0u}, SctFooterEntryKind::SctString);
    const auto assessment = SctSourceTextDetector::assess(parsed);
    ASSERT_TRUE(assessment.sourceUsable);
    ASSERT_EQ(assessment.records.size(), 2u);
    ASSERT_EQ(assessment.recommendation.status,
        SctSourceTextRecommendationStatus::Unique);
    EXPECT_EQ(assessment.recommendation.convention,
        SctKnownTextConvention::Windows1252Byte7F);
    EXPECT_EQ(assessment.recommendation.basis,
        SctSourceTextRecommendationBasis::IndexedSctStrings);
    EXPECT_EQ(assessment.footerSctStrings.viableConventions,
        (std::vector<SctKnownTextConvention>{SctKnownTextConvention::ShiftJisByte7F,
            SctKnownTextConvention::ShiftJis8140}));
}

TEST(SctTextEvidence, RecommendationDistinguishesInsufficientAmbiguousAndConflictingEvidence) {
    const auto noIndexed = SctSourceTextDetector::assess(parsedTextSources({}, {'A', 0u}));
    EXPECT_EQ(noIndexed.recommendation.status,
        SctSourceTextRecommendationStatus::InsufficientEvidence);

    const auto asciiOnly = SctSourceTextDetector::assess(
        parsedTextSource({'A', 'S', 'C', 'I', 'I', 0u}));
    EXPECT_EQ(asciiOnly.recommendation.status,
        SctSourceTextRecommendationStatus::InsufficientEvidence);

    const auto ambiguous = SctSourceTextDetector::assess(
        parsedTextSource({0x81u, 0x40u, 0u}));
    EXPECT_EQ(ambiguous.recommendation.status,
        SctSourceTextRecommendationStatus::Ambiguous);
    EXPECT_FALSE(ambiguous.recommendation.convention.has_value());

    const auto conflicting = SctSourceTextDetector::assess(parsedTextSources({
        {'c', 'a', 'f', 0xe9u, 0u},
        {0x81u, 0x40u, 0u},
    }));
    EXPECT_EQ(conflicting.recommendation.status,
        SctSourceTextRecommendationStatus::Conflicting);
    EXPECT_FALSE(conflicting.recommendation.convention.has_value());

    const auto opaqueOnly = SctSourceTextDetector::assess(
        parsedTextSource({'f', 'i', 'r', 's', 't', 0u, 's', 'e', 'c', 'o', 'n', 'd', 0u}));
    EXPECT_EQ(opaqueOnly.recommendation.status,
        SctSourceTextRecommendationStatus::InsufficientEvidence);
}

TEST(SctTextEvidence, SourceDetectionUsesWholePhysicalIndexedRecordAndRejectsFailedParse) {
    const auto parsed = parsedTextSource({'f', 'i', 'r', 's', 't', 0u,
        's', 'e', 'c', 'o', 'n', 'd', 0u});
    const auto assessment = SctSourceTextDetector::assess(parsed);
    ASSERT_TRUE(assessment.sourceUsable);
    ASSERT_EQ(assessment.records.size(), 1u);
    EXPECT_EQ(assessment.records.front().bytes.size(), 16u);
    EXPECT_TRUE(assessment.records.front().viableConventions.empty());
    EXPECT_TRUE(assessment.indexedSctStrings.viableConventions.empty());
    EXPECT_EQ(assessment.recommendation.status,
        SctSourceTextRecommendationStatus::InsufficientEvidence);

    SctParseResult failed;
    const auto failedAssessment = SctSourceTextDetector::assess(failed);
    EXPECT_FALSE(failedAssessment.sourceUsable);
    EXPECT_TRUE(failedAssessment.records.empty());
    EXPECT_FALSE(failedAssessment.issues.empty());
}
