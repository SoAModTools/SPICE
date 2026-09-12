#pragma once

#include "MldDocumentImporter.h"
#include "MldMotionFrameProjector.h"

namespace spice::mld {

enum class MldSourceBindingStatus { Matched, ReceiptRequired, ReceiptMismatch, MissingEntry, InvalidIdentity, SourceChanged };
enum class MldMotionListStatus { Unqualified, Absent, PresentEmpty, PresentNonempty,
    PointerOutOfBounds, TruncatedCount, ExcessiveCount, TruncatedValues };

struct MldMotionListProjection {
    MldSourceBindingStatus bindingStatus{ MldSourceBindingStatus::ReceiptRequired };
    MldMotionListStatus listStatus{ MldMotionListStatus::Unqualified };
    std::optional<MldEntryId> entryId{};
    std::optional<std::size_t> sourceOrdinal{};
    std::optional<std::uint64_t> sourceEntryOffset{};
    std::optional<std::uint32_t> sourceListPointer{};
    std::optional<std::uint32_t> declaredSlotCount{};
    std::vector<std::uint32_t> sourceSlotReferences{};
    std::array<std::uint8_t, 32U> sourceSha256{};
    std::uint64_t sourceSize{ 0U };
    MldPlatform platform{ MldPlatform::GameCube };
    // Current motion resources; list evidence does not certify edited motions.
    MldMotionFrameProjectionResult motionFrames{};
    [[nodiscard]] bool verifiedPresentEmpty() const noexcept {
        return bindingStatus == MldSourceBindingStatus::Matched && listStatus == MldMotionListStatus::PresentEmpty;
    }
    [[nodiscard]] bool validPresentList() const noexcept {
        return bindingStatus == MldSourceBindingStatus::Matched
            && (listStatus == MldMotionListStatus::PresentEmpty || listStatus == MldMotionListStatus::PresentNonempty);
    }
};

class MldMotionListProjector {
public:
    // Validates entry order/identity and the selected entry against immutable
    // import evidence. No default entry selection; negative ordinals fail.
    [[nodiscard]] static MldMotionListProjection selectImportedEntry(const MldDocument& document,
        std::int64_t ordinal, const MldImportReceipt& receipt);
    [[nodiscard]] static MldMotionListProjection project(const MldDocument& document,
        MldEntryId entry, const MldImportReceipt& receipt);
};

} // namespace spice::mld
