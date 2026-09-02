#include "SctStringGroups.h"

namespace spice::sct {
namespace {

template <typename Record>
void indexRecord(const Record& record, std::size_t index,
    std::unordered_map<std::uint64_t, std::size_t>& markers,
    std::unordered_map<std::uint64_t, std::size_t>& sections,
    std::unordered_map<std::uint64_t, std::size_t>& strings) {
    if (record.markerSection) markers.try_emplace(record.markerSection->value(), index);
    for (const auto section : record.memberSections) sections.try_emplace(section.value(), index);
    for (const auto string : record.strings) strings.try_emplace(string.value(), index);
}

template <typename Record>
const Record* findRecord(const std::vector<Record>& records,
    const std::unordered_map<std::uint64_t, std::size_t>& index, std::uint64_t id) noexcept {
    const auto found = index.find(id);
    return found == index.end() || found->second >= records.size()
        ? nullptr : &records[found->second];
}

} // namespace

SctIndexedStringGroupIndex SctIndexedStringGroupIndex::build(const SctDocument& document,
    std::span<const SctImportedIndexedStringGroupObservation> imported) {
    SctIndexedStringGroupIndex result;
    for (std::size_t index = 0; index < document.sections.size();) {
        const auto& section = document.sections[index];
        const bool marker = std::holds_alternative<SctStringGroupMarkerSectionContent>(section.content);
        const bool string = std::holds_alternative<SctStringSectionContent>(section.content);
        if (!marker && !string) {
            ++index;
            continue;
        }

        SctIndexedStringGroupRecord group;
        group.ordinal = result.currentGroups_.size();
        if (marker) {
            group.basis = SctIndexedStringGroupBasis::ExplicitMarker;
            group.markerSection = section.id;
            ++index;
        } else {
            group.basis = SctIndexedStringGroupBasis::UnmarkedContiguousRun;
        }
        while (index < document.sections.size()) {
            const auto* content = std::get_if<SctStringSectionContent>(
                &document.sections[index].content);
            if (content == nullptr) break;
            group.memberSections.push_back(document.sections[index].id);
            group.strings.push_back(content->string.id);
            ++index;
        }
        result.currentGroups_.push_back(std::move(group));
    }

    result.importedGroups_.assign(imported.begin(), imported.end());
    for (std::size_t index = 0; index < result.currentGroups_.size(); ++index) {
        indexRecord(result.currentGroups_[index], index, result.currentMarkers_,
            result.currentSections_, result.currentStrings_);
    }
    for (std::size_t index = 0; index < result.importedGroups_.size(); ++index) {
        indexRecord(result.importedGroups_[index], index, result.importedMarkers_,
            result.importedSections_, result.importedStrings_);
    }
    return result;
}

const SctIndexedStringGroupRecord* SctIndexedStringGroupIndex::currentByMarker(
    SctSectionId marker) const noexcept {
    return findRecord(currentGroups_, currentMarkers_, marker.value());
}

const SctIndexedStringGroupRecord* SctIndexedStringGroupIndex::currentContainingSection(
    SctSectionId section) const noexcept {
    return findRecord(currentGroups_, currentSections_, section.value());
}

const SctIndexedStringGroupRecord* SctIndexedStringGroupIndex::currentContainingString(
    SctStringId string) const noexcept {
    return findRecord(currentGroups_, currentStrings_, string.value());
}

const SctImportedIndexedStringGroupObservation* SctIndexedStringGroupIndex::importedByMarker(
    SctSectionId marker) const noexcept {
    return findRecord(importedGroups_, importedMarkers_, marker.value());
}

const SctImportedIndexedStringGroupObservation* SctIndexedStringGroupIndex::importedContainingSection(
    SctSectionId section) const noexcept {
    return findRecord(importedGroups_, importedSections_, section.value());
}

const SctImportedIndexedStringGroupObservation* SctIndexedStringGroupIndex::importedContainingString(
    SctStringId string) const noexcept {
    return findRecord(importedGroups_, importedStrings_, string.value());
}

} // namespace spice::sct
