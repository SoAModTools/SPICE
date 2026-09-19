#include "EncounterPatcher.h"
#include "PatchInternals.h"
#include "../Internal/MldSha256.h"
#include "../Parsing/MldParser.h"
#include "../../SpiceRoot/Binary/EndianReader.h"

#include <algorithm>
#include <exception>
#include <limits>

namespace spice::mld::patching {
struct MldEncounterPatchPlan::Data {
    std::array<std::uint8_t, 32> sourceSha256{};
    std::uint64_t sourceSize = 0;
    bool compressedAklz = false;
    std::vector<detail::ByteWrite> writes{};
};

namespace {
using Diagnostics = std::vector<model::MldDiagnostic>;
void error(Diagnostics& diagnostics, std::string message, std::optional<std::size_t> offset = {}) {
    model::MldDiagnostic diagnostic{.severity = model::MldDiagnostic::Severity::Error, .message = std::move(message)};
    if (offset && *offset <= std::numeric_limits<std::uint32_t>::max())
        diagnostic.sourceOffset = static_cast<std::uint32_t>(*offset);
    diagnostics.push_back(std::move(diagnostic));
}
bool hasErrors(const Diagnostics& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& d) {
        return d.severity == model::MldDiagnostic::Severity::Error;
    });
}
bool sameHeader(const model::MldHeader& a, const model::MldHeader& b) {
    return a.entryCount == b.entryCount && a.indexTableOffset == b.indexTableOffset
        && a.functionParametersOffset == b.functionParametersOffset && a.realDataOffset == b.realDataOffset
        && a.textureTableOffset == b.textureTableOffset;
}
const model::MldIndexEntryRecord* entryAt(const model::MldFile& file, std::size_t index) {
    const model::MldIndexEntryRecord* found = nullptr;
    for (const auto& record : file.entries) if (record.entry.tableIndex == index) {
        if (found) return nullptr;
        found = &record;
    }
    return found;
}
std::vector<std::uint8_t> wordBytes(std::uint32_t word, root::Endian endian) {
    std::vector<std::uint8_t> bytes(4);
    for (unsigned i = 0; i < 4; ++i)
        bytes[i] = static_cast<std::uint8_t>(word >> (8U * (endian == root::Endian::Big ? 3U - i : i)));
    return bytes;
}
struct Range { std::size_t begin{}, end{}; std::string label{}; };
bool overlaps(const Range& a, const Range& b) { return a.begin < b.end && b.begin < a.end; }

// Build ranges only from a fresh native parse, never from caller-editable provenance.
bool exclusiveParameterList(const model::MldFile& file, std::size_t entryIndex,
    const model::U32List& target, const std::string& context, Diagnostics& diagnostics) {
    const Range selected{target.pointer, target.pointer + 4U + target.values.size() * 4U, "parameter list"};
    bool valid = true;
    const auto protect = [&](std::size_t offset, std::size_t size, const std::string& label) {
        if (size == 0 || offset > file.decodedBytes.size() || size > file.decodedBytes.size() - offset) {
            error(diagnostics, context + ": Cannot establish bounds for " + label + ".", offset);
            valid = false;
        } else if (overlaps(selected, {offset, offset + size, label})) {
            error(diagnostics, context + ": Parameter list shares or overlaps " + label + ".", target.pointer);
            valid = false;
        }
    };
    protect(0, 0x14, "MLD header");
    protect(file.header.indexTableOffset, file.header.entryCount * std::size_t{0x68}, "entry table");
    for (const auto& record : file.entries) {
        const auto& e = record.entry;
        const std::array lists{e.groundLinks, e.paramList2, e.functionParameters,
            e.objectAddresses, e.groundAddresses, e.motionAddresses};
        constexpr std::array names{"groundLinks", "parameterList2", "functionParameters", "objects", "grounds", "motions"};
        for (std::size_t i = 0; i < lists.size(); ++i) {
            const auto& list = lists[i];
            if (e.tableIndex == entryIndex && i == 2) continue;
            if (list && list->status == model::U32ListStatus::Absent && list->pointer == 0) continue;
            const auto label = "entry[" + std::to_string(e.tableIndex) + "]." + names[i];
            if (!list || !list->valid || list->status != model::U32ListStatus::Present
                || !list->declaredCount || *list->declaredCount != list->values.size()) {
                error(diagnostics, context + ": Cannot establish bounds for " + label + ".");
                valid = false;
                continue;
            }
            protect(list->pointer, 4U + list->values.size() * 4U, label);
        }
        const auto requireResources = [&](const auto& list, const auto& resources, const char* role, bool allowSpatial = false) {
            if (!list) return;
            for (const auto address : list->values) if (address != 0 && !resources.contains(address)
                && !(allowSpatial && file.groundResources.contains(address))) {
                error(diagnostics, context + ": Cannot establish bounds for referenced " + role + " resource.", address);
                valid = false;
            }
        };
        requireResources(e.objectAddresses, file.objectResources, "object", true);
        requireResources(e.groundAddresses, file.groundResources, "ground");
        requireResources(e.motionAddresses, file.motionResources, "motion");
        if (e.texturesPointer != 0 && !file.textureListResources.contains(e.texturesPointer)) {
            error(diagnostics, context + ": Cannot establish bounds for referenced texture list.", e.texturesPointer);
            valid = false;
        }
    }
    for (const auto& [_, r] : file.groundResources) protect(r.sourceAddress, r.blockSize, "ground resource");
    for (const auto& [_, r] : file.objectResources) protect(r.blockOffset, r.blockSize, "object resource");
    for (const auto& [_, r] : file.motionResources) protect(r.blockOffset, r.blockSize, "motion resource");
    // rawDataBlocks may contain a 64-byte inspection prefix, not an allocation
    // range (notably for short texture lists). Use the resolved native ranges.
    for (const auto& [_, r] : file.textureListResources) {
        if (r.wrapperRange) protect(r.wrapperRange->offset, r.wrapperRange->size, "texture-list wrapper");
        protect(r.listRange.offset, r.listRange.size, "texture list");
        for (const auto& entry : r.entries)
            if (entry.nameRange) protect(entry.nameRange->offset, entry.nameRange->size, "texture name");
    }
    if (file.textureArchive) {
        const auto& r = *file.textureArchive;
        protect(r.archiveStartOffset, r.archiveEndOffset - r.archiveStartOffset, "texture archive");
    }
    return valid;
}

void addTriangleWrites(const model::MldFile& original, const model::MldFile& canonical,
    const MldEncounterPatchRequest& request, std::vector<detail::ByteWrite>& writes, Diagnostics& diagnostics) {
    for (std::size_t i = 0; i < request.triangleEdits.size(); ++i) {
        const auto& edit = request.triangleEdits[i];
        const auto context = "triangle[" + std::to_string(i) + "] resource=" + std::to_string(edit.resourceAddress)
            + " node=" + (edit.gobjNodeIndex ? std::to_string(*edit.gobjNodeIndex) : "none")
            + " triangle=" + std::to_string(edit.triangleIndex);
        if (edit.resourceKind != TriangleResourceKind::Grnd && edit.resourceKind != TriangleResourceKind::Gobj) {
            error(diagnostics, context + ": Invalid resource kind.", edit.resourceAddress);
            continue;
        }
        const auto selected = std::span<const TriangleSelectorEdit>(&edit, 1);
        const auto before = detail::planTriangleWords(original, selected, true);
        const auto native = detail::planTriangleWords(canonical, selected, true);
        for (const auto* plan : {&before, &native}) for (auto diagnostic : plan->diagnostics) {
            diagnostic.message = context + ": " + diagnostic.message;
            diagnostics.push_back(std::move(diagnostic));
        }
        if (!before.ok() || !native.ok()) continue;
        const auto& oldResource = original.groundResources.at(edit.resourceAddress);
        const auto& nativeResource = canonical.groundResources.at(edit.resourceAddress);
        if (oldResource.rawBytes != nativeResource.rawBytes || oldResource.blockSize != nativeResource.blockSize
            || oldResource.sourceAddress != nativeResource.sourceAddress || oldResource.kind != nativeResource.kind
            || oldResource.originalSemanticHash != nativeResource.originalSemanticHash) {
            error(diagnostics, context + ": Parsed resource provenance differs from the source.", edit.resourceAddress);
            continue;
        }
        if (before.patches.size() != 1 || native.patches.size() != 1) {
            error(diagnostics, context + ": Triangle did not resolve to one native word.", edit.resourceAddress);
            continue;
        }
        const auto& a = before.patches.front();
        const auto& b = native.patches.front();
        if (a.decodedPayloadOffset != b.decodedPayloadOffset || a.expectedBytes != b.expectedBytes
            || a.replacementBytes != b.replacementBytes) {
            error(diagnostics, context + ": Parsed triangle provenance differs from the source.", a.decodedPayloadOffset);
            continue;
        }
        writes.push_back({b.decodedPayloadOffset, {b.expectedBytes.begin(), b.expectedBytes.end()},
            {b.replacementBytes.begin(), b.replacementBytes.end()}, context});
    }
}

void addParameterWrites(const model::MldFile& original, const model::MldFile& canonical,
    const MldEncounterPatchRequest& request, std::vector<detail::ByteWrite>& writes, Diagnostics& diagnostics) {
    const root::EndianReader reader(canonical.decodedBytes, canonical.endian);
    for (std::size_t i = 0; i < request.parameterEdits.size(); ++i) {
        const auto& edit = request.parameterEdits[i];
        const auto context = "parameter[" + std::to_string(i) + "] entry=" + std::to_string(edit.entryTableIndex)
            + " word=" + std::to_string(edit.parameterIndex);
        const auto* old = entryAt(original, edit.entryTableIndex);
        const auto* native = entryAt(canonical, edit.entryTableIndex);
        if (!old || !native) {
            error(diagnostics, context + ": Entry table index is missing or ambiguous.");
            continue;
        }
        const auto entryOffset = canonical.header.indexTableOffset + edit.entryTableIndex * std::size_t{0x68};
        const auto& a = old->entry;
        const auto& b = native->entry;
        if (a.entryId != b.entryId || a.tblId != b.tblId || a.fxnName != b.fxnName
            || old->rawBytes != native->rawBytes || old->functionParametersPointer != native->functionParametersPointer
            || b.entryId != edit.expectedEntryId || b.tblId != edit.expectedTableId || b.fxnName != edit.expectedFunctionName) {
            error(diagnostics, context + ": Entry identity or native record does not match the source.", entryOffset);
            continue;
        }
        const auto& list = b.functionParameters;
        const auto& oldList = a.functionParameters;
        if (!list || !oldList || !list->valid || list->status != model::U32ListStatus::Present
            || list->pointer == 0 || !list->declaredCount || *list->declaredCount != list->values.size()
            || oldList->pointer != list->pointer || oldList->valid != list->valid || oldList->status != list->status
            || oldList->declaredCount != list->declaredCount || oldList->values != list->values
            || *list->declaredCount != edit.expectedParameterCount || edit.parameterIndex >= list->values.size()) {
            error(diagnostics, context + ": Parameter list pointer, count, index, or parsed values are invalid or stale.",
                native->functionParametersPointer);
            continue;
        }
        const auto offset = std::size_t{list->pointer} + 4U + edit.parameterIndex * 4U;
        if (reader.try_read_u32(list->pointer) != list->declaredCount
            || reader.try_read_u32(offset) != edit.expectedValue || list->values[edit.parameterIndex] != edit.expectedValue) {
            error(diagnostics, context + ": Native count or expected parameter value does not match.", offset);
            continue;
        }
        if (!exclusiveParameterList(canonical, edit.entryTableIndex, *list, context, diagnostics)) continue;
        writes.push_back({offset, wordBytes(edit.expectedValue, canonical.endian),
            wordBytes(edit.replacementValue, canonical.endian), context});
    }
}
}

bool MldEncounterPatchPlan::ok() const noexcept { return data_ && !hasErrors(diagnostics_); }

MldEncounterPatchPlan planEncounterPatches(const model::MldFile& original, const MldEncounterPatchRequest& request) try {
    MldEncounterPatchPlan result;
    auto& diagnostics = result.diagnostics_;
    if (original.sourceBytes.empty() || request.sourceSize != original.sourceBytes.size()
        || request.sourceSha256 != mld::detail::sha256(original.sourceBytes)) {
        error(diagnostics, "Source fingerprint does not match the original encoded MLD.");
        return result;
    }
    // Re-derive native locations to avoid trusting mutable caller-side parser provenance.
    // This is a native parse, not document reconstruction; no importer or writer is used.
    const auto canonical = parsing::MldParser{}.parseBytes(original.sourceBytes);
    if (canonical.parseStatus == model::MldParseStatus::Failed || original.parseStatus == model::MldParseStatus::Failed
        || canonical.parseStatus == model::MldParseStatus::Empty || original.parseStatus == model::MldParseStatus::Empty
        || canonical.entries.size() != canonical.header.entryCount
        || canonical.decodedBytes != original.decodedBytes || !sameHeader(canonical.header, original.header)
        || canonical.endian != original.endian || canonical.sourcePlatform != original.sourcePlatform
        || canonical.sourceWasCompressedAklz != original.sourceWasCompressedAklz
        || canonical.entries.size() != original.entries.size()) {
        error(diagnostics, "Original parsed MLD layout or decoded bytes do not match the fingerprinted source.");
        return result;
    }
    auto data = std::make_shared<MldEncounterPatchPlan::Data>();
    data->sourceSha256 = request.sourceSha256;
    data->sourceSize = request.sourceSize;
    data->compressedAklz = canonical.sourceWasCompressedAklz;
    addTriangleWrites(original, canonical, request, data->writes, diagnostics);
    addParameterWrites(original, canonical, request, data->writes, diagnostics);
    auto& writes = data->writes;
    std::sort(writes.begin(), writes.end(), [](const auto& a, const auto& b) { return a.offset < b.offset; });
    std::vector<detail::ByteWrite> unique;
    for (auto& write : writes) {
        if (!unique.empty()) {
            const auto& previous = unique.back();
            if (write.offset == previous.offset && write.expected == previous.expected && write.replacement == previous.replacement)
                continue;
            if (write.offset < previous.offset + previous.expected.size()) {
                error(diagnostics, write.context + ": Conflicts or overlaps with " + previous.context + ".", write.offset);
                continue;
            }
        }
        unique.push_back(std::move(write));
    }
    if (hasErrors(diagnostics)) return result;
    std::erase_if(unique, [](const auto& write) { return write.expected == write.replacement; });
    data->writes = std::move(unique);
    result.data_ = std::move(data);
    return result;
} catch (const std::exception& exception) {
    MldEncounterPatchPlan result;
    error(result.diagnostics_, "Encounter patch planning failed: " + std::string(exception.what()));
    return result;
}

MldPatchApplyResult materializeEncounterPatchPlan(std::span<const std::uint8_t> sourceBytes,
    const MldEncounterPatchPlan& plan) try {
    MldPatchApplyResult result;
    if (!plan.ok()) {
        result.diagnostics = plan.diagnostics();
        error(result.diagnostics, "Cannot materialize an invalid encounter patch plan.");
        return result;
    }
    const auto& data = *plan.data_;
    if (sourceBytes.size() != data.sourceSize || mld::detail::sha256(sourceBytes) != data.sourceSha256) {
        error(result.diagnostics, "Source fingerprint does not match the encounter patch plan.");
        return result;
    }
    return detail::materializeWrites(sourceBytes, data.compressedAklz, data.writes);
} catch (const std::exception& exception) {
    MldPatchApplyResult result;
    error(result.diagnostics, "Encounter patch materialization failed: " + std::string(exception.what()));
    return result;
}
} // namespace spice::mld::patching
