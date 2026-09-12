#include "MldMotionListProjection.h"
#include "Internal/MldDocumentReceiptState.h"
#include "Internal/MldSha256.h"

#include <algorithm>
#include <bit>
#include <set>

namespace spice::mld {
namespace detail {
std::array<std::uint8_t, 32U> entryBindingHash(const MldEntry& entry) {
    std::vector<std::uint8_t> bytes;
    const auto word = [&](std::uint64_t value) {
        for (unsigned i = 0; i < 8; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
    };
    word(entry.id.value); word(entry.entryId); word(static_cast<std::uint32_t>(entry.tableId));
    word(entry.functionName.size());
    bytes.insert(bytes.end(), entry.functionName.begin(), entry.functionName.end());
    const auto& t = entry.transform;
    for (float value : {t.position.x, t.position.y, t.position.z, t.rotationRaw.x, t.rotationRaw.y,
        t.rotationRaw.z, t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w, t.scale.x, t.scale.y, t.scale.z})
        word(std::bit_cast<std::uint32_t>(value));
    for (const auto* values : {&entry.groundLinks, &entry.parameterList2, &entry.functionParameters}) {
        word(values->size()); for (auto value : *values) word(value);
    }
    const auto slots = [&](const auto& values) {
        word(values.size());
        for (const auto& value : values) { word(value.has_value()); if (value) word(value->value); }
    };
    slots(entry.objectSlots); slots(entry.groundSlots); slots(entry.motionSlots);
    word(entry.textureList.has_value()); if (entry.textureList) word(entry.textureList->value);
    return sha256(bytes);
}
}

MldMotionListProjection MldMotionListProjector::selectImportedEntry(const MldDocument& document,
    std::int64_t ordinal, const MldImportReceipt& receipt) {
    if (ordinal < 0 || static_cast<std::uint64_t>(ordinal) >= document.entries.size())
        return {.bindingStatus = MldSourceBindingStatus::MissingEntry};
    return project(document, document.entries[static_cast<std::size_t>(ordinal)].id, receipt);
}

MldMotionListProjection MldMotionListProjector::project(const MldDocument& document,
    MldEntryId id, const MldImportReceipt& receipt) {
    MldMotionListProjection out;
    const auto fail = [&](MldSourceBindingStatus status) { out.bindingStatus = status; return out; };
    std::set<std::uint64_t> ids, motions, variants;
    for (const auto& entry : document.entries)
        if (!entry.id || !ids.insert(entry.id.value).second) return fail(MldSourceBindingStatus::InvalidIdentity);
    for (const auto& motion : document.motions) {
        if (!motion.id || !motions.insert(motion.id.value).second) return fail(MldSourceBindingStatus::InvalidIdentity);
        if (const auto* decoded = std::get_if<MldDecodedMotion>(&motion.payload))
            for (const auto& variant : decoded->variants)
                if (!variant.id || !variants.insert(variant.id.value).second) return fail(MldSourceBindingStatus::InvalidIdentity);
    }
    const auto entry = std::find_if(document.entries.begin(), document.entries.end(),
        [&](const auto& value) { return value.id == id; });
    if (entry == document.entries.end()) return fail(MldSourceBindingStatus::MissingEntry);
    out.entryId = id;
    out.motionFrames = MldMotionFrameProjector::project(document, id);
    const auto& state = receipt.state_;
    if (!state) return out;
    if (document.sourceIdentity.token_ != state->token
        || receipt.sourceSha256 != state->sourceHash || receipt.sourceSize != state->sourceSize
        || receipt.decodedSize != state->decodedSize || receipt.platform != state->platform
        || receipt.wrapper != state->wrapper || receipt.endian != state->endian)
        return fail(MldSourceBindingStatus::ReceiptMismatch);
    if (state->entryOrder.size() != document.entries.size()) return fail(MldSourceBindingStatus::SourceChanged);
    for (std::size_t i = 0; i < document.entries.size(); ++i)
        if (document.entries[i].id != state->entryOrder[i]) return fail(MldSourceBindingStatus::SourceChanged);
    const auto ordinal = static_cast<std::size_t>(entry - document.entries.begin());
    if (detail::entryBindingHash(*entry) != state->entryHashes[ordinal]) return fail(MldSourceBindingStatus::SourceChanged);
    const auto& record = state->encodingSkeleton.entries[ordinal];
    const auto offset = static_cast<std::uint64_t>(state->encodingSkeleton.header.indexTableOffset)
        + static_cast<std::uint64_t>(record.entry.tableIndex) * 0x68U;
    if (offset > state->decodedSize || 0x68U > state->decodedSize - offset)
        return fail(MldSourceBindingStatus::ReceiptMismatch);
    out.bindingStatus = MldSourceBindingStatus::Matched;
    out.sourceOrdinal = ordinal;
    out.sourceEntryOffset = offset;
    out.sourceSha256 = state->sourceHash;
    out.sourceSize = state->sourceSize;
    out.platform = state->platform;
    const auto& list = record.entry.motionAddresses;
    if (!list) return out;
    out.sourceListPointer = list->pointer;
    out.declaredSlotCount = list->declaredCount;
    out.sourceSlotReferences = list->values;
    using Status = model::U32ListStatus;
    switch (list->status) {
    case Status::Absent: out.listStatus = MldMotionListStatus::Absent; break;
    case Status::Present: out.listStatus = list->values.empty() ? MldMotionListStatus::PresentEmpty : MldMotionListStatus::PresentNonempty; break;
    case Status::PointerOutOfBounds: out.listStatus = MldMotionListStatus::PointerOutOfBounds; break;
    case Status::TruncatedCount: out.listStatus = MldMotionListStatus::TruncatedCount; break;
    case Status::ExcessiveCount: out.listStatus = MldMotionListStatus::ExcessiveCount; break;
    case Status::TruncatedValues: out.listStatus = MldMotionListStatus::TruncatedValues; break;
    default: break;
    }
    return out;
}
} // namespace spice::mld
