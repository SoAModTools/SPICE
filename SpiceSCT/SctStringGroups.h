#pragma once

#include "SctDocument.h"
#include "SctModel.h"

#include <cstddef>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace spice::sct {

enum class SctIndexedStringGroupBasis {
    ExplicitMarker,
    UnmarkedContiguousRun,
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
    std::vector<SctIndexedStringGroupRecord> currentGroups_;
    std::vector<SctImportedIndexedStringGroupObservation> importedGroups_;
    std::unordered_map<std::uint64_t, std::size_t> currentMarkers_;
    std::unordered_map<std::uint64_t, std::size_t> currentSections_;
    std::unordered_map<std::uint64_t, std::size_t> currentStrings_;
    std::unordered_map<std::uint64_t, std::size_t> importedMarkers_;
    std::unordered_map<std::uint64_t, std::size_t> importedSections_;
    std::unordered_map<std::uint64_t, std::size_t> importedStrings_;
};

} // namespace spice::sct
