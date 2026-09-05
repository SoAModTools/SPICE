#include "MldDocumentValidator.h"
#include "Internal/MldDocumentReceiptState.h"

#include <algorithm>
#include <set>
#include <type_traits>

namespace spice::mld {
namespace {

void error(MldDocumentValidationResult& result, std::string message) {
    result.diagnostics.push_back({ MldDiagnosticSeverity::Error, std::move(message) });
}

template <typename Collection>
void validateIds(const Collection& values, const char* label, MldDocumentValidationResult& result) {
    std::set<std::uint64_t> ids{};
    for (const auto& value : values) {
        if (!value.id || !ids.insert(value.id.value).second) {
            error(result, std::string(label) + " contains a zero or duplicate stable ID.");
            return;
        }
    }
}

template <typename Id, typename Collection>
[[nodiscard]] bool contains(const Collection& values, const Id id) {
    return std::any_of(values.begin(), values.end(), [&](const auto& value) { return value.id == id; });
}

} // namespace

MldDocumentValidationResult MldDocumentValidator::validate(
    const MldDocument& document,
    const MldWriteTarget& target,
    const MldImportReceipt* receipt) {
    MldDocumentValidationResult result{};
    if (document.entries.empty()) error(result, "An MLD document must contain at least one index entry.");
    if (target.platform == MldPlatform::Dreamcast && target.wrapper != MldWrapper::Raw) {
        error(result, "Dreamcast MLD output must be raw.");
    }
    validateIds(document.entries, "Entry collection", result);
    validateIds(document.objects, "Object collection", result);
    validateIds(document.motions, "Motion collection", result);
    validateIds(document.grounds, "Ground collection", result);
    validateIds(document.textureLists, "Texture-list collection", result);
    validateIds(document.textureArchives, "Texture-archive collection", result);
    validateIds(document.opaqueMembers, "Opaque-member collection", result);

    for (const auto& entry : document.entries) {
        if (entry.functionName.size() > 20U || std::any_of(entry.functionName.begin(), entry.functionName.end(), [](const unsigned char value) {
                return value < 0x20U || value > 0x7EU;
            })) error(result, "An entry function name is not representable as a 20-byte ASCII field.");
        for (const auto& id : entry.objectSlots) if (id.has_value() && !contains(document.objects, *id))
            error(result, "An entry refers to a missing object resource.");
        for (const auto& id : entry.motionSlots) if (id.has_value() && !contains(document.motions, *id))
            error(result, "An entry refers to a missing motion resource.");
        for (const auto& id : entry.groundSlots) if (id.has_value() && !contains(document.grounds, *id))
            error(result, "An entry refers to a missing ground resource.");
        if (entry.textureList.has_value() && !contains(document.textureLists, *entry.textureList))
            error(result, "An entry refers to a missing texture-list resource.");
    }

    std::set<std::pair<std::size_t, std::uint64_t>> layoutIds{};
    for (const auto& item : document.layout) {
        std::visit([&](const auto id) {
            using Id = std::decay_t<decltype(id)>;
            bool exists = false;
            if constexpr (std::is_same_v<Id, MldEntryId>) exists = contains(document.entries, id);
            else if constexpr (std::is_same_v<Id, MldObjectId>) exists = contains(document.objects, id);
            else if constexpr (std::is_same_v<Id, MldMotionId>) exists = contains(document.motions, id);
            else if constexpr (std::is_same_v<Id, MldGroundId>) exists = contains(document.grounds, id);
            else if constexpr (std::is_same_v<Id, MldTextureListId>) exists = contains(document.textureLists, id);
            else if constexpr (std::is_same_v<Id, MldTextureArchiveId>) exists = contains(document.textureArchives, id);
            else if constexpr (std::is_same_v<Id, MldOpaqueMemberId>) exists = contains(document.opaqueMembers, id);
            if (!exists) error(result, "The top-level layout contains a dangling resource ID.");
            if (!layoutIds.emplace(item.index(), id.value).second)
                error(result, "The top-level layout contains a duplicate resource ID.");
        }, item);
    }
    const auto expectedLayoutSize = document.entries.size() + document.objects.size() + document.motions.size()
        + document.grounds.size() + document.textureLists.size() + document.textureArchives.size()
        + document.opaqueMembers.size();
    if (document.layout.size() != expectedLayoutSize)
        error(result, "The top-level layout must contain every resource exactly once.");

    if (document.hasOpaqueContent() && (receipt == nullptr || !receipt->state_)) {
        error(result, "Writing opaque MLD content requires the matching import receipt.");
    }
    if (document.hasOpaqueContent() && receipt != nullptr && receipt->platform != target.platform) {
        error(result, "Opaque MLD resources cannot be converted across platforms in this release.");
    }
    if (receipt != nullptr && receipt->state_) {
        if (document.entries.size() != receipt->state_->encodingSkeleton.entries.size())
            error(result, "This release cannot add or remove MLD index entries during output.");
        const auto receiptCount = [&](const std::size_t variantIndex) {
            return std::count_if(receipt->layout.begin(), receipt->layout.end(),
                [&](const auto& item) { return item.item.index() == variantIndex; });
        };
        if (document.objects.size() != receiptCount(MldLayoutItem{ MldObjectId{} }.index())
            || document.motions.size() != receiptCount(MldLayoutItem{ MldMotionId{} }.index())
            || document.grounds.size() != receiptCount(MldLayoutItem{ MldGroundId{} }.index()))
            error(result, "This release cannot add or remove encoded MLD resources during output.");
        if (document.textureLists.size() != receipt->state_->encodingSkeleton.textureListResources.size()) {
            error(result, "This release cannot add or remove MLD texture lists during output.");
        }
    }
    result.readiness = result.diagnostics.empty() ? MldWriteReadiness::Ready : MldWriteReadiness::Invalid;
    return result;
}

} // namespace spice::mld
