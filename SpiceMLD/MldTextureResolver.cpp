#include "MldTextureResolver.h"

#include <algorithm>

namespace spice::mld {
namespace {
template <typename Values, typename Id>
auto find(const Values& values, Id id) {
    return std::find_if(values.begin(), values.end(), [&](const auto& item) { return item.id == id; });
}
}

const char* textureBindingStatusName(const MldTextureBindingStatus status) noexcept {
    switch (status) {
    case MldTextureBindingStatus::Resolved: return "resolved";
    case MldTextureBindingStatus::OwnerNotFound: return "owner-not-found";
    case MldTextureBindingStatus::ObjectSlotOutOfRange: return "object-slot-out-of-range";
    case MldTextureBindingStatus::AbsentObjectSlot: return "absent-object-slot";
    case MldTextureBindingStatus::AbsentList: return "absent-list";
    case MldTextureBindingStatus::MissingList: return "missing-list";
    case MldTextureBindingStatus::InvalidList: return "invalid-list";
    case MldTextureBindingStatus::SlotOutOfRange: return "texture-slot-out-of-range";
    case MldTextureBindingStatus::EmptyName: return "empty-name";
    case MldTextureBindingStatus::ImageNotFound: return "image-not-found";
    case MldTextureBindingStatus::AmbiguousImage: return "ambiguous-image";
    case MldTextureBindingStatus::ImageDataUnavailable: return "image-data-unavailable";
    }
    return "unknown";
}

MldTextureBinding MldTextureResolver::resolveListTexture(const MldDocument& document,
    const std::optional<MldTextureListId> list, const std::size_t slot) {
    MldTextureBinding result{ .list = list, .slot = slot };
    if (!list) return result;
    const auto found = find(document.textureLists, *list);
    if (found == document.textureLists.end()) { result.status = MldTextureBindingStatus::MissingList; return result; }
    if (!found->complete) { result.status = MldTextureBindingStatus::InvalidList; return result; }
    if (slot >= found->names.size()) { result.status = MldTextureBindingStatus::SlotOutOfRange; return result; }
    result.name = found->names[slot];
    if (result.name.empty()) { result.status = MldTextureBindingStatus::EmptyName; return result; }
    for (const auto& archive : document.textureArchives) for (const auto& texture : archive.textures) {
        if (texture.name != result.name) continue;
        result.candidates.push_back(texture.id);
        result.archive = archive.id;
        result.textureId = texture.id;
        result.texture = &texture;
    }
    if (result.candidates.empty()) result.status = MldTextureBindingStatus::ImageNotFound;
    else if (result.candidates.size() > 1U) {
        result.status = MldTextureBindingStatus::AmbiguousImage;
        result.archive.reset(); result.textureId.reset(); result.texture = nullptr;
    } else if (result.texture->encodedBytes.empty() && (!result.texture->decoded || result.texture->rgba8.empty()))
        result.status = MldTextureBindingStatus::ImageDataUnavailable;
    else result.status = MldTextureBindingStatus::Resolved;
    return result;
}

MldTextureBinding MldTextureResolver::resolveEntryTexture(const MldDocument& document,
    const MldEntryId entry, const std::size_t slot) {
    const auto found = find(document.entries, entry);
    if (found == document.entries.end()) return { .status = MldTextureBindingStatus::OwnerNotFound, .slot = slot };
    return resolveListTexture(document, found->textureList, slot);
}

MldTextureBinding MldTextureResolver::resolveObjectTexture(const MldDocument& document,
    const MldObjectId object, const std::size_t slot) {
    const auto found = find(document.objects, object);
    if (found == document.objects.end()) return { .status = MldTextureBindingStatus::OwnerNotFound, .slot = slot };
    return resolveListTexture(document, found->textureList, slot);
}

MldTextureBinding MldTextureResolver::resolveEntryObjectTexture(const MldDocument& document,
    const MldEntryId entry, const std::size_t objectSlot, const std::size_t textureSlot) {
    const auto found = find(document.entries, entry);
    if (found == document.entries.end()) return { .status = MldTextureBindingStatus::OwnerNotFound, .slot = textureSlot };
    if (objectSlot >= found->objectSlots.size()) return { .status = MldTextureBindingStatus::ObjectSlotOutOfRange, .slot = textureSlot };
    if (!found->objectSlots[objectSlot]) return { .status = MldTextureBindingStatus::AbsentObjectSlot, .slot = textureSlot };
    return resolveObjectTexture(document, *found->objectSlots[objectSlot], textureSlot);
}
} // namespace spice::mld
