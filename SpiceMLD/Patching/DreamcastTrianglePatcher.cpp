#include "TriangleMetadataPatcher.h"
#include "PatchInternals.h"

#include <algorithm>
#include <limits>
#include <map>
#include <string>
#include <utility>

namespace spice::mld::patching {
namespace {

[[nodiscard]] bool hasErrors(const std::vector<model::MldDiagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity == model::MldDiagnostic::Severity::Error;
    });
}

[[nodiscard]] std::optional<std::uint32_t> diagnosticOffset(const std::size_t offset) {
    if (offset > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(offset);
}

void addError(
    std::vector<model::MldDiagnostic>& diagnostics,
    std::string message,
    const std::optional<std::uint32_t> sourceOffset = std::nullopt) {
    diagnostics.push_back(model::MldDiagnostic{
        .severity = model::MldDiagnostic::Severity::Error,
        .message = std::move(message),
        .sourceOffset = sourceOffset,
    });
}

struct ResolvedTriangle {
    std::uint16_t rawFaceWord = 0;
    std::size_t flagSourceOffset = 0;
};

[[nodiscard]] std::optional<ResolvedTriangle> resolveTriangle(
    const model::MldGroundResource& resource,
    const TriangleSelectorEdit& edit,
    std::vector<model::MldDiagnostic>& diagnostics) {
    if (edit.resourceKind == TriangleResourceKind::Grnd) {
        if (edit.gobjNodeIndex.has_value()) {
            addError(diagnostics, "A GRND selector edit must not specify a GOBJ node index.", resource.sourceAddress);
            return std::nullopt;
        }
        if (resource.kind != model::MldGroundResource::Kind::Grnd || !resource.grnd.has_value()) {
            addError(diagnostics, "The requested resource is not a decoded GRND resource.", resource.sourceAddress);
            return std::nullopt;
        }
        const auto& grnd = *resource.grnd;
        if (edit.triangleIndex >= grnd.mesh.triangleMetadata.size()) {
            addError(diagnostics, "The requested GRND triangle index is out of bounds.", resource.sourceAddress);
            return std::nullopt;
        }
        if (edit.triangleIndex >= grnd.triangleSources.size()) {
            addError(diagnostics, "The requested GRND triangle has no source provenance.", resource.sourceAddress);
            return std::nullopt;
        }
        return ResolvedTriangle{
            .rawFaceWord = grnd.mesh.triangleMetadata[edit.triangleIndex].rawU16[2],
            .flagSourceOffset = grnd.triangleSources[edit.triangleIndex].flagSourceOffsets[2],
        };
    }

    if (!edit.gobjNodeIndex.has_value()) {
        addError(diagnostics, "A GOBJ selector edit requires a node index.", resource.sourceAddress);
        return std::nullopt;
    }
    if (resource.kind != model::MldGroundResource::Kind::Gobj || !resource.gobj.has_value()) {
        addError(diagnostics, "The requested resource is not a decoded GOBJ resource.", resource.sourceAddress);
        return std::nullopt;
    }
    const auto& gobj = *resource.gobj;
    if (*edit.gobjNodeIndex >= gobj.nodes.size()) {
        addError(diagnostics, "The requested GOBJ node index is out of bounds.", resource.sourceAddress);
        return std::nullopt;
    }
    const auto& node = gobj.nodes[*edit.gobjNodeIndex];
    if (edit.triangleIndex >= node.streamMesh.triangleMetadata.size()) {
        addError(diagnostics, "The requested GOBJ triangle index is out of bounds.", resource.sourceAddress);
        return std::nullopt;
    }
    if (edit.triangleIndex >= node.streamTriangleSources.size()) {
        addError(diagnostics, "The requested GOBJ triangle has no source provenance.", resource.sourceAddress);
        return std::nullopt;
    }
    return ResolvedTriangle{
        .rawFaceWord = node.streamMesh.triangleMetadata[edit.triangleIndex].rawU16[2],
        .flagSourceOffset = node.streamTriangleSources[edit.triangleIndex].flagSourceOffsets[2],
    };
}

[[nodiscard]] std::array<std::uint8_t, 2> endianBytes(
    const std::uint16_t value,
    const spice::root::Endian endian) {
    if (endian == spice::root::Endian::Big) {
        return {
            static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
            static_cast<std::uint8_t>(value & 0xFFU),
        };
    }
    return {
        static_cast<std::uint8_t>(value & 0xFFU),
        static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
    };
}

[[nodiscard]] bool bytesMatch(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset,
    const std::array<std::uint8_t, 2>& expected) {
    return offset <= bytes.size() && expected.size() <= bytes.size() - offset &&
        bytes[offset] == expected[0] && bytes[offset + 1U] == expected[1];
}

} // namespace

bool MldPatchPlan::ok() const noexcept {
    return !hasErrors(diagnostics);
}

bool MldPatchApplyResult::ok() const noexcept {
    return !hasErrors(diagnostics);
}

MldPatchPlan detail::planTriangleWords(
    const model::MldFile& file,
    const std::span<const TriangleSelectorEdit> edits, const bool retainNoOps) {
    MldPatchPlan result{};
    result.endian = file.endian;
    result.sourceWasCompressedAklz = file.sourceWasCompressedAklz;
    if (file.parseStatus == model::MldParseStatus::Failed) {
        addError(result.diagnostics, "Cannot plan patches for a failed MLD parse.");
    }
    const bool validPlatformEndian =
        (file.sourcePlatform == model::TargetPlatform::Dreamcast && file.endian == spice::root::Endian::Little) ||
        (file.sourcePlatform == model::TargetPlatform::GameCube && file.endian == spice::root::Endian::Big);
    if (!validPlatformEndian) {
        addError(result.diagnostics, "Triangle selector patching requires a recognized MLD platform and matching endian.");
    }
    if (file.sourceWasCompressedAklz && file.sourcePlatform != model::TargetPlatform::GameCube) {
        addError(result.diagnostics, "AKLZ-wrapped triangle selector patching is supported only for GameCube MLD files.");
    }
    if (file.sourceBytes.empty()) {
        addError(result.diagnostics, "Triangle selector patching requires preserved source bytes.");
    }
    if (file.decodedBytes.empty()) {
        addError(result.diagnostics, "Triangle selector patching requires preserved decoded MLD bytes.");
    }
    if (!file.sourceWasCompressedAklz && file.decodedBytes.size() != file.sourceBytes.size()) {
        addError(result.diagnostics, "Uncompressed source and decoded MLD sizes do not match.");
    }
    if (!result.ok()) {
        return result;
    }

    std::map<std::size_t, MldBytePatch> patchesByOffset{};
    for (std::size_t editIndex = 0; editIndex < edits.size(); ++editIndex) {
        const auto& edit = edits[editIndex];
        const auto context = "triangle[" + std::to_string(editIndex) + "] resource=" + std::to_string(edit.resourceAddress)
            + " node=" + (edit.gobjNodeIndex ? std::to_string(*edit.gobjNodeIndex) : "none")
            + " triangle=" + std::to_string(edit.triangleIndex) + ": ";
        const auto fail = [&](std::string message, std::optional<std::uint32_t> offset = {}) {
            addError(result.diagnostics, context + message, offset);
        };
        if (edit.resourceKind != TriangleResourceKind::Grnd && edit.resourceKind != TriangleResourceKind::Gobj) {
            fail("Invalid resource kind.", edit.resourceAddress);
            continue;
        }
        if (edit.selectorDigit > 9U) {
            fail("A triangle selector digit must be between 0 and 9.", edit.resourceAddress);
            continue;
        }
        const auto found = file.groundResources.find(edit.resourceAddress);
        if (found == file.groundResources.end()) {
            fail("The requested ground resource address was not parsed.", edit.resourceAddress);
            continue;
        }
        const auto& resource = found->second;
        const auto diagnosticStart = result.diagnostics.size();
        const auto resolved = resolveTriangle(resource, edit, result.diagnostics);
        for (std::size_t i = diagnosticStart; i < result.diagnostics.size(); ++i)
            result.diagnostics[i].message = context + result.diagnostics[i].message;
        if (!resolved.has_value()) {
            continue;
        }

        const auto rawLow15 = static_cast<std::uint16_t>(resolved->rawFaceWord & 0x7FFFU);
        const auto currentDigit = static_cast<std::uint16_t>((rawLow15 / 10U) % 10U);
        const auto replacementLow = static_cast<std::uint32_t>(rawLow15) - currentDigit * 10U + edit.selectorDigit * 10U;
        if (replacementLow > 0x7FFFU) {
            fail("The requested selector digit would overflow the low 15-bit triangle value.",
                diagnosticOffset(resolved->flagSourceOffset));
            continue;
        }
        const auto replacementWord = static_cast<std::uint16_t>(
            (resolved->rawFaceWord & 0x8000U) | static_cast<std::uint16_t>(replacementLow));
        const auto expected = endianBytes(resolved->rawFaceWord, file.endian);
        const auto replacement = endianBytes(replacementWord, file.endian);
        if (!bytesMatch(file.decodedBytes, resolved->flagSourceOffset, expected) ||
            (!file.sourceWasCompressedAklz && !bytesMatch(file.sourceBytes, resolved->flagSourceOffset, expected))) {
            fail("The triangle source bytes do not match the parsed metadata word.",
                diagnosticOffset(resolved->flagSourceOffset));
            continue;
        }
        if (resolved->flagSourceOffset < resource.sourceAddress) {
            fail("The triangle source offset precedes its resource.",
                diagnosticOffset(resolved->flagSourceOffset));
            continue;
        }
        const auto resourceOffset = resolved->flagSourceOffset - resource.sourceAddress;
        if (!bytesMatch(resource.rawBytes, resourceOffset, expected)) {
            fail("The retained resource bytes do not match the parsed metadata word.",
                diagnosticOffset(resolved->flagSourceOffset));
            continue;
        }

        MldBytePatch patch{
            .decodedPayloadOffset = resolved->flagSourceOffset,
            .expectedBytes = expected,
            .replacementBytes = replacement,
            .source = edit,
        };
        const auto [existing, patchInserted] = patchesByOffset.emplace(patch.decodedPayloadOffset, patch);
        if (!patchInserted && (existing->second.expectedBytes != patch.expectedBytes ||
            existing->second.replacementBytes != patch.replacementBytes)) {
            fail("Conflicting triangle selector edits target the same source word.",
                diagnosticOffset(patch.decodedPayloadOffset));
        }
    }

    result.patches.reserve(patchesByOffset.size());
    for (auto& [offset, patch] : patchesByOffset) {
        (void)offset;
        if (retainNoOps || patch.expectedBytes != patch.replacementBytes)
            result.patches.push_back(std::move(patch));
    }
    return result;
}

MldPatchPlan planTriangleSelectorPatches(
    const model::MldFile& file, const std::span<const TriangleSelectorEdit> edits) {
    return detail::planTriangleWords(file, edits, false);
}

MldPatchPlan planDreamcastTriangleSelectorPatches(
    const model::MldFile& file,
    const std::span<const DreamcastTriangleSelectorEdit> edits) {
    auto result = planTriangleSelectorPatches(file, edits);
    if (file.sourcePlatform != model::TargetPlatform::Dreamcast || file.endian != spice::root::Endian::Little ||
        file.sourceWasCompressedAklz) {
        result.patches.clear();
        addError(result.diagnostics, "Dreamcast triangle selector patching requires an uncompressed little-endian Dreamcast MLD.");
    }
    return result;
}

namespace {
std::vector<detail::ByteWrite> byteWrites(const MldPatchPlan& plan) {
    std::vector<detail::ByteWrite> writes;
    for (const auto& patch : plan.patches)
        writes.push_back({patch.decodedPayloadOffset,
            {patch.expectedBytes.begin(), patch.expectedBytes.end()},
            {patch.replacementBytes.begin(), patch.replacementBytes.end()}, "triangle"});
    return writes;
}
}

MldPatchApplyResult applyMldPatchPlan(std::span<std::uint8_t> bytes, const MldPatchPlan& plan) {
    if (!plan.ok()) {
        MldPatchApplyResult result;
        addError(result.diagnostics, "Cannot apply an invalid MLD patch plan.");
        return result;
    }
    return detail::applyWrites(bytes, byteWrites(plan));
}

MldPatchApplyResult materializeMldPatchPlan(std::span<const std::uint8_t> sourceBytes, const MldPatchPlan& plan) {
    if (!plan.ok()) {
        MldPatchApplyResult result;
        addError(result.diagnostics, "Cannot materialize an invalid MLD patch plan.");
        return result;
    }
    return detail::materializeWrites(sourceBytes, plan.sourceWasCompressedAklz, byteWrites(plan));
}

} // namespace spice::mld::patching
