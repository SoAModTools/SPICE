#pragma once

#include "../Model/MldFile.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace spice::mld::patching {

enum class TriangleResourceKind {
    Grnd,
    Gobj,
};

struct TriangleSelectorEdit {
    TriangleResourceKind resourceKind = TriangleResourceKind::Grnd;
    std::uint32_t resourceAddress = 0;
    std::optional<std::size_t> gobjNodeIndex{};
    std::size_t triangleIndex = 0;
    std::uint8_t selectorDigit = 0;
};

using DreamcastTriangleSelectorEdit = TriangleSelectorEdit;

struct MldBytePatch {
    std::size_t decodedPayloadOffset = 0;
    std::array<std::uint8_t, 2> expectedBytes{};
    std::array<std::uint8_t, 2> replacementBytes{};
    TriangleSelectorEdit source{};
};

struct MldPatchPlan {
    spice::root::Endian endian = spice::root::Endian::Big;
    bool sourceWasCompressedAklz = false;
    std::vector<MldBytePatch> patches{};
    std::vector<model::MldDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept;
};

struct MldPatchApplyResult {
    std::size_t appliedPatchCount = 0;
    std::vector<std::uint8_t> bytes{};
    std::vector<model::MldDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] MldPatchPlan planTriangleSelectorPatches(
    const model::MldFile& file,
    std::span<const TriangleSelectorEdit> edits);

[[nodiscard]] MldPatchPlan planDreamcastTriangleSelectorPatches(
    const model::MldFile& file,
    std::span<const DreamcastTriangleSelectorEdit> edits);

[[nodiscard]] MldPatchApplyResult applyMldPatchPlan(
    std::span<std::uint8_t> bytes,
    const MldPatchPlan& plan);

[[nodiscard]] MldPatchApplyResult materializeMldPatchPlan(
    std::span<const std::uint8_t> sourceBytes,
    const MldPatchPlan& plan);

} // namespace spice::mld::patching
