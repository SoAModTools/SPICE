#include "GrndParser.h"

#include "../../SpiceCore/Binary/EndianReader.h"
#include "../common/ByteUtils.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace spice::mld::parsing {
namespace {

constexpr std::uint32_t kGrndTag = 0x47524E44U; // "GRND"

struct TriangleSet {
    std::size_t headerOffset = 0;
    std::size_t vertexBlockOffset = 0;
    std::size_t triangleStreamOffset = 0;
    std::size_t streamEntryCount = 0;
    std::uint32_t declaredTriangleCount = 0;
};

struct StreamEntry {
    std::uint16_t floatIndex = 0;
    std::uint16_t flags = 0;
};

struct TriangleRef {
    std::uint16_t triangleSet = 0;
    std::uint16_t triangleIndex = 0;

    [[nodiscard]] bool operator==(const TriangleRef&) const = default;
};

struct TriangleRefHash {
    [[nodiscard]] std::size_t operator()(const TriangleRef& ref) const noexcept {
        return (static_cast<std::size_t>(ref.triangleSet) << 16U) ^ ref.triangleIndex;
    }
};

[[nodiscard]] std::uint32_t readTag(std::span<const std::uint8_t> bytes, const std::size_t offset) {
    if (offset + 4U > bytes.size()) {
        return 0U;
    }
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
        static_cast<std::uint32_t>(bytes[offset + 3U]);
}

[[nodiscard]] std::optional<std::size_t> addRelativeOffset(
    const std::size_t base,
    const std::int32_t relative,
    const std::size_t size) {
    const auto target = static_cast<std::int64_t>(base) + static_cast<std::int64_t>(relative);
    if (target < 0 || static_cast<std::uint64_t>(target) > static_cast<std::uint64_t>(size)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(target);
}

[[nodiscard]] std::string hexOffset(const std::size_t value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result = "0x00000000";
    auto v = static_cast<std::uint64_t>(value);
    for (int i = 9; i >= 2; --i) {
        result[static_cast<std::size_t>(i)] = digits[v & 0xFU];
        v >>= 4U;
    }
    return result;
}

[[nodiscard]] std::optional<TriangleSet> readTriangleSet(
    std::span<const std::uint8_t> bytes,
    spice::core::Endian endian,
    const std::size_t setOffset,
    const std::size_t nextSetOrBlockEnd,
    std::vector<std::string>& diagnostics) {
    if (setOffset + 0x18U > bytes.size()) {
        diagnostics.push_back("GRND triangle set header out of bounds at " + hexOffset(setOffset) + ".");
        return std::nullopt;
    }

    const spice::core::EndianReader reader(bytes, endian);
    const auto vertexRel = reader.try_read_i32(setOffset + 0x0CU);
    const auto streamRel = reader.try_read_i32(setOffset + 0x10U);
    const auto triangleCount = reader.try_read_u32(setOffset + 0x14U);
    if (!vertexRel.has_value() || !streamRel.has_value() || !triangleCount.has_value()) {
        diagnostics.push_back("GRND triangle set header is truncated at " + hexOffset(setOffset) + ".");
        return std::nullopt;
    }

    const auto vertexBlockOffset = addRelativeOffset(setOffset + 0x0CU, *vertexRel, bytes.size());
    const auto triangleStreamOffset = addRelativeOffset(setOffset + 0x10U, *streamRel, bytes.size());
    if (!vertexBlockOffset.has_value() || !triangleStreamOffset.has_value()) {
        diagnostics.push_back("GRND triangle set has an out-of-bounds relative pointer at " + hexOffset(setOffset) + ".");
        return std::nullopt;
    }

    TriangleSet out{};
    out.headerOffset = setOffset;
    out.vertexBlockOffset = *vertexBlockOffset;
    out.triangleStreamOffset = *triangleStreamOffset;
    out.declaredTriangleCount = *triangleCount;

    if (out.vertexBlockOffset > out.triangleStreamOffset) {
        out.streamEntryCount = (out.vertexBlockOffset - out.triangleStreamOffset) / 4U;
    } else if (out.declaredTriangleCount == 0U && nextSetOrBlockEnd >= out.triangleStreamOffset) {
        out.streamEntryCount = (nextSetOrBlockEnd - out.triangleStreamOffset) / 4U;
    }

    return out;
}

[[nodiscard]] std::optional<StreamEntry> readStreamEntry(
    std::span<const std::uint8_t> bytes,
    spice::core::Endian endian,
    const TriangleSet& set,
    const std::size_t index) {
    if (index >= set.streamEntryCount) {
        return std::nullopt;
    }
    const auto offset = set.triangleStreamOffset + (index * 4U);
    const spice::core::EndianReader reader(bytes, endian);
    const auto floatIndex = reader.try_read_u16(offset);
    const auto flags = reader.try_read_u16(offset + 2U);
    if (!floatIndex.has_value() || !flags.has_value()) {
        return std::nullopt;
    }
    return StreamEntry{
        .floatIndex = *floatIndex,
        .flags = *flags,
    };
}

[[nodiscard]] std::optional<model::MeshVertex> readVertexForFloatIndex(
    std::span<const std::uint8_t> bytes,
    spice::core::Endian endian,
    const TriangleSet& set,
    const std::uint16_t floatIndex) {
    const std::size_t offset = set.vertexBlockOffset + (static_cast<std::size_t>(floatIndex) * 4U);
    if (offset + 24U > bytes.size()) {
        return std::nullopt;
    }

    const spice::core::EndianReader reader(bytes, endian);
    const auto px = reader.try_read_f32(offset);
    const auto py = reader.try_read_f32(offset + 4U);
    const auto pz = reader.try_read_f32(offset + 8U);
    const auto nx = reader.try_read_f32(offset + 12U);
    const auto ny = reader.try_read_f32(offset + 16U);
    const auto nz = reader.try_read_f32(offset + 20U);
    if (!px.has_value() || !py.has_value() || !pz.has_value() ||
        !nx.has_value() || !ny.has_value() || !nz.has_value()) {
        return std::nullopt;
    }

    model::MeshVertex out{};
    out.position = model::Vec3{ *px, *py, *pz };
    out.normal = model::Vec3{ *nx, *ny, *nz };
    return out;
}

[[nodiscard]] std::uint32_t getOrCreateVertex(
    std::span<const std::uint8_t> bytes,
    spice::core::Endian endian,
    const TriangleSet& set,
    const std::uint16_t floatIndex,
    std::unordered_map<std::uint64_t, std::uint32_t>& vertexIndexByKey,
    model::MeshData& mesh,
    std::size_t& skippedReferenceCount) {
    const std::uint64_t key = (static_cast<std::uint64_t>(set.headerOffset) << 16U) ^ floatIndex;
    if (const auto found = vertexIndexByKey.find(key); found != vertexIndexByKey.end()) {
        return found->second;
    }

    const auto vertex = readVertexForFloatIndex(bytes, endian, set, floatIndex);
    if (!vertex.has_value()) {
        ++skippedReferenceCount;
        return std::numeric_limits<std::uint32_t>::max();
    }

    const auto index = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(*vertex);
    vertexIndexByKey.emplace(key, index);
    return index;
}

} // namespace

GrndDecodeResult GrndParser::decode(std::span<const std::uint8_t> blockBytes, const std::uint32_t sourceOffset, const spice::core::Endian endian) const {
    GrndDecodeResult result{};
    if (blockBytes.size() < 0x2CU) {
        result.diagnostics.push_back("GRND block at " + hexOffset(sourceOffset) + " is too small.");
        return result;
    }

    if (readTag(blockBytes, 0U) != kGrndTag) {
        result.diagnostics.push_back("GRND block at " + hexOffset(sourceOffset) + " does not start with GRND magic.");
        return result;
    }

    const spice::core::EndianReader blockReader(blockBytes, endian);
    const auto declaredSize = blockReader.try_read_u32(4U);
    if (!declaredSize.has_value() || *declaredSize < 0x2CU || *declaredSize > blockBytes.size()) {
        result.diagnostics.push_back("GRND block at " + hexOffset(sourceOffset) + " has an invalid declared size.");
        return result;
    }

    const auto bytes = blockBytes.first(static_cast<std::size_t>(*declaredSize));
    constexpr std::size_t innerHeader = 0x10U;
    const spice::core::EndianReader reader(bytes, endian);
    const auto relTriangleSets = reader.try_read_i32(innerHeader);
    const auto relQuadRegistry = reader.try_read_i32(innerHeader + 4U);
    const auto gridX = reader.try_read_u16(innerHeader + 0x10U);
    const auto gridZ = reader.try_read_u16(innerHeader + 0x12U);
    const auto cellSizeX = reader.try_read_u16(innerHeader + 0x14U);
    const auto cellSizeZ = reader.try_read_u16(innerHeader + 0x16U);
    const auto triangleSetCount = reader.try_read_u16(innerHeader + 0x18U);
    const auto quadCellCount = reader.try_read_u16(innerHeader + 0x1AU);

    if (!relTriangleSets.has_value() || !relQuadRegistry.has_value() ||
        !gridX.has_value() || !gridZ.has_value() || !cellSizeX.has_value() || !cellSizeZ.has_value() ||
        !triangleSetCount.has_value() || !quadCellCount.has_value()) {
        result.diagnostics.push_back("GRND block at " + hexOffset(sourceOffset) + " has a truncated inner header.");
        return result;
    }

    result.gridX = *gridX;
    result.gridZ = *gridZ;
    result.cellSizeX = *cellSizeX;
    result.cellSizeZ = *cellSizeZ;
    result.triangleSetCount = *triangleSetCount;
    result.quadCellCount = *quadCellCount;
    result.data.outerHeaderBytes.assign(bytes.begin(), bytes.begin() + 0x10U);
    result.data.innerHeaderUnknownBytes.assign(bytes.begin() + 0x18U, bytes.begin() + 0x20U);
    result.data.gridX = *gridX;
    result.data.gridZ = *gridZ;
    result.data.cellSizeX = *cellSizeX;
    result.data.cellSizeZ = *cellSizeZ;

    const auto triangleSetsOffset = addRelativeOffset(innerHeader, *relTriangleSets, bytes.size());
    const auto quadRegistryOffset = addRelativeOffset(innerHeader, *relQuadRegistry, bytes.size());
    if (!triangleSetsOffset.has_value() || !quadRegistryOffset.has_value()) {
        result.diagnostics.push_back("GRND block at " + hexOffset(sourceOffset) + " has out-of-bounds registry pointers.");
        return result;
    }

    const std::size_t quadTableOffset = *quadRegistryOffset + 4U;
    if (quadTableOffset + (static_cast<std::size_t>(*quadCellCount) * 8U) > bytes.size()) {
        result.diagnostics.push_back("GRND block at " + hexOffset(sourceOffset) + " has an out-of-bounds quad table.");
        return result;
    }

    std::vector<std::optional<TriangleSet>> triangleSets(static_cast<std::size_t>(*triangleSetCount));
    result.data.triangleSets.resize(static_cast<std::size_t>(*triangleSetCount));
    for (std::size_t setIndex = 0; setIndex < *triangleSetCount; ++setIndex) {
        const std::size_t setOffset = *triangleSetsOffset + (setIndex * 0x18U);
        const std::size_t nextSetOrBlockEnd = (setIndex + 1U < *triangleSetCount)
            ? *triangleSetsOffset + ((setIndex + 1U) * 0x18U)
            : bytes.size();
        auto set = readTriangleSet(bytes, endian, setOffset, nextSetOrBlockEnd, result.diagnostics);
        if (!set.has_value()) {
            continue;
        }
        triangleSets[setIndex] = *set;
        auto& sourceSet = result.data.triangleSets[setIndex];
        sourceSet.sourceHeaderOffset = static_cast<std::uint32_t>(setOffset);
        sourceSet.headerPrefixBytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(setOffset),
            bytes.begin() + static_cast<std::ptrdiff_t>(setOffset + 0x0CU));
        sourceSet.declaredTriangleCount = set->declaredTriangleCount;
        sourceSet.streamEntries.reserve(set->streamEntryCount);
        for (std::size_t streamIndex = 0; streamIndex < set->streamEntryCount; ++streamIndex) {
            const auto streamEntry = readStreamEntry(bytes, endian, *set, streamIndex);
            if (!streamEntry.has_value()) {
                break;
            }
            sourceSet.streamEntries.push_back(model::GrndStreamEntry{
                .floatIndex = streamEntry->floatIndex,
                .flags = streamEntry->flags,
            });
            if (!sourceSet.verticesByFloatIndex.contains(streamEntry->floatIndex)) {
                if (const auto vertex = readVertexForFloatIndex(bytes, endian, *set, streamEntry->floatIndex)) {
                    sourceSet.verticesByFloatIndex.emplace(streamEntry->floatIndex, *vertex);
                }
            }
        }
    }

    std::unordered_set<TriangleRef, TriangleRefHash> uniqueReferences{};
    std::unordered_map<TriangleRef, std::size_t, TriangleRefHash> meshTriangleByReference{};
    std::unordered_map<std::uint64_t, std::uint32_t> vertexIndexByKey{};
    result.data.cells.resize(static_cast<std::size_t>(*quadCellCount));

    for (std::size_t quadIndex = 0; quadIndex < *quadCellCount; ++quadIndex) {
        const std::size_t quadOffset = quadTableOffset + (quadIndex * 8U);
        auto& sourceCell = result.data.cells[quadIndex];
        sourceCell.sourceOffset = static_cast<std::uint32_t>(quadOffset);
        const auto refCount = reader.try_read_u32(quadOffset);
        const auto relRefList = reader.try_read_i32(quadOffset + 4U);
        if (!refCount.has_value() || !relRefList.has_value()) {
            ++result.skippedReferenceCount;
            continue;
        }
        if (*refCount == 0U) {
            continue;
        }

        const auto refListOffset = addRelativeOffset(quadOffset + 4U, *relRefList, bytes.size());
        if (!refListOffset.has_value() ||
            *refListOffset + (static_cast<std::size_t>(*refCount) * 4U) > bytes.size()) {
            result.skippedReferenceCount += *refCount;
            continue;
        }

        for (std::size_t refIndex = 0; refIndex < *refCount; ++refIndex) {
            const std::size_t refOffset = *refListOffset + (refIndex * 4U);
            const auto setIndex = reader.try_read_u16(refOffset);
            const auto triangleIndex = reader.try_read_u16(refOffset + 2U);
            if (!setIndex.has_value() || !triangleIndex.has_value()) {
                ++result.skippedReferenceCount;
                continue;
            }

            const TriangleRef ref{
                .triangleSet = *setIndex,
                .triangleIndex = *triangleIndex,
            };
            sourceCell.references.push_back(model::GrndTriangleReference{
                .triangleSet = *setIndex,
                .streamIndex = *triangleIndex,
            });
            if (!uniqueReferences.insert(ref).second) {
                if (const auto found = meshTriangleByReference.find(ref); found != meshTriangleByReference.end()) {
                    sourceCell.references.back().meshTriangleIndex = found->second;
                }
                ++result.duplicateReferenceCount;
                continue;
            }
            if (*setIndex >= triangleSets.size() || !triangleSets[*setIndex].has_value()) {
                ++result.skippedReferenceCount;
                continue;
            }

            const auto& set = *triangleSets[*setIndex];
            const std::size_t streamIndex = *triangleIndex;
            const auto e0 = readStreamEntry(bytes, endian, set, streamIndex);
            const auto e1 = readStreamEntry(bytes, endian, set, streamIndex + 1U);
            const auto e2 = readStreamEntry(bytes, endian, set, streamIndex + 2U);
            if (!e0.has_value() || !e1.has_value() || !e2.has_value()) {
                ++result.skippedReferenceCount;
                continue;
            }

            const auto i0 = getOrCreateVertex(bytes, endian, set, e0->floatIndex, vertexIndexByKey, result.mesh, result.skippedReferenceCount);
            const auto i1 = getOrCreateVertex(bytes, endian, set, e1->floatIndex, vertexIndexByKey, result.mesh, result.skippedReferenceCount);
            const auto i2 = getOrCreateVertex(bytes, endian, set, e2->floatIndex, vertexIndexByKey, result.mesh, result.skippedReferenceCount);
            if (i0 == std::numeric_limits<std::uint32_t>::max() ||
                i1 == std::numeric_limits<std::uint32_t>::max() ||
                i2 == std::numeric_limits<std::uint32_t>::max()) {
                continue;
            }

            if ((e2->flags & 0x8000U) != 0U) {
                result.mesh.indices.push_back(i2);
                result.mesh.indices.push_back(i1);
                result.mesh.indices.push_back(i0);
            } else {
                result.mesh.indices.push_back(i0);
                result.mesh.indices.push_back(i1);
                result.mesh.indices.push_back(i2);
            }
            result.mesh.triangleMetadata.push_back(model::TriangleMetadata{
                .rawU16 = { e0->flags, e1->flags, e2->flags },
            });
            const auto meshTriangleIndex = result.mesh.indices.size() / 3U - 1U;
            sourceCell.references.back().meshTriangleIndex = meshTriangleIndex;
            meshTriangleByReference.emplace(ref, meshTriangleIndex);
            ++result.referencedTriangleCount;
        }
    }

    result.data.mesh = result.mesh;
    result.decoded = !result.mesh.vertices.empty() && !result.mesh.indices.empty();
    result.diagnostics.push_back("GRND decoded at " + hexOffset(sourceOffset) +
        ": vertices=" + std::to_string(result.mesh.vertices.size()) +
        ", triangles=" + std::to_string(result.mesh.indices.size() / 3U) +
        ", duplicateRefs=" + std::to_string(result.duplicateReferenceCount) +
        ", skippedRefs=" + std::to_string(result.skippedReferenceCount) + ".");
    return result;
}

} // namespace spice::mld::parsing
