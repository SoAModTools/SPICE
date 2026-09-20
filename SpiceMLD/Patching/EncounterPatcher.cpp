#include "EncounterPatcher.h"
#include "PatchInternals.h"
#include "../Internal/MldSha256.h"
#include "../../SpiceRoot/Binary/EndianReader.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <map>

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
std::vector<std::uint8_t> wordBytes(std::uint32_t word, root::Endian endian) {
    std::vector<std::uint8_t> bytes(4);
    for (unsigned i = 0; i < 4; ++i)
        bytes[i] = static_cast<std::uint8_t>(word >> (8U * (endian == root::Endian::Big ? 3U - i : i)));
    return bytes;
}
struct Range { std::size_t begin{}, end{}; std::string label{}; };
bool overlaps(const Range& a, const Range& b) { return a.begin < b.end && b.begin < a.end; }

// The caller supplies the unchanged original parse. Check ownership once per affected list.
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

void addTriangleWrites(const model::MldFile& original, const MldEncounterPatchRequest& request,
    std::vector<detail::ByteWrite>& writes, Diagnostics& diagnostics) {
    const auto triangles = detail::planTriangleWords(original, request.triangleEdits, true);
    diagnostics.insert(diagnostics.end(), triangles.diagnostics.begin(), triangles.diagnostics.end());
    for (const auto& patch : triangles.patches)
        writes.push_back({patch.decodedPayloadOffset, {patch.expectedBytes.begin(), patch.expectedBytes.end()},
            {patch.replacementBytes.begin(), patch.replacementBytes.end()},
            "triangle resource=" + std::to_string(patch.source.resourceAddress)
                + " triangle=" + std::to_string(patch.source.triangleIndex)});
}

void addParameterWrites(const model::MldFile& original, const MldEncounterPatchRequest& request,
    std::vector<detail::ByteWrite>& writes, Diagnostics& diagnostics) {
    if (request.parameterEdits.empty()) return;
    const root::EndianReader reader(original.decodedBytes, original.endian);
    std::map<std::size_t, const model::MldIndexEntryRecord*> entries;
    for (const auto& record : original.entries) {
        const auto [it, inserted] = entries.emplace(record.entry.tableIndex, &record);
        if (!inserted) it->second = nullptr;
    }
    std::map<std::uint32_t, bool> ownershipByPointer;
    for (std::size_t i = 0; i < request.parameterEdits.size(); ++i) {
        const auto& edit = request.parameterEdits[i];
        const auto context = "parameter[" + std::to_string(i) + "] entry=" + std::to_string(edit.entryTableIndex)
            + " word=" + std::to_string(edit.parameterIndex);
        const auto found = entries.find(edit.entryTableIndex);
        if (found == entries.end() || !found->second || edit.entryTableIndex >= original.header.entryCount) {
            error(diagnostics, context + ": Entry table index is missing or ambiguous.");
            continue;
        }
        const auto& record = *found->second;
        const auto& entry = record.entry;
        const auto entryOffset = original.header.indexTableOffset + edit.entryTableIndex * std::size_t{0x68};
        if (entry.entryId != edit.expectedEntryId || entry.tblId != edit.expectedTableId
            || entry.fxnName != edit.expectedFunctionName) {
            error(diagnostics, context + ": Entry identity does not match the original parse.", entryOffset);
            continue;
        }
        const auto& list = entry.functionParameters;
        if (!list || !list->valid || list->status != model::U32ListStatus::Present
            || list->pointer == 0 || record.functionParametersPointer != list->pointer
            || !list->declaredCount || *list->declaredCount != list->values.size()
            || *list->declaredCount != edit.expectedParameterCount || edit.parameterIndex >= list->values.size()
            || list->pointer > original.decodedBytes.size() || original.decodedBytes.size() - list->pointer < 4U
            || list->values.size() > (original.decodedBytes.size() - list->pointer - 4U) / 4U) {
            error(diagnostics, context + ": Parameter list pointer, count, or index is invalid.", record.functionParametersPointer);
            continue;
        }
        const auto offset = std::size_t{list->pointer} + 4U + edit.parameterIndex * 4U;
        if (reader.try_read_u32(list->pointer) != list->declaredCount
            || reader.try_read_u32(offset) != edit.expectedValue || list->values[edit.parameterIndex] != edit.expectedValue) {
            error(diagnostics, context + ": Native count or expected parameter value does not match.", offset);
            continue;
        }
        const auto [ownership, inserted] = ownershipByPointer.emplace(list->pointer, false);
        if (inserted) ownership->second = exclusiveParameterList(original, edit.entryTableIndex, *list, context, diagnostics);
        if (!ownership->second) continue;
        writes.push_back({offset, wordBytes(edit.expectedValue, original.endian),
            wordBytes(edit.replacementValue, original.endian), context});
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
    // Input contract: original is the unchanged SPICE parse of the fingerprinted
    // source. Validate usable structure and requested writes, not caller history.
    const bool validPlatformEndian =
        (original.sourcePlatform == model::TargetPlatform::Dreamcast && original.endian == root::Endian::Little)
        || (original.sourcePlatform == model::TargetPlatform::GameCube && original.endian == root::Endian::Big);
    if (original.parseStatus == model::MldParseStatus::Failed || original.parseStatus == model::MldParseStatus::Empty
        || !validPlatformEndian || original.decodedBytes.size() < 0x14U
        || original.header.entryCount != original.entries.size()
        || original.header.indexTableOffset > original.decodedBytes.size()
        || original.entries.size() > (original.decodedBytes.size() - original.header.indexTableOffset) / 0x68U) {
        error(diagnostics, "The original parsed MLD has an unsupported or invalid layout.");
        return result;
    }
    auto data = std::make_shared<MldEncounterPatchPlan::Data>();
    data->sourceSha256 = request.sourceSha256;
    data->sourceSize = request.sourceSize;
    data->compressedAklz = original.sourceWasCompressedAklz;
    addTriangleWrites(original, request, data->writes, diagnostics);
    addParameterWrites(original, request, data->writes, diagnostics);
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
