#pragma once

#include "Types.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace spice::mld::model {

struct GrndStreamEntry {
    std::uint16_t floatIndex = 0;
    std::uint16_t flags = 0;
};

struct GrndTriangleSet {
    std::uint32_t sourceHeaderOffset = 0;
    std::vector<std::uint8_t> headerPrefixBytes{};
    std::uint32_t declaredTriangleCount = 0;
    std::vector<GrndStreamEntry> streamEntries{};
    std::map<std::uint16_t, MeshVertex> verticesByFloatIndex{};
};

struct GrndTriangleReference {
    std::uint16_t triangleSet = 0;
    std::uint16_t streamIndex = 0;
    std::optional<std::size_t> meshTriangleIndex{};
};

struct GrndCell {
    std::uint32_t sourceOffset = 0;
    std::vector<GrndTriangleReference> references{};
};

struct GrndData {
    std::vector<std::uint8_t> outerHeaderBytes{};
    std::vector<std::uint8_t> innerHeaderUnknownBytes{};
    std::uint16_t gridX = 0;
    std::uint16_t gridZ = 0;
    std::uint16_t cellSizeX = 0;
    std::uint16_t cellSizeZ = 0;
    std::vector<GrndTriangleSet> triangleSets{};
    std::vector<GrndCell> cells{};
    MeshData mesh{};
};

struct GobjStreamEntry {
    std::uint16_t floatIndex = 0;
    std::uint16_t flags = 0;
    bool separator = false;
};

struct GobjVertexChunk {
    std::uint8_t chunkType = 0;
    std::uint8_t recordWords = 0;
    std::uint32_t headerWord0 = 0;
    std::uint32_t headerWord1 = 0;
    std::map<std::uint16_t, MeshVertex> verticesByFloatIndex{};
};

struct GobjAttach {
    std::uint32_t sourceAttachOffset = 0;
    std::vector<std::uint8_t> prefixBytes{};
    std::vector<GobjStreamEntry> streamEntries{};
    GobjVertexChunk vertexChunk{};
};

struct GobjNode {
    std::uint32_t sourceNodeOffset = 0;
    std::uint32_t sourceAttachOffset = 0;
    std::vector<std::uint8_t> sourceBytes{};
    Transform transform{};
    std::optional<std::size_t> parentNodeIndex{};
    std::vector<std::size_t> childNodeIndices{};
    std::optional<GobjAttach> attach{};
    MeshData streamMesh{};
};

struct GobjData {
    std::vector<std::uint8_t> outerHeaderBytes{};
    std::vector<GobjNode> nodes{};
    std::vector<std::size_t> rootNodeIndices{};
};

namespace detail {

inline void hashGroundWord(std::uint64_t& hash, const std::uint64_t value) {
    for (unsigned shift = 0; shift < 64U; shift += 8U) {
        hash ^= static_cast<std::uint8_t>(value >> shift);
        hash *= 1099511628211ULL;
    }
}

inline void hashVec3(std::uint64_t& hash, const Vec3& value) {
    hashGroundWord(hash, std::bit_cast<std::uint32_t>(value.x));
    hashGroundWord(hash, std::bit_cast<std::uint32_t>(value.y));
    hashGroundWord(hash, std::bit_cast<std::uint32_t>(value.z));
}

inline void hashMesh(std::uint64_t& hash, const MeshData& mesh) {
    hashGroundWord(hash, mesh.vertices.size());
    for (const auto& vertex : mesh.vertices) {
        hashVec3(hash, vertex.position);
        hashVec3(hash, vertex.normal);
        hashGroundWord(hash, vertex.hasNormal);
        hashGroundWord(hash, vertex.rawUserAttributesU32.value_or(0U));
        hashGroundWord(hash, vertex.rawUserAttributesU32.has_value());
    }
    hashGroundWord(hash, mesh.indices.size());
    for (const auto index : mesh.indices) {
        hashGroundWord(hash, index);
    }
    hashGroundWord(hash, mesh.triangleMetadata.size());
    for (const auto& metadata : mesh.triangleMetadata) {
        for (const auto word : metadata.rawU16) {
            hashGroundWord(hash, word);
        }
    }
}

} // namespace detail

[[nodiscard]] inline std::uint64_t semanticHash(const GrndData& data) {
    std::uint64_t hash = 1469598103934665603ULL;
    detail::hashGroundWord(hash, data.gridX);
    detail::hashGroundWord(hash, data.gridZ);
    detail::hashGroundWord(hash, data.cellSizeX);
    detail::hashGroundWord(hash, data.cellSizeZ);
    detail::hashMesh(hash, data.mesh);
    detail::hashGroundWord(hash, data.cells.size());
    for (const auto& cell : data.cells) {
        detail::hashGroundWord(hash, cell.references.size());
        for (const auto& reference : cell.references) {
            detail::hashGroundWord(hash, reference.meshTriangleIndex.value_or(static_cast<std::size_t>(-1)));
        }
    }
    return hash;
}

[[nodiscard]] inline std::uint64_t semanticHash(const GobjData& data) {
    std::uint64_t hash = 1469598103934665603ULL;
    detail::hashGroundWord(hash, data.nodes.size());
    for (const auto& node : data.nodes) {
        detail::hashVec3(hash, node.transform.position);
        detail::hashVec3(hash, node.transform.rotationRaw);
        detail::hashVec3(hash, node.transform.scale);
        detail::hashGroundWord(hash, node.parentNodeIndex.value_or(static_cast<std::size_t>(-1)));
        detail::hashGroundWord(hash, node.childNodeIndices.size());
        for (const auto child : node.childNodeIndices) {
            detail::hashGroundWord(hash, child);
        }
        detail::hashMesh(hash, node.streamMesh);
    }
    detail::hashGroundWord(hash, data.rootNodeIndices.size());
    for (const auto root : data.rootNodeIndices) {
        detail::hashGroundWord(hash, root);
    }
    return hash;
}

} // namespace spice::mld::model
