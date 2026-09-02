#include "SctTextEvidence.h"

#include <algorithm>
#include <limits>

namespace spice::sct {
namespace {

std::uint32_t narrowOffset(std::uint64_t value) {
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(
        value, std::numeric_limits<std::uint32_t>::max()));
}

bool contains(const std::vector<SctKnownTextConvention>& values,
    SctKnownTextConvention value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

SctSourceTextEvidenceGroupAssessment& groupFor(
    SctSourceTextAssessment& result, const SctSourceTextRecordLocation& location) {
    if (location.storage == SctTextStorage::IndexedSection) {
        return result.indexedSctStrings;
    }
    return location.kind == SctTextKind::SctString
        ? result.footerSctStrings : result.footerPlainStrings;
}

void summarizeGroup(SctSourceTextEvidenceGroupAssessment& group,
    const std::vector<SctSourceTextRecordAssessment>& records) {
    bool initialized = false;
    for (const auto ordinal : group.recordOrdinals) {
        const auto& record = records[ordinal];
        if (record.viableConventions.empty()) continue;
        if (!initialized) {
            group.viableConventions = record.viableConventions;
            initialized = true;
            continue;
        }
        std::erase_if(group.viableConventions, [&](SctKnownTextConvention convention) {
            return !contains(record.viableConventions, convention);
        });
    }
}

void buildRecommendation(SctSourceTextAssessment& result) {
    const auto allConventionCount = sctKnownTextConventions().size();
    bool hasDecodableEvidence = false;
    bool hasDiscriminatingEvidence = false;
    for (const auto ordinal : result.indexedSctStrings.recordOrdinals) {
        const auto& record = result.records[ordinal];
        if (record.viableConventions.empty()) continue;
        hasDecodableEvidence = true;
        if (record.viableConventions.size() < allConventionCount) {
            hasDiscriminatingEvidence = true;
        }
    }

    auto& recommendation = result.recommendation;
    if (!hasDecodableEvidence || !hasDiscriminatingEvidence) {
        recommendation.status = SctSourceTextRecommendationStatus::InsufficientEvidence;
        return;
    }
    if (result.indexedSctStrings.viableConventions.empty()) {
        recommendation.status = SctSourceTextRecommendationStatus::Conflicting;
        return;
    }
    if (result.indexedSctStrings.viableConventions.size() == 1u) {
        recommendation.status = SctSourceTextRecommendationStatus::Unique;
        recommendation.convention = result.indexedSctStrings.viableConventions.front();
        return;
    }
    recommendation.status = SctSourceTextRecommendationStatus::Ambiguous;
}

void assessRecord(SctSourceTextRecordAssessment& record) {
    for (const auto& descriptor : sctKnownTextConventions()) {
        record.interpretations.push_back(SctTextInspectionService::interpret(
            record.bytes, record.location.kind, record.location.storage, descriptor.encoding));
        if (record.interpretations.back().complete) {
            record.viableConventions.push_back(descriptor.convention);
        }
    }
    record.ambiguous = record.viableConventions.size() > 1u;
}

} // namespace

SctSourceTextAssessment SctSourceTextDetector::assess(const SctParseResult& parsed) {
    SctSourceTextAssessment result;
    if (!parsed.parseOk) {
        result.issues.push_back({std::nullopt,
            "Source-text assessment requires a successful structural parse."});
        return result;
    }

    const auto& payload = parsed.file.originalPayloadBytes;
    const std::uint64_t dataStart64 = 12ull + 20ull * parsed.file.sections.size();
    if (dataStart64 > payload.size()) {
        result.issues.push_back({std::nullopt,
            "Decoded SCT payload is shorter than its physical index table."});
        return result;
    }
    const auto dataStart = static_cast<std::uint32_t>(dataStart64);
    result.sourceUsable = true;

    for (std::size_t sectionIndex = 0; sectionIndex < parsed.file.sections.size(); ++sectionIndex) {
        const auto& section = parsed.file.sections[sectionIndex];
        if (!section.stringEntry) continue;
        SctSourceTextRecordAssessment record;
        record.location.kind = SctTextKind::SctString;
        record.location.storage = SctTextStorage::IndexedSection;
        record.location.physicalSectionIndex = static_cast<std::uint32_t>(sectionIndex);

        if (section.startOffset > section.endOffset || section.endOffset > payload.size()) {
            result.sourceUsable = false;
            result.issues.push_back({record.location,
                "Indexed text section has contradictory decoded-payload bounds."});
            continue;
        }
        const auto sectionSize = static_cast<std::size_t>(section.endOffset - section.startOffset);
        const auto local = static_cast<std::size_t>(section.stringEntry->textStartOffset);
        record.location.decodedPayloadOffset = narrowOffset(
            static_cast<std::uint64_t>(section.startOffset) + local);
        if (local > sectionSize) {
            result.sourceUsable = false;
            result.issues.push_back({record.location,
                "Indexed text start lies outside its physical section."});
            continue;
        }

        std::vector<std::uint8_t> physicalText(
            payload.begin() + section.startOffset + local,
            payload.begin() + section.endOffset);
        std::size_t recordSize = physicalText.size();
        const auto& parsedRecord = section.stringEntry->rawTextBytes;
        if (!parsedRecord.empty() && parsedRecord.size() <= physicalText.size()) {
            const auto suffix = physicalText.size() - parsedRecord.size();
            const bool derivedAlignment = suffix <= 3u
                && std::all_of(physicalText.begin() + parsedRecord.size(), physicalText.end(),
                    [](std::uint8_t byte) { return byte == 0u; });
            if (derivedAlignment) recordSize = parsedRecord.size();
        } else if (physicalText.empty()) {
            recordSize = 0u;
        }
        record.bytes.assign(physicalText.begin(), physicalText.begin() + recordSize);
        assessRecord(record);
        result.indexedSctStrings.recordOrdinals.push_back(result.records.size());
        result.records.push_back(std::move(record));
    }

    if (parsed.file.footer) {
        const auto& footer = *parsed.file.footer;
        const auto dataSize = payload.size() - dataStart64;
        if (footer.payloadStartOffset > footer.payloadEndOffset
            || footer.payloadEndOffset > dataSize) {
            result.sourceUsable = false;
            result.issues.push_back({std::nullopt,
                "Parsed footer has contradictory decoded-payload bounds."});
        } else {
            for (std::size_t entryIndex = 0; entryIndex < footer.entries.size(); ++entryIndex) {
                const auto& entry = footer.entries[entryIndex];
                SctSourceTextRecordAssessment record;
                record.location.kind = entry.kind == SctFooterEntryKind::SctString
                    ? SctTextKind::SctString : SctTextKind::PlainString;
                record.location.storage = SctTextStorage::Footer;
                record.location.footerEntryIndex = static_cast<std::uint32_t>(entryIndex);
                record.location.decodedPayloadOffset = narrowOffset(
                    static_cast<std::uint64_t>(dataStart) + entry.payloadOffset);
                const auto entryEnd = static_cast<std::uint64_t>(entry.payloadOffset)
                    + entry.rawBytes.size();
                if (entry.payloadOffset < footer.payloadStartOffset
                    || entryEnd > footer.payloadEndOffset) {
                    result.sourceUsable = false;
                    result.issues.push_back({record.location,
                        "Footer text record lies outside the parsed footer bounds."});
                    continue;
                }
                record.bytes = entry.rawBytes;
                assessRecord(record);
                groupFor(result, record.location).recordOrdinals.push_back(result.records.size());
                result.records.push_back(std::move(record));
            }
        }
    }

    summarizeGroup(result.indexedSctStrings, result.records);
    summarizeGroup(result.footerSctStrings, result.records);
    summarizeGroup(result.footerPlainStrings, result.records);
    buildRecommendation(result);
    return result;
}

} // namespace spice::sct
