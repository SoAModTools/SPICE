#pragma once

#include "MldDocument.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace spice::mld {

enum class MldMotionFrameSlotStatus {
    Resolved,
    EmptySlot,
    MissingMotion,
    OpaqueMotion,
    NoDecodedVariants,
    ConflictingDeclaredFrameCounts,
};

struct MldMotionFrameVariantProjection {
    MldMotionVariantId variantId{};
    std::uint32_t declaredFrameCount{ 0U };
};

struct MldMotionFrameSlotProjection {
    std::size_t slotOrdinal{ 0U };
    std::optional<MldMotionId> motionId{};
    modeling::MotionKind kind{ modeling::MotionKind::Unknown };
    std::vector<MldMotionFrameVariantProjection> variants{};
    std::optional<std::uint32_t> agreedDeclaredFrameCount{};
    MldMotionFrameSlotStatus status{ MldMotionFrameSlotStatus::EmptySlot };
};

struct MldMotionFrameProjectionResult {
    std::optional<MldEntryId> entryId{};
    std::vector<MldMotionFrameSlotProjection> slots{};
    [[nodiscard]] bool ok() const noexcept;
};

class MldMotionFrameProjector {
public:
    [[nodiscard]] static MldMotionFrameProjectionResult project(
        const MldDocument& document, MldEntryId entryId);
};

[[nodiscard]] const char* toString(MldMotionFrameSlotStatus status) noexcept;

} // namespace spice::mld
