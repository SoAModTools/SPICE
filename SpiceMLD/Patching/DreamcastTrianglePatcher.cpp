#include "TriangleMetadataPatcher.h"

#include "../../Compression/Aklz.h"

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

MldPatchPlan planTriangleSelectorPatches(
    const model::MldFile& file,
    const std::span<const TriangleSelectorEdit> edits) {
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
        const auto expected = endianBytes(resolved->rawFaceWord, file.endian);
        const auto replacement = endianBytes(replacementWord, file.endian);
        if (!bytesMatch(file.decodedBytes, resolved->flagSourceOffset, expected) ||
            (!file.sourceWasCompressedAklz && !bytesMatch(file.sourceBytes, resolved->flagSourceOffset, expected))) {
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
            .decodedPayloadOffset = resolved->flagSourceOffset,
            .expectedBytes = expected,
            .replacementBytes = replacement,
            .source = edit,
        };
        const auto [existing, patchInserted] = patchesByOffset.emplace(patch.decodedPayloadOffset, patch);
        if (!patchInserted && (existing->second.expectedBytes != patch.expectedBytes ||
            existing->second.replacementBytes != patch.replacementBytes)) {
            addError(result.diagnostics, "Conflicting triangle selector edits target the same source word.",
                diagnosticOffset(patch.decodedPayloadOffset));
        }
    }

    result.patches.reserve(patchesByOffset.size());
    for (auto& [offset, patch] : patchesByOffset) {
        (void)offset;
        result.patches.push_back(std::move(patch));
    }
    return result;
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
        return lhs->decodedPayloadOffset < rhs->decodedPayloadOffset;
    });

    std::optional<std::size_t> previousEnd{};
    for (const auto* patch : ordered) {
        if (patch->decodedPayloadOffset > bytes.size() ||
            patch->expectedBytes.size() > bytes.size() - patch->decodedPayloadOffset) {
            addError(result.diagnostics, "An MLD byte patch is out of bounds.",
                diagnosticOffset(patch->decodedPayloadOffset));
            continue;
        }
        if (previousEnd.has_value() && patch->decodedPayloadOffset < *previousEnd) {
            addError(result.diagnostics, "MLD byte patches overlap.", diagnosticOffset(patch->decodedPayloadOffset));
            continue;
        }
        previousEnd = patch->decodedPayloadOffset + patch->expectedBytes.size();
        if (!bytesMatch(bytes, patch->decodedPayloadOffset, patch->expectedBytes)) {
            addError(result.diagnostics, "The MLD bytes no longer match a patch's expected value.",
                diagnosticOffset(patch->decodedPayloadOffset));
        }
    }
    if (!result.ok()) {
        return result;
    }

    for (const auto* patch : ordered) {
        bytes[patch->decodedPayloadOffset] = patch->replacementBytes[0];
        bytes[patch->decodedPayloadOffset + 1U] = patch->replacementBytes[1];
    }
    result.appliedPatchCount = ordered.size();
    return result;
}

MldPatchApplyResult materializeMldPatchPlan(
    const std::span<const std::uint8_t> sourceBytes,
    const MldPatchPlan& plan) {
    MldPatchApplyResult result{};
    if (!plan.ok()) {
        addError(result.diagnostics, "Cannot materialize an invalid MLD patch plan.");
        return result;
    }
    if (plan.patches.empty()) {
        result.bytes.assign(sourceBytes.begin(), sourceBytes.end());
        return result;
    }

    std::vector<std::uint8_t> decoded{};
    if (plan.sourceWasCompressedAklz) {
        if (!spice::compression::aklz::isAklz(sourceBytes)) {
            addError(result.diagnostics, "The patch plan expects an AKLZ-wrapped source file.");
            return result;
        }
        auto decompressed = spice::compression::aklz::decompress(sourceBytes);
        if (!decompressed.ok()) {
            addError(result.diagnostics, "AKLZ decompression failed while materializing the patch plan.");
            return result;
        }
        decoded = std::move(decompressed.bytes);
    } else {
        if (spice::compression::aklz::isAklz(sourceBytes)) {
            addError(result.diagnostics, "The patch plan expects an uncompressed MLD source file.");
            return result;
        }
        decoded.assign(sourceBytes.begin(), sourceBytes.end());
    }

    const auto applied = applyMldPatchPlan(decoded, plan);
    result.appliedPatchCount = applied.appliedPatchCount;
    result.diagnostics = applied.diagnostics;
    if (!result.ok()) {
        return result;
    }

    if (!plan.sourceWasCompressedAklz) {
        result.bytes = std::move(decoded);
        return result;
    }

    auto compressed = spice::compression::aklz::compress(decoded);
    if (!compressed.ok()) {
        addError(result.diagnostics, "AKLZ compression failed while materializing the patch plan.");
        return result;
    }
    auto verified = spice::compression::aklz::decompress(compressed.bytes);
    if (!verified.ok() || verified.bytes != decoded) {
        addError(result.diagnostics, "AKLZ patch output failed decoded-payload verification.");
        return result;
    }
    result.bytes = std::move(compressed.bytes);
    return result;
}

} // namespace spice::mld::patching
