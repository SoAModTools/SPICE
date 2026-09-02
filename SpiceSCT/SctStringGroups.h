#pragma once

#include "SctDocument.h"
#include "SctModel.h"

#include <cstddef>
#include <optional>
#include <span>
#include <unordered_map>
#include <variant>
#include <vector>

namespace spice::sct {

enum class SctIndexedStringGroupBasis {
    // A marker section represents the group in the current flat document. It
    // does not prove game-level semantic ownership or author intent.
    ExplicitMarker,
    UnmarkedContiguousRun,
};

enum class SctImportedStringGroupMembershipKind {
    MarkerSection,
    MemberSection,
    String,
};

using SctImportedStringGroupMembershipTarget = std::variant<SctSectionId, SctStringId>;

struct SctImportedIndexedStringGroupAmbiguity {
    SctImportedStringGroupMembershipKind kind =
        SctImportedStringGroupMembershipKind::MemberSection;
    SctImportedStringGroupMembershipTarget target;
    // Indexes into importedGroups(), in caller-supplied order.
    std::vector<std::size_t> importedGroupIndexes;
};

struct SctIndexedStringGroupRecord {
    std::size_t ordinal = 0;
    SctIndexedStringGroupBasis basis = SctIndexedStringGroupBasis::UnmarkedContiguousRun;
    std::optional<SctSectionId> markerSection;
    std::vector<SctSectionId> memberSections;
    std::vector<SctStringId> strings;
};

struct SctImportedIndexedStringGroupObservation {
    std::size_t ordinal = 0;
    SctIndexedStringGroupBasis basis = SctIndexedStringGroupBasis::UnmarkedContiguousRun;
    std::optional<SctSectionId> markerSection;
    std::vector<SctSectionId> memberSections;
    std::vector<SctStringId> strings;
    SctSemanticConfidence confidence = SctSemanticConfidence::Heuristic;
};

// A revision-scoped view over the flat physical section sequence. Group records
// are never serialization authority and must be rebuilt after section edits.
class SctIndexedStringGroupIndex {
public:
    [[nodiscard]] static SctIndexedStringGroupIndex build(const SctDocument& document,
        std::span<const SctImportedIndexedStringGroupObservation> imported = {});

    [[nodiscard]] std::span<const SctIndexedStringGroupRecord> currentGroups() const noexcept {
        return currentGroups_;
    }
    [[nodiscard]] std::span<const SctImportedIndexedStringGroupObservation>
        importedGroups() const noexcept {
        return importedGroups_;
    }
    // Importer-produced observations are expected to be disjoint. Public
    // callers may supply arbitrary historical observations; overlaps are
    // reported here, and the corresponding singular lookup returns nullptr.
    [[nodiscard]] std::span<const SctImportedIndexedStringGroupAmbiguity>
        importedAmbiguities() const noexcept {
        return importedAmbiguities_;
    }

    [[nodiscard]] const SctIndexedStringGroupRecord* currentByMarker(
        SctSectionId marker) const noexcept;
    [[nodiscard]] const SctIndexedStringGroupRecord* currentContainingSection(
        SctSectionId section) const noexcept;
    [[nodiscard]] const SctIndexedStringGroupRecord* currentContainingString(
        SctStringId string) const noexcept;
    [[nodiscard]] const SctImportedIndexedStringGroupObservation* importedByMarker(
        SctSectionId marker) const noexcept;
    [[nodiscard]] const SctImportedIndexedStringGroupObservation* importedContainingSection(
        SctSectionId section) const noexcept;
    [[nodiscard]] const SctImportedIndexedStringGroupObservation* importedContainingString(
        SctStringId string) const noexcept;

private:
    using MembershipIndex =
        std::unordered_map<std::uint64_t, std::vector<std::size_t>>;

    std::vector<SctIndexedStringGroupRecord> currentGroups_;
    std::vector<SctImportedIndexedStringGroupObservation> importedGroups_;
    std::vector<SctImportedIndexedStringGroupAmbiguity> importedAmbiguities_;
    MembershipIndex currentMarkers_;
    MembershipIndex currentSections_;
    MembershipIndex currentStrings_;
    MembershipIndex importedMarkers_;
    MembershipIndex importedSections_;
    MembershipIndex importedStrings_;
};

} // namespace spice::sct
