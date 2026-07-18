#include "MldFileWriter.h"

#include "../Model/MldGroundEditing.h"
#include "../../Compression/Aklz.h"
#include "../../SpiceCore/Binary/EndianReader.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <unordered_map>

namespace spice::mld::exporting {
namespace {

using spice::core::Endian;
using spice::core::EndianReader;

constexpr std::size_t kHeaderSize = 0x14U;
constexpr std::size_t kEntrySize = 0x68U;
constexpr float kTau = 6.28318530717958647692F;

void addDiagnostic(MldWriteResult& result, const model::MldDiagnostic::Severity severity,
    std::string message, const std::optional<std::uint32_t> offset = std::nullopt) {
    result.diagnostics.push_back(model::MldDiagnostic{
        .severity = severity,
        .message = std::move(message),
        .sourceOffset = offset,
    });
}

[[nodiscard]] Endian endianForPlatform(const model::TargetPlatform platform, const Endian fallback) {
    if (platform == model::TargetPlatform::Dreamcast) {
        return Endian::Little;
    }
    if (platform == model::TargetPlatform::GameCube) {
        return Endian::Big;
    }
    return fallback;
}

[[nodiscard]] model::TargetPlatform platformForEndian(const Endian endian) {
    return endian == Endian::Little ? model::TargetPlatform::Dreamcast : model::TargetPlatform::GameCube;
}

void ensureSize(std::vector<std::uint8_t>& bytes, const std::size_t size) {
    if (bytes.size() < size) {
        bytes.resize(size, 0U);
    }
}

void writeU16(std::vector<std::uint8_t>& bytes, const std::size_t offset,
    const std::uint16_t value, const Endian endian) {
    ensureSize(bytes, offset + 2U);
    if (endian == Endian::Big) {
        bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
        bytes[offset + 1U] = static_cast<std::uint8_t>(value);
    } else {
        bytes[offset] = static_cast<std::uint8_t>(value);
        bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    }
}

void writeU32(std::vector<std::uint8_t>& bytes, const std::size_t offset,
    const std::uint32_t value, const Endian endian) {
    ensureSize(bytes, offset + 4U);
    if (endian == Endian::Big) {
        bytes[offset] = static_cast<std::uint8_t>(value >> 24U);
        bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 16U);
        bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[offset + 3U] = static_cast<std::uint8_t>(value);
    } else {
        bytes[offset] = static_cast<std::uint8_t>(value);
        bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
        bytes[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
    }
}

void writeF32(std::vector<std::uint8_t>& bytes, const std::size_t offset,
    const float value, const Endian endian) {
    writeU32(bytes, offset, std::bit_cast<std::uint32_t>(value), endian);
}

void writeTag(std::vector<std::uint8_t>& bytes, const std::size_t offset, const char* tag) {
    ensureSize(bytes, offset + 4U);
    for (std::size_t i = 0; i < 4U; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>(tag[i]);
    }
}

void writeAscii(std::vector<std::uint8_t>& bytes, const std::size_t offset,
    const std::string& text, const std::size_t width) {
    ensureSize(bytes, offset + width);
    std::fill_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), width, 0U);
    const auto count = std::min(width, text.size());
    std::copy_n(text.begin(), count, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

[[nodiscard]] std::string readAscii(const std::span<const std::uint8_t> bytes,
    const std::size_t offset, const std::size_t width) {
    std::string result{};
    if (offset >= bytes.size()) {
        return result;
    }
    const auto end = std::min(bytes.size(), offset + width);
    for (std::size_t i = offset; i < end; ++i) {
        if (bytes[i] == 0U) {
            break;
        }
        result.push_back(bytes[i] >= 0x20U && bytes[i] <= 0x7EU
            ? static_cast<char>(bytes[i]) : '?');
    }
    return result;
}

[[nodiscard]] std::uint32_t appendAligned(std::vector<std::uint8_t>& bytes,
    const std::span<const std::uint8_t> data, const std::size_t alignment = 4U) {
    const auto aligned = (bytes.size() + alignment - 1U) & ~(alignment - 1U);
    bytes.resize(aligned, 0U);
    if (aligned > std::numeric_limits<std::uint32_t>::max() ||
        data.size() > std::numeric_limits<std::uint32_t>::max() - aligned) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    bytes.insert(bytes.end(), data.begin(), data.end());
    return static_cast<std::uint32_t>(aligned);
}

struct FreeRange {
    std::size_t offset = 0;
    std::size_t size = 0;
};

void releaseKnownRange(std::vector<FreeRange>& ranges, const std::size_t offset, const std::size_t size) {
    if (size == 0U) {
        return;
    }
    ranges.push_back(FreeRange{ .offset = offset, .size = size });
    std::sort(ranges.begin(), ranges.end(), [](const auto& left, const auto& right) {
        return left.offset < right.offset;
    });
    std::vector<FreeRange> merged{};
    for (const auto& range : ranges) {
        if (merged.empty() || range.offset > merged.back().offset + merged.back().size) {
            merged.push_back(range);
        } else {
            const auto end = std::max(merged.back().offset + merged.back().size, range.offset + range.size);
            merged.back().size = end - merged.back().offset;
        }
    }
    ranges = std::move(merged);
}

[[nodiscard]] std::optional<std::uint32_t> allocateKnownGap(
    std::vector<std::uint8_t>& bytes,
    std::vector<FreeRange>& ranges,
    const std::span<const std::uint8_t> data,
    const std::size_t alignment = 4U) {
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        const auto range = ranges[i];
        if (range.offset > bytes.size() || range.size > bytes.size() - range.offset) {
            continue;
        }
        const auto aligned = (range.offset + alignment - 1U) & ~(alignment - 1U);
        const auto rangeEnd = range.offset + range.size;
        if (aligned > rangeEnd || data.size() > rangeEnd - aligned || aligned > std::numeric_limits<std::uint32_t>::max()) {
            continue;
        }
        ranges.erase(ranges.begin() + static_cast<std::ptrdiff_t>(i));
        releaseKnownRange(ranges, range.offset, aligned - range.offset);
        releaseKnownRange(ranges, aligned + data.size(), rangeEnd - aligned - data.size());
        std::copy(data.begin(), data.end(), bytes.begin() + static_cast<std::ptrdiff_t>(aligned));
        return static_cast<std::uint32_t>(aligned);
    }
    return std::nullopt;
}

[[nodiscard]] std::uint32_t placeRelocated(
    std::vector<std::uint8_t>& bytes,
    std::vector<FreeRange>& ranges,
    const std::span<const std::uint8_t> data) {
    if (const auto gap = allocateKnownGap(bytes, ranges, data)) {
        return *gap;
    }
    return appendAligned(bytes, data);
}

[[nodiscard]] bool validateMesh(const model::MeshData& mesh, std::string& reason) {
    if ((mesh.indices.size() % 3U) != 0U) {
        reason = "index count is not divisible by three";
        return false;
    }
    if (!mesh.triangleMetadata.empty() && mesh.triangleMetadata.size() != mesh.indices.size() / 3U) {
        reason = "triangle metadata count does not match triangle count";
        return false;
    }
    if (std::any_of(mesh.indices.begin(), mesh.indices.end(), [&](const auto index) { return index >= mesh.vertices.size(); })) {
        reason = "mesh contains an out-of-range vertex index";
        return false;
    }
    return true;
}

[[nodiscard]] model::TriangleMetadata metadataAt(const model::MeshData& mesh, const std::size_t triangle) {
    return mesh.triangleMetadata.empty() ? model::TriangleMetadata{} : mesh.triangleMetadata[triangle];
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> buildGrnd(
    const model::GrndData& source, const Endian endian, std::string& reason) {
    auto data = source;
    std::vector<std::string> diagnostics{};
    if (!model::synchronizeGrndSourceView(data, &diagnostics)) {
        reason = diagnostics.empty() ? "failed to synchronize GRND source view" : diagnostics.front();
        return std::nullopt;
    }
    if (data.mesh.vertices.size() > (std::numeric_limits<std::uint16_t>::max() / 6U) + 1U ||
        data.mesh.indices.size() / 3U > std::numeric_limits<std::uint32_t>::max()) {
        reason = "GRND topology exceeds source index limits";
        return std::nullopt;
    }

    constexpr std::size_t inner = 0x10U;
    constexpr std::size_t setsOffset = 0x30U;
    constexpr std::size_t setSize = 0x18U;
    const auto triangleCount = data.mesh.indices.size() / 3U;
    const auto streamOffset = setsOffset + setSize;
    const auto vertexOffset = (streamOffset + triangleCount * 12U + 3U) & ~3U;
    const auto registryOffset = (vertexOffset + data.mesh.vertices.size() * 24U + 3U) & ~3U;
    const auto tableOffset = registryOffset + 4U;
    const auto refsOffset = tableOffset + data.cells.size() * 8U;
    std::size_t refCount = 0U;
    for (const auto& cell : data.cells) {
        refCount += cell.references.size();
    }
    const auto totalSize = refsOffset + refCount * 4U;
    if (totalSize > std::numeric_limits<std::uint32_t>::max()) {
        reason = "GRND resource exceeds 32-bit size";
        return std::nullopt;
    }
    std::vector<std::uint8_t> out(totalSize, 0U);
    if (data.outerHeaderBytes.size() >= 0x10U) {
        std::copy_n(data.outerHeaderBytes.begin(), 0x10U, out.begin());
    }
    writeTag(out, 0U, "GRND");
    writeU32(out, 4U, static_cast<std::uint32_t>(totalSize), endian);
    writeU32(out, inner, static_cast<std::uint32_t>(setsOffset - inner), endian);
    writeU32(out, inner + 4U, static_cast<std::uint32_t>(registryOffset - inner), endian);
    writeU16(out, inner + 0x10U, data.gridX, endian);
    writeU16(out, inner + 0x12U, data.gridZ, endian);
    writeU16(out, inner + 0x14U, data.cellSizeX, endian);
    writeU16(out, inner + 0x16U, data.cellSizeZ, endian);
    writeU16(out, inner + 0x18U, 1U, endian);
    writeU16(out, inner + 0x1AU, static_cast<std::uint16_t>(data.cells.size()), endian);

    writeU32(out, setsOffset + 0x0CU, static_cast<std::uint32_t>(vertexOffset - (setsOffset + 0x0CU)), endian);
    writeU32(out, setsOffset + 0x10U, static_cast<std::uint32_t>(streamOffset - (setsOffset + 0x10U)), endian);
    writeU32(out, setsOffset + 0x14U, static_cast<std::uint32_t>(triangleCount), endian);
    for (std::size_t triangle = 0; triangle < triangleCount; ++triangle) {
        const auto metadata = metadataAt(data.mesh, triangle);
        std::array<std::uint32_t, 3> indices{
            data.mesh.indices[triangle * 3U], data.mesh.indices[triangle * 3U + 1U], data.mesh.indices[triangle * 3U + 2U],
        };
        if ((metadata.rawU16[2] & 0x8000U) != 0U) {
            std::swap(indices[0], indices[2]);
        }
        for (std::size_t corner = 0; corner < 3U; ++corner) {
            const auto offset = streamOffset + (triangle * 3U + corner) * 4U;
            writeU16(out, offset, static_cast<std::uint16_t>(indices[corner] * 6U), endian);
            writeU16(out, offset + 2U, metadata.rawU16[corner], endian);
        }
    }
    for (std::size_t i = 0; i < data.mesh.vertices.size(); ++i) {
        const auto offset = vertexOffset + i * 24U;
        const auto& vertex = data.mesh.vertices[i];
        writeF32(out, offset, vertex.position.x, endian);
        writeF32(out, offset + 4U, vertex.position.y, endian);
        writeF32(out, offset + 8U, vertex.position.z, endian);
        writeF32(out, offset + 12U, vertex.normal.x, endian);
        writeF32(out, offset + 16U, vertex.normal.y, endian);
        writeF32(out, offset + 20U, vertex.normal.z, endian);
    }
    writeU32(out, registryOffset, 0U, endian);
    auto refCursor = refsOffset;
    for (std::size_t cell = 0; cell < data.cells.size(); ++cell) {
        const auto table = tableOffset + cell * 8U;
        writeU32(out, table, static_cast<std::uint32_t>(data.cells[cell].references.size()), endian);
        writeU32(out, table + 4U, static_cast<std::uint32_t>(refCursor - (table + 4U)), endian);
        for (const auto& reference : data.cells[cell].references) {
            writeU16(out, refCursor, 0U, endian);
            writeU16(out, refCursor + 2U, static_cast<std::uint16_t>(*reference.meshTriangleIndex * 3U), endian);
            refCursor += 4U;
        }
    }
    return out;
}

[[nodiscard]] std::int32_t radiansToBams32(const float radians) {
    return static_cast<std::int32_t>(std::llround(static_cast<double>(radians) * 65536.0 / static_cast<double>(kTau)));
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> buildGobj(
    const model::GobjData& source, const Endian endian, std::string& reason) {
    auto data = source;
    std::vector<std::string> diagnostics{};
    if (!model::synchronizeGobjSourceView(data, &diagnostics)) {
        reason = diagnostics.empty() ? "failed to synchronize GOBJ source view" : diagnostics.front();
        return std::nullopt;
    }
    if (data.nodes.empty()) {
        reason = "GOBJ has no nodes";
        return std::nullopt;
    }

    constexpr std::size_t nodeSize = 0x34U;
    constexpr std::size_t attachPrefixSize = 0x5CU;
    std::vector<std::uint8_t> out(0x10U + data.nodes.size() * nodeSize, 0U);
    if (data.outerHeaderBytes.size() >= 0x10U) {
        std::copy_n(data.outerHeaderBytes.begin(), 0x10U, out.begin());
    }
    writeTag(out, 0U, "GOBJ");

    std::vector<std::optional<std::uint32_t>> attachOffsets(data.nodes.size());
    for (std::size_t nodeIndex = 0; nodeIndex < data.nodes.size(); ++nodeIndex) {
        const auto& node = data.nodes[nodeIndex];
        if (node.streamMesh.indices.empty()) {
            continue;
        }
        std::string meshReason{};
        if (!validateMesh(node.streamMesh, meshReason)) {
            reason = "GOBJ node " + std::to_string(nodeIndex) + ": " + meshReason;
            return std::nullopt;
        }
        const auto& attach = *node.attach;
        const auto recordWords = attach.vertexChunk.recordWords;
        const auto triangleCount = node.streamMesh.indices.size() / 3U;
        const auto attachOffset = (out.size() + 3U) & ~3U;
        out.resize(attachOffset + attachPrefixSize, 0U);
        if (attach.prefixBytes.size() >= attachPrefixSize) {
            std::copy_n(attach.prefixBytes.begin(), attachPrefixSize,
                out.begin() + static_cast<std::ptrdiff_t>(attachOffset));
        }
        const auto streamOffset = attachOffset + attachPrefixSize;
        out.resize(streamOffset + triangleCount * 16U, 0U);
        const auto vertexOffset = (out.size() + 3U) & ~3U;
        out.resize(vertexOffset + 8U + node.streamMesh.vertices.size() * recordWords * 4U, 0U);
        attachOffsets[nodeIndex] = static_cast<std::uint32_t>(attachOffset);
        writeU32(out, attachOffset + 0x10U,
            static_cast<std::uint32_t>(vertexOffset - (attachOffset + 0x10U)), endian);

        for (std::size_t triangle = 0; triangle < triangleCount; ++triangle) {
            const auto metadata = metadataAt(node.streamMesh, triangle);
            std::array<std::uint32_t, 3> indices{
                node.streamMesh.indices[triangle * 3U], node.streamMesh.indices[triangle * 3U + 1U],
                node.streamMesh.indices[triangle * 3U + 2U],
            };
            if ((metadata.rawU16[2] & 0x8000U) != 0U) {
                std::swap(indices[0], indices[2]);
            }
            const auto runOffset = streamOffset + triangle * 16U;
            for (std::size_t corner = 0; corner < 3U; ++corner) {
                writeU16(out, runOffset + corner * 4U,
                    static_cast<std::uint16_t>(2U + indices[corner] * recordWords), endian);
                writeU16(out, runOffset + corner * 4U + 2U, metadata.rawU16[corner], endian);
            }
            writeU16(out, runOffset + 12U, 0xFFFFU, endian);
            writeU16(out, runOffset + 14U, 0xFFFFU, endian);
        }
        const auto header0 = (attach.vertexChunk.headerWord0 & 0xFFFFFF00U) | attach.vertexChunk.chunkType;
        const auto header1 = (static_cast<std::uint32_t>(node.streamMesh.vertices.size()) << 16U)
            | (attach.vertexChunk.headerWord1 & 0xFFFFU);
        writeU32(out, vertexOffset, header0, endian);
        writeU32(out, vertexOffset + 4U, header1, endian);
        for (std::size_t vertexIndex = 0; vertexIndex < node.streamMesh.vertices.size(); ++vertexIndex) {
            const auto& vertex = node.streamMesh.vertices[vertexIndex];
            const auto offset = vertexOffset + 8U + vertexIndex * recordWords * 4U;
            writeF32(out, offset, vertex.position.x, endian);
            writeF32(out, offset + 4U, vertex.position.y, endian);
            writeF32(out, offset + 8U, vertex.position.z, endian);
            if (recordWords >= 6U) {
                writeF32(out, offset + 12U, vertex.normal.x, endian);
                writeF32(out, offset + 16U, vertex.normal.y, endian);
                writeF32(out, offset + 20U, vertex.normal.z, endian);
            }
            if (recordWords == 7U) {
                writeU32(out, offset + 24U, vertex.rawUserAttributesU32.value_or(0U), endian);
            }
        }
    }

    std::vector<std::optional<std::size_t>> nextSibling(data.nodes.size());
    for (std::size_t parent = 0; parent < data.nodes.size(); ++parent) {
        const auto& children = data.nodes[parent].childNodeIndices;
        for (std::size_t i = 0; i + 1U < children.size(); ++i) {
            if (children[i] < nextSibling.size() && children[i + 1U] < nextSibling.size()) {
                nextSibling[children[i]] = children[i + 1U];
            }
        }
    }
    for (std::size_t i = 0; i + 1U < data.rootNodeIndices.size(); ++i) {
        if (data.rootNodeIndices[i] < nextSibling.size() && data.rootNodeIndices[i + 1U] < nextSibling.size()) {
            nextSibling[data.rootNodeIndices[i]] = data.rootNodeIndices[i + 1U];
        }
    }

    for (std::size_t nodeIndex = 0; nodeIndex < data.nodes.size(); ++nodeIndex) {
        const auto offset = 0x10U + nodeIndex * nodeSize;
        const auto& node = data.nodes[nodeIndex];
        if (node.sourceBytes.size() >= nodeSize) {
            std::copy_n(node.sourceBytes.begin(), nodeSize, out.begin() + static_cast<std::ptrdiff_t>(offset));
        }
        writeU32(out, offset, attachOffsets[nodeIndex].has_value()
            ? static_cast<std::uint32_t>(*attachOffsets[nodeIndex] - offset) : 0U, endian);
        writeF32(out, offset + 8U, node.transform.position.x, endian);
        writeF32(out, offset + 0x0CU, node.transform.position.y, endian);
        writeF32(out, offset + 0x10U, node.transform.position.z, endian);
        writeU32(out, offset + 0x14U, std::bit_cast<std::uint32_t>(radiansToBams32(node.transform.rotationRaw.x)), endian);
        writeU32(out, offset + 0x18U, std::bit_cast<std::uint32_t>(radiansToBams32(node.transform.rotationRaw.y)), endian);
        writeU32(out, offset + 0x1CU, std::bit_cast<std::uint32_t>(radiansToBams32(node.transform.rotationRaw.z)), endian);
        writeF32(out, offset + 0x20U, node.transform.scale.x, endian);
        writeF32(out, offset + 0x24U, node.transform.scale.y, endian);
        writeF32(out, offset + 0x28U, node.transform.scale.z, endian);
        const auto child = node.childNodeIndices.empty() ? std::optional<std::size_t>{} : std::optional<std::size_t>{node.childNodeIndices.front()};
        writeU32(out, offset + 0x2CU, child.has_value()
            ? static_cast<std::uint32_t>((0x10U + *child * nodeSize) - (offset + 0x2CU)) : 0U, endian);
        writeU32(out, offset + 0x30U, nextSibling[nodeIndex].has_value()
            ? static_cast<std::uint32_t>((0x10U + *nextSibling[nodeIndex] * nodeSize) - (offset + 0x2CU)) : 0U, endian);
    }
    writeU32(out, 4U, static_cast<std::uint32_t>(out.size()), endian);
    return out;
}

[[nodiscard]] std::vector<std::uint8_t> buildTextureArchive(
    const model::MldTextureArchive& archive, const Endian endian) {
    constexpr std::size_t recordSize = 44U;
    const auto requiredPrefixSize = 4U + archive.entries.size() * recordSize;
    const EndianReader prefixReader(archive.archivePrefixBytes, endian);
    const auto originalCount = prefixReader.try_read_u32(0U).value_or(0U);
    std::vector<std::uint8_t> out{};
    if (originalCount == archive.entries.size() && archive.archivePrefixBytes.size() >= requiredPrefixSize) {
        out = archive.archivePrefixBytes;
    } else {
        out.resize(requiredPrefixSize, 0U);
        const auto recordsToPreserve = std::min<std::size_t>(originalCount, archive.entries.size());
        const auto preserveSize = std::min(archive.archivePrefixBytes.size(), 4U + recordsToPreserve * recordSize);
        std::copy_n(archive.archivePrefixBytes.begin(), preserveSize, out.begin());
    }
    writeU32(out, 0U, static_cast<std::uint32_t>(archive.entries.size()), endian);
    for (std::size_t i = 0; i < archive.entries.size(); ++i) {
        const auto nameOffset = 4U + i * recordSize;
        if (nameOffset + 32U <= out.size()) {
            writeAscii(out, nameOffset, archive.entries[i].textureName, 32U);
        }
        out.insert(out.end(), archive.entries[i].gvrData.begin(), archive.entries[i].gvrData.end());
    }
    return out;
}

[[nodiscard]] bool unknownContainsPointer(const model::MldFile& file,
    const std::uint32_t pointer, const Endian endian, std::uint32_t& foundAt) {
    for (const auto& range : file.paddingAndUnknownRanges) {
        EndianReader reader(range.bytes, endian);
        for (std::size_t offset = 0; offset + 4U <= range.bytes.size(); offset += 4U) {
            if (reader.read_u32(offset) == pointer) {
                foundAt = static_cast<std::uint32_t>(range.offset + offset);
                return true;
            }
        }
    }
    return false;
}

} // namespace

bool MldWriteResult::ok() const noexcept {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity == model::MldDiagnostic::Severity::Error;
    });
}

MldWriteResult MldFileWriter::write(const model::MldFile& file, const MldWriteOptions& options) const {
    MldWriteResult result{};
    result.sourceSize = file.sourceBytes.empty() ? file.decodedBytes.size() : file.sourceBytes.size();
    if (file.decodedBytes.empty() || file.parseStatus == model::MldParseStatus::Failed) {
        addDiagnostic(result, model::MldDiagnostic::Severity::Error, "Cannot write an MLD without a parsed decoded payload.");
        return result;
    }

    const auto sourcePlatform = file.sourcePlatform == model::TargetPlatform::Unknown
        ? platformForEndian(file.endian) : file.sourcePlatform;
    const auto targetPlatform = options.platform.value_or(sourcePlatform);
    const auto targetEndian = endianForPlatform(targetPlatform, file.endian);
    const bool targetCompressed = options.compressAklz.value_or(file.sourceWasCompressedAklz);
    if (targetCompressed && targetPlatform != model::TargetPlatform::GameCube) {
        addDiagnostic(result, model::MldDiagnostic::Severity::Error, "AKLZ compression is GameCube-only.");
        return result;
    }

    auto out = file.decodedBytes;
    std::map<std::uint32_t, std::uint32_t> resourceRelocations{};
    std::vector<FreeRange> freeKnownRanges{};

    for (const auto& [address, resource] : file.objectResources) {
        if (resource.model != resource.originalModel) {
            addDiagnostic(result, model::MldDiagnostic::Severity::Error,
                "NJ model values are read-only for MLD binary export; preserve the original parsed model.", address);
        }
        result.layout.push_back(MldWriteLayoutRecord{
            .kind = "ninja-object",
            .sourceOffset = address,
            .outputOffset = address,
            .sourceSize = resource.blockSize,
            .outputSize = resource.blockSize,
            .copiedVerbatim = true,
        });
    }
    for (const auto& [address, resource] : file.motionResources) {
        for (const auto& variant : resource.variants) {
            if (variant.motion != variant.originalMotion) {
                addDiagnostic(result, model::MldDiagnostic::Severity::Error,
                    "NJ motion values are read-only for MLD binary export; preserve the original parsed motion.", address);
                break;
            }
        }
        result.layout.push_back(MldWriteLayoutRecord{
            .kind = "ninja-motion",
            .sourceOffset = address,
            .outputOffset = address,
            .sourceSize = resource.blockSize,
            .outputSize = resource.blockSize,
            .copiedVerbatim = true,
        });
    }
    if (!result.ok()) {
        return result;
    }

    for (const auto& [address, resource] : file.groundResources) {
        bool changed = targetEndian != file.endian;
        if (resource.grnd.has_value()) {
            changed = changed || model::semanticHash(*resource.grnd) != resource.originalSemanticHash;
        } else if (resource.gobj.has_value()) {
            changed = changed || model::semanticHash(*resource.gobj) != resource.originalSemanticHash;
        }
        if (!changed) {
            result.layout.push_back(MldWriteLayoutRecord{
                .kind = resource.tag,
                .sourceOffset = address,
                .outputOffset = address,
                .sourceSize = resource.blockSize,
                .outputSize = resource.blockSize,
                .copiedVerbatim = true,
            });
            continue;
        }

        std::string reason{};
        std::optional<std::vector<std::uint8_t>> rebuilt{};
        if (resource.grnd.has_value()) {
            rebuilt = buildGrnd(*resource.grnd, targetEndian, reason);
        } else if (resource.gobj.has_value()) {
            rebuilt = buildGobj(*resource.gobj, targetEndian, reason);
        }
        if (!rebuilt.has_value()) {
            addDiagnostic(result, model::MldDiagnostic::Severity::Error,
                "Failed to rebuild " + resource.tag + ": " + reason, address);
            continue;
        }
        std::uint32_t outputAddress = address;
        bool relocated = false;
        if (address <= out.size() && resource.blockSize <= out.size() - address && rebuilt->size() <= resource.blockSize) {
            std::copy(rebuilt->begin(), rebuilt->end(), out.begin() + static_cast<std::ptrdiff_t>(address));
            std::fill(out.begin() + static_cast<std::ptrdiff_t>(address + rebuilt->size()),
                out.begin() + static_cast<std::ptrdiff_t>(address + resource.blockSize), 0U);
            releaseKnownRange(freeKnownRanges, address + rebuilt->size(), resource.blockSize - rebuilt->size());
        } else {
            releaseKnownRange(freeKnownRanges, address, resource.blockSize);
            outputAddress = placeRelocated(out, freeKnownRanges, *rebuilt);
            if (outputAddress == std::numeric_limits<std::uint32_t>::max()) {
                addDiagnostic(result, model::MldDiagnostic::Severity::Error, "MLD output exceeded 32-bit address space.", address);
                continue;
            }
            resourceRelocations.emplace(address, outputAddress);
            relocated = true;
        }
        result.layout.push_back(MldWriteLayoutRecord{
            .kind = resource.tag,
            .sourceOffset = address,
            .outputOffset = outputAddress,
            .sourceSize = resource.blockSize,
            .outputSize = rebuilt->size(),
            .relocated = relocated,
        });
    }

    std::optional<std::uint32_t> textureOutput{};
    if (file.textureArchive.has_value()) {
        const auto& archive = *file.textureArchive;
        const auto rebuilt = buildTextureArchive(archive, targetEndian);
        const auto originalSize = archive.archiveEndOffset >= archive.archiveStartOffset
            ? archive.archiveEndOffset - archive.archiveStartOffset : 0U;
        const bool changed = targetEndian != file.endian || archive.archiveStartOffset > file.decodedBytes.size()
            || originalSize > file.decodedBytes.size() - std::min(archive.archiveStartOffset, file.decodedBytes.size())
            || !std::equal(rebuilt.begin(), rebuilt.end(),
                file.decodedBytes.begin() + static_cast<std::ptrdiff_t>(archive.archiveStartOffset),
                file.decodedBytes.begin() + static_cast<std::ptrdiff_t>(std::min(archive.archiveEndOffset, file.decodedBytes.size())));
        if (changed) {
            if (archive.archiveStartOffset <= out.size() && originalSize <= out.size() - archive.archiveStartOffset
                && rebuilt.size() <= originalSize) {
                std::copy(rebuilt.begin(), rebuilt.end(), out.begin() + static_cast<std::ptrdiff_t>(archive.archiveStartOffset));
                std::fill(out.begin() + static_cast<std::ptrdiff_t>(archive.archiveStartOffset + rebuilt.size()),
                    out.begin() + static_cast<std::ptrdiff_t>(archive.archiveEndOffset), 0U);
                releaseKnownRange(freeKnownRanges, archive.archiveStartOffset + rebuilt.size(), originalSize - rebuilt.size());
                textureOutput = static_cast<std::uint32_t>(archive.archiveStartOffset);
            } else {
                releaseKnownRange(freeKnownRanges, archive.archiveStartOffset, originalSize);
                textureOutput = placeRelocated(out, freeKnownRanges, rebuilt);
                resourceRelocations.emplace(static_cast<std::uint32_t>(archive.archiveStartOffset), *textureOutput);
            }
            result.layout.push_back(MldWriteLayoutRecord{
                .kind = "texture-archive",
                .sourceOffset = static_cast<std::uint32_t>(archive.archiveStartOffset),
                .outputOffset = *textureOutput,
                .sourceSize = originalSize,
                .outputSize = rebuilt.size(),
                .relocated = *textureOutput != archive.archiveStartOffset,
            });
        }
    }

    std::set<const model::U32List*> resourcePointerLists{};
    for (const auto& record : file.entries) {
        if (record.entry.groundAddresses) {
            resourcePointerLists.insert(record.entry.groundAddresses.get());
        }
        if (record.entry.objectAddresses) {
            resourcePointerLists.insert(record.entry.objectAddresses.get());
        }
    }
    std::unordered_map<const model::U32List*, std::uint32_t> listOutputOffsets{};
    for (const auto& [sourceOffset, list] : file.u32Lists) {
        if (!list) {
            continue;
        }
        auto values = list->values;
        if (resourcePointerLists.contains(list.get())) {
            for (auto& value : values) {
                if (const auto relocated = resourceRelocations.find(value); relocated != resourceRelocations.end()) {
                    value = relocated->second;
                }
            }
        }
        std::vector<std::uint8_t> serialized(4U + values.size() * 4U, 0U);
        writeU32(serialized, 0U, static_cast<std::uint32_t>(values.size()), targetEndian);
        for (std::size_t i = 0; i < values.size(); ++i) {
            writeU32(serialized, 4U + i * 4U, values[i], targetEndian);
        }
        std::size_t originalSize = 0U;
        if (sourceOffset + 4U <= file.decodedBytes.size()) {
            const EndianReader reader(file.decodedBytes, file.endian);
            const auto originalCount = reader.try_read_u32(sourceOffset).value_or(0U);
            originalSize = 4U + static_cast<std::size_t>(originalCount) * 4U;
        }
        std::uint32_t outputOffset = sourceOffset;
        if (sourceOffset <= out.size() && originalSize <= out.size() - sourceOffset && serialized.size() <= originalSize) {
            std::copy(serialized.begin(), serialized.end(), out.begin() + static_cast<std::ptrdiff_t>(sourceOffset));
            std::fill(out.begin() + static_cast<std::ptrdiff_t>(sourceOffset + serialized.size()),
                out.begin() + static_cast<std::ptrdiff_t>(sourceOffset + originalSize), 0U);
            releaseKnownRange(freeKnownRanges, sourceOffset + serialized.size(), originalSize - serialized.size());
        } else {
            releaseKnownRange(freeKnownRanges, sourceOffset, originalSize);
            outputOffset = placeRelocated(out, freeKnownRanges, serialized);
            resourceRelocations.emplace(sourceOffset, outputOffset);
        }
        listOutputOffsets.emplace(list.get(), outputOffset);
        result.layout.push_back(MldWriteLayoutRecord{
            .kind = "u32-list",
            .sourceOffset = sourceOffset,
            .outputOffset = outputOffset,
            .sourceSize = originalSize,
            .outputSize = serialized.size(),
            .relocated = outputOffset != sourceOffset,
        });
    }

    std::uint32_t entryTableOffset = file.header.indexTableOffset;
    const auto originalEntryTableSize = static_cast<std::size_t>(file.header.entryCount) * kEntrySize;
    const auto requiredEntryTableSize = file.entries.size() * kEntrySize;
    if (requiredEntryTableSize > originalEntryTableSize || entryTableOffset > out.size()
        || originalEntryTableSize > out.size() - entryTableOffset) {
        std::vector<std::uint8_t> table(requiredEntryTableSize, 0U);
        releaseKnownRange(freeKnownRanges, entryTableOffset, originalEntryTableSize);
        entryTableOffset = placeRelocated(out, freeKnownRanges, table);
        resourceRelocations.emplace(file.header.indexTableOffset, entryTableOffset);
    } else if (requiredEntryTableSize < originalEntryTableSize) {
        std::fill(out.begin() + static_cast<std::ptrdiff_t>(entryTableOffset + requiredEntryTableSize),
            out.begin() + static_cast<std::ptrdiff_t>(entryTableOffset + originalEntryTableSize), 0U);
        releaseKnownRange(freeKnownRanges, entryTableOffset + requiredEntryTableSize,
            originalEntryTableSize - requiredEntryTableSize);
    }

    const auto listPointer = [&](const std::shared_ptr<model::U32List>& list, const std::uint32_t fallback) {
        if (!list) {
            return 0U;
        }
        if (const auto found = listOutputOffsets.find(list.get()); found != listOutputOffsets.end()) {
            return found->second;
        }
        return fallback;
    };
    for (std::size_t i = 0; i < file.entries.size(); ++i) {
        const auto& record = file.entries[i];
        const auto offset = static_cast<std::size_t>(entryTableOffset) + i * kEntrySize;
        ensureSize(out, offset + kEntrySize);
        writeU32(out, offset, record.entry.entryId, targetEndian);
        writeU32(out, offset + 4U, std::bit_cast<std::uint32_t>(record.entry.tblId), targetEndian);
        writeU32(out, offset + 8U, listPointer(record.entry.groundLinks, record.groundLinksPointer), targetEndian);
        writeU32(out, offset + 0x0CU, listPointer(record.entry.paramList2, record.paramList2Pointer), targetEndian);
        writeU32(out, offset + 0x10U, listPointer(record.entry.functionParameters, record.functionParametersPointer), targetEndian);
        writeU32(out, offset + 0x14U, listPointer(record.entry.objectAddresses, record.objectAddressesPointer), targetEndian);
        writeU32(out, offset + 0x18U, listPointer(record.entry.groundAddresses, record.groundAddressesPointer), targetEndian);
        writeU32(out, offset + 0x1CU, listPointer(record.entry.motionAddresses, record.motionAddressesPointer), targetEndian);
        auto texturesPointer = record.entry.texturesPointer;
        if (textureOutput.has_value() && file.textureArchive.has_value()
            && texturesPointer == file.textureArchive->tableOffset) {
            texturesPointer = *textureOutput;
        }
        writeU32(out, offset + 0x20U, texturesPointer, targetEndian);
        const bool entryStayedAtSource = entryTableOffset == file.header.indexTableOffset
            && i == record.entry.tableIndex;
        if (!entryStayedAtSource || readAscii(record.rawBytes, 0x24U, 0x14U) != record.entry.fxnName) {
            writeAscii(out, offset + 0x24U, record.entry.fxnName, 0x14U);
        }
        writeF32(out, offset + 0x44U, record.entry.transform.position.x, targetEndian);
        writeF32(out, offset + 0x48U, record.entry.transform.position.y, targetEndian);
        writeF32(out, offset + 0x4CU, record.entry.transform.position.z, targetEndian);
        writeF32(out, offset + 0x50U, record.entry.transform.rotationRaw.x, targetEndian);
        writeF32(out, offset + 0x54U, record.entry.transform.rotationRaw.y, targetEndian);
        writeF32(out, offset + 0x58U, record.entry.transform.rotationRaw.z, targetEndian);
        writeF32(out, offset + 0x5CU, record.entry.transform.scale.x, targetEndian);
        writeF32(out, offset + 0x60U, record.entry.transform.scale.y, targetEndian);
        writeF32(out, offset + 0x64U, record.entry.transform.scale.z, targetEndian);
    }

    ensureSize(out, kHeaderSize);
    writeU32(out, 0U, static_cast<std::uint32_t>(file.entries.size()), targetEndian);
    writeU32(out, 4U, entryTableOffset, targetEndian);
    const auto relocatedHeaderPointer = [&](const std::uint32_t pointer) {
        const auto found = resourceRelocations.find(pointer);
        return found == resourceRelocations.end() ? pointer : found->second;
    };
    writeU32(out, 8U, relocatedHeaderPointer(file.header.functionParametersOffset), targetEndian);
    writeU32(out, 0x0CU, file.header.realDataOffset, targetEndian);
    const auto textureTableOffset = textureOutput.value_or(static_cast<std::uint32_t>(file.header.textureTableOffset));
    writeU32(out, 0x10U, textureTableOffset, targetEndian);

    if (options.rejectUnknownPointerRelocations) {
        for (const auto& [source, destination] : resourceRelocations) {
            if (source == destination) {
                continue;
            }
            std::uint32_t foundAt = 0U;
            if (unknownContainsPointer(file, source, file.endian, foundAt)) {
                addDiagnostic(result, model::MldDiagnostic::Severity::Error,
                    "Relocation rejected because an unclassified range contains the source address.", foundAt);
            }
        }
    }
    if (!result.ok()) {
        result.bytes.clear();
        return result;
    }

    const bool exactSourceSettings = targetPlatform == sourcePlatform && targetCompressed == file.sourceWasCompressedAklz;
    if (exactSourceSettings && out == file.decodedBytes && !file.sourceBytes.empty()) {
        result.bytes = file.sourceBytes;
        result.outputSize = result.bytes.size();
        addDiagnostic(result, model::MldDiagnostic::Severity::Info, "No semantic changes detected; returned original source bytes.");
        return result;
    }
    if (targetCompressed) {
        auto compressed = spice::compression::aklz::compress(out);
        if (!compressed.ok()) {
            addDiagnostic(result, model::MldDiagnostic::Severity::Error,
                "AKLZ compression failed: " + std::string(spice::compression::aklz::errorToString(compressed.error)));
            return result;
        }
        result.bytes = std::move(compressed.bytes);
    } else {
        result.bytes = std::move(out);
    }
    result.outputSize = result.bytes.size();
    return result;
}

} // namespace spice::mld::exporting
