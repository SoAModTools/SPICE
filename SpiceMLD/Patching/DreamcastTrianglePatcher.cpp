#include "DreamcastTrianglePatcher.h"

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
    const DreamcastTriangleSelectorEdit& edit,
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

[[nodiscard]] std::array<std::uint8_t, 2> littleEndianBytes(const std::uint16_t value) {
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

MldPatchPlan planDreamcastTriangleSelectorPatches(
    const model::MldFile& file,
    const std::span<const DreamcastTriangleSelectorEdit> edits) {
    MldPatchPlan result{};
    if (file.parseStatus == model::MldParseStatus::Failed) {
        addError(result.diagnostics, "Cannot plan patches for a failed MLD parse.");
    }
    if (file.sourcePlatform != model::TargetPlatform::Dreamcast || file.endian != spice::root::Endian::Little) {
        addError(result.diagnostics, "Dreamcast triangle selector patching requires a little-endian Dreamcast MLD.");
    }
    if (file.sourceWasCompressedAklz) {
        addError(result.diagnostics, "Dreamcast triangle selector patching does not support compressed MLD files.");
    }
    if (file.sourceBytes.empty()) {
        addError(result.diagnostics, "Dreamcast triangle selector patching requires preserved source bytes.");
    }
    if (file.decodedBytes.size() != file.sourceBytes.size()) {
        addError(result.diagnostics, "Dreamcast source and decoded MLD sizes do not match.");
    }
    if (!result.ok()) {
        return result;
    }

    std::map<std::size_t, MldBytePatch> patchesByOffset{};
    std::map<std::uint32_t, bool> unmodifiedResourceByAddress{};
    for (const auto& edit : edits) {
        if (edit.selectorDigit > 9U) {
            addError(result.diagnostics, "A triangle selector digit must be between 0 and 9.", edit.resourceAddress);
            continue;
        }
        const auto found = file.groundResources.find(edit.resourceAddress);
        if (found == file.groundResources.end()) {
            addError(result.diagnostics, "The requested ground resource address was not parsed.", edit.resourceAddress);
            continue;
        }
        const auto& resource = found->second;
        const auto [unmodified, cacheInserted] = unmodifiedResourceByAddress.emplace(edit.resourceAddress, false);
        if (cacheInserted) {
            if (resource.grnd.has_value()) {
                unmodified->second = model::semanticHash(*resource.grnd) == resource.originalSemanticHash;
            } else if (resource.gobj.has_value()) {
                unmodified->second = model::semanticHash(*resource.gobj) == resource.originalSemanticHash;
            }
            if (!unmodified->second) {
                addError(result.diagnostics, "Patch planning requires an unmodified parsed ground-resource model.",
                    resource.sourceAddress);
            }
        }
        if (!unmodified->second) {
            continue;
        }
        const auto resolved = resolveTriangle(resource, edit, result.diagnostics);
        if (!resolved.has_value()) {
            continue;
        }

        const auto rawLow15 = static_cast<std::uint16_t>(resolved->rawFaceWord & 0x7FFFU);
        const auto currentDigit = static_cast<std::uint16_t>((rawLow15 / 10U) % 10U);
        const auto replacementLow = static_cast<std::uint32_t>(rawLow15) - currentDigit * 10U + edit.selectorDigit * 10U;
        if (replacementLow > 0x7FFFU) {
            addError(result.diagnostics, "The requested selector digit would overflow the low 15-bit triangle value.",
                diagnosticOffset(resolved->flagSourceOffset));
            continue;
        }
        const auto replacementWord = static_cast<std::uint16_t>(
            (resolved->rawFaceWord & 0x8000U) | static_cast<std::uint16_t>(replacementLow));
        const auto expected = littleEndianBytes(resolved->rawFaceWord);
        const auto replacement = littleEndianBytes(replacementWord);
        if (!bytesMatch(file.sourceBytes, resolved->flagSourceOffset, expected) ||
            !bytesMatch(file.decodedBytes, resolved->flagSourceOffset, expected)) {
            addError(result.diagnostics, "The triangle source bytes do not match the parsed metadata word.",
                diagnosticOffset(resolved->flagSourceOffset));
            continue;
        }
        if (resolved->flagSourceOffset < resource.sourceAddress) {
            addError(result.diagnostics, "The triangle source offset precedes its resource.",
                diagnosticOffset(resolved->flagSourceOffset));
            continue;
        }
        const auto resourceOffset = resolved->flagSourceOffset - resource.sourceAddress;
        if (!bytesMatch(resource.rawBytes, resourceOffset, expected)) {
            addError(result.diagnostics, "The retained resource bytes do not match the parsed metadata word.",
                diagnosticOffset(resolved->flagSourceOffset));
            continue;
        }
        if (replacementWord == resolved->rawFaceWord) {
            continue;
        }

        MldBytePatch patch{
            .fileOffset = resolved->flagSourceOffset,
            .expectedBytes = expected,
            .replacementBytes = replacement,
            .source = edit,
        };
        const auto [existing, patchInserted] = patchesByOffset.emplace(patch.fileOffset, patch);
        if (!patchInserted && (existing->second.expectedBytes != patch.expectedBytes ||
            existing->second.replacementBytes != patch.replacementBytes)) {
            addError(result.diagnostics, "Conflicting triangle selector edits target the same source word.",
                diagnosticOffset(patch.fileOffset));
        }
    }

    result.patches.reserve(patchesByOffset.size());
    for (auto& [offset, patch] : patchesByOffset) {
        (void)offset;
        result.patches.push_back(std::move(patch));
    }
    return result;
}

MldPatchApplyResult applyMldPatchPlan(
    const std::span<std::uint8_t> bytes,
    const MldPatchPlan& plan) {
    MldPatchApplyResult result{};
    if (!plan.ok()) {
        addError(result.diagnostics, "Cannot apply an invalid MLD patch plan.");
        return result;
    }

    std::vector<const MldBytePatch*> ordered{};
    ordered.reserve(plan.patches.size());
    for (const auto& patch : plan.patches) {
        ordered.push_back(&patch);
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->fileOffset < rhs->fileOffset;
    });

    std::optional<std::size_t> previousEnd{};
    for (const auto* patch : ordered) {
        if (patch->fileOffset > bytes.size() || patch->expectedBytes.size() > bytes.size() - patch->fileOffset) {
            addError(result.diagnostics, "An MLD byte patch is out of bounds.", diagnosticOffset(patch->fileOffset));
            continue;
        }
        if (previousEnd.has_value() && patch->fileOffset < *previousEnd) {
            addError(result.diagnostics, "MLD byte patches overlap.", diagnosticOffset(patch->fileOffset));
            continue;
        }
        previousEnd = patch->fileOffset + patch->expectedBytes.size();
        if (!bytesMatch(bytes, patch->fileOffset, patch->expectedBytes)) {
            addError(result.diagnostics, "The MLD bytes no longer match a patch's expected value.",
                diagnosticOffset(patch->fileOffset));
        }
    }
    if (!result.ok()) {
        return result;
    }

    for (const auto* patch : ordered) {
        bytes[patch->fileOffset] = patch->replacementBytes[0];
        bytes[patch->fileOffset + 1U] = patch->replacementBytes[1];
    }
    result.appliedPatchCount = ordered.size();
    return result;
}

} // namespace spice::mld::patching
