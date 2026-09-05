#pragma once

#include "../MldDocument.h"
#include "../Model/MldGroundModel.h"

#include <utility>

namespace spice::mld::detail {

[[nodiscard]] inline MldGrndDocument toDocument(const model::GrndData& source) {
    MldGrndDocument result{};
    result.outerHeaderOpaque = source.outerHeaderBytes;
    result.innerHeaderOpaque = source.innerHeaderUnknownBytes;
    result.gridOriginX = source.gridOriginX;
    result.gridOriginZ = source.gridOriginZ;
    result.gridX = source.gridX;
    result.gridZ = source.gridZ;
    result.cellSizeX = source.cellSizeX;
    result.cellSizeZ = source.cellSizeZ;
    result.mesh = source.mesh;
    result.cells.reserve(source.cells.size());
    for (const auto& cell : source.cells) {
        MldGrndCell output{};
        for (const auto& reference : cell.references)
            if (reference.meshTriangleIndex.has_value()) output.triangleIndices.push_back(*reference.meshTriangleIndex);
        result.cells.push_back(std::move(output));
    }
    return result;
}

[[nodiscard]] inline model::GrndData fromDocument(const MldGrndDocument& source) {
    model::GrndData result{};
    result.outerHeaderBytes = source.outerHeaderOpaque;
    result.innerHeaderUnknownBytes = source.innerHeaderOpaque;
    result.gridOriginX = source.gridOriginX;
    result.gridOriginZ = source.gridOriginZ;
    result.gridX = source.gridX;
    result.gridZ = source.gridZ;
    result.cellSizeX = source.cellSizeX;
    result.cellSizeZ = source.cellSizeZ;
    result.mesh = source.mesh;
    result.cells.reserve(source.cells.size());
    for (const auto& cell : source.cells) {
        model::GrndCell output{};
        for (const auto triangle : cell.triangleIndices)
            output.references.push_back(model::GrndTriangleReference{ .meshTriangleIndex = triangle });
        result.cells.push_back(std::move(output));
    }
    return result;
}

[[nodiscard]] inline MldGobjDocument toDocument(const model::GobjData& source) {
    MldGobjDocument result{};
    result.outerHeaderOpaque = source.outerHeaderBytes;
    result.rootNodes = source.rootNodeIndices;
    result.nodes.reserve(source.nodes.size());
    for (const auto& node : source.nodes) {
        MldGobjNode output{};
        output.nodeOpaque = node.sourceBytes;
        output.transform = node.transform;
        output.parentNode = node.parentNodeIndex;
        output.childNodes = node.childNodeIndices;
        output.mesh = node.streamMesh;
        if (node.attach.has_value()) {
            output.attachPrefixOpaque = node.attach->prefixBytes;
            output.vertexHeaderWord0 = node.attach->vertexChunk.headerWord0;
            output.vertexHeaderWord1 = node.attach->vertexChunk.headerWord1;
        }
        result.nodes.push_back(std::move(output));
    }
    return result;
}

[[nodiscard]] inline model::GobjData fromDocument(const MldGobjDocument& source) {
    model::GobjData result{};
    result.outerHeaderBytes = source.outerHeaderOpaque;
    result.rootNodeIndices = source.rootNodes;
    result.nodes.reserve(source.nodes.size());
    for (const auto& node : source.nodes) {
        model::GobjNode output{};
        output.sourceBytes = node.nodeOpaque;
        output.transform = node.transform;
        output.parentNodeIndex = node.parentNode;
        output.childNodeIndices = node.childNodes;
        output.streamMesh = node.mesh;
        if (!node.mesh.indices.empty() || !node.attachPrefixOpaque.empty()) {
            output.attach.emplace();
            output.attach->prefixBytes = node.attachPrefixOpaque;
            output.attach->vertexChunk.headerWord0 = node.vertexHeaderWord0;
            output.attach->vertexChunk.headerWord1 = node.vertexHeaderWord1;
        }
        result.nodes.push_back(std::move(output));
    }
    return result;
}

} // namespace spice::mld::detail
