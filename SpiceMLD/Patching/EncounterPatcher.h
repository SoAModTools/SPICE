#pragma once

#include "TriangleMetadataPatcher.h"

#include <memory>
#include <string>

namespace spice::mld::patching {

struct MldFunctionParameterEdit {
    std::size_t entryTableIndex = 0;
    std::uint32_t expectedEntryId = 0;
    std::int32_t expectedTableId = 0;
    std::string expectedFunctionName{};
    std::uint32_t expectedParameterCount = 0;
    std::size_t parameterIndex = 0;
    std::uint32_t expectedValue = 0;
    std::uint32_t replacementValue = 0;
};

struct MldEncounterPatchRequest {
    // Copy from MldImportReceipt; these identify the encoded source, not decoded bytes.
    std::array<std::uint8_t, 32> sourceSha256{};
    std::uint64_t sourceSize = 0;
    std::vector<TriangleSelectorEdit> triangleEdits{};
    std::vector<MldFunctionParameterEdit> parameterEdits{};
};

class MldEncounterPatchPlan {
public:
    MldEncounterPatchPlan() = default;
    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] const std::vector<model::MldDiagnostic>& diagnostics() const noexcept { return diagnostics_; }

private:
    struct Data;
    std::shared_ptr<const Data> data_{};
    std::vector<model::MldDiagnostic> diagnostics_{};
    friend MldEncounterPatchPlan planEncounterPatches(const model::MldFile&, const MldEncounterPatchRequest&);
    friend MldPatchApplyResult materializeEncounterPatchPlan(std::span<const std::uint8_t>, const MldEncounterPatchPlan&);
};

[[nodiscard]] MldEncounterPatchPlan planEncounterPatches(
    const model::MldFile& originalParsedMld, const MldEncounterPatchRequest& request);
[[nodiscard]] MldPatchApplyResult materializeEncounterPatchPlan(
    std::span<const std::uint8_t> sourceBytes, const MldEncounterPatchPlan& plan);

} // namespace spice::mld::patching
