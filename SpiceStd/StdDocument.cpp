#include "StdDocument.h"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace spice::stdfile {
namespace {

template <typename Id, typename Range, typename GetId>
Id allocateId(const Range& values, GetId getId) noexcept {
    std::uint64_t next = 1U;
    for (const auto& value : values) next = std::max(next, getId(value).value + 1U);
    return Id{ next };
}

} // namespace

StdActionRowId StdDocument::allocateActionRowId() const noexcept {
    const auto* value = std::get_if<StdActionRowsContent>(&content);
    return value == nullptr ? StdActionRowId{ 1U }
        : allocateId<StdActionRowId>(value->rows, [](const auto& item) { return item.id; });
}

StdEntryRecordId StdDocument::allocateEntryRecordId() const noexcept {
    const auto* value = std::get_if<StdEntryTableContent>(&content);
    return value == nullptr ? StdEntryRecordId{ 1U }
        : allocateId<StdEntryRecordId>(value->records, [](const auto& item) { return item.id; });
}

StdEntryPayloadId StdDocument::allocateEntryPayloadId() const noexcept {
    const auto* value = std::get_if<StdEntryTableContent>(&content);
    return value == nullptr ? StdEntryPayloadId{ 1U }
        : allocateId<StdEntryPayloadId>(value->payloads, [](const auto& item) { return item.id; });
}

StdEntryTerminatorId StdDocument::allocateEntryTerminatorId() const noexcept {
    const auto* value = std::get_if<StdEntryTableContent>(&content);
    return value == nullptr || !value->terminator.id ? StdEntryTerminatorId{ 1U }
        : StdEntryTerminatorId{ value->terminator.id.value + 1U };
}

StdOpaqueFragmentId StdDocument::allocateOpaqueFragmentId() const noexcept {
    const auto* value = std::get_if<StdEntryTableContent>(&content);
    return value == nullptr ? StdOpaqueFragmentId{ 1U }
        : allocateId<StdOpaqueFragmentId>(value->opaqueFragments, [](const auto& item) { return item.id; });
}

bool StdDocument::hasOpaqueContent() const noexcept {
    if (std::holds_alternative<StdOpaqueContent>(content)) return true;
    const auto* table = std::get_if<StdEntryTableContent>(&content);
    if (table == nullptr) return false;
    if (!table->opaqueFragments.empty()) return true;
    return std::any_of(table->payloads.begin(), table->payloads.end(), [](const auto& payload) {
        return std::holds_alternative<StdOpaquePayload>(payload.content);
    });
}

const StdEntryPayload* findEntryPayload(const StdEntryTableContent& table, const StdEntryPayloadId id) noexcept {
    const auto found = std::find_if(table.payloads.begin(), table.payloads.end(),
        [&](const auto& item) { return item.id == id; });
    return found == table.payloads.end() ? nullptr : &*found;
}

StdEntryPayload* findEntryPayload(StdEntryTableContent& table, const StdEntryPayloadId id) noexcept {
    return const_cast<StdEntryPayload*>(findEntryPayload(std::as_const(table), id));
}

const StdOpaqueFragment* findOpaqueFragment(const StdEntryTableContent& table, const StdOpaqueFragmentId id) noexcept {
    const auto found = std::find_if(table.opaqueFragments.begin(), table.opaqueFragments.end(),
        [&](const auto& item) { return item.id == id; });
    return found == table.opaqueFragments.end() ? nullptr : &*found;
}

StdOpaqueFragment* findOpaqueFragment(StdEntryTableContent& table, const StdOpaqueFragmentId id) noexcept {
    return const_cast<StdOpaqueFragment*>(findOpaqueFragment(std::as_const(table), id));
}

} // namespace spice::stdfile
