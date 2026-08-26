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

struct DreamcastTriangleSelectorEdit {
    TriangleResourceKind resourceKind = TriangleResourceKind::Grnd;
    std::uint32_t resourceAddress = 0;
    std::optional<std::size_t> gobjNodeIndex{};
    std::size_t triangleIndex = 0;
    std::uint8_t selectorDigit = 0;
};

struct MldBytePatch {
    std::size_t fileOffset = 0;
    std::array<std::uint8_t, 2> expectedBytes{};
    std::array<std::uint8_t, 2> replacementBytes{};
    DreamcastTriangleSelectorEdit source{};
};

struct MldPatchPlan {
    std::vector<MldBytePatch> patches{};
    std::vector<model::MldDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept;
};

struct MldPatchApplyResult {
    std::size_t appliedPatchCount = 0;
    std::vector<model::MldDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] MldPatchPlan planDreamcastTriangleSelectorPatches(
    const model::MldFile& file,
    std::span<const DreamcastTriangleSelectorEdit> edits);

[[nodiscard]] MldPatchApplyResult applyMldPatchPlan(
    std::span<std::uint8_t> bytes,
    const MldPatchPlan& plan);

} // namespace spice::mld::patching
