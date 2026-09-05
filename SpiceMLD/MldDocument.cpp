#include "MldDocument.h"

#include <algorithm>

namespace spice::mld {
namespace {

template <typename Id, typename Collection>
[[nodiscard]] Id nextId(const Collection& collection) noexcept {
    std::uint64_t highest = 0U;
    for (const auto& item : collection) highest = std::max(highest, item.id.value);
    return Id{ highest + 1U };
}

} // namespace

MldEntryId MldDocument::allocateEntryId() const noexcept { return nextId<MldEntryId>(entries); }
MldObjectId MldDocument::allocateObjectId() const noexcept { return nextId<MldObjectId>(objects); }
MldMotionId MldDocument::allocateMotionId() const noexcept { return nextId<MldMotionId>(motions); }
MldMotionVariantId MldDocument::allocateMotionVariantId() const noexcept {
    std::uint64_t maximum = 0U;
    for (const auto& motion : motions) {
        const auto* decoded = std::get_if<MldDecodedMotion>(&motion.payload);
        if (decoded == nullptr) continue;
        for (const auto& variant : decoded->variants) maximum = std::max(maximum, variant.id.value);
    }
    return MldMotionVariantId{ maximum + 1U };
}
MldGroundId MldDocument::allocateGroundId() const noexcept { return nextId<MldGroundId>(grounds); }
MldTextureListId MldDocument::allocateTextureListId() const noexcept { return nextId<MldTextureListId>(textureLists); }
MldTextureArchiveId MldDocument::allocateTextureArchiveId() const noexcept { return nextId<MldTextureArchiveId>(textureArchives); }
MldOpaqueMemberId MldDocument::allocateOpaqueMemberId() const noexcept { return nextId<MldOpaqueMemberId>(opaqueMembers); }

bool MldDocument::hasOpaqueContent() const noexcept {
    if (!opaqueMembers.empty()) return true;
    if (std::any_of(objects.begin(), objects.end(), [](const auto& item) {
            return std::holds_alternative<MldOpaquePayload>(item.payload);
        })) return true;
    if (std::any_of(motions.begin(), motions.end(), [](const auto& item) {
            return std::holds_alternative<MldOpaquePayload>(item.payload);
        })) return true;
    return std::any_of(grounds.begin(), grounds.end(), [](const auto& item) {
        return std::holds_alternative<MldOpaquePayload>(item.payload);
    });
}

} // namespace spice::mld
