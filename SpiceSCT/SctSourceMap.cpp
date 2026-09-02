#include "SctSourceMap.h"

#include <algorithm>
#include <limits>
#include <map>
#include <type_traits>

namespace spice::sct {
namespace {

const SctDocumentEntityId* targetEntity(const std::optional<SctImportedSourceTarget>& target) {
    return target ? std::get_if<SctDocumentEntityId>(&*target) : nullptr;
}

bool sameEntity(const std::optional<SctImportedSourceTarget>& candidate,
    const SctDocumentEntityId& entity) {
    const auto* value = targetEntity(candidate);
    return value != nullptr && *value == entity;
}

bool isSemanticAdjacencyEntity(const SctDocumentEntityId& entity) {
    return std::holds_alternative<SctInstructionId>(entity)
        || std::holds_alternative<SctStringId>(entity)
        || std::holds_alternative<SctFooterEntryId>(entity);
}

bool contains(SctImportedByteSpan outer, SctImportedByteSpan inner) {
    return outer.offset <= inner.offset && outer.endOffset() >= inner.endOffset();
}

bool overlaps(SctImportedByteSpan left, SctImportedByteSpan right) {
    return static_cast<std::uint64_t>(left.offset) < right.endOffset()
        && static_cast<std::uint64_t>(right.offset) < left.endOffset();
}

bool validEntity(const SctDocumentEntityId& entity) {
    return std::visit([](const auto& id) {
        using T = std::decay_t<decltype(id)>;
        if constexpr (std::is_same_v<T, std::monostate>) return false;
        else return id.value() != 0u;
    }, entity);
}

bool validTarget(const SctImportedSourceTarget& target) {
    return std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, SctDocumentEntityId>) {
            return validEntity(value);
        } else if constexpr (std::is_same_v<T, SctParameterSite>
            || std::is_same_v<T, SctExpressionSite>) {
            return value.instruction.value() != 0u;
        } else {
            return std::visit([](const auto& id) { return id.value() != 0u; }, value.text);
        }
    }, target);
}

} // namespace

SctImportedSourceMap::SctImportedSourceMap(std::uint32_t decodedPayloadSize,
    std::vector<SctSourceSpanRecord> records)
    : decodedPayloadSize_(decodedPayloadSize), records_(std::move(records)) {
    std::stable_sort(records_.begin(), records_.end(), [](const auto& left, const auto& right) {
        if (left.span.offset != right.span.offset) return left.span.offset < right.span.offset;
        if (left.layer != right.layer) return left.layer == SctSourceSpanLayer::Envelope;
        return left.span.size > right.span.size;
    });
}

SctImportedSourceMap::BuildResult SctImportedSourceMap::build(
    std::uint32_t decodedPayloadSize, std::vector<SctSourceSpanRecord> records) {
    BuildResult result;
    std::stable_sort(records.begin(), records.end(), [](const auto& left, const auto& right) {
        if (left.span.offset != right.span.offset) return left.span.offset < right.span.offset;
        if (left.layer != right.layer) return left.layer == SctSourceSpanLayer::Envelope;
        return left.span.size > right.span.size;
    });
    std::uint64_t expectedLeaf = 0;
    std::map<SctDocumentEntityId, std::size_t> entityPrimaryCounts;
    std::map<SctDocumentEntityId, SctImportedByteSpan> primarySpans;
    for (const auto& record : records) {
        if (record.span.endOffset() > decodedPayloadSize) {
            result.issues.push_back({SctSourceMapIssueCode::OutOfBounds, record.span,
                "Source span exceeds the decoded SCT payload."});
        }
        if (record.target && !validTarget(*record.target)) {
            result.issues.push_back({SctSourceMapIssueCode::InvalidTarget, record.span,
                "Source-map targets must use nonzero imported-revision entity IDs."});
        }
        if (const auto* entity = targetEntity(record.target); entity != nullptr && validEntity(*entity)) {
            entityPrimaryCounts.try_emplace(*entity, 0u);
            if (record.primaryEntityLocation) {
                auto& count = entityPrimaryCounts[*entity];
                if (count++ != 0u) {
                    result.issues.push_back({SctSourceMapIssueCode::DuplicatePrimaryLocation,
                        record.span, "Entity has more than one primary source location."});
                } else primarySpans.emplace(*entity, record.span);
            }
        } else if (record.primaryEntityLocation) {
            result.issues.push_back({SctSourceMapIssueCode::DuplicatePrimaryLocation,
                record.span, "A primary source location must target a document entity."});
        }
        if (record.containingSection && record.containingSection->value() == 0u) {
            result.issues.push_back({SctSourceMapIssueCode::InvalidTarget, record.span,
                "A containing section must use a nonzero imported-revision ID."});
        }
        if (record.sectionRelativeOffset && !record.containingSection) {
            result.issues.push_back({SctSourceMapIssueCode::InvalidContainingSection,
                record.span, "A section-relative offset requires a containing section."});
        }
        if (record.layer != SctSourceSpanLayer::Leaf) continue;
        if (record.span.size == 0u) {
            result.issues.push_back({SctSourceMapIssueCode::ZeroLengthLeaf, record.span,
                "Zero-length source entities must use envelopes, not coverage leaves."});
            continue;
        }
        if (record.span.offset > expectedLeaf) {
            result.issues.push_back({SctSourceMapIssueCode::LeafGap, record.span,
                "Leaf coverage has a gap."});
        } else if (record.span.offset < expectedLeaf) {
            result.issues.push_back({SctSourceMapIssueCode::LeafOverlap, record.span,
                "Leaf coverage overlaps an earlier leaf."});
        }
        expectedLeaf = std::max(expectedLeaf, record.span.endOffset());
    }
    if (expectedLeaf < decodedPayloadSize) {
        result.issues.push_back({SctSourceMapIssueCode::LeafGap,
            SctImportedByteSpan{static_cast<std::uint32_t>(expectedLeaf),
                decodedPayloadSize - static_cast<std::uint32_t>(expectedLeaf)},
            "Leaf coverage does not reach the end of the decoded payload."});
    }
    std::vector<SctImportedByteSpan> activeEnvelopes;
    for (const auto& record : records) {
        if (record.layer != SctSourceSpanLayer::Envelope || record.span.size == 0u) continue;
        while (!activeEnvelopes.empty()
            && activeEnvelopes.back().endOffset() <= record.span.offset) {
            activeEnvelopes.pop_back();
        }
        if (!activeEnvelopes.empty() && !contains(activeEnvelopes.back(), record.span)) {
            result.issues.push_back({SctSourceMapIssueCode::IllegalEnvelopeOverlap,
                record.span, "Envelope spans may nest but may not partially overlap."});
        } else {
            activeEnvelopes.push_back(record.span);
        }
    }
    for (const auto& [entity, primaryCount] : entityPrimaryCounts) {
        if (primaryCount == 0u) {
            result.issues.push_back({SctSourceMapIssueCode::MissingPrimaryLocation, std::nullopt,
                "Mapped document entity has no primary source location."});
        }
    }
    for (const auto& record : records) {
        if (!record.containingSection || record.region != SctSourceRegion::SectionPayload) continue;
        const SctDocumentEntityId sectionEntity{*record.containingSection};
        const auto primary = primarySpans.find(sectionEntity);
        if (primary == primarySpans.end() || !contains(primary->second, record.span)) {
            result.issues.push_back({SctSourceMapIssueCode::InvalidContainingSection, record.span,
                "Section-payload span is not contained by its section's primary envelope."});
            continue;
        }
        if (record.sectionRelativeOffset
            && record.span.offset - primary->second.offset != *record.sectionRelativeOffset) {
            result.issues.push_back({SctSourceMapIssueCode::InvalidSectionRelativeOffset,
                record.span, "Section-relative offset disagrees with the section envelope."});
        }
    }
    if (result.issues.empty()) {
        result.map = SctImportedSourceMap{decodedPayloadSize, std::move(records)};
    }
    return result;
}

std::vector<SctSourceSpanRecord> SctImportedSourceMap::recordsFor(
    const SctImportedSourceTarget& target) const {
    std::vector<SctSourceSpanRecord> result;
    for (const auto& record : records_) if (record.target == target) result.push_back(record);
    return result;
}

std::vector<SctSourceSpanRecord> SctImportedSourceMap::recordsAt(std::uint32_t offset) const {
    std::vector<SctSourceSpanRecord> result;
    for (const auto& record : records_) {
        if (record.span.size != 0u && offset >= record.span.offset
            && static_cast<std::uint64_t>(offset) < record.span.endOffset()) {
            result.push_back(record);
        }
    }
    return result;
}

std::vector<SctSourceSpanRecord> SctImportedSourceMap::recordsContaining(
    SctImportedByteSpan span) const {
    std::vector<SctSourceSpanRecord> result;
    for (const auto& record : records_) if (contains(record.span, span)) result.push_back(record);
    return result;
}

std::optional<SctSourceEntityLocation> SctImportedSourceMap::location(
    const SctDocumentEntityId& entity) const {
    for (const auto& record : records_) {
        if (record.primaryEntityLocation && sameEntity(record.target, entity)) {
            return SctSourceEntityLocation{entity, record.span, record.containingSection,
                record.sectionRelativeOffset, record.region};
        }
    }
    return std::nullopt;
}

std::vector<SctSourceEntityLocation> SctImportedSourceMap::semanticLocations() const {
    std::vector<SctSourceEntityLocation> result;
    for (const auto& record : records_) {
        const auto* entity = targetEntity(record.target);
        if (!record.primaryEntityLocation || entity == nullptr
            || !isSemanticAdjacencyEntity(*entity)) continue;
        result.push_back({*entity, record.span, record.containingSection,
            record.sectionRelativeOffset, record.region});
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.primarySpan.offset != right.primarySpan.offset) {
            return left.primarySpan.offset < right.primarySpan.offset;
        }
        return left.primarySpan.size < right.primarySpan.size;
    });
    return result;
}

std::optional<SctDocumentEntityId> SctImportedSourceMap::previousSemanticEntity(
    const SctDocumentEntityId& entity) const {
    const auto target = location(entity);
    if (!target) return std::nullopt;
    const auto ordered = semanticLocations();
    std::optional<SctDocumentEntityId> result;
    for (const auto& candidate : ordered) {
        if (candidate.primarySpan.offset >= target->primarySpan.offset) break;
        result = candidate.entity;
    }
    return result;
}

std::optional<SctDocumentEntityId> SctImportedSourceMap::nextSemanticEntity(
    const SctDocumentEntityId& entity) const {
    const auto target = location(entity);
    if (!target) return std::nullopt;
    for (const auto& candidate : semanticLocations()) {
        if (candidate.primarySpan.offset > target->primarySpan.offset) return candidate.entity;
    }
    return std::nullopt;
}

std::vector<SctDocumentEntityId> SctImportedSourceMap::semanticEntitiesBetween(
    const SctDocumentEntityId& first, const SctDocumentEntityId& second) const {
    std::vector<SctDocumentEntityId> result;
    const auto firstLocation = location(first);
    const auto secondLocation = location(second);
    if (!firstLocation || !secondLocation) return result;
    const auto low = std::min(firstLocation->primarySpan.offset, secondLocation->primarySpan.offset);
    const auto high = std::max(firstLocation->primarySpan.offset, secondLocation->primarySpan.offset);
    for (const auto& candidate : semanticLocations()) {
        if (candidate.primarySpan.offset > low && candidate.primarySpan.offset < high) {
            result.push_back(candidate.entity);
        }
    }
    return result;
}

std::optional<SctSourceRelationship> SctImportedSourceMap::relationship(
    const SctDocumentEntityId& first, const SctDocumentEntityId& second) const {
    const auto left = location(first);
    const auto right = location(second);
    if (!left || !right) return std::nullopt;
    if (left->primarySpan == right->primarySpan) return SctSourceRelationship::SameSpan;
    if (contains(left->primarySpan, right->primarySpan)) return SctSourceRelationship::Contains;
    if (contains(right->primarySpan, left->primarySpan)) return SctSourceRelationship::ContainedBy;
    if (overlaps(left->primarySpan, right->primarySpan)) return SctSourceRelationship::Overlaps;
    if (left->primarySpan.endOffset() <= right->primarySpan.offset) return SctSourceRelationship::Before;
    return SctSourceRelationship::After;
}

bool SctImportedSourceMap::hasCompleteLeafCoverage() const noexcept {
    std::uint64_t expected = 0;
    for (const auto& record : records_) {
        if (record.layer != SctSourceSpanLayer::Leaf || record.span.size == 0u) continue;
        if (record.span.offset != expected) return false;
        expected = record.span.endOffset();
        if (expected > decodedPayloadSize_) return false;
    }
    return expected == decodedPayloadSize_;
}

} // namespace spice::sct
