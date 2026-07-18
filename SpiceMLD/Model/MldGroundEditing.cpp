#include "MldGroundEditing.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace spice::mld::model {
namespace {

void addDiagnostic(std::vector<std::string>* diagnostics, std::string message) {
    if (diagnostics != nullptr) {
        diagnostics->push_back(std::move(message));
    }
}

[[nodiscard]] bool validateMesh(const MeshData& mesh, std::vector<std::string>* diagnostics) {
    if ((mesh.indices.size() % 3U) != 0U) {
        addDiagnostic(diagnostics, "Mesh index count is not divisible by three.");
        return false;
    }
    if (!mesh.triangleMetadata.empty() && mesh.triangleMetadata.size() != mesh.indices.size() / 3U) {
        addDiagnostic(diagnostics, "Triangle metadata count does not match mesh triangle count.");
        return false;
    }
    if (std::any_of(mesh.indices.begin(), mesh.indices.end(), [&](const auto index) { return index >= mesh.vertices.size(); })) {
        addDiagnostic(diagnostics, "Mesh contains an out-of-range vertex index.");
        return false;
    }
    return true;
}

[[nodiscard]] TriangleMetadata metadataAt(const MeshData& mesh, const std::size_t triangle) {
    return mesh.triangleMetadata.empty() ? TriangleMetadata{} : mesh.triangleMetadata[triangle];
}

} // namespace

bool assignGrndTrianglesToIntersectingCells(
    GrndData& data,
    const GrndGridAssignmentOptions& options,
    std::vector<std::string>* diagnostics) {
    if (!validateMesh(data.mesh, diagnostics)) {
        return false;
    }
    if (data.gridX == 0U || data.gridZ == 0U || data.cellSizeX == 0U || data.cellSizeZ == 0U) {
        addDiagnostic(diagnostics, "GRND grid dimensions and cell sizes must be nonzero.");
        return false;
    }
    const auto cellCount = static_cast<std::size_t>(data.gridX) * static_cast<std::size_t>(data.gridZ);
    data.cells.assign(cellCount, GrndCell{});
    const auto triangleCount = data.mesh.indices.size() / 3U;
    for (std::size_t triangle = 0; triangle < triangleCount; ++triangle) {
        const auto& a = data.mesh.vertices[data.mesh.indices[triangle * 3U + 0U]].position;
        const auto& b = data.mesh.vertices[data.mesh.indices[triangle * 3U + 1U]].position;
        const auto& c = data.mesh.vertices[data.mesh.indices[triangle * 3U + 2U]].position;
        const float minX = std::min({ a.x, b.x, c.x });
        const float maxX = std::max({ a.x, b.x, c.x });
        const float minZ = std::min({ a.z, b.z, c.z });
        const float maxZ = std::max({ a.z, b.z, c.z });
        const auto clampCell = [](const float value, const float origin, const float size, const std::uint16_t count) {
            const auto raw = static_cast<long long>(std::floor((value - origin) / size));
            return static_cast<std::size_t>(std::clamp<long long>(raw, 0, static_cast<long long>(count) - 1));
        };
        const auto minCellX = clampCell(minX, options.originX, static_cast<float>(data.cellSizeX), data.gridX);
        const auto maxCellX = clampCell(maxX, options.originX, static_cast<float>(data.cellSizeX), data.gridX);
        const auto minCellZ = clampCell(minZ, options.originZ, static_cast<float>(data.cellSizeZ), data.gridZ);
        const auto maxCellZ = clampCell(maxZ, options.originZ, static_cast<float>(data.cellSizeZ), data.gridZ);
        for (std::size_t z = minCellZ; z <= maxCellZ; ++z) {
            for (std::size_t x = minCellX; x <= maxCellX; ++x) {
                data.cells[z * data.gridX + x].references.push_back(GrndTriangleReference{
                    .meshTriangleIndex = triangle,
                });
            }
        }
    }
    return synchronizeGrndSourceView(data, diagnostics);
}

bool synchronizeGrndSourceView(GrndData& data, std::vector<std::string>* diagnostics) {
    if (!validateMesh(data.mesh, diagnostics)) {
        return false;
    }
    if (data.mesh.vertices.size() > (std::numeric_limits<std::uint16_t>::max() / 6U) + 1U) {
        addDiagnostic(diagnostics, "GRND mesh has too many vertices for 16-bit float indices.");
        return false;
    }
    const auto triangleCount = data.mesh.indices.size() / 3U;
    std::vector<bool> assigned(triangleCount, false);
    for (auto& cell : data.cells) {
        for (auto& reference : cell.references) {
            if (!reference.meshTriangleIndex.has_value() || *reference.meshTriangleIndex >= triangleCount) {
                addDiagnostic(diagnostics, "GRND cell contains a missing or out-of-range mesh triangle assignment.");
                return false;
            }
            const auto triangle = *reference.meshTriangleIndex;
            assigned[triangle] = true;
            reference.triangleSet = 0U;
            reference.streamIndex = static_cast<std::uint16_t>(triangle * 3U);
        }
    }
    if (std::find(assigned.begin(), assigned.end(), false) != assigned.end()) {
        addDiagnostic(diagnostics, "Every GRND triangle must be assigned to at least one grid cell.");
        return false;
    }

    GrndTriangleSet set{};
    set.declaredTriangleCount = static_cast<std::uint32_t>(triangleCount);
    for (std::size_t vertex = 0; vertex < data.mesh.vertices.size(); ++vertex) {
        set.verticesByFloatIndex.emplace(static_cast<std::uint16_t>(vertex * 6U), data.mesh.vertices[vertex]);
    }
    set.streamEntries.reserve(triangleCount * 3U);
    for (std::size_t triangle = 0; triangle < triangleCount; ++triangle) {
        const auto metadata = metadataAt(data.mesh, triangle);
        std::array<std::uint32_t, 3> indices{
            data.mesh.indices[triangle * 3U + 0U],
            data.mesh.indices[triangle * 3U + 1U],
            data.mesh.indices[triangle * 3U + 2U],
        };
        if ((metadata.rawU16[2] & 0x8000U) != 0U) {
            std::swap(indices[0], indices[2]);
        }
        for (std::size_t corner = 0; corner < 3U; ++corner) {
            set.streamEntries.push_back(GrndStreamEntry{
                .floatIndex = static_cast<std::uint16_t>(indices[corner] * 6U),
                .flags = metadata.rawU16[corner],
            });
        }
    }
    data.triangleSets.assign(1U, std::move(set));
    return true;
}

bool synchronizeGobjSourceView(GobjData& data, std::vector<std::string>* diagnostics) {
    for (auto& node : data.nodes) {
        if (node.streamMesh.indices.empty()) {
            node.attach.reset();
            continue;
        }
        if (!validateMesh(node.streamMesh, diagnostics)) {
            return false;
        }
        const bool hasUser = std::any_of(node.streamMesh.vertices.begin(), node.streamMesh.vertices.end(),
            [](const auto& vertex) { return vertex.rawUserAttributesU32.has_value(); });
        const bool hasNormals = std::all_of(node.streamMesh.vertices.begin(), node.streamMesh.vertices.end(),
            [](const auto& vertex) { return vertex.hasNormal; });
        const std::uint8_t recordWords = hasUser ? 7U : (hasNormals ? 6U : 3U);
        if (node.streamMesh.vertices.size() > (std::numeric_limits<std::uint16_t>::max() - 2U) / recordWords + 1U) {
            addDiagnostic(diagnostics, "GOBJ mesh has too many vertices for its selected chunk layout.");
            return false;
        }
        if (!node.attach.has_value()) {
            node.attach.emplace();
        }
        auto& attach = *node.attach;
        attach.vertexChunk.chunkType = hasUser ? 0x2BU : (hasNormals ? 0x29U : 0x22U);
        attach.vertexChunk.recordWords = recordWords;
        attach.vertexChunk.verticesByFloatIndex.clear();
        for (std::size_t vertex = 0; vertex < node.streamMesh.vertices.size(); ++vertex) {
            attach.vertexChunk.verticesByFloatIndex.emplace(
                static_cast<std::uint16_t>(2U + vertex * recordWords), node.streamMesh.vertices[vertex]);
        }
        attach.streamEntries.clear();
        const auto triangleCount = node.streamMesh.indices.size() / 3U;
        for (std::size_t triangle = 0; triangle < triangleCount; ++triangle) {
            const auto metadata = metadataAt(node.streamMesh, triangle);
            std::array<std::uint32_t, 3> indices{
                node.streamMesh.indices[triangle * 3U + 0U],
                node.streamMesh.indices[triangle * 3U + 1U],
                node.streamMesh.indices[triangle * 3U + 2U],
            };
            if ((metadata.rawU16[2] & 0x8000U) != 0U) {
                std::swap(indices[0], indices[2]);
            }
            for (std::size_t corner = 0; corner < 3U; ++corner) {
                attach.streamEntries.push_back(GobjStreamEntry{
                    .floatIndex = static_cast<std::uint16_t>(2U + indices[corner] * recordWords),
                    .flags = metadata.rawU16[corner],
                });
            }
            attach.streamEntries.push_back(GobjStreamEntry{ .floatIndex = 0xFFFFU, .flags = 0xFFFFU, .separator = true });
        }
    }
    return true;
}

} // namespace spice::mld::model
