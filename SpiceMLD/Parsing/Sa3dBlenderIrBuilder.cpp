#include "Sa3dBlenderIrBuilder.h"

#include "BlenderIrDiagnostics.h"
#include "GobjParser.h"
#include "GvrTextureDecoder.h"

#include "../../Sa3Dport/Sa3Dport.h"
#include "../common/ByteUtils.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace spice::mld::parsing {
namespace {

using Sa3Dport::Mesh::Buffer::BufferMesh;
using Sa3Dport::Mesh::Converters::ChunkBufferContext;
using Sa3Dport::Mesh::Converters::buffer_chunk_attach_with_active_poly_chunks;
using Sa3Dport::Mesh::Converters::get_active_poly_chunks;
using Sa3Dport::ObjectData::NodePtr;

[[nodiscard]] std::string hexOffset(const std::uint32_t value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result = "0x00000000";
    auto v = value;
    for (int i = 9; i >= 2; --i) {
        result[static_cast<std::size_t>(i)] = digits[v & 0xFU];
        v >>= 4U;
    }
    return result;
}

[[nodiscard]] model::Vec3 toVec3(const Sa3Dport::Structs::Vector3& value) {
    return model::Vec3{ value.x, value.y, value.z };
}

[[nodiscard]] model::Quat toQuat(const Sa3Dport::Structs::Quaternion& value) {
    return model::Quat{ value.x, value.y, value.z, value.w };
}

[[nodiscard]] Sa3Dport::Structs::Vector3 transformPoint(
    const Sa3Dport::Structs::Vector3& point,
    const Sa3Dport::Structs::Matrix4x4& matrix) {
    return {
        point.x * matrix.m11 + point.y * matrix.m21 + point.z * matrix.m31 + matrix.m41,
        point.x * matrix.m12 + point.y * matrix.m22 + point.z * matrix.m32 + matrix.m42,
        point.x * matrix.m13 + point.y * matrix.m23 + point.z * matrix.m33 + matrix.m43,
    };
}

[[nodiscard]] Sa3Dport::Structs::Vector3 transformNormal(
    const Sa3Dport::Structs::Vector3& normal,
    const Sa3Dport::Structs::Matrix4x4& matrix) {
    return {
        normal.x * matrix.m11 + normal.y * matrix.m21 + normal.z * matrix.m31,
        normal.x * matrix.m12 + normal.y * matrix.m22 + normal.z * matrix.m32,
        normal.x * matrix.m13 + normal.y * matrix.m23 + normal.z * matrix.m33,
    };
}

[[nodiscard]] std::optional<Sa3Dport::Structs::Matrix4x4> inverseMatrix(const Sa3Dport::Structs::Matrix4x4& matrix) {
    Sa3Dport::Structs::Matrix4x4 inverse{};
    if (!Sa3Dport::Structs::invert(matrix, inverse)) {
        return std::nullopt;
    }
    return inverse;
}

[[nodiscard]] std::string interpolationModeName(const Sa3Dport::Animation::InterpolationMode mode) {
    switch (mode) {
    case Sa3Dport::Animation::InterpolationMode::Linear:
        return "linear";
    case Sa3Dport::Animation::InterpolationMode::Spline:
        return "spline";
    case Sa3Dport::Animation::InterpolationMode::User:
        return "user";
    }
    return "unknown";
}

[[nodiscard]] model::Transform toTransform(const NodePtr& node) {
    model::Transform result{};
    result.position = toVec3(node->position);
    result.rotationRaw = toVec3(node->euler_rotation);
    result.rotation = toQuat(node->quaternion_rotation);
    result.scale = toVec3(node->scale);
    return result;
}

[[nodiscard]] Sa3Dport::Structs::Matrix4x4 effectiveLocalMatrix(const NodePtr& node) {
    auto position = node->position;
    auto rotation = node->quaternion_rotation;
    auto scale = node->scale;
    if (node->no_position()) {
        position = Sa3Dport::Structs::Vector3::zero();
    }
    if (node->no_rotation()) {
        rotation = Sa3Dport::Structs::Quaternion::identity();
    }
    if (node->no_scale()) {
        scale = Sa3Dport::Structs::Vector3::one();
    }
    return Sa3Dport::Structs::MatrixUtilities::create_transform_matrix(position, rotation, scale);
}

[[nodiscard]] std::vector<Sa3Dport::Structs::Matrix4x4> buildWorldMatrices(const std::vector<NodePtr>& nodes) {
    std::unordered_map<const Sa3Dport::ObjectData::Node*, std::size_t> nodeIndexByPtr{};
    nodeIndexByPtr.reserve(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        nodeIndexByPtr[nodes[i].get()] = i;
    }

    std::vector<Sa3Dport::Structs::Matrix4x4> matrices(nodes.size(), Sa3Dport::Structs::identity());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto local = effectiveLocalMatrix(nodes[i]);
        if (const auto parent = nodes[i]->parent()) {
            if (const auto found = nodeIndexByPtr.find(parent.get()); found != nodeIndexByPtr.end()) {
                matrices[i] = local * matrices[found->second];
                continue;
            }
        }
        matrices[i] = local;
    }
    return matrices;
}

[[nodiscard]] std::vector<std::optional<std::size_t>> buildParentIndices(const std::vector<NodePtr>& nodes) {
    std::unordered_map<const Sa3Dport::ObjectData::Node*, std::size_t> nodeIndexByPtr{};
    nodeIndexByPtr.reserve(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        nodeIndexByPtr[nodes[i].get()] = i;
    }

    std::vector<std::optional<std::size_t>> result(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (const auto parent = nodes[i]->parent()) {
            if (const auto found = nodeIndexByPtr.find(parent.get()); found != nodeIndexByPtr.end()) {
                result[i] = found->second;
            }
        }
    }
    return result;
}

[[nodiscard]] std::size_t computeCommonNodeIndex(
    const std::set<std::size_t>& nodeIndices,
    const std::vector<std::optional<std::size_t>>& parentIndices) {
    if (nodeIndices.empty()) {
        return 0U;
    }
    if (nodeIndices.size() == 1U) {
        return *nodeIndices.begin();
    }

    std::vector<std::size_t> ancestorCounts(parentIndices.size(), 0U);
    for (const auto nodeIndex : nodeIndices) {
        std::optional<std::size_t> current = nodeIndex;
        while (current.has_value() && *current < ancestorCounts.size()) {
            ++ancestorCounts[*current];
            current = parentIndices[*current];
        }
    }

    for (std::size_t i = ancestorCounts.size(); i > 0U; --i) {
        const auto index = i - 1U;
        if (ancestorCounts[index] == nodeIndices.size()) {
            return index;
        }
    }
    return 0U;
}

[[nodiscard]] std::uint64_t hashMaterial(const Sa3Dport::Mesh::Buffer::BufferMaterial& material,
    const bool strippified,
    const bool hasColors,
    const bool flatShading) {
    std::uint64_t h = static_cast<std::uint64_t>(material.texture_index);
    auto mix = [&h](const std::uint64_t value) {
        h ^= value + 0x9E3779B97F4A7C15ULL + (h << 6U) + (h >> 2U);
    };

    std::uint64_t attrib = 0U;
    attrib |= static_cast<std::uint64_t>(material.use_texture ? 1U : 0U) << 0U;
    attrib |= static_cast<std::uint64_t>(material.anisotropic_filtering ? 1U : 0U) << 1U;
    attrib |= static_cast<std::uint64_t>(material.clamp_u ? 1U : 0U) << 2U;
    attrib |= static_cast<std::uint64_t>(material.clamp_v ? 1U : 0U) << 3U;
    attrib |= static_cast<std::uint64_t>(material.mirror_u ? 1U : 0U) << 4U;
    attrib |= static_cast<std::uint64_t>(material.mirror_v ? 1U : 0U) << 5U;
    attrib |= static_cast<std::uint64_t>(material.normal_mapping ? 1U : 0U) << 6U;
    attrib |= static_cast<std::uint64_t>(material.no_lighting ? 1U : 0U) << 7U;
    attrib |= static_cast<std::uint64_t>(material.no_ambient ? 1U : 0U) << 8U;
    attrib |= static_cast<std::uint64_t>(material.no_specular ? 1U : 0U) << 9U;
    attrib |= static_cast<std::uint64_t>(material.flat ? 1U : 0U) << 10U;
    attrib |= static_cast<std::uint64_t>(material.use_alpha ? 1U : 0U) << 11U;
    attrib |= static_cast<std::uint64_t>(material.backface_culling ? 1U : 0U) << 12U;
    attrib |= static_cast<std::uint64_t>(material.no_alpha_test ? 1U : 0U) << 13U;
    attrib |= static_cast<std::uint64_t>(material.source_blend_mode & 0x7U) << 16U;
    attrib |= static_cast<std::uint64_t>(material.destination_blend_mode & 0x7U) << 19U;
    attrib |= static_cast<std::uint64_t>(material.texture_filtering & 0x3U) << 22U;
    mix(attrib);
    mix(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(material.mipmap_distance_multiplier)));
    mix(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(material.specular_exponent)));
    mix(static_cast<std::uint64_t>(strippified ? 1U : 0U));
    mix(static_cast<std::uint64_t>(hasColors ? 1U : 0U));
    mix(static_cast<std::uint64_t>(flatShading ? 1U : 0U));
    return h;
}

[[nodiscard]] std::uint32_t packMaterialStateKey(const Sa3Dport::Mesh::Buffer::BufferMaterial& material) {
    std::uint32_t value = 0U;
    value |= static_cast<std::uint32_t>(material.use_texture ? 1U : 0U) << 0U;
    value |= static_cast<std::uint32_t>(material.anisotropic_filtering ? 1U : 0U) << 1U;
    value |= static_cast<std::uint32_t>(material.clamp_u ? 1U : 0U) << 2U;
    value |= static_cast<std::uint32_t>(material.clamp_v ? 1U : 0U) << 3U;
    value |= static_cast<std::uint32_t>(material.mirror_u ? 1U : 0U) << 4U;
    value |= static_cast<std::uint32_t>(material.mirror_v ? 1U : 0U) << 5U;
    value |= static_cast<std::uint32_t>(material.normal_mapping ? 1U : 0U) << 6U;
    value |= static_cast<std::uint32_t>(material.no_lighting ? 1U : 0U) << 7U;
    value |= static_cast<std::uint32_t>(material.no_ambient ? 1U : 0U) << 8U;
    value |= static_cast<std::uint32_t>(material.no_specular ? 1U : 0U) << 9U;
    value |= static_cast<std::uint32_t>(material.flat ? 1U : 0U) << 10U;
    value |= static_cast<std::uint32_t>(material.use_alpha ? 1U : 0U) << 11U;
    value |= static_cast<std::uint32_t>(material.backface_culling ? 1U : 0U) << 12U;
    value |= static_cast<std::uint32_t>(material.no_alpha_test ? 1U : 0U) << 13U;
    value |= static_cast<std::uint32_t>(material.source_blend_mode & 0x7U) << 16U;
    value |= static_cast<std::uint32_t>(material.destination_blend_mode & 0x7U) << 19U;
    value |= static_cast<std::uint32_t>(material.texture_filtering & 0x3U) << 22U;
    return value;
}

[[nodiscard]] std::string readFixedAsciiName(std::span<const std::uint8_t> bytes,
    const std::size_t offset,
    const std::size_t maxLength) {
    std::string out{};
    if (offset >= bytes.size()) {
        return out;
    }
    const auto end = std::min(bytes.size(), offset + maxLength);
    for (std::size_t i = offset; i < end; ++i) {
        const auto ch = bytes[i];
        if (ch == 0U) {
            break;
        }
        if (std::isprint(static_cast<unsigned char>(ch)) == 0) {
            break;
        }
        out.push_back(static_cast<char>(ch));
    }
    return out;
}

[[nodiscard]] std::vector<std::string> parseNjtlTextureNames(const ExtractedNjBlock& block) {
    std::vector<std::string> names{};
    if (block.bytes.size() < 16U) {
        return names;
    }

    std::size_t njtlOffset = block.textureListOffset.has_value() && *block.textureListOffset >= block.offset
        ? static_cast<std::size_t>(*block.textureListOffset - block.offset)
        : 0U;
    auto tag = common::readU32AtBE(block.bytes, njtlOffset).value_or(0U);
    if (tag != 0x4E4A544CU && tag != 0x474A544CU && block.bytes.size() >= 0x10U) {
        const auto wrappedNjtlPointer = common::readU32AtBE(block.bytes, 0x08U).value_or(0U);
        if (wrappedNjtlPointer == 0U || wrappedNjtlPointer >= block.bytes.size()) {
            return names;
        }
        njtlOffset = wrappedNjtlPointer;
        tag = common::readU32AtBE(block.bytes, njtlOffset).value_or(0U);
    }
    if (tag != 0x4E4A544CU && tag != 0x474A544CU) {
        return names;
    }
    const auto blockSize = common::readU32AtBE(block.bytes, njtlOffset + 4U);
    const auto count = common::readU32AtBE(block.bytes, njtlOffset + 12U);
    if (!blockSize.has_value() || !count.has_value() || *count > 4096U) {
        return names;
    }

    constexpr std::size_t blockHeaderSize = 8U;
    constexpr std::size_t textureRecordTableOffset = 8U;
    constexpr std::size_t textureRecordSize = 12U;
    const std::size_t payloadStart = njtlOffset + blockHeaderSize;
    const std::size_t payloadSize = std::min<std::size_t>(*blockSize, block.bytes.size() - payloadStart);
    const auto payload = std::span<const std::uint8_t>(block.bytes.data() + static_cast<std::ptrdiff_t>(payloadStart), payloadSize);
    names.reserve(*count);
    for (std::uint32_t i = 0; i < *count; ++i) {
        const std::size_t recordOffset = textureRecordTableOffset + (static_cast<std::size_t>(i) * textureRecordSize);
        const auto namePointer = common::readU32AtBE(payload, recordOffset);
        if (!namePointer.has_value() || *namePointer >= payload.size()) {
            names.push_back({});
            continue;
        }
        names.push_back(readFixedAsciiName(payload, *namePointer, payload.size() - *namePointer));
    }
    return names;
}

[[nodiscard]] std::string textureNameForLocalIndex(const std::uint32_t textureIndex,
    const std::vector<std::string>& localTextureNames) {
    if (textureIndex == 0xFFFFFFFFu || textureIndex == 0xFFFFu) {
        return {};
    }
    if (textureIndex < localTextureNames.size() && !localTextureNames[textureIndex].empty()) {
        return localTextureNames[textureIndex];
    }
    return "texture_" + std::to_string(textureIndex);
}

[[nodiscard]] std::uint32_t resolveObjectAddress(const ExtractedNjBlock& block, const ParseResult& parseResult) {
    if (block.sourceObjectAddress.has_value()) {
        return *block.sourceObjectAddress;
    }

    std::vector<std::uint32_t> candidates{};
    for (const auto& entry : parseResult.rawEntries) {
        for (const auto address : entry.objectAddresses) {
            if (address >= block.offset && static_cast<std::uint64_t>(address) < static_cast<std::uint64_t>(block.offset) + block.size) {
                candidates.push_back(address);
            }
        }
    }

    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    if (!candidates.empty()) {
        return candidates.front();
    }

    return block.offset;
}

[[nodiscard]] std::span<const std::byte> asByteSpan(const std::vector<std::uint8_t>& bytes) {
    return std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(bytes.data()),
        bytes.size());
}

struct ParsedSa3dModel {
    Sa3Dport::File::ModelFile model{};
    std::size_t byteTrim = 0;
};

[[nodiscard]] std::optional<ParsedSa3dModel> tryReadModel(const ExtractedNjBlock& block,
    std::vector<std::string>& diagnostics) {
    if (block.bytes.empty()) {
        diagnostics.push_back("SA3D adapter skipped empty NJ block at " + std::to_string(block.offset) + ".");
        return std::nullopt;
    }

    std::vector<std::size_t> readOffsets{};
    const auto appendReadOffset = [&](const std::size_t offset) {
        if (std::find(readOffsets.begin(), readOffsets.end(), offset) == readOffsets.end()) {
            readOffsets.push_back(offset);
        }
    };
    if (block.modelReadOffset.has_value()) {
        appendReadOffset(*block.modelReadOffset);
    }
    appendReadOffset(0U);
    if (block.kind == ExtractedNjBlock::Kind::Object) {
        appendReadOffset(0x10U);
    }

    auto tryRead = [&](const std::size_t trim) -> std::optional<ParsedSa3dModel> {
        if (trim >= block.bytes.size()) {
            return std::nullopt;
        }
        const auto bytes = asByteSpan(block.bytes).subspan(trim);
        try {
            ParsedSa3dModel parsed{};
            parsed.model = Sa3Dport::File::ModelFile::read_from_bytes(bytes);
            parsed.byteTrim = trim;
            return parsed;
        } catch (const std::exception&) {
            return std::nullopt;
        }
    };

    for (const auto readOffset : readOffsets) {
        if (auto parsed = tryRead(readOffset); parsed.has_value()) {
            if (readOffset != 0U) {
                const auto layout = block.wrapperLayout.empty() ? std::string("legacy-offset") : block.wrapperLayout;
                diagnostics.push_back("SA3D adapter resolved " + layout +
                    " NJ model at " + std::to_string(block.offset + readOffset) +
                    " from object block " + std::to_string(block.offset) + ".");
            }
            return parsed;
        }
    }

    diagnostics.push_back("SA3D adapter could not parse NJ model block at " + std::to_string(block.offset) + ".");
    return std::nullopt;
}

[[nodiscard]] std::uint32_t vertexKey(const Sa3Dport::Mesh::Buffer::BufferVertex& vertex,
    const BufferMesh& mesh) {
    return static_cast<std::uint32_t>(vertex.index) + static_cast<std::uint32_t>(mesh.vertex_write_offset);
}

[[nodiscard]] std::uint32_t cornerKey(const Sa3Dport::Mesh::Buffer::BufferCorner& corner,
    const BufferMesh& mesh) {
    return static_cast<std::uint32_t>(corner.vertex_index) + static_cast<std::uint32_t>(mesh.vertex_read_offset);
}

struct SplitVertexKey {
    std::uint32_t sourceIndex = 0;
    std::uint32_t normalXBits = 0;
    std::uint32_t normalYBits = 0;
    std::uint32_t normalZBits = 0;

    [[nodiscard]] bool operator==(const SplitVertexKey&) const = default;
};

struct SplitVertexKeyHash {
    [[nodiscard]] std::size_t operator()(const SplitVertexKey& key) const noexcept {
        std::size_t h = static_cast<std::size_t>(key.sourceIndex);
        h = (h * 1315423911u) ^ key.normalXBits;
        h = (h * 1315423911u) ^ key.normalYBits;
        h = (h * 1315423911u) ^ key.normalZBits;
        return h;
    }
};

[[nodiscard]] SplitVertexKey makeSplitVertexKey(const std::uint32_t sourceIndex,
    const Sa3Dport::Structs::Vector3& normal) {
    return SplitVertexKey{
        .sourceIndex = sourceIndex,
        .normalXBits = std::bit_cast<std::uint32_t>(normal.x),
        .normalYBits = std::bit_cast<std::uint32_t>(normal.y),
        .normalZBits = std::bit_cast<std::uint32_t>(normal.z),
    };
}

[[nodiscard]] model::BlenderIrVertex toIrVertex(const Sa3Dport::Mesh::Buffer::BufferVertex& sourceVertex,
    const Sa3Dport::Structs::Vector3& normal,
    const bool hasNormal,
    const bool continueWeight) {
    model::BlenderIrVertex vertex{};
    vertex.position = toVec3(sourceVertex.position);
    vertex.normal = toVec3(normal);
    vertex.hasPosition = true;
    vertex.hasNormal = hasNormal;
    if (sourceVertex.weight != 1.0f || continueWeight) {
        vertex.weights.push_back(model::BlenderIrWeight{
            .boneOrNodeIndex = 0,
            .weight = sourceVertex.weight,
        });
    }
    return vertex;
}

[[nodiscard]] model::BlenderIrVertex toIrVertex(const Sa3Dport::Mesh::Buffer::BufferVertex& sourceVertex,
    const Sa3Dport::Structs::Vector3& normal,
    const bool hasNormal,
    const BufferMesh& bufferMesh) {
    return toIrVertex(sourceVertex, normal, hasNormal, bufferMesh.continue_weight);
}

struct SourceVertexContribution {
    Sa3Dport::Mesh::Buffer::BufferVertex vertex{};
    bool hasNormals = false;
    std::size_t nodeIndex = 0;
};

struct SourceVertexRecord {
    std::vector<SourceVertexContribution> contributions{};
};

[[nodiscard]] bool hasWeightedContributions(const SourceVertexRecord& source) {
    if (source.contributions.size() > 1U) {
        return true;
    }
    if (source.contributions.empty()) {
        return false;
    }
    return source.contributions.front().vertex.weight != 1.0f;
}

[[nodiscard]] model::BlenderIrVertex toWeightedIrVertex(
    const SourceVertexRecord& source,
    const Sa3Dport::Structs::Vector3& fallbackNormal,
    const bool hasNormal,
    const std::size_t rootNodeIndex,
    const std::vector<Sa3Dport::Structs::Matrix4x4>& worldMatrices) {
    model::BlenderIrVertex vertex{};
    vertex.hasPosition = true;
    vertex.hasNormal = hasNormal;

    auto inverseRoot = rootNodeIndex < worldMatrices.size()
        ? inverseMatrix(worldMatrices[rootNodeIndex])
        : std::optional<Sa3Dport::Structs::Matrix4x4>{};

    float weightSum = 0.0f;
    Sa3Dport::Structs::Vector3 position{};
    Sa3Dport::Structs::Vector3 normal{};
    for (const auto& contribution : source.contributions) {
        if (contribution.vertex.weight <= 0.0f) {
            continue;
        }

        auto weightedPosition = contribution.vertex.position;
        auto weightedNormal = hasNormal ? contribution.vertex.normal : fallbackNormal;
        if (inverseRoot.has_value() &&
            contribution.nodeIndex < worldMatrices.size() &&
            contribution.nodeIndex != rootNodeIndex) {
            const auto sourceToRoot = worldMatrices[contribution.nodeIndex] * *inverseRoot;
            weightedPosition = transformPoint(weightedPosition, sourceToRoot);
            weightedNormal = transformNormal(weightedNormal, sourceToRoot);
        }

        position += weightedPosition * contribution.vertex.weight;
        normal += weightedNormal * contribution.vertex.weight;
        weightSum += contribution.vertex.weight;
    }

    if (weightSum > 0.0f && weightSum != 1.0f) {
        position /= weightSum;
        normal /= weightSum;
    }
    vertex.position = toVec3(position);
    vertex.normal = toVec3(Sa3Dport::Structs::normalize(normal));

    for (const auto& contribution : source.contributions) {
        if (contribution.vertex.weight <= 0.0f) {
            continue;
        }
        vertex.weights.push_back(model::BlenderIrWeight{
            .boneOrNodeIndex = static_cast<std::uint32_t>(contribution.nodeIndex),
            .weight = weightSum > 0.0f ? contribution.vertex.weight / weightSum : contribution.vertex.weight,
        });
    }
    return vertex;
}

void appendBufferMeshGeometry(const BufferMesh& bufferMesh,
    const std::vector<std::string>& localTextureNames,
    model::BlenderIrMesh& outMesh,
    std::unordered_map<std::uint32_t, SourceVertexRecord>& sourceVertexByKey,
    std::unordered_map<std::uint32_t, std::uint32_t>& vertexIndexByKey,
    const std::size_t targetNodeIndex,
    const std::vector<Sa3Dport::Structs::Matrix4x4>& worldMatrices,
    const std::vector<std::optional<std::size_t>>& parentIndices) {
    for (const auto& sourceVertex : bufferMesh.vertices) {
        const auto key = vertexKey(sourceVertex, bufferMesh);
        auto& record = sourceVertexByKey[key];
        if (!bufferMesh.continue_weight) {
            record.contributions.clear();
        }
        if (bufferMesh.continue_weight && sourceVertex.weight <= 0.0f) {
            continue;
        }
        record.contributions.push_back(SourceVertexContribution{
            .vertex = sourceVertex,
            .hasNormals = bufferMesh.has_normals,
            .nodeIndex = targetNodeIndex,
        });
    }

    if (!bufferMesh.has_corners()) {
        return;
    }

    const auto materialHash = hashMaterial(bufferMesh.material, bufferMesh.strippified, bufferMesh.has_colors, bufferMesh.flat_shading);
    std::size_t materialIndex = outMesh.materials.size();
    for (std::size_t i = 0; i < outMesh.materials.size(); ++i) {
        if (outMesh.materials[i].materialHash == materialHash) {
            materialIndex = i;
            break;
        }
    }
    if (materialIndex == outMesh.materials.size()) {
        model::BlenderIrMaterial material{};
        material.polyType = bufferMesh.strippified ? 5U : 3U;
        material.chunkFlags =
            static_cast<std::uint8_t>((bufferMesh.material.mirror_u ? 0x1U : 0U) |
                                      (bufferMesh.material.mirror_v ? 0x2U : 0U) |
                                      (bufferMesh.material.clamp_u ? 0x4U : 0U) |
                                      (bufferMesh.material.clamp_v ? 0x8U : 0U));
        material.flatShading = bufferMesh.flat_shading;
        material.materialStateKey = packMaterialStateKey(bufferMesh.material);
        material.useTexture = bufferMesh.material.use_texture;
        material.useAlpha = bufferMesh.material.use_alpha;
        material.noAlphaTest = bufferMesh.material.no_alpha_test;
        material.doubleSided = !bufferMesh.material.backface_culling;
        material.clampU = bufferMesh.material.clamp_u;
        material.clampV = bufferMesh.material.clamp_v;
        material.mirrorU = bufferMesh.material.mirror_u;
        material.mirrorV = bufferMesh.material.mirror_v;
        material.normalMapping = bufferMesh.material.normal_mapping;
        material.noLighting = bufferMesh.material.no_lighting;
        material.noAmbient = bufferMesh.material.no_ambient;
        material.noSpecular = bufferMesh.material.no_specular;
        material.anisotropicFiltering = bufferMesh.material.anisotropic_filtering;
        material.textureFiltering = bufferMesh.material.texture_filtering;
        material.sourceAlpha = bufferMesh.material.source_blend_mode;
        material.destinationAlpha = bufferMesh.material.destination_blend_mode;
        material.mipmapDistanceMultiplier = bufferMesh.material.mipmap_distance_multiplier;
        material.textureId = static_cast<std::uint16_t>(std::min<std::uint32_t>(bufferMesh.material.texture_index, 0xFFFFU));
        material.textureName = textureNameForLocalIndex(bufferMesh.material.texture_index, localTextureNames);
        material.materialHash = materialHash;
        outMesh.materials.push_back(std::move(material));
    }

    model::BlenderIrTriangleSet triangleSet{};
    triangleSet.materialIndex = materialIndex;
    triangleSet.polyType = bufferMesh.strippified ? 5U : 3U;
    triangleSet.fromCacheReplay = false;

    const auto corners = bufferMesh.corner_triangle_list();
    std::set<std::size_t> dependencyNodeIndices{};
    bool hasWeightedBinding = false;
    for (const auto& sourceCorner : corners) {
        const auto key = cornerKey(sourceCorner, bufferMesh);
        const auto sourceVertex = sourceVertexByKey.find(key);
        if (sourceVertex == sourceVertexByKey.end()) {
            continue;
        }
        hasWeightedBinding = hasWeightedBinding || hasWeightedContributions(sourceVertex->second);
        for (const auto& contribution : sourceVertex->second.contributions) {
            if (contribution.vertex.weight > 0.0f) {
                dependencyNodeIndices.insert(contribution.nodeIndex);
            }
        }
    }

    std::size_t rootNodeIndex = targetNodeIndex;
    if (!dependencyNodeIndices.empty()) {
        rootNodeIndex = computeCommonNodeIndex(dependencyNodeIndices, parentIndices);
    }
    hasWeightedBinding = hasWeightedBinding || rootNodeIndex != targetNodeIndex || dependencyNodeIndices.size() > 1U;
    if (hasWeightedBinding && !outMesh.weightedBinding.has_value()) {
        outMesh.weightedBinding = model::BlenderIrWeightedBinding{
            .rootNodeIndex = rootNodeIndex,
            .sourceNodeIndex = targetNodeIndex,
            .nodeIndices = std::vector<std::size_t>(dependencyNodeIndices.begin(), dependencyNodeIndices.end()),
        };
    } else if (hasWeightedBinding && outMesh.weightedBinding.has_value()) {
        outMesh.weightedBinding->sourceNodeIndex = targetNodeIndex;
        std::set<std::size_t> merged(outMesh.weightedBinding->nodeIndices.begin(), outMesh.weightedBinding->nodeIndices.end());
        merged.insert(dependencyNodeIndices.begin(), dependencyNodeIndices.end());
        outMesh.weightedBinding->rootNodeIndex = computeCommonNodeIndex(merged, parentIndices);
        outMesh.weightedBinding->nodeIndices.assign(merged.begin(), merged.end());
    }

    std::unordered_map<SplitVertexKey, std::uint32_t, SplitVertexKeyHash> splitVertexIndexByKey{};
    triangleSet.corners.reserve(corners.size());
    auto convertSourceVertex = [&](const SourceVertexRecord& source, const Sa3Dport::Structs::Vector3& normal, const bool hasNormal) {
        if (hasWeightedBinding) {
            return toWeightedIrVertex(source, normal, hasNormal, rootNodeIndex, worldMatrices);
        }

        auto converted = source.contributions.front().vertex;
        auto convertedNormal = normal;
        const auto sourceNodeIndex = source.contributions.front().nodeIndex;
        if (sourceNodeIndex != targetNodeIndex &&
            sourceNodeIndex < worldMatrices.size() &&
            targetNodeIndex < worldMatrices.size()) {
            if (auto inverseTarget = inverseMatrix(worldMatrices[targetNodeIndex]); inverseTarget.has_value()) {
                const auto sourceToTarget = worldMatrices[sourceNodeIndex] * *inverseTarget;
                converted.position = transformPoint(converted.position, sourceToTarget);
                converted.normal = transformNormal(converted.normal, sourceToTarget);
                convertedNormal = transformNormal(convertedNormal, sourceToTarget);
            }
        }
        return toIrVertex(converted, convertedNormal, hasNormal, false);
    };

    auto resolveCorner = [&](const Sa3Dport::Mesh::Buffer::BufferCorner& sourceCorner) -> std::optional<model::BlenderIrCorner> {
        const auto key = cornerKey(sourceCorner, bufferMesh);
        model::BlenderIrCorner corner{};
        const auto sourceVertex = sourceVertexByKey.find(key);
        if (sourceVertex == sourceVertexByKey.end() || sourceVertex->second.contributions.empty()) {
            return std::nullopt;
        }

        if (sourceCorner.has_normal) {
            const auto splitKey = makeSplitVertexKey(key, sourceCorner.normal);
            if (const auto splitFound = splitVertexIndexByKey.find(splitKey); splitFound != splitVertexIndexByKey.end()) {
                corner.vertexIndex = splitFound->second;
            } else {
                auto vertex = convertSourceVertex(sourceVertex->second, sourceCorner.normal, true);
                corner.vertexIndex = static_cast<std::uint32_t>(outMesh.vertices.size());
                outMesh.vertices.push_back(std::move(vertex));
                splitVertexIndexByKey.emplace(splitKey, corner.vertexIndex);
            }
        } else if (const auto found = vertexIndexByKey.find(key); found != vertexIndexByKey.end()) {
            corner.vertexIndex = found->second;
        } else {
            auto vertex = convertSourceVertex(sourceVertex->second,
                sourceVertex->second.contributions.front().vertex.normal,
                sourceVertex->second.contributions.front().hasNormals);
            corner.vertexIndex = static_cast<std::uint32_t>(outMesh.vertices.size());
            outMesh.vertices.push_back(std::move(vertex));
            vertexIndexByKey[key] = corner.vertexIndex;
        }
        corner.u = sourceCorner.texcoord.x;
        corner.v = sourceCorner.texcoord.y;
        corner.hasUv = true;
        corner.colorR = sourceCorner.color.red_f();
        corner.colorG = sourceCorner.color.green_f();
        corner.colorB = sourceCorner.color.blue_f();
        corner.colorA = sourceCorner.color.alpha_f();
        corner.hasColor = bufferMesh.has_colors;
        return corner;
    };

    for (std::size_t i = 0; i + 2U < corners.size(); i += 3U) {
        auto c0 = resolveCorner(corners[i]);
        auto c1 = resolveCorner(corners[i + 1U]);
        auto c2 = resolveCorner(corners[i + 2U]);
        if (!c0.has_value() || !c1.has_value() || !c2.has_value()) {
            continue;
        }

        triangleSet.corners.push_back(*c0);
        triangleSet.corners.push_back(*c1);
        triangleSet.corners.push_back(*c2);
    }

    if (!triangleSet.corners.empty()) {
        outMesh.triangleSets.push_back(std::move(triangleSet));
    }
}

[[nodiscard]] std::optional<std::size_t> appendAttachMesh(const NodePtr& node,
    const std::size_t nodeIndex,
    const std::uint32_t objectAddress,
    const std::size_t sourceChunkOffset,
    const std::optional<Sa3Dport::Mesh::Converters::ActivePolyChunkList>& activePolyChunks,
    const std::vector<std::string>& localTextureNames,
    ChunkBufferContext& bufferContext,
    std::unordered_map<std::uint32_t, SourceVertexRecord>& sourceVertexByKey,
    const std::vector<Sa3Dport::Structs::Matrix4x4>& worldMatrices,
    const std::vector<std::optional<std::size_t>>& parentIndices,
    model::BlenderIrScene& out) {
    const auto chunkAttach = std::dynamic_pointer_cast<Sa3Dport::Mesh::Chunk::ChunkAttach>(node->attach);
    if (!chunkAttach) {
        return std::nullopt;
    }

    const auto bufferMeshes = buffer_chunk_attach_with_active_poly_chunks(*chunkAttach, activePolyChunks, bufferContext);
    if (bufferMeshes.empty()) {
        return std::nullopt;
    }

    model::BlenderIrMesh mesh{};
    mesh.label = "SA3D_obj_" + std::to_string(objectAddress) + "_node_" + std::to_string(nodeIndex) +
        "_attach_" + std::to_string(chunkAttach->source_address);
    mesh.sourceObjectAddress = objectAddress;
    mesh.sourceChunkOffset = sourceChunkOffset;
    mesh.sourceAttachOffset = chunkAttach->source_address;

    std::unordered_map<std::uint32_t, std::uint32_t> vertexIndexByKey{};
    for (const auto& bufferMesh : bufferMeshes) {
        appendBufferMeshGeometry(
            bufferMesh,
            localTextureNames,
            mesh,
            sourceVertexByKey,
            vertexIndexByKey,
            nodeIndex,
            worldMatrices,
            parentIndices);
    }

    if (mesh.vertices.empty() || mesh.triangleSets.empty()) {
        return std::nullopt;
    }

    BlenderIrDiagnostics::finalizeMesh(mesh);
    out.meshes.push_back(std::move(mesh));
    return out.meshes.size() - 1U;
}

[[nodiscard]] std::optional<std::size_t> appendGobjAttachMesh(
    const GobjNode& node,
    const std::size_t nodeIndex,
    const std::uint32_t sourceAddress,
    model::BlenderIrScene& out) {
    model::BlenderIrMesh mesh{};
    mesh.label = "GOBJ_" + hexOffset(sourceAddress) + "_node_" + std::to_string(nodeIndex) +
        "_attach_" + hexOffset(node.sourceAttachOffset);
    mesh.sourceObjectAddress = sourceAddress;
    mesh.sourceChunkOffset = sourceAddress;
    mesh.sourceAttachOffset = node.sourceAttachOffset;

    if (!node.streamMesh.vertices.empty() && !node.streamMesh.indices.empty()) {
        mesh.vertices.reserve(node.streamMesh.vertices.size());
        for (const auto& sourceVertex : node.streamMesh.vertices) {
            model::BlenderIrVertex vertex{};
            vertex.position = sourceVertex.position;
            vertex.normal = sourceVertex.normal;
            vertex.hasPosition = true;
            vertex.hasNormal = true;
            mesh.vertices.push_back(std::move(vertex));
        }

        model::BlenderIrMaterial material{};
        material.polyType = 3U;
        material.useTexture = false;
        material.doubleSided = true;
        material.flatShading = false;
        material.textureId = 0xFFFFU;
        material.textureFiltering = 1U;
        material.materialHash = 0x474F424AULL ^ static_cast<std::uint64_t>(sourceAddress) ^ nodeIndex;
        mesh.materials.push_back(std::move(material));

        model::BlenderIrTriangleSet triangleSet{};
        triangleSet.materialIndex = 0U;
        triangleSet.polyType = 3U;
        triangleSet.sourceChunkOffset = sourceAddress;
        triangleSet.corners.reserve(node.streamMesh.indices.size());
        for (const auto index : node.streamMesh.indices) {
            model::BlenderIrCorner corner{};
            corner.vertexIndex = index;
            triangleSet.corners.push_back(corner);
        }
        mesh.triangleSets.push_back(std::move(triangleSet));
    }

    if (mesh.vertices.empty() || mesh.triangleSets.empty()) {
        return std::nullopt;
    }

    BlenderIrDiagnostics::finalizeMesh(mesh);
    out.meshes.push_back(std::move(mesh));
    return out.meshes.size() - 1U;
}

void appendTextureArchive(const ParseResult& parseResult, model::BlenderIrScene& out) {
    if (!parseResult.textureArchive.has_value()) {
        return;
    }

    for (std::size_t i = 0; i < parseResult.textureArchive->entries.size(); ++i) {
        const auto& tx = parseResult.textureArchive->entries[i];
        const std::uint32_t textureId = tx.hasGlobalIndex ? tx.globalIndex : tx.archiveTextureIndex;

        model::BlenderIrTexture outTexture{};
        outTexture.textureId = textureId;
        outTexture.hasTextureId = true;
        outTexture.sourceOffset = tx.gvrDataOffset;
        outTexture.sourceSize = tx.gvrDataSize;
        outTexture.encodedFormat = "gvr";
        outTexture.sourceContainer = "gvm";
        outTexture.sourceTextureFormat = tx.sourceFormat;
        outTexture.sourcePaletteFormat = tx.sourcePaletteFormat;
        outTexture.decodeWarnings = tx.diagnostics;
        outTexture.width = tx.width;
        outTexture.height = tx.height;
        outTexture.pixelFormat = "rgba8";
        if (!tx.textureName.empty()) {
            outTexture.textureName = tx.textureName;
        } else {
            outTexture.textureName = "texture_" + std::to_string(textureId);
        }

        const auto decoded = decodeGvrToRgba8(tx);
        if (decoded.decoded) {
            outTexture.width = decoded.width;
            outTexture.height = decoded.height;
            outTexture.pixelData = decoded.rgba8;
            outTexture.hasDecodedPixels = !decoded.rgba8.empty();
        } else {
            out.diagnostics.push_back("SA3D adapter texture decode warning: " +
                (decoded.diagnostics.empty() ? std::string("unknown decode failure.") : decoded.diagnostics.front()));
        }

        out.textures.push_back(std::move(outTexture));
    }
}

void appendUnsupportedChannel(std::vector<model::BlenderIrUnsupportedAnimationChannel>& out,
    const std::size_t nodeIndex,
    const std::string& channel,
    const std::size_t count) {
    if (count == 0U) {
        return;
    }
    out.push_back(model::BlenderIrUnsupportedAnimationChannel{
        .nodeIndex = nodeIndex,
        .channel = channel,
        .keyframeCount = count,
    });
}

template <class Map>
[[nodiscard]] std::vector<model::BlenderIrVec3Keyframe> toVec3Keyframes(const Map& values) {
    std::vector<model::BlenderIrVec3Keyframe> out;
    out.reserve(values.size());
    for (const auto& [frame, value] : values) {
        out.push_back(model::BlenderIrVec3Keyframe{
            .frame = frame,
            .value = toVec3(value),
        });
    }
    return out;
}

template <class Map>
[[nodiscard]] std::vector<model::BlenderIrQuatKeyframe> toQuatKeyframes(const Map& values) {
    std::vector<model::BlenderIrQuatKeyframe> out;
    out.reserve(values.size());
    for (const auto& [frame, value] : values) {
        out.push_back(model::BlenderIrQuatKeyframe{
            .frame = frame,
            .value = toQuat(value),
        });
    }
    return out;
}

void appendAnimations(const ParseResult& parseResult,
    const std::unordered_map<std::uint32_t, std::vector<std::size_t>>& treeIndicesByObjectAddress,
    model::BlenderIrScene& out) {
    for (const auto& source : parseResult.animations) {
        if (!source.motion) {
            continue;
        }
        const auto foundTrees = treeIndicesByObjectAddress.find(source.sourceObjectAddress);
        if (foundTrees == treeIndicesByObjectAddress.end() || foundTrees->second.empty()) {
            out.diagnostics.push_back("Animation for entry " + std::to_string(source.tableIndex) +
                " could not bind to object tree " + std::to_string(source.sourceObjectAddress) + ".");
            continue;
        }

        model::BlenderIrAnimation animation{};
        animation.sourceEntryId = source.sourceEntryId;
        animation.tableIndex = source.tableIndex;
        animation.sourceObjectAddress = source.sourceObjectAddress;
        animation.sourceMotionAddress = source.sourceMotionAddress;
        animation.motionSlot = source.motionSlot;
        animation.objectTreeIndex = foundTrees->second.front();
        animation.nodeCount = source.nodeCount;
        animation.frameCount = source.motion->frame_count();
        animation.interpolationMode = interpolationModeName(source.motion->interpolation_mode);

        for (const auto& [nodeIndex, keyframes] : source.motion->keyframes) {
            model::BlenderIrNodeAnimation nodeAnimation{};
            nodeAnimation.nodeIndex = static_cast<std::size_t>(nodeIndex);
            nodeAnimation.position = toVec3Keyframes(keyframes.position);
            nodeAnimation.eulerRotation = toVec3Keyframes(keyframes.euler_rotation);
            nodeAnimation.scale = toVec3Keyframes(keyframes.scale);
            nodeAnimation.quaternionRotation = toQuatKeyframes(keyframes.quaternion_rotation);

            appendUnsupportedChannel(animation.unsupportedChannels, nodeAnimation.nodeIndex, "vector", keyframes.vector.size());
            appendUnsupportedChannel(animation.unsupportedChannels, nodeAnimation.nodeIndex, "vertex", keyframes.vertex.size());
            appendUnsupportedChannel(animation.unsupportedChannels, nodeAnimation.nodeIndex, "normal", keyframes.normal.size());
            appendUnsupportedChannel(animation.unsupportedChannels, nodeAnimation.nodeIndex, "target", keyframes.target.size());
            appendUnsupportedChannel(animation.unsupportedChannels, nodeAnimation.nodeIndex, "roll", keyframes.roll.size());
            appendUnsupportedChannel(animation.unsupportedChannels, nodeAnimation.nodeIndex, "angle", keyframes.angle.size());
            appendUnsupportedChannel(animation.unsupportedChannels, nodeAnimation.nodeIndex, "lightColor", keyframes.light_color.size());
            appendUnsupportedChannel(animation.unsupportedChannels, nodeAnimation.nodeIndex, "intensity", keyframes.intensity.size());
            appendUnsupportedChannel(animation.unsupportedChannels, nodeAnimation.nodeIndex, "spot", keyframes.spot.size());
            appendUnsupportedChannel(animation.unsupportedChannels, nodeAnimation.nodeIndex, "point", keyframes.point.size());

            if (!nodeAnimation.position.empty() ||
                !nodeAnimation.eulerRotation.empty() ||
                !nodeAnimation.scale.empty() ||
                !nodeAnimation.quaternionRotation.empty()) {
                animation.nodes.push_back(std::move(nodeAnimation));
            }
        }

        out.animations.push_back(std::move(animation));
    }
}

void appendGrndMeshes(
    const ParseResult& parseResult,
    model::BlenderIrScene& out,
    std::unordered_map<std::uint32_t, std::vector<std::size_t>>& meshIndicesByGroundAddress) {
    for (const auto& grnd : parseResult.world.grndSurfaces) {
        if (grnd.mesh.vertices.empty() || grnd.mesh.indices.empty()) {
            continue;
        }

        model::BlenderIrMesh mesh{};
        mesh.label = "GRND_" + hexOffset(grnd.sourceOffset);
        mesh.sourceObjectAddress = grnd.id;
        mesh.sourceChunkOffset = grnd.sourceOffset;

        mesh.vertices.reserve(grnd.mesh.vertices.size());
        for (const auto& sourceVertex : grnd.mesh.vertices) {
            model::BlenderIrVertex vertex{};
            vertex.position = sourceVertex.position;
            vertex.normal = sourceVertex.normal;
            vertex.hasPosition = true;
            vertex.hasNormal = true;
            mesh.vertices.push_back(std::move(vertex));
        }

        model::BlenderIrMaterial material{};
        material.polyType = 3U;
        material.useTexture = false;
        material.doubleSided = true;
        material.flatShading = false;
        material.textureId = 0xFFFFU;
        material.textureFiltering = 1U;
        material.materialHash = 0x47524E44ULL ^ static_cast<std::uint64_t>(grnd.sourceOffset);
        mesh.materials.push_back(std::move(material));

        model::BlenderIrTriangleSet triangleSet{};
        triangleSet.materialIndex = 0U;
        triangleSet.polyType = 3U;
        triangleSet.sourceChunkOffset = grnd.sourceOffset;
        triangleSet.corners.reserve(grnd.mesh.indices.size());
        for (const auto index : grnd.mesh.indices) {
            model::BlenderIrCorner corner{};
            corner.vertexIndex = index;
            triangleSet.corners.push_back(corner);
        }
        mesh.triangleSets.push_back(std::move(triangleSet));

        BlenderIrDiagnostics::finalizeMesh(mesh);
        out.meshes.push_back(std::move(mesh));
        meshIndicesByGroundAddress[grnd.id].push_back(out.meshes.size() - 1U);
    }
}

void appendGobjTrees(
    const ParseResult& parseResult,
    model::BlenderIrScene& out,
    std::unordered_map<std::uint32_t, std::vector<std::size_t>>& treeIndicesByGroundAddress) {
    GobjParser parser{};
    for (const auto& block : parseResult.extractedSpatialBlocks) {
        if (block.kind != ExtractedMldSpatialBlock::Kind::Gobj) {
            continue;
        }

        auto decoded = parser.decode(block.bytes, block.offset, block.endian);
        for (const auto& diagnostic : decoded.diagnostics) {
            out.diagnostics.push_back(diagnostic);
        }
        if (!decoded.decoded) {
            continue;
        }

        model::BlenderIrObjectTree tree{};
        tree.label = "GOBJ_" + hexOffset(block.offset);
        tree.sourceObjectAddress = block.offset;
        tree.sourceChunkOffset = block.offset;
        tree.rootNodeIndices = decoded.rootNodeIndices;
        tree.nodes.reserve(decoded.nodes.size());

        for (std::size_t nodeIndex = 0; nodeIndex < decoded.nodes.size(); ++nodeIndex) {
            const auto& sourceNode = decoded.nodes[nodeIndex];
            model::BlenderIrNode irNode{};
            irNode.sourceNodeOffset = sourceNode.sourceNodeOffset;
            irNode.sourceAttachOffset = sourceNode.sourceAttachOffset;
            irNode.hasAttach = sourceNode.sourceAttachOffset != 0U;
            irNode.meshIndex = appendGobjAttachMesh(sourceNode, nodeIndex, block.offset, out);
            irNode.localTransform = sourceNode.transform;
            irNode.parentNodeIndex = sourceNode.parentNodeIndex;
            irNode.childNodeIndices = sourceNode.childNodeIndices;
            tree.nodes.push_back(std::move(irNode));
        }

        out.objectTrees.push_back(std::move(tree));
        treeIndicesByGroundAddress[block.offset].push_back(out.objectTrees.size() - 1U);
    }
}

} // namespace

model::BlenderIrScene Sa3dBlenderIrBuilder::build(const ParseResult& parseResult) const {
    model::BlenderIrScene out{};
    std::unordered_map<std::uint32_t, std::vector<std::size_t>> meshIndicesByObjectAddress{};
    std::unordered_map<std::uint32_t, std::vector<std::size_t>> meshIndicesByGroundAddress{};
    std::unordered_map<std::uint32_t, std::vector<std::size_t>> treeIndicesByGroundAddress{};
    std::unordered_map<std::uint32_t, std::vector<std::size_t>> treeIndicesByObjectAddress{};

    appendGrndMeshes(parseResult, out, meshIndicesByGroundAddress);
    appendGobjTrees(parseResult, out, treeIndicesByGroundAddress);

    for (const auto& block : parseResult.extractedNjBlocks) {
        if (block.kind != ExtractedNjBlock::Kind::Object) {
            continue;
        }

        auto parsed = tryReadModel(block, out.diagnostics);
        if (!parsed.has_value() || !parsed->model.model) {
            continue;
        }

        const auto objectAddress = resolveObjectAddress(block, parseResult);
        const auto localTextureNames = parseNjtlTextureNames(block);
        if (!localTextureNames.empty()) {
            out.diagnostics.push_back("SA3D adapter parsed " + std::to_string(localTextureNames.size()) +
                " NJTL texture name(s) for object " + std::to_string(objectAddress) + ".");
        }
        const auto nodes = parsed->model.model->tree_nodes();
        const auto activePolyChunks = get_active_poly_chunks(nodes);
        const auto worldMatrices = buildWorldMatrices(nodes);
        const auto parentIndices = buildParentIndices(nodes);
        ChunkBufferContext bufferContext{};
        std::unordered_map<std::uint32_t, SourceVertexRecord> sourceVertexByKey{};

        model::BlenderIrObjectTree tree{};
        tree.label = "SA3D_obj_" + std::to_string(objectAddress);
        tree.sourceObjectAddress = objectAddress;
        tree.sourceChunkOffset = block.offset + parsed->byteTrim;
        tree.nodes.reserve(nodes.size());

        std::unordered_map<const Sa3Dport::ObjectData::Node*, std::size_t> nodeIndexByPtr{};
        std::vector<std::optional<std::size_t>> meshIndexByNodeIndex(nodes.size());
        for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
            const auto& node = nodes[nodeIndex];
            nodeIndexByPtr[node.get()] = nodeIndex;

            meshIndexByNodeIndex[nodeIndex] = appendAttachMesh(
                node,
                nodeIndex,
                objectAddress,
                tree.sourceChunkOffset,
                nodeIndex < activePolyChunks.size() ? activePolyChunks[nodeIndex] : std::nullopt,
                localTextureNames,
                bufferContext,
                sourceVertexByKey,
                worldMatrices,
                parentIndices,
                out);
            if (meshIndexByNodeIndex[nodeIndex].has_value()) {
                meshIndicesByObjectAddress[objectAddress].push_back(*meshIndexByNodeIndex[nodeIndex]);
            }

            model::BlenderIrNode irNode{};
            irNode.sourceNodeOffset = node->source_address;
            irNode.sourceEvalFlags = static_cast<std::uint32_t>(node->attributes);
            irNode.sourceAttachOffset = node->attach_address;
            irNode.hasAttach = node->attach != nullptr;
            irNode.meshIndex = meshIndexByNodeIndex[nodeIndex];
            irNode.localTransform = toTransform(node);
            tree.nodes.push_back(std::move(irNode));
        }

        for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
            const auto& node = nodes[nodeIndex];
            if (const auto parent = node->parent()) {
                if (const auto found = nodeIndexByPtr.find(parent.get()); found != nodeIndexByPtr.end()) {
                    tree.nodes[nodeIndex].parentNodeIndex = found->second;
                }
            }

            for (const auto& child : node->direct_children()) {
                if (const auto found = nodeIndexByPtr.find(child.get()); found != nodeIndexByPtr.end()) {
                    tree.nodes[nodeIndex].childNodeIndices.push_back(found->second);
                }
            }

            if (!tree.nodes[nodeIndex].parentNodeIndex.has_value()) {
                tree.rootNodeIndices.push_back(nodeIndex);
            }
        }

        out.objectTrees.push_back(std::move(tree));
        treeIndicesByObjectAddress[objectAddress].push_back(out.objectTrees.size() - 1U);
    }

    out.indexEntries.reserve(parseResult.rawEntries.size());
    for (const auto& entry : parseResult.rawEntries) {
        model::BlenderIrInstance instance{};
        instance.sourceEntryId = entry.sourceEntryId;
        instance.tableIndex = entry.tableIndex;
        instance.tblId = entry.tblId;
        instance.fxnName = entry.fxnName;
        instance.transform = entry.transform;
        instance.objectAddresses = entry.objectAddresses;
        instance.groundAddresses = entry.groundAddresses;

        for (const auto objectAddress : entry.objectAddresses) {
            if (const auto found = meshIndicesByObjectAddress.find(objectAddress); found != meshIndicesByObjectAddress.end()) {
                instance.meshIndices.insert(instance.meshIndices.end(), found->second.begin(), found->second.end());
            }
            if (const auto found = treeIndicesByObjectAddress.find(objectAddress); found != treeIndicesByObjectAddress.end()) {
                instance.objectTreeIndices.insert(instance.objectTreeIndices.end(), found->second.begin(), found->second.end());
            }
        }
        for (const auto groundAddress : entry.groundAddresses) {
            if (const auto found = meshIndicesByGroundAddress.find(groundAddress); found != meshIndicesByGroundAddress.end()) {
                instance.meshIndices.insert(instance.meshIndices.end(), found->second.begin(), found->second.end());
            }
            if (const auto found = treeIndicesByGroundAddress.find(groundAddress); found != treeIndicesByGroundAddress.end()) {
                instance.objectTreeIndices.insert(instance.objectTreeIndices.end(), found->second.begin(), found->second.end());
            }
        }

        if (instance.objectTreeIndices.empty() && !instance.objectAddresses.empty()) {
            out.diagnostics.push_back("SA3D adapter entry " + std::to_string(entry.sourceEntryId) +
                " references object(s) with no parsed SA3D object tree.");
        }

        out.indexEntries.push_back(std::move(instance));
    }

    appendAnimations(parseResult, treeIndicesByObjectAddress, out);
    appendTextureArchive(parseResult, out);
    out.diagnostics.push_back("SA3D adapter produced " + std::to_string(out.meshes.size()) + " meshes, " +
        std::to_string(out.objectTrees.size()) + " object trees and " +
        std::to_string(out.indexEntries.size()) + " index entries.");
    return out;
}

} // namespace spice::mld::parsing
