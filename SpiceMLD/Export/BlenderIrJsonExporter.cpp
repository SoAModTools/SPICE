#include "BlenderIrJsonExporter.h"

#include <cmath>
#include <cstdint>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace spice::mld::exporting {
namespace {

void writeJsonString(std::ostringstream& out, const std::string& value) {
    out << '"';
    for (const auto ch : value) {
        switch (ch) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default: out << ch; break;
        }
    }
    out << '"';
}

struct NonFiniteCollector {
    static constexpr std::size_t kPathLimit = 32U;
    std::size_t count = 0;
    std::vector<std::string> paths{};

    void record(std::string path) {
        ++count;
        if (paths.size() < kPathLimit) {
            paths.push_back(std::move(path));
        }
    }

    [[nodiscard]] std::string diagnostic() const {
        std::string message = "Blender IR JSON sanitized " + std::to_string(count) +
            " non-finite floating-point value(s) to 0.0";
        if (!paths.empty()) {
            message += "; paths: ";
            for (std::size_t i = 0; i < paths.size(); ++i) {
                if (i != 0U) {
                    message += ", ";
                }
                message += paths[i];
            }
        }
        if (count > paths.size()) {
            message += "; omitted " + std::to_string(count - paths.size()) + " additional path(s)";
        }
        return message + ".";
    }
};

void writeFiniteFloat(
    std::ostringstream& out,
    const float value,
    std::string path,
    NonFiniteCollector& nonFinite) {
    if (std::isfinite(value)) {
        out << value;
    } else {
        out << "0.0";
        nonFinite.record(std::move(path));
    }
}

void writeVec3(
    std::ostringstream& out,
    const model::Vec3& v,
    NonFiniteCollector& nonFinite,
    const std::string& path) {
    out << '[';
    writeFiniteFloat(out, v.x, path + ".x", nonFinite);
    out << ',';
    writeFiniteFloat(out, v.y, path + ".y", nonFinite);
    out << ',';
    writeFiniteFloat(out, v.z, path + ".z", nonFinite);
    out << ']';
}

void writeVec2(
    std::ostringstream& out,
    const model::Vec2& v,
    NonFiniteCollector& nonFinite,
    const std::string& path) {
    out << '[';
    writeFiniteFloat(out, v.x, path + ".x", nonFinite);
    out << ',';
    writeFiniteFloat(out, v.y, path + ".y", nonFinite);
    out << ']';
}

void writeQuat(
    std::ostringstream& out,
    const model::Quat& q,
    NonFiniteCollector& nonFinite,
    const std::string& path) {
    out << '[';
    writeFiniteFloat(out, q.x, path + ".x", nonFinite);
    out << ',';
    writeFiniteFloat(out, q.y, path + ".y", nonFinite);
    out << ',';
    writeFiniteFloat(out, q.z, path + ".z", nonFinite);
    out << ',';
    writeFiniteFloat(out, q.w, path + ".w", nonFinite);
    out << ']';
}

void writeColor(std::ostringstream& out, const model::ColorRgba8& c) {
    out << '[' << static_cast<unsigned int>(c.r)
        << ',' << static_cast<unsigned int>(c.g)
        << ',' << static_cast<unsigned int>(c.b)
        << ',' << static_cast<unsigned int>(c.a) << ']';
}

void writeSpot(
    std::ostringstream& out,
    const model::SpotlightValue& spot,
    NonFiniteCollector& nonFinite,
    const std::string& path) {
    out << "{\"nearDistance\":";
    writeFiniteFloat(out, spot.nearDistance, path + ".nearDistance", nonFinite);
    out << ",\"farDistance\":";
    writeFiniteFloat(out, spot.farDistance, path + ".farDistance", nonFinite);
    out << ",\"insideAngle\":";
    writeFiniteFloat(out, spot.insideAngle, path + ".insideAngle", nonFinite);
    out << ",\"outsideAngle\":";
    writeFiniteFloat(out, spot.outsideAngle, path + ".outsideAngle", nonFinite);
    out << '}';
}

void writeTransform(
    std::ostringstream& out,
    const model::Transform& tx,
    NonFiniteCollector& nonFinite,
    const std::string& path) {
    out << "{\"position\":";
    writeVec3(out, tx.position, nonFinite, path + ".position");
    out << ",\"rotationRaw\":";
    writeVec3(out, tx.rotationRaw, nonFinite, path + ".rotationRaw");
    out << ",\"rotation\":";
    writeQuat(out, tx.rotation, nonFinite, path + ".rotation");
    out << ",\"scale\":";
    writeVec3(out, tx.scale, nonFinite, path + ".scale");
    out << '}';
}

void writeVec3Keyframes(
    std::ostringstream& out,
    const std::vector<model::BlenderIrVec3Keyframe>& keys,
    NonFiniteCollector& nonFinite,
    const std::string& path) {
    out << '[';
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << "{\"frame\":" << keys[i].frame << ",\"value\":";
        writeVec3(out, keys[i].value, nonFinite, path + "[" + std::to_string(i) + "].value");
        out << '}';
    }
    out << ']';
}

void writeQuatKeyframes(
    std::ostringstream& out,
    const std::vector<model::BlenderIrQuatKeyframe>& keys,
    NonFiniteCollector& nonFinite,
    const std::string& path) {
    out << '[';
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << "{\"frame\":" << keys[i].frame << ",\"value\":";
        writeQuat(out, keys[i].value, nonFinite, path + "[" + std::to_string(i) + "].value");
        out << '}';
    }
    out << ']';
}

void writeVec2Keyframes(
    std::ostringstream& out,
    const std::vector<model::BlenderIrVec2Keyframe>& keys,
    NonFiniteCollector& nonFinite,
    const std::string& path) {
    out << '[';
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << "{\"frame\":" << keys[i].frame << ",\"value\":";
        writeVec2(out, keys[i].value, nonFinite, path + "[" + std::to_string(i) + "].value");
        out << '}';
    }
    out << ']';
}

void writeFloatKeyframes(
    std::ostringstream& out,
    const std::vector<model::BlenderIrFloatKeyframe>& keys,
    NonFiniteCollector& nonFinite,
    const std::string& path) {
    out << '[';
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << "{\"frame\":" << keys[i].frame << ",\"value\":";
        writeFiniteFloat(out, keys[i].value, path + "[" + std::to_string(i) + "].value", nonFinite);
        out << '}';
    }
    out << ']';
}

void writeColorKeyframes(std::ostringstream& out, const std::vector<model::BlenderIrColorKeyframe>& keys) {
    out << '[';
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << "{\"frame\":" << keys[i].frame << ",\"value\":";
        writeColor(out, keys[i].value);
        out << '}';
    }
    out << ']';
}

void writeSpotKeyframes(
    std::ostringstream& out,
    const std::vector<model::BlenderIrSpotKeyframe>& keys,
    NonFiniteCollector& nonFinite,
    const std::string& path) {
    out << '[';
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << "{\"frame\":" << keys[i].frame << ",\"value\":";
        writeSpot(out, keys[i].value, nonFinite, path + "[" + std::to_string(i) + "].value");
        out << '}';
    }
    out << ']';
}

void writeVectorArrayKeyframes(
    std::ostringstream& out,
    const std::vector<model::BlenderIrVectorArrayKeyframe>& keys,
    NonFiniteCollector& nonFinite,
    const std::string& path) {
    out << '[';
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << "{\"frame\":" << keys[i].frame << ",\"label\":";
        writeJsonString(out, keys[i].label);
        out << ",\"values\":[";
        for (std::size_t vi = 0; vi < keys[i].values.size(); ++vi) {
            if (vi != 0) {
                out << ',';
            }
            writeVec3(out, keys[i].values[vi], nonFinite,
                path + "[" + std::to_string(i) + "].values[" + std::to_string(vi) + "]");
        }
        out << "]}";
    }
    out << ']';
}

void writeAnimationChannelKeyframes(
    std::ostringstream& out,
    const model::BlenderIrAnimationChannel& channel,
    NonFiniteCollector& nonFinite,
    const std::string& path) {
    if (!channel.vec3Values.empty()) {
        writeVec3Keyframes(out, channel.vec3Values, nonFinite, path);
    } else if (!channel.vec2Values.empty()) {
        writeVec2Keyframes(out, channel.vec2Values, nonFinite, path);
    } else if (!channel.floatValues.empty()) {
        writeFloatKeyframes(out, channel.floatValues, nonFinite, path);
    } else if (!channel.colorValues.empty()) {
        writeColorKeyframes(out, channel.colorValues);
    } else if (!channel.spotValues.empty()) {
        writeSpotKeyframes(out, channel.spotValues, nonFinite, path);
    } else {
        writeVectorArrayKeyframes(out, channel.vectorArrayValues, nonFinite, path);
    }
}

[[nodiscard]] std::string toBase64(std::span<const std::uint8_t> data) {
    static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out{};
    out.reserve(((data.size() + 2U) / 3U) * 4U);

    std::size_t i = 0;
    while (i + 2U < data.size()) {
        const std::uint32_t chunk = (static_cast<std::uint32_t>(data[i]) << 16U) |
            (static_cast<std::uint32_t>(data[i + 1U]) << 8U) |
            static_cast<std::uint32_t>(data[i + 2U]);
        out.push_back(kTable[(chunk >> 18U) & 0x3FU]);
        out.push_back(kTable[(chunk >> 12U) & 0x3FU]);
        out.push_back(kTable[(chunk >> 6U) & 0x3FU]);
        out.push_back(kTable[chunk & 0x3FU]);
        i += 3U;
    }

    const auto remaining = data.size() - i;
    if (remaining == 1U) {
        const std::uint32_t chunk = static_cast<std::uint32_t>(data[i]) << 16U;
        out.push_back(kTable[(chunk >> 18U) & 0x3FU]);
        out.push_back(kTable[(chunk >> 12U) & 0x3FU]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2U) {
        const std::uint32_t chunk = (static_cast<std::uint32_t>(data[i]) << 16U) |
            (static_cast<std::uint32_t>(data[i + 1U]) << 8U);
        out.push_back(kTable[(chunk >> 18U) & 0x3FU]);
        out.push_back(kTable[(chunk >> 12U) & 0x3FU]);
        out.push_back(kTable[(chunk >> 6U) & 0x3FU]);
        out.push_back('=');
    }

    return out;
}

} // namespace

std::string BlenderIrJsonExporter::toJson(const model::BlenderIrScene& scene) const {
    std::ostringstream out;
    NonFiniteCollector nonFinite{};
    out << "{\"meshes\":[";

    for (std::size_t meshIdx = 0; meshIdx < scene.meshes.size(); ++meshIdx) {
        if (meshIdx != 0) {
            out << ',';
        }

        const auto& mesh = scene.meshes[meshIdx];
        out << '{';
        out << "\"label\":";
        writeJsonString(out, mesh.label);
        out << ",\"sourceObjectAddress\":" << mesh.sourceObjectAddress;
        out << ",\"sourceChunkOffset\":" << mesh.sourceChunkOffset;
        out << ",\"sourceAttachOffset\":" << mesh.sourceAttachOffset;
        out << ",\"weightedBinding\":";
        if (mesh.weightedBinding.has_value()) {
            out << "{\"rootNodeIndex\":" << mesh.weightedBinding->rootNodeIndex
                << ",\"sourceNodeIndex\":" << mesh.weightedBinding->sourceNodeIndex
                << ",\"nodeIndices\":[";
            for (std::size_t ni = 0; ni < mesh.weightedBinding->nodeIndices.size(); ++ni) {
                if (ni != 0) {
                    out << ',';
                }
                out << mesh.weightedBinding->nodeIndices[ni];
            }
            out << "]}";
        } else {
            out << "null";
        }

        out << ",\"vertices\":[";
        for (std::size_t vIdx = 0; vIdx < mesh.vertices.size(); ++vIdx) {
            if (vIdx != 0) {
                out << ',';
            }
            const auto& v = mesh.vertices[vIdx];
            const auto vertexPath = "meshes[" + std::to_string(meshIdx) + "].vertices[" +
                std::to_string(vIdx) + "]";
            out << '{';
            out << "\"position\":";
            writeVec3(out, v.position, nonFinite, vertexPath + ".position");
            out << ",\"hasPosition\":" << (v.hasPosition ? "true" : "false");
            out << ",\"normal\":";
            writeVec3(out, v.normal, nonFinite, vertexPath + ".normal");
            out << ",\"hasNormal\":" << (v.hasNormal ? "true" : "false");
            out << ",\"rawUserAttributesU32\":";
            if (v.rawUserAttributesU32.has_value()) {
                out << *v.rawUserAttributesU32;
            } else {
                out << "null";
            }
            out << ",\"weights\":[";
            for (std::size_t wi = 0; wi < v.weights.size(); ++wi) {
                if (wi != 0) {
                    out << ',';
                }
                out << "{\"boneOrNodeIndex\":" << v.weights[wi].boneOrNodeIndex
                    << ",\"weight\":";
                writeFiniteFloat(out, v.weights[wi].weight,
                    vertexPath + ".weights[" + std::to_string(wi) + "].weight", nonFinite);
                out << '}';
            }
            out << "]}";
        }
        out << ']';

        out << ",\"materials\":[";
        for (std::size_t matIdx = 0; matIdx < mesh.materials.size(); ++matIdx) {
            if (matIdx != 0) {
                out << ',';
            }
            const auto& material = mesh.materials[matIdx];
            out << '{'
                << "\"polyType\":" << static_cast<unsigned>(material.polyType)
                << ",\"chunkFlags\":" << static_cast<unsigned>(material.chunkFlags)
                << ",\"fromCacheReplay\":" << (material.fromCacheReplay ? "true" : "false")
                << ",\"flatShading\":" << (material.flatShading ? "true" : "false")
                << ",\"materialStateKey\":" << material.materialStateKey
                << ",\"useTexture\":" << (material.useTexture ? "true" : "false")
                << ",\"useAlpha\":" << (material.useAlpha ? "true" : "false")
                << ",\"noAlphaTest\":" << (material.noAlphaTest ? "true" : "false")
                << ",\"doubleSided\":" << (material.doubleSided ? "true" : "false")
                << ",\"clampU\":" << (material.clampU ? "true" : "false")
                << ",\"clampV\":" << (material.clampV ? "true" : "false")
                << ",\"mirrorU\":" << (material.mirrorU ? "true" : "false")
                << ",\"mirrorV\":" << (material.mirrorV ? "true" : "false")
                << ",\"normalMapping\":" << (material.normalMapping ? "true" : "false")
                << ",\"noLighting\":" << (material.noLighting ? "true" : "false")
                << ",\"noAmbient\":" << (material.noAmbient ? "true" : "false")
                << ",\"noSpecular\":" << (material.noSpecular ? "true" : "false")
                << ",\"anisotropicFiltering\":" << (material.anisotropicFiltering ? "true" : "false")
                << ",\"textureFiltering\":" << static_cast<unsigned>(material.textureFiltering)
                << ",\"sourceAlpha\":" << static_cast<unsigned>(material.sourceAlpha)
                << ",\"destinationAlpha\":" << static_cast<unsigned>(material.destinationAlpha)
                << ",\"mipmapDistanceMultiplier\":";
            writeFiniteFloat(out, material.mipmapDistanceMultiplier,
                "meshes[" + std::to_string(meshIdx) + "].materials[" + std::to_string(matIdx) +
                    "].mipmapDistanceMultiplier",
                nonFinite);
            out << ",\"textureId\":" << material.textureId
                << ",\"textureName\":";
            writeJsonString(out, material.textureName);
            out
                << ",\"materialHash\":" << material.materialHash
                << '}';
        }
        out << ']';

        out << ",\"triangleSets\":[";
        for (std::size_t tsIdx = 0; tsIdx < mesh.triangleSets.size(); ++tsIdx) {
            if (tsIdx != 0) {
                out << ',';
            }
            const auto& ts = mesh.triangleSets[tsIdx];
            out << '{'
                << "\"materialIndex\":" << ts.materialIndex
                << ",\"polyType\":" << static_cast<unsigned>(ts.polyType)
                << ",\"sourceChunkOffset\":" << ts.sourceChunkOffset
                << ",\"fromCacheReplay\":" << (ts.fromCacheReplay ? "true" : "false")
                << ",\"corners\":[";
            for (std::size_t cIdx = 0; cIdx < ts.corners.size(); ++cIdx) {
                if (cIdx != 0) {
                    out << ',';
                }
                const auto& corner = ts.corners[cIdx];
                const auto cornerPath = "meshes[" + std::to_string(meshIdx) + "].triangleSets[" +
                    std::to_string(tsIdx) + "].corners[" + std::to_string(cIdx) + "]";
                out << '{'
                    << "\"vertexIndex\":" << corner.vertexIndex
                    << ",\"u\":";
                writeFiniteFloat(out, corner.u, cornerPath + ".u", nonFinite);
                out << ",\"v\":";
                writeFiniteFloat(out, corner.v, cornerPath + ".v", nonFinite);
                out << ",\"hasUv\":" << (corner.hasUv ? "true" : "false")
                    << ",\"color\":[";
                writeFiniteFloat(out, corner.colorR, cornerPath + ".color[0]", nonFinite);
                out << ',';
                writeFiniteFloat(out, corner.colorG, cornerPath + ".color[1]", nonFinite);
                out << ',';
                writeFiniteFloat(out, corner.colorB, cornerPath + ".color[2]", nonFinite);
                out << ',';
                writeFiniteFloat(out, corner.colorA, cornerPath + ".color[3]", nonFinite);
                out << "]"
                    << ",\"hasColor\":" << (corner.hasColor ? "true" : "false")
                    << '}';
            }
            out << "],\"triangleMetadata\":[";
            for (std::size_t triangleIdx = 0; triangleIdx < ts.triangleMetadata.size(); ++triangleIdx) {
                if (triangleIdx != 0) {
                    out << ',';
                }
                const auto& metadata = ts.triangleMetadata[triangleIdx];
                out << "{\"rawU16\":["
                    << metadata.rawU16[0] << ','
                    << metadata.rawU16[1] << ','
                    << metadata.rawU16[2] << "]}";
            }
            out << "]}";
        }
        out << ']';

        out << ",\"diagnostics\":{"
            << "\"degenerateTriangleCount\":" << mesh.diagnostics.degenerateTriangleCount << ','
            << "\"outOfRangeIndexCount\":" << mesh.diagnostics.outOfRangeIndexCount << ','
            << "\"cacheReplayTriangleCount\":" << mesh.diagnostics.cacheReplayTriangleCount
            << "}";

        out << '}';
    }

    out << "],\"objectTrees\":[";
    for (std::size_t treeIdx = 0; treeIdx < scene.objectTrees.size(); ++treeIdx) {
        if (treeIdx != 0) {
            out << ',';
        }
        const auto& tree = scene.objectTrees[treeIdx];
        out << '{';
        out << "\"label\":";
        writeJsonString(out, tree.label);
        out << ",\"sourceObjectAddress\":" << tree.sourceObjectAddress;
        out << ",\"sourceChunkOffset\":" << tree.sourceChunkOffset;
        out << ",\"rootNodeIndices\":[";
        for (std::size_t ri = 0; ri < tree.rootNodeIndices.size(); ++ri) {
            if (ri != 0) {
                out << ',';
            }
            out << tree.rootNodeIndices[ri];
        }
        out << ']';

        out << ",\"nodes\":[";
        for (std::size_t nodeIdx = 0; nodeIdx < tree.nodes.size(); ++nodeIdx) {
            if (nodeIdx != 0) {
                out << ',';
            }
            const auto& node = tree.nodes[nodeIdx];
            out << '{'
                << "\"sourceNodeOffset\":" << node.sourceNodeOffset
                << ",\"sourceEvalFlags\":" << node.sourceEvalFlags
                << ",\"sourceAttachOffset\":" << node.sourceAttachOffset
                << ",\"hasAttach\":" << (node.hasAttach ? "true" : "false")
                << ",\"meshIndex\":";
            if (node.meshIndex.has_value()) {
                out << *node.meshIndex;
            } else {
                out << "null";
            }
            out << ",\"localTransform\":";
            writeTransform(out, node.localTransform, nonFinite,
                "objectTrees[" + std::to_string(treeIdx) + "].nodes[" + std::to_string(nodeIdx) +
                    "].localTransform");
            out << ",\"parentNodeIndex\":";
            if (node.parentNodeIndex.has_value()) {
                out << *node.parentNodeIndex;
            } else {
                out << "null";
            }
            out << ",\"childNodeIndices\":[";
            for (std::size_t ci = 0; ci < node.childNodeIndices.size(); ++ci) {
                if (ci != 0) {
                    out << ',';
                }
                out << node.childNodeIndices[ci];
            }
            out << "],\"auxiliaryGeometry\":[";
            for (std::size_t ai = 0; ai < node.auxiliaryGeometry.size(); ++ai) {
                if (ai != 0) {
                    out << ',';
                }
                const auto& auxiliary = node.auxiliaryGeometry[ai];
                const auto auxiliaryPath = "objectTrees[" + std::to_string(treeIdx) + "].nodes[" +
                    std::to_string(nodeIdx) + "].auxiliaryGeometry[" + std::to_string(ai) + "]";
                out << "{\"role\":\"type56ShadowVolume\""
                    << ",\"sourceAttachOffset\":" << auxiliary.sourceAttachOffset
                    << ",\"sourceChunkOffset\":" << auxiliary.sourceChunkOffset
                    << ",\"sourceChunkType\":" << static_cast<unsigned>(auxiliary.sourceChunkType)
                    << ",\"chunkAttributes\":" << static_cast<unsigned>(auxiliary.chunkAttributes)
                    << ",\"rawCountAndUserWord\":" << auxiliary.rawCountAndUserWord
                    << ",\"fromCacheReplay\":" << (auxiliary.fromCacheReplay ? "true" : "false")
                    << ",\"vertices\":[";
                for (std::size_t vi = 0; vi < auxiliary.vertices.size(); ++vi) {
                    if (vi != 0) {
                        out << ',';
                    }
                    out << "{\"sourceCacheIndex\":" << auxiliary.vertices[vi].sourceCacheIndex
                        << ",\"position\":";
                    writeVec3(out, auxiliary.vertices[vi].position, nonFinite,
                        auxiliaryPath + ".vertices[" + std::to_string(vi) + "].position");
                    out << '}';
                }
                out << "],\"triangles\":[";
                for (std::size_t pi = 0; pi < auxiliary.triangles.size(); ++pi) {
                    if (pi != 0) {
                        out << ',';
                    }
                    const auto& triangle = auxiliary.triangles[pi];
                    out << "{\"primitiveOrdinal\":" << triangle.primitiveOrdinal
                        << ",\"sourceIndices\":[" << triangle.sourceIndices[0] << ','
                        << triangle.sourceIndices[1] << ',' << triangle.sourceIndices[2] << ']'
                        << ",\"vertexIndices\":[" << triangle.vertexIndices[0] << ','
                        << triangle.vertexIndices[1] << ',' << triangle.vertexIndices[2] << ']'
                        << ",\"userWords\":[";
                    for (std::size_t ui = 0; ui < triangle.userWords.size(); ++ui) {
                        if (ui != 0) {
                            out << ',';
                        }
                        out << triangle.userWords[ui];
                    }
                    out << "]}";
                }
                out << "]}";
            }
            out << "]}";
        }
        out << "]}";
    }

    out << "],\"indexEntries\":[";
    for (std::size_t idx = 0; idx < scene.indexEntries.size(); ++idx) {
        if (idx != 0) {
            out << ',';
        }

        const auto& entry = scene.indexEntries[idx];
        out << '{';
        out << "\"sourceEntryId\":" << entry.sourceEntryId;
        out << ",\"tableIndex\":" << entry.tableIndex;
        out << ",\"tblId\":" << entry.tblId;
        out << ",\"fxnName\":";
        writeJsonString(out, entry.fxnName);
        out << ",\"transform\":";
        writeTransform(out, entry.transform, nonFinite,
            "indexEntries[" + std::to_string(idx) + "].transform");

        out << ",\"functionParameters\":[";
        for (std::size_t pi = 0; pi < entry.functionParameters.size(); ++pi) {
            if (pi != 0) {
                out << ',';
            }
            out << entry.functionParameters[pi];
        }
        out << ']';

        out << ",\"objectAddresses\":[";
        for (std::size_t oi = 0; oi < entry.objectAddresses.size(); ++oi) {
            if (oi != 0) {
                out << ',';
            }
            out << entry.objectAddresses[oi];
        }
        out << ']';

        out << ",\"groundAddresses\":[";
        for (std::size_t gi = 0; gi < entry.groundAddresses.size(); ++gi) {
            if (gi != 0) {
                out << ',';
            }
            out << entry.groundAddresses[gi];
        }
        out << ']';

        out << ",\"meshIndices\":[";
        for (std::size_t mi = 0; mi < entry.meshIndices.size(); ++mi) {
            if (mi != 0) {
                out << ',';
            }
            out << entry.meshIndices[mi];
        }
        out << ']';
        out << ",\"objectTreeIndices\":[";
        for (std::size_t oi = 0; oi < entry.objectTreeIndices.size(); ++oi) {
            if (oi != 0) {
                out << ',';
            }
            out << entry.objectTreeIndices[oi];
        }
        out << ']';

        out << ",\"cameraMotions\":[";
        for (std::size_t ci = 0; ci < entry.cameraMotions.size(); ++ci) {
            if (ci != 0) {
                out << ',';
            }
            const auto& camera = entry.cameraMotions[ci];
            const auto cameraPath = "indexEntries[" + std::to_string(idx) + "].cameraMotions[" +
                std::to_string(ci) + "]";
            out << "{\"sourceMotionAddress\":" << camera.sourceMotionAddress
                << ",\"motionSlot\":" << camera.motionSlot
                << ",\"frameCount\":" << camera.frameCount
                << ",\"interpolationMode\":";
            writeJsonString(out, camera.interpolationMode);
            out << ",\"position\":";
            writeVec3Keyframes(out, camera.position, nonFinite, cameraPath + ".position");
            out << ",\"target\":";
            writeVec3Keyframes(out, camera.target, nonFinite, cameraPath + ".target");
            out << ",\"unsupportedChannels\":[";
            for (std::size_t ui = 0; ui < camera.unsupportedChannels.size(); ++ui) {
                if (ui != 0) {
                    out << ',';
                }
                const auto& unsupported = camera.unsupportedChannels[ui];
                out << "{\"nodeIndex\":" << unsupported.nodeIndex << ",\"channel\":";
                writeJsonString(out, unsupported.channel);
                out << ",\"keyframeCount\":" << unsupported.keyframeCount << '}';
            }
            out << "]}";
        }
        out << ']';

        out << '}';
    }

    out << "],\"animations\":[";
    for (std::size_t ai = 0; ai < scene.animations.size(); ++ai) {
        if (ai != 0) {
            out << ',';
        }
        const auto& animation = scene.animations[ai];
        out << '{'
            << "\"sourceEntryId\":" << animation.sourceEntryId;
        out
            << ",\"tableIndex\":" << animation.tableIndex
            << ",\"sourceObjectAddress\":" << animation.sourceObjectAddress
            << ",\"sourceMotionAddress\":" << animation.sourceMotionAddress
            << ",\"motionSlot\":" << animation.motionSlot
            << ",\"objectTreeIndex\":" << animation.objectTreeIndex
            << ",\"nodeCount\":" << animation.nodeCount
            << ",\"frameCount\":" << animation.frameCount
            << ",\"interpolationMode\":";
        writeJsonString(out, animation.interpolationMode);

        out << ",\"nodes\":[";
        for (std::size_t ni = 0; ni < animation.nodes.size(); ++ni) {
            if (ni != 0) {
                out << ',';
            }
            const auto& node = animation.nodes[ni];
            const auto nodePath = "animations[" + std::to_string(ai) + "].nodes[" + std::to_string(ni) + "]";
            out << "{\"nodeIndex\":" << node.nodeIndex
                << ",\"position\":";
            writeVec3Keyframes(out, node.position, nonFinite, nodePath + ".position");
            out << ",\"eulerRotation\":";
            writeVec3Keyframes(out, node.eulerRotation, nonFinite, nodePath + ".eulerRotation");
            out << ",\"scale\":";
            writeVec3Keyframes(out, node.scale, nonFinite, nodePath + ".scale");
            out << ",\"quaternionRotation\":";
            writeQuatKeyframes(out, node.quaternionRotation, nonFinite, nodePath + ".quaternionRotation");
            out << '}';
        }
        out << ']';

        out << ",\"channels\":[";
        for (std::size_t ci = 0; ci < animation.channels.size(); ++ci) {
            if (ci != 0) {
                out << ',';
            }
            const auto& channel = animation.channels[ci];
            out << "{\"nodeIndex\":" << channel.nodeIndex
                << ",\"channel\":";
            writeJsonString(out, channel.channel);
            out << ",\"valueType\":";
            writeJsonString(out, channel.valueType);
            out << ",\"keyframes\":";
            writeAnimationChannelKeyframes(out, channel, nonFinite,
                "animations[" + std::to_string(ai) + "].channels[" + std::to_string(ci) + "].keyframes");
            out << '}';
        }
        out << ']';

        out << ",\"unsupportedChannels\":[";
        for (std::size_t ui = 0; ui < animation.unsupportedChannels.size(); ++ui) {
            if (ui != 0) {
                out << ',';
            }
            const auto& unsupported = animation.unsupportedChannels[ui];
            out << "{\"nodeIndex\":" << unsupported.nodeIndex
                << ",\"channel\":";
            writeJsonString(out, unsupported.channel);
            out << ",\"keyframeCount\":" << unsupported.keyframeCount << '}';
        }
        out << "]}";
    }

    out << "],\"textures\":[";
    for (std::size_t ti = 0; ti < scene.textures.size(); ++ti) {
        if (ti != 0) {
            out << ',';
        }
        const auto& t = scene.textures[ti];
        out << '{'
            << "\"textureId\":";
        if (t.hasTextureId) {
            out << t.textureId;
        } else {
            out << "null";
        }
        out
            << ",\"hasTextureId\":" << (t.hasTextureId ? "true" : "false")
            << ",\"sourceOffset\":" << t.sourceOffset
            << ",\"sourceSize\":" << t.sourceSize
            << ",\"encodedFormat\":";
        writeJsonString(out, t.encodedFormat);
        out
            << ",\"textureName\":";
        writeJsonString(out, t.textureName);
        out << ",\"sourceContainer\":";
        writeJsonString(out, t.sourceContainer);
        out << ",\"sourceTextureFormat\":";
        writeJsonString(out, t.sourceTextureFormat);
        out << ",\"sourcePaletteFormat\":";
        writeJsonString(out, t.sourcePaletteFormat);
        out << ",\"hasDecodedPixels\":" << (t.hasDecodedPixels ? "true" : "false");
        out << ",\"encodedDataBase64\":";
        writeJsonString(out, toBase64(std::span<const std::uint8_t>(t.encodedData.data(), t.encodedData.size())));
        out << ",\"width\":" << t.width
            << ",\"height\":" << t.height
            << ",\"pixelFormat\":";
        writeJsonString(out, t.pixelFormat);
        out << ",\"pixelDataBase64\":";
        writeJsonString(out, toBase64(std::span<const std::uint8_t>(t.pixelData.data(), t.pixelData.size())));
        out << ",\"decodeWarnings\":[";
        for (std::size_t wi = 0; wi < t.decodeWarnings.size(); ++wi) {
            if (wi != 0) {
                out << ',';
            }
            writeJsonString(out, t.decodeWarnings[wi]);
        }
        out << ']';
        out
            << '}';
    }

    out << "],\"diagnostics\":[";
    for (std::size_t ii = 0; ii < scene.diagnostics.size(); ++ii) {
        if (ii != 0) {
            out << ',';
        }
        writeJsonString(out, scene.diagnostics[ii]);
    }
    if (nonFinite.count != 0U) {
        if (!scene.diagnostics.empty()) {
            out << ',';
        }
        writeJsonString(out, nonFinite.diagnostic());
    }
    out << "]}";

    return out.str();
}

} // namespace spice::mld::exporting
