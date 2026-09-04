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
        } else if constexpr (std::is_same_v<T, SctExpressionOperationSite>) {
            return value.expression.instruction.value() != 0u;
        } else {
            return std::visit([](const auto& id) { return id.value() != 0u; }, value.text);
        }
    }, target);
}

} // namespace

SctImportedSourceMap::SctImportedSourceMap(std::uint32_t decodedPayloadSize,
    std::vector<SctSourceSpanRecord> records)
    : decodedPayloadSize_(decodedPayloadSize), records_(std::move(records)) {
    buildIndexes();
}

void SctImportedSourceMap::buildIndexes() {
    std::uint64_t prefixMaximumEnd = 0;
    for (std::uint32_t ordinal = 0; ordinal < records_.size(); ++ordinal) {
        const auto& record = records_[ordinal];
        prefixMaximumEnd = std::max(prefixMaximumEnd, record.span.endOffset());
        intervalPrefixMaximumEnd_.push_back(prefixMaximumEnd);
        if (record.target) targetRecordOrdinals_[*record.target].push_back(ordinal);
        if (record.primaryEntityLocation) {
            if (const auto* entity = targetEntity(record.target)) {
                primaryEntityOrdinals_.emplace(*entity, ordinal);
                if (isSemanticAdjacencyEntity(*entity)) {
                    semanticLocations_.push_back({*entity, record.span, record.containingSection,
                        record.sectionRelativeOffset, record.region});
                }
            }
        }
        if (record.layer == SctSourceSpanLayer::Leaf) {
            leafOrdinals_.push_back(ordinal);
            if (record.target) targetedLeafOrdinals_.push_back(ordinal);
        } else if (record.target) {
            targetedEnvelopeOrdinals_.push_back(ordinal);
        }
    }
    std::stable_sort(semanticLocations_.begin(), semanticLocations_.end(),
        [](const auto& left, const auto& right) {
            if (left.primarySpan.offset != right.primarySpan.offset) {
                return left.primarySpan.offset < right.primarySpan.offset;
            }
            return left.primarySpan.size < right.primarySpan.size;
        });
    for (std::size_t index = 0; index < semanticLocations_.size(); ++index) {
        semanticOrderIndex_.emplace(semanticLocations_[index].entity, index);
    }
    std::uint64_t targetedEnvelopeMaximumEnd = 0;
    targetedEnvelopePrefixMaximumEnd_.reserve(targetedEnvelopeOrdinals_.size());
    for (const auto ordinal : targetedEnvelopeOrdinals_) {
        targetedEnvelopeMaximumEnd = std::max(
            targetedEnvelopeMaximumEnd, records_[ordinal].span.endOffset());
        targetedEnvelopePrefixMaximumEnd_.push_back(targetedEnvelopeMaximumEnd);
    }
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
    const auto found = targetRecordOrdinals_.find(target);
    if (found == targetRecordOrdinals_.end()) return result;
    result.reserve(found->second.size());
    for (const auto ordinal : found->second) result.push_back(records_[ordinal]);
    return result;
}

std::vector<SctSourceSpanRecord> SctImportedSourceMap::recordsAt(std::uint32_t offset) const {
    std::vector<SctSourceSpanRecord> result;
    const auto after = std::upper_bound(records_.begin(), records_.end(), offset,
        [](std::uint32_t value, const auto& record) { return value < record.span.offset; });
    std::size_t index = static_cast<std::size_t>(after - records_.begin());
    while (index != 0u) {
        --index;
        if (intervalPrefixMaximumEnd_[index] <= offset) break;
        const auto& record = records_[index];
        if (record.span.size != 0u && offset >= record.span.offset
            && static_cast<std::uint64_t>(offset) < record.span.endOffset()) {
            result.push_back(record);
        }
    }
    std::reverse(result.begin(), result.end());
    return result;
}

std::vector<SctSourceSpanRecord> SctImportedSourceMap::recordsContaining(
    SctImportedByteSpan span) const {
    std::vector<SctSourceSpanRecord> result;
    const auto after = std::upper_bound(records_.begin(), records_.end(), span.offset,
        [](std::uint32_t value, const auto& record) { return value < record.span.offset; });
    std::size_t index = static_cast<std::size_t>(after - records_.begin());
    while (index != 0u) {
        --index;
        if (intervalPrefixMaximumEnd_[index] < span.endOffset()) break;
        if (contains(records_[index].span, span)) result.push_back(records_[index]);
    }
    std::reverse(result.begin(), result.end());
    return result;
}

SctSourceRecordSummary SctImportedSourceMap::summarize(std::uint32_t ordinal) const {
    const auto& record = records_[ordinal];
    return {{ordinal}, record.span, record.role, record.layer, record.target};
}

std::optional<SctSourceRecordSummary> SctImportedSourceMap::recordSummary(
    SctSourceRecordOrdinal ordinal) const {
    if (ordinal.value >= records_.size()) return std::nullopt;
    return summarize(ordinal.value);
}

SctSourceRecordNeighborhood SctImportedSourceMap::neighborhood(
    SctImportedByteSpan span) const {
    return neighborhood(span, std::nullopt);
}

std::optional<SctSourceRecordNeighborhood> SctImportedSourceMap::neighborhood(
    SctSourceRecordOrdinal ordinal) const {
    if (ordinal.value >= records_.size()) return std::nullopt;
    return neighborhood(records_[ordinal.value].span, ordinal.value);
}

SctSourceRecordNeighborhood SctImportedSourceMap::neighborhood(
    SctImportedByteSpan span, std::optional<std::uint32_t> excludedOrdinal) const {
    SctSourceRecordNeighborhood result;

    auto preceding = std::partition_point(targetedLeafOrdinals_.begin(),
        targetedLeafOrdinals_.end(), [&](std::uint32_t ordinal) {
            return records_[ordinal].span.endOffset() <= span.offset;
        });
    while (preceding != targetedLeafOrdinals_.begin()) {
        --preceding;
        if (!excludedOrdinal || *preceding != *excludedOrdinal) {
            result.precedingTargetedLeaf = summarize(*preceding);
            break;
        }
    }

    auto following = std::lower_bound(targetedLeafOrdinals_.begin(),
        targetedLeafOrdinals_.end(), span.endOffset(),
        [&](std::uint32_t ordinal, std::uint64_t endOffset) {
            return records_[ordinal].span.offset < endOffset;
        });
    while (following != targetedLeafOrdinals_.end()) {
        if (!excludedOrdinal || *following != *excludedOrdinal) {
            result.followingTargetedLeaf = summarize(*following);
            break;
        }
        ++following;
    }

    const auto afterEnvelope = std::upper_bound(targetedEnvelopeOrdinals_.begin(),
        targetedEnvelopeOrdinals_.end(), span.offset,
        [&](std::uint32_t offset, std::uint32_t ordinal) {
            return offset < records_[ordinal].span.offset;
        });
    std::size_t envelopeIndex = static_cast<std::size_t>(
        afterEnvelope - targetedEnvelopeOrdinals_.begin());
    while (envelopeIndex != 0u) {
        --envelopeIndex;
        if (targetedEnvelopePrefixMaximumEnd_[envelopeIndex] < span.endOffset()) break;
        const auto ordinal = targetedEnvelopeOrdinals_[envelopeIndex];
        if ((!excludedOrdinal || ordinal != *excludedOrdinal)
            && contains(records_[ordinal].span, span)) {
            result.containingTargetedEnvelopes.push_back(summarize(ordinal));
        }
    }
    std::stable_sort(result.containingTargetedEnvelopes.begin(),
        result.containingTargetedEnvelopes.end(), [](const auto& left, const auto& right) {
            if (left.span.size != right.span.size) return left.span.size < right.span.size;
            return left.ordinal.value > right.ordinal.value;
        });
    return result;
}

std::optional<SctSourceEntityLocation> SctImportedSourceMap::location(
    const SctDocumentEntityId& entity) const {
    const auto found = primaryEntityOrdinals_.find(entity);
    if (found == primaryEntityOrdinals_.end()) return std::nullopt;
    const auto& record = records_[found->second];
    return SctSourceEntityLocation{entity, record.span, record.containingSection,
        record.sectionRelativeOffset, record.region};
}

std::optional<SctDocumentEntityId> SctImportedSourceMap::previousSemanticEntity(
    const SctDocumentEntityId& entity) const {
    if (const auto semantic = semanticOrderIndex_.find(entity);
        semantic != semanticOrderIndex_.end()) {
        return semantic->second == 0u
            ? std::nullopt
            : std::optional<SctDocumentEntityId>{semanticLocations_[semantic->second - 1u].entity};
    }
    const auto target = location(entity);
    if (!target) return std::nullopt;
    const auto found = std::lower_bound(semanticLocations_.begin(), semanticLocations_.end(),
        target->primarySpan.offset, [](const auto& candidate, std::uint32_t offset) {
            return candidate.primarySpan.offset < offset;
        });
    if (found == semanticLocations_.begin()) return std::nullopt;
    return std::prev(found)->entity;
}

std::optional<SctDocumentEntityId> SctImportedSourceMap::nextSemanticEntity(
    const SctDocumentEntityId& entity) const {
    if (const auto semantic = semanticOrderIndex_.find(entity);
        semantic != semanticOrderIndex_.end()) {
        return semantic->second + 1u >= semanticLocations_.size()
            ? std::nullopt
            : std::optional<SctDocumentEntityId>{semanticLocations_[semantic->second + 1u].entity};
    }
    const auto target = location(entity);
    if (!target) return std::nullopt;
    const auto found = std::upper_bound(semanticLocations_.begin(), semanticLocations_.end(),
        target->primarySpan.offset, [](std::uint32_t offset, const auto& candidate) {
            return offset < candidate.primarySpan.offset;
        });
    return found == semanticLocations_.end()
        ? std::nullopt : std::optional<SctDocumentEntityId>{found->entity};
}

std::vector<SctDocumentEntityId> SctImportedSourceMap::semanticEntitiesBetween(
    const SctDocumentEntityId& first, const SctDocumentEntityId& second) const {
    std::vector<SctDocumentEntityId> result;
    const auto firstLocation = location(first);
    const auto secondLocation = location(second);
    if (!firstLocation || !secondLocation) return result;
    const auto low = std::min(firstLocation->primarySpan.offset, secondLocation->primarySpan.offset);
    const auto high = std::max(firstLocation->primarySpan.offset, secondLocation->primarySpan.offset);
    const auto begin = std::upper_bound(semanticLocations_.begin(), semanticLocations_.end(), low,
        [](std::uint32_t offset, const auto& candidate) {
            return offset < candidate.primarySpan.offset;
        });
    for (auto found = begin; found != semanticLocations_.end()
         && found->primarySpan.offset < high; ++found) {
        result.push_back(found->entity);
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
    for (const auto ordinal : leafOrdinals_) {
        const auto& record = records_[ordinal];
        if (record.span.size == 0u) continue;
        if (record.span.offset != expected) return false;
        expected = record.span.endOffset();
        if (expected > decodedPayloadSize_) return false;
    }
    return expected == decodedPayloadSize_;
}

} // namespace spice::sct
