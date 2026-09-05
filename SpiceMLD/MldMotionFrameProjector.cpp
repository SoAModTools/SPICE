#include "MldMotionFrameProjector.h"

#include <algorithm>
#include <utility>

namespace spice::mld {

bool MldMotionFrameProjectionResult::ok() const noexcept {
    return entryId.has_value() && !slots.empty() && std::all_of(slots.begin(), slots.end(), [](const auto& slot) {
        return slot.status == MldMotionFrameSlotStatus::Resolved;
    });
}

MldMotionFrameProjectionResult MldMotionFrameProjector::project(
    const MldDocument& document, const MldEntryId entryId) {
    MldMotionFrameProjectionResult result{};
    const auto entry = std::find_if(document.entries.begin(), document.entries.end(),
        [&](const auto& candidate) { return candidate.id == entryId; });
    if (entry == document.entries.end()) return result;
    result.entryId = entryId;
    result.slots.reserve(entry->motionSlots.size());
    for (std::size_t ordinal = 0U; ordinal < entry->motionSlots.size(); ++ordinal) {
        MldMotionFrameSlotProjection slot{ .slotOrdinal = ordinal };
        const auto motionId = entry->motionSlots[ordinal];
        if (!motionId.has_value()) {
            result.slots.push_back(std::move(slot));
            continue;
        }
        slot.motionId = motionId;
        const auto motion = std::find_if(document.motions.begin(), document.motions.end(),
            [&](const auto& candidate) { return candidate.id == *motionId; });
        if (motion == document.motions.end()) {
            slot.status = MldMotionFrameSlotStatus::MissingMotion;
            result.slots.push_back(std::move(slot));
            continue;
        }
        const auto* decoded = std::get_if<MldDecodedMotion>(&motion->payload);
        if (decoded == nullptr) {
            slot.status = MldMotionFrameSlotStatus::OpaqueMotion;
            result.slots.push_back(std::move(slot));
            continue;
        }
        slot.kind = decoded->kind;
        if (decoded->variants.empty()) {
            slot.status = MldMotionFrameSlotStatus::NoDecodedVariants;
            result.slots.push_back(std::move(slot));
            continue;
        }
        for (const auto& variant : decoded->variants) {
            if (!variant.document) continue;
            slot.variants.push_back({ variant.id, variant.document->motion().declared_frame_count });
        }
        if (slot.variants.empty()) {
            slot.status = MldMotionFrameSlotStatus::NoDecodedVariants;
        } else {
            const auto count = slot.variants.front().declaredFrameCount;
            const bool agree = std::all_of(slot.variants.begin(), slot.variants.end(),
                [&](const auto& candidate) { return candidate.declaredFrameCount == count; });
            if (agree) {
                slot.agreedDeclaredFrameCount = count;
                slot.status = MldMotionFrameSlotStatus::Resolved;
            } else {
                slot.status = MldMotionFrameSlotStatus::ConflictingDeclaredFrameCounts;
            }
        }
        result.slots.push_back(std::move(slot));
    }
    return result;
}

const char* toString(const MldMotionFrameSlotStatus status) noexcept {
    switch (status) {
    case MldMotionFrameSlotStatus::Resolved: return "resolved";
    case MldMotionFrameSlotStatus::EmptySlot: return "empty_slot";
    case MldMotionFrameSlotStatus::MissingMotion: return "missing_motion";
    case MldMotionFrameSlotStatus::OpaqueMotion: return "opaque_motion";
    case MldMotionFrameSlotStatus::NoDecodedVariants: return "no_decoded_variants";
    case MldMotionFrameSlotStatus::ConflictingDeclaredFrameCounts: return "conflicting_declared_frame_counts";
    }
    return "unknown";
}

} // namespace spice::mld
