#include "SstSmlDocument.h"

#include <algorithm>

namespace spice::sstsml {
namespace {

template <typename Id, typename Range, typename Projection>
Id allocateId(const Range& range, Projection projection) noexcept {
    std::uint64_t maximum = 0U;
    for (const auto& value : range) maximum = std::max(maximum, projection(value).value);
    return Id{ maximum + 1U };
}

} // namespace

SstSmlStageMemberId SstSmlDocument::allocateStageMemberId() const noexcept {
    return allocateId<SstSmlStageMemberId>(members, [](const auto& value) { return value.id; });
}

SmlRecordId SstSmlDocument::allocateSmlRecordId() const noexcept {
    return allocateId<SmlRecordId>(members, [](const auto& value) { return value.sml.id; });
}

SmlEmbeddedResourceId SstSmlDocument::allocateEmbeddedResourceId() const noexcept {
    return allocateId<SmlEmbeddedResourceId>(members, [](const auto& value) { return value.sml.resource.id; });
}

SstRecordId SstSmlDocument::allocateSstRecordId() const noexcept {
    return allocateId<SstRecordId>(members, [](const auto& value) { return value.sst.id; });
}

SstCommandBlockId SstSmlDocument::allocateCommandBlockId() const noexcept {
    return allocateId<SstCommandBlockId>(members, [](const auto& value) { return value.sst.commandBlock.id; });
}

SstCommandId SstSmlDocument::allocateCommandId() const noexcept {
    std::uint64_t maximum = 0U;
    for (const auto& member : members) {
        for (const auto& value : member.sst.commandBlock.commands) maximum = std::max(maximum, value.id.value);
    }
    return SstCommandId{ maximum + 1U };
}

SstCommandFieldId SstSmlDocument::allocateCommandFieldId() const noexcept {
    std::uint64_t maximum = 0U;
    for (const auto& member : members) {
        for (const auto& command : member.sst.commandBlock.commands) {
            for (const auto& value : command.fields) maximum = std::max(maximum, value.id.value);
        }
    }
    return SstCommandFieldId{ maximum + 1U };
}

SstPlacementId SstSmlDocument::allocatePlacementId() const noexcept {
    std::uint64_t maximum = 0U;
    for (const auto& member : members) {
        for (const auto& command : member.sst.commandBlock.commands) {
            if (command.placement) maximum = std::max(maximum, command.placement->id.value);
        }
    }
    return SstPlacementId{ maximum + 1U };
}

SstLightingRowId SstSmlDocument::allocateLightingRowId() const noexcept {
    std::uint64_t maximum = 0U;
    for (const auto& member : members) {
        for (const auto& command : member.sst.commandBlock.commands) {
            for (const auto& value : command.lightingRows) maximum = std::max(maximum, value.id.value);
        }
    }
    return SstLightingRowId{ maximum + 1U };
}

SstBattleGridTerrainId SstSmlDocument::allocateBattleGridTerrainId() const noexcept {
    std::uint64_t maximum = 0U;
    for (const auto& member : members) {
        if (member.sst.commandBlock.battleGrid) {
            maximum = std::max(maximum, member.sst.commandBlock.battleGrid->id.value);
        }
    }
    return SstBattleGridTerrainId{ maximum + 1U };
}

SstSmlOpaqueBlockId SstSmlDocument::allocateOpaqueBlockId() const noexcept {
    std::uint64_t maximum = 0U;
    const auto include = [&](const SstSmlOpaqueBlockId id) { maximum = std::max(maximum, id.value); };
    for (const auto& member : members) {
        for (const auto& command : member.sst.commandBlock.commands) {
            for (const auto& value : command.opaquePayloadFragments) include(value.id);
        }
        if (member.sst.commandBlock.trailingOpaque) include(member.sst.commandBlock.trailingOpaque->id);
    }
    for (const auto& item : smlBodyLayout) {
        if (const auto* value = std::get_if<SstSmlOpaqueBlock>(&item)) include(value->id);
    }
    for (const auto& item : sstBodyLayout) {
        if (const auto* value = std::get_if<SstSmlOpaqueBlock>(&item)) include(value->id);
    }
    return SstSmlOpaqueBlockId{ maximum + 1U };
}

} // namespace spice::sstsml
