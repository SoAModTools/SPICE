#pragma once

#include "MldDocument.h"

namespace spice::mld {

enum class MldTextureBindingStatus {
    Resolved, OwnerNotFound, ObjectSlotOutOfRange, AbsentObjectSlot,
    AbsentList, MissingList, InvalidList, SlotOutOfRange, EmptyName,
    ImageNotFound, AmbiguousImage, ImageDataUnavailable,
};

struct MldTextureBinding {
    MldTextureBindingStatus status{ MldTextureBindingStatus::AbsentList };
    std::optional<MldTextureListId> list{};
    std::size_t slot{};
    std::string name{};
    std::optional<MldTextureArchiveId> archive{};
    std::optional<MldTextureId> textureId{};
    // Borrowed from the queried document; invalidate on document mutation/destruction.
    const MldTexture* texture{};
    std::vector<MldTextureId> candidates{};
    [[nodiscard]] bool resolved() const noexcept { return status == MldTextureBindingStatus::Resolved; }
};

[[nodiscard]] const char* textureBindingStatusName(MldTextureBindingStatus status) noexcept;

class MldTextureResolver {
public:
    // Entry slots and model material texture slots are independent namespaces.
    // Matching uses exact stored names across all document archives. No index,
    // case-folding, entry/model precedence, or external resource fallback is implied.
    [[nodiscard]] static MldTextureBinding resolveEntryTexture(
        const MldDocument& document, MldEntryId entry, std::size_t textureSlot);
    [[nodiscard]] static MldTextureBinding resolveObjectTexture(
        const MldDocument& document, MldObjectId object, std::size_t textureSlot);
    [[nodiscard]] static MldTextureBinding resolveEntryObjectTexture(
        const MldDocument& document, MldEntryId entry, std::size_t objectSlot, std::size_t textureSlot);
    [[nodiscard]] static MldTextureBinding resolveListTexture(
        const MldDocument& document, std::optional<MldTextureListId> list, std::size_t textureSlot);
};

} // namespace spice::mld
