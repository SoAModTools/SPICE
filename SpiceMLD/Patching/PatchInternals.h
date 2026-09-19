#pragma once

#include "TriangleMetadataPatcher.h"
#include <string>

namespace spice::mld::patching::detail {

struct ByteWrite {
    std::size_t offset = 0;
    std::vector<std::uint8_t> expected{}, replacement{};
    std::string context{};
};

// Retain no-ops until the encounter planner has checked all mixed-edit conflicts.
[[nodiscard]] MldPatchPlan planTriangleWords(const model::MldFile& file,
    std::span<const TriangleSelectorEdit> edits, bool retainNoOps);
[[nodiscard]] MldPatchApplyResult applyWrites(std::span<std::uint8_t> bytes, std::span<const ByteWrite> writes);
[[nodiscard]] MldPatchApplyResult materializeWrites(std::span<const std::uint8_t> source,
    bool compressedAklz, std::span<const ByteWrite> writes);

} // namespace spice::mld::patching::detail
