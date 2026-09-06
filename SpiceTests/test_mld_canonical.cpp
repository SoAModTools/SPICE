#include "../SpiceMLD/SpiceMLD.h"
#include "../SpiceMLD/Export/MldFileWriter.h"
#include "../SpiceMLD/Model/MldGroundEditing.h"
#include "../SpiceMLD/Parsing/MldParser.h"
#include "../SpiceMLD/Parsing/Sa3dBlenderIrBuilder.h"
#include "../Compression/Aklz.h"
#include "../SpiceModeling/SpiceModeling.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using spice::root::Endian;
using spice::mld::exporting::MldFileWriter;
using spice::mld::parsing::MldParser;

std::string describeDiagnostics(const spice::mld::model::MldFile& file) {
    std::ostringstream out{};
    for (const auto& diagnostic : file.parseDiagnostics) {
        out << '\n' << static_cast<int>(diagnostic.severity) << ": " << diagnostic.message;
        if (diagnostic.sourceOffset.has_value()) {
            out << " at 0x" << std::hex << *diagnostic.sourceOffset << std::dec;
        }
    }
    return out.str();
}
void writeU16(std::vector<std::uint8_t>& bytes, const std::size_t offset,
    const std::uint16_t value, const Endian endian = Endian::Big) {
    if (endian == Endian::Big) {
        bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
        bytes[offset + 1U] = static_cast<std::uint8_t>(value);
    } else {
        bytes[offset] = static_cast<std::uint8_t>(value);
        bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    }
}

void writeU32(std::vector<std::uint8_t>& bytes, const std::size_t offset,
    const std::uint32_t value, const Endian endian = Endian::Big) {
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

void writeF32(std::vector<std::uint8_t>& bytes, const std::size_t offset, const float value,
    const Endian endian = Endian::Big) {
    writeU32(bytes, offset, std::bit_cast<std::uint32_t>(value), endian);
}

void writeTag(std::vector<std::uint8_t>& bytes, const std::size_t offset, const char* tag) {
    for (std::size_t i = 0; i < 4U; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>(tag[i]);
    }
}

void writeList(std::vector<std::uint8_t>& bytes, const std::size_t offset,
    const std::span<const std::uint32_t> values) {
    writeU32(bytes, offset, static_cast<std::uint32_t>(values.size()));
    for (std::size_t i = 0; i < values.size(); ++i) {
        writeU32(bytes, offset + 4U + i * 4U, values[i]);
    }
}

std::vector<std::uint8_t> makeBaseMld(const std::uint32_t resourceAddress = 0U,
    const bool resourceIsObject = false) {
    constexpr std::size_t entry = 0x20U;
    constexpr std::size_t groundLinks = 0x100U;
    constexpr std::size_t sharedParams = 0x108U;
    constexpr std::size_t objects = 0x118U;
    constexpr std::size_t grounds = 0x120U;
    constexpr std::size_t motions = 0x128U;
    std::vector<std::uint8_t> bytes(0x340U, 0xCDU);
    writeU32(bytes, 0U, 1U);
    writeU32(bytes, 4U, static_cast<std::uint32_t>(entry));
    writeU32(bytes, 8U, static_cast<std::uint32_t>(sharedParams));
    writeU32(bytes, 0x0CU, resourceAddress == 0U ? 0x180U : resourceAddress);
    writeU32(bytes, 0x10U, 0x320U);
    writeU32(bytes, entry, 7U);
    writeU32(bytes, entry + 4U, 9U);
    writeU32(bytes, entry + 8U, static_cast<std::uint32_t>(groundLinks));
    writeU32(bytes, entry + 0x0CU, static_cast<std::uint32_t>(sharedParams));
    writeU32(bytes, entry + 0x10U, static_cast<std::uint32_t>(sharedParams));
    writeU32(bytes, entry + 0x14U, static_cast<std::uint32_t>(objects));
    writeU32(bytes, entry + 0x18U, static_cast<std::uint32_t>(grounds));
    writeU32(bytes, entry + 0x1CU, static_cast<std::uint32_t>(motions));
    writeU32(bytes, entry + 0x20U, 0U);
    const char name[] = "wall";
    std::copy_n(name, 4U, bytes.begin() + static_cast<std::ptrdiff_t>(entry + 0x24U));
    writeF32(bytes, entry + 0x5CU, 1.0F);
    writeF32(bytes, entry + 0x60U, 1.0F);
    writeF32(bytes, entry + 0x64U, 1.0F);
    const std::array<std::uint32_t, 0> empty{};
    const std::array<std::uint32_t, 2> params{ 11U, 22U };
    writeList(bytes, groundLinks, empty);
    writeList(bytes, sharedParams, params);
    const std::array<std::uint32_t, 1> resource{ resourceAddress };
    writeList(bytes, objects, resourceIsObject && resourceAddress != 0U ? std::span<const std::uint32_t>(resource) : std::span<const std::uint32_t>(empty));
    writeList(bytes, grounds, !resourceIsObject && resourceAddress != 0U ? std::span<const std::uint32_t>(resource) : std::span<const std::uint32_t>(empty));
    writeList(bytes, motions, empty);
    writeU32(bytes, 0x320U, 0U);
    return bytes;
}

std::vector<std::uint8_t> makeGrndMld(
    const std::array<float, 3> translation = {},
    const std::array<float, 2> gridOrigin = {}) {
    constexpr std::uint32_t address = 0x180U;
    auto bytes = makeBaseMld(address, false);
    constexpr std::size_t sets = 0x40U;
    constexpr std::size_t stream = 0x60U;
    constexpr std::size_t vertices = 0x80U;
    constexpr std::size_t registry = 0xC8U;
    constexpr std::size_t table = registry + 4U;
    constexpr std::size_t refs = 0xD4U;
    constexpr std::size_t size = 0xD8U;
    writeTag(bytes, address, "GRND");
    writeU32(bytes, address + 4U, static_cast<std::uint32_t>(size));
    writeU32(bytes, address + 0x10U, static_cast<std::uint32_t>(sets - 0x10U));
    writeU32(bytes, address + 0x14U, static_cast<std::uint32_t>(registry - 0x10U));
    writeF32(bytes, address + 0x18U, gridOrigin[0]);
    writeF32(bytes, address + 0x1CU, gridOrigin[1]);
    writeU16(bytes, address + 0x20U, 1U);
    writeU16(bytes, address + 0x22U, 1U);
    writeU16(bytes, address + 0x24U, 10U);
    writeU16(bytes, address + 0x26U, 10U);
    writeU16(bytes, address + 0x28U, 1U);
    writeU16(bytes, address + 0x2AU, 1U);
    writeF32(bytes, address + sets, translation[0]);
    writeF32(bytes, address + sets + 4U, translation[1]);
    writeF32(bytes, address + sets + 8U, translation[2]);
    writeU32(bytes, address + sets + 0x0CU, static_cast<std::uint32_t>(vertices - (sets + 0x0CU)));
    writeU32(bytes, address + sets + 0x10U, static_cast<std::uint32_t>(stream - (sets + 0x10U)));
    writeU32(bytes, address + sets + 0x14U, 1U);
    for (std::size_t i = 0; i < 3U; ++i) {
        writeU16(bytes, address + stream + i * 4U, static_cast<std::uint16_t>(i * 6U));
        writeU16(bytes, address + stream + i * 4U + 2U, static_cast<std::uint16_t>(i + 1U));
        const auto vertex = address + vertices + i * 24U;
        writeF32(bytes, vertex, static_cast<float>(i == 1U));
        writeF32(bytes, vertex + 4U, 0.0F);
        writeF32(bytes, vertex + 8U, static_cast<float>(i == 2U));
        writeF32(bytes, vertex + 12U, 0.0F);
        writeF32(bytes, vertex + 16U, 1.0F);
        writeF32(bytes, vertex + 20U, 0.0F);
    }
    writeU32(bytes, address + table, 1U);
    writeU32(bytes, address + table + 4U, static_cast<std::uint32_t>(refs - (table + 4U)));
    writeU16(bytes, address + refs, 0U);
    writeU16(bytes, address + refs + 2U, 0U);
    return bytes;
}

std::vector<std::uint8_t> makeGobjMld(const bool normalDiffuse = false) {
    constexpr std::uint32_t address = 0x180U;
    auto bytes = makeBaseMld(address, true);
    constexpr std::size_t node = 0x10U;
    constexpr std::size_t attach = 0x50U;
    constexpr std::size_t payload = attach + 0x10U;
    constexpr std::size_t stream = payload + 76U;
    constexpr std::size_t vertices = 0xBCU;
    const std::size_t recordWords = normalDiffuse ? 7U : 6U;
    const std::size_t size = vertices + 8U + 3U * recordWords * 4U;
    writeTag(bytes, address, "GOBJ");
    writeU32(bytes, address + 4U, static_cast<std::uint32_t>(size));
    writeU32(bytes, address + node, static_cast<std::uint32_t>(attach - node));
    writeF32(bytes, address + node + 0x20U, 1.0F);
    writeF32(bytes, address + node + 0x24U, 1.0F);
    writeF32(bytes, address + node + 0x28U, 1.0F);
    writeU32(bytes, address + payload, static_cast<std::uint32_t>(vertices - payload));
    for (std::size_t i = 0; i < 3U; ++i) {
        writeU16(bytes, address + stream + i * 4U, static_cast<std::uint16_t>(2U + i * recordWords));
        writeU16(bytes, address + stream + i * 4U + 2U, static_cast<std::uint16_t>(i + 1U));
    }
    writeU16(bytes, address + stream + 12U, 0xFFFFU);
    writeU16(bytes, address + stream + 14U, 0xFFFFU);
    writeU32(bytes, address + vertices, normalDiffuse ? 0x2AU : 0x29U);
    writeU32(bytes, address + vertices + 4U, 3U << 16U);
    for (std::size_t i = 0; i < 3U; ++i) {
        const auto vertex = address + vertices + 8U + i * recordWords * 4U;
        writeF32(bytes, vertex, static_cast<float>(i == 1U));
        writeF32(bytes, vertex + 4U, 0.0F);
        writeF32(bytes, vertex + 8U, static_cast<float>(i == 2U));
        writeF32(bytes, vertex + 12U, 0.0F);
        writeF32(bytes, vertex + 16U, 1.0F);
        writeF32(bytes, vertex + 20U, 0.0F);
        if (normalDiffuse) {
            constexpr std::array<std::uint32_t, 3> colors{ 0x11223344U, 0x80ABCDEFU, 0xFFFFFFFFU };
            writeU32(bytes, vertex + 24U, colors[i]);
        }
    }
    return bytes;
}

template <typename VertexRange>
std::array<float, 6> positionBounds(const VertexRange& vertices) {
    std::array<float, 6> bounds{
        std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
    };
    for (const auto& vertex : vertices) {
        bounds[0] = std::min(bounds[0], vertex.position.x);
        bounds[1] = std::max(bounds[1], vertex.position.x);
        bounds[2] = std::min(bounds[2], vertex.position.y);
        bounds[3] = std::max(bounds[3], vertex.position.y);
        bounds[4] = std::min(bounds[4], vertex.position.z);
        bounds[5] = std::max(bounds[5], vertex.position.z);
    }
    return bounds;
}

} // namespace

TEST(MldCanonical, ParseBytesOwnsSharedListsAndCompleteSourceRanges) {
    const auto bytes = makeBaseMld();
    const auto file = MldParser{}.parseBytes(bytes);
    ASSERT_EQ(file.parseStatus, spice::mld::model::MldParseStatus::Complete)
        << describeDiagnostics(file);
    EXPECT_EQ(file.assetStatus, spice::mld::model::MldResourceStatus::Empty);
    ASSERT_EQ(file.entries.size(), 1U);
    ASSERT_TRUE(file.entries[0].entry.paramList2);
    ASSERT_TRUE(file.entries[0].entry.functionParameters);
    EXPECT_EQ(file.entries[0].entry.paramList2.get(), file.entries[0].entry.functionParameters.get());
    EXPECT_EQ(file.u32Lists.at(0x108U).get(), file.entries[0].entry.functionParameters.get());
    ASSERT_FALSE(file.sourceRanges.empty());
    std::size_t cursor = 0U;
    for (const auto& range : file.sourceRanges) {
        EXPECT_EQ(range.offset, cursor);
        cursor += range.size;
    }
    EXPECT_EQ(cursor, file.decodedBytes.size());
}

TEST(MldCanonical, WriterReturnsExactSourceAndRelocatesGrowingSharedList) {
    const auto bytes = makeBaseMld();
    auto file = MldParser{}.parseBytes(bytes);
    const auto unchanged = MldFileWriter{}.write(file);
    ASSERT_TRUE(unchanged.ok());
    ASSERT_EQ(unchanged.bytes.size(), bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        ASSERT_EQ(unchanged.bytes[i], bytes[i]) << "first changed byte at 0x" << std::hex << i;
    }

    file.entries[0].entry.functionParameters->values.resize(40U, 0xAABBCCDDU);
    const auto changed = MldFileWriter{}.write(file);
    ASSERT_TRUE(changed.ok());
    const auto reparsed = MldParser{}.parseBytes(changed.bytes);
    ASSERT_EQ(reparsed.entries.size(), 1U);
    ASSERT_TRUE(reparsed.entries[0].entry.paramList2);
    ASSERT_TRUE(reparsed.entries[0].entry.functionParameters);
    EXPECT_EQ(reparsed.entries[0].entry.paramList2.get(), reparsed.entries[0].entry.functionParameters.get());
    EXPECT_EQ(reparsed.entries[0].entry.functionParameters->values.size(), 40U);
}

TEST(MldCanonical, WriterReusesVacatedKnownRangesBeforeGrowingOutput) {
    const auto source = makeBaseMld();
    auto file = MldParser{}.parseBytes(source);
    auto& entry = file.entries[0].entry;
    ASSERT_TRUE(entry.functionParameters);
    ASSERT_TRUE(entry.objectAddresses);
    entry.functionParameters->values.clear();
    entry.objectAddresses->values = { 0x300U };

    const auto written = MldFileWriter{}.write(file);
    ASSERT_TRUE(written.ok());
    EXPECT_EQ(written.bytes.size(), source.size());
    const auto objectListLayout = std::find_if(written.layout.begin(), written.layout.end(), [](const auto& item) {
        return item.kind == "u32-list" && item.sourceOffset == 0x118U;
    });
    ASSERT_NE(objectListLayout, written.layout.end());
    EXPECT_EQ(objectListLayout->outputOffset, 0x10CU);

    const auto reparsed = MldParser{}.parseBytes(written.bytes);
    ASSERT_TRUE(reparsed.entries[0].entry.functionParameters);
    ASSERT_TRUE(reparsed.entries[0].entry.objectAddresses);
    EXPECT_TRUE(reparsed.entries[0].entry.functionParameters->values.empty());
    EXPECT_EQ(reparsed.entries[0].entry.objectAddresses->values, (std::vector<std::uint32_t>{ 0x300U }));
}

TEST(MldCanonical, WriterRebuildsEditedGrndTopology) {
    auto file = MldParser{}.parseBytes(makeGrndMld());
    auto& resource = file.groundResources.at(0x180U);
    ASSERT_TRUE(resource.grnd.has_value());
    auto& data = *resource.grnd;
    ASSERT_EQ(data.mesh.indices.size(), 3U);
    data.mesh.indices.insert(data.mesh.indices.end(), data.mesh.indices.begin(), data.mesh.indices.begin() + 3);
    data.mesh.triangleMetadata.push_back(data.mesh.triangleMetadata.front());
    data.cells[0].references.push_back(spice::mld::model::GrndTriangleReference{ .meshTriangleIndex = 1U });
    const auto written = MldFileWriter{}.write(file);
    ASSERT_TRUE(written.ok());
    const auto reparsed = MldParser{}.parseBytes(written.bytes);
    ASSERT_TRUE(reparsed.groundResources.at(reparsed.entries[0].entry.groundAddresses->values[0]).grnd.has_value());
    EXPECT_EQ(reparsed.groundResources.at(reparsed.entries[0].entry.groundAddresses->values[0]).grnd->mesh.indices.size(), 6U);
}

TEST(MldCanonical, WriterPreservesBakedGrndPlacementWhenCanonicalizingToZeroTranslation) {
    const auto source = makeGrndMld({ 10.0F, 20.0F, -30.0F }, { -100.5F, 200.25F });
    auto file = MldParser{}.parseBytes(source);
    auto& data = *file.groundResources.at(0x180U).grnd;
    ASSERT_EQ(data.triangleSets.size(), 1U);
    EXPECT_FLOAT_EQ(data.triangleSets[0].localToResourceTranslation.x, 10.0F);
    EXPECT_FLOAT_EQ(data.triangleSets[0].verticesByFloatIndex.at(0U).position.x, 0.0F);
    ASSERT_EQ(data.mesh.vertices.size(), 3U);
    EXPECT_FLOAT_EQ(data.mesh.vertices[0].position.x, 10.0F);
    EXPECT_FLOAT_EQ(data.mesh.vertices[0].position.y, 20.0F);
    EXPECT_FLOAT_EQ(data.mesh.vertices[0].position.z, -30.0F);

    const auto unchanged = MldFileWriter{}.write(file);
    ASSERT_TRUE(unchanged.ok());
    EXPECT_EQ(unchanged.bytes, source);

    const auto converted = MldFileWriter{}.write(file, spice::mld::exporting::MldWriteOptions{
        .platform = spice::mld::model::TargetPlatform::Dreamcast,
    });
    ASSERT_TRUE(converted.ok());
    const auto convertedFile = MldParser{}.parseBytes(converted.bytes);
    const auto convertedAddress = convertedFile.entries[0].entry.groundAddresses->values[0];
    const auto& convertedData = *convertedFile.groundResources.at(convertedAddress).grnd;
    ASSERT_EQ(convertedData.triangleSets.size(), 1U);
    EXPECT_FLOAT_EQ(convertedData.triangleSets[0].localToResourceTranslation.x, 0.0F);
    EXPECT_FLOAT_EQ(convertedData.triangleSets[0].localToResourceTranslation.y, 0.0F);
    EXPECT_FLOAT_EQ(convertedData.triangleSets[0].localToResourceTranslation.z, 0.0F);
    EXPECT_EQ(positionBounds(convertedData.mesh.vertices), positionBounds(data.mesh.vertices));
    EXPECT_FLOAT_EQ(convertedData.gridOriginX, -100.5F);
    EXPECT_FLOAT_EQ(convertedData.gridOriginZ, 200.25F);

    data.mesh.triangleMetadata[0].rawU16[0] ^= 1U;
    const auto changed = MldFileWriter{}.write(file);
    ASSERT_TRUE(changed.ok());
    const auto reparsed = MldParser{}.parseBytes(changed.bytes);
    const auto address = reparsed.entries[0].entry.groundAddresses->values[0];
    const auto& rebuilt = *reparsed.groundResources.at(address).grnd;
    ASSERT_EQ(rebuilt.triangleSets.size(), 1U);
    EXPECT_FLOAT_EQ(rebuilt.triangleSets[0].localToResourceTranslation.x, 0.0F);
    EXPECT_FLOAT_EQ(rebuilt.triangleSets[0].localToResourceTranslation.y, 0.0F);
    EXPECT_FLOAT_EQ(rebuilt.triangleSets[0].localToResourceTranslation.z, 0.0F);
    EXPECT_FLOAT_EQ(rebuilt.gridOriginX, -100.5F);
    EXPECT_FLOAT_EQ(rebuilt.gridOriginZ, 200.25F);
    ASSERT_EQ(rebuilt.mesh.vertices.size(), 3U);
    EXPECT_FLOAT_EQ(rebuilt.mesh.vertices[0].position.x, 10.0F);
    EXPECT_FLOAT_EQ(rebuilt.mesh.vertices[0].position.y, 20.0F);
    EXPECT_FLOAT_EQ(rebuilt.mesh.vertices[0].position.z, -30.0F);
    EXPECT_EQ(rebuilt.mesh.triangleMetadata[0].rawU16[0], data.mesh.triangleMetadata[0].rawU16[0]);
}

TEST(MldCanonical, GrndGridHelperAssignsEveryTriangleForWriting) {
    auto file = MldParser{}.parseBytes(makeGrndMld());
    auto& data = *file.groundResources.at(0x180U).grnd;
    data.cells.clear();
    std::vector<std::string> diagnostics{};
    ASSERT_TRUE(spice::mld::model::assignGrndTrianglesToIntersectingCells(
        data,
        spice::mld::model::GrndGridAssignmentOptions{ .originX = 0.0F, .originZ = 0.0F },
        &diagnostics));
    ASSERT_EQ(data.cells.size(), 1U);
    ASSERT_EQ(data.cells[0].references.size(), 1U);
    EXPECT_EQ(data.cells[0].references[0].meshTriangleIndex, 0U);
    EXPECT_FLOAT_EQ(data.gridOriginX, 0.0F);
    EXPECT_FLOAT_EQ(data.gridOriginZ, 0.0F);
    EXPECT_TRUE(MldFileWriter{}.write(file).ok());
}

TEST(MldCanonical, WriterRebuildsEditedGobjTopologyAndUpdatesObjectPointer) {
    auto file = MldParser{}.parseBytes(makeGobjMld());
    auto& resource = file.groundResources.at(0x180U);
    ASSERT_TRUE(resource.gobj.has_value());
    auto& mesh = resource.gobj->nodes[0].streamMesh;
    ASSERT_EQ(mesh.indices.size(), 3U);
    mesh.indices.insert(mesh.indices.end(), mesh.indices.begin(), mesh.indices.begin() + 3);
    mesh.triangleMetadata.push_back(mesh.triangleMetadata.front());
    const auto written = MldFileWriter{}.write(file);
    ASSERT_TRUE(written.ok());
    const auto reparsed = MldParser{}.parseBytes(written.bytes);
    const auto newAddress = reparsed.entries[0].entry.objectAddresses->values[0];
    ASSERT_TRUE(reparsed.groundResources.at(newAddress).gobj.has_value());
    EXPECT_EQ(reparsed.groundResources.at(newAddress).gobj->nodes[0].streamMesh.indices.size(), 6U);
}

TEST(MldCanonical, WriterPreservesAndRebuildsNormalDiffuseGobj) {
    const auto source = makeGobjMld(true);
    auto file = MldParser{}.parseBytes(source);
    auto& resource = file.groundResources.at(0x180U);
    ASSERT_TRUE(resource.gobj.has_value());
    auto& node = resource.gobj->nodes[0];
    ASSERT_TRUE(node.attach.has_value());
    EXPECT_EQ(node.attach->vertexChunk.chunkType, 0x2AU);
    ASSERT_EQ(node.streamMesh.vertices.size(), 3U);
    ASSERT_TRUE(node.streamMesh.vertices[0].diffuseColor.has_value());

    const auto unchanged = MldFileWriter{}.write(file);
    ASSERT_TRUE(unchanged.ok());
    EXPECT_EQ(unchanged.bytes, source);

    node.streamMesh.vertices[0].diffuseColor = spice::mld::model::ColorRgba8{
        .r = 1U, .g = 2U, .b = 3U, .a = 4U,
    };
    const auto written = MldFileWriter{}.write(file);
    ASSERT_TRUE(written.ok());
    const auto reparsed = MldParser{}.parseBytes(written.bytes);
    const auto address = reparsed.entries[0].entry.objectAddresses->values[0];
    const auto& rebuiltNode = reparsed.groundResources.at(address).gobj->nodes[0];
    ASSERT_TRUE(rebuiltNode.attach.has_value());
    EXPECT_EQ(rebuiltNode.attach->vertexChunk.chunkType, 0x2AU);
    ASSERT_TRUE(rebuiltNode.streamMesh.vertices[0].diffuseColor.has_value());
    EXPECT_EQ(rebuiltNode.streamMesh.vertices[0].diffuseColor->r, 1U);
    EXPECT_EQ(rebuiltNode.streamMesh.vertices[0].diffuseColor->g, 2U);
    EXPECT_EQ(rebuiltNode.streamMesh.vertices[0].diffuseColor->b, 3U);
    EXPECT_EQ(rebuiltNode.streamMesh.vertices[0].diffuseColor->a, 4U);

    const auto gameCubeWritten = MldFileWriter{}.write(file, spice::mld::exporting::MldWriteOptions{
        .platform = spice::mld::model::TargetPlatform::GameCube,
        .compressAklz = false,
    });
    ASSERT_TRUE(gameCubeWritten.ok());
    const auto gameCubeFile = MldParser{}.parseBytes(gameCubeWritten.bytes);
    EXPECT_EQ(gameCubeFile.endian, Endian::Big);
    const auto gameCubeAddress = gameCubeFile.entries[0].entry.objectAddresses->values[0];
    const auto& gameCubeNode = gameCubeFile.groundResources.at(gameCubeAddress).gobj->nodes[0];
    ASSERT_TRUE(gameCubeNode.streamMesh.vertices[0].diffuseColor.has_value());
    EXPECT_EQ(gameCubeNode.streamMesh.vertices[0].diffuseColor->r, 1U);
    EXPECT_EQ(gameCubeNode.streamMesh.vertices[0].diffuseColor->g, 2U);
    EXPECT_EQ(gameCubeNode.streamMesh.vertices[0].diffuseColor->b, 3U);
    EXPECT_EQ(gameCubeNode.streamMesh.vertices[0].diffuseColor->a, 4U);

    const auto scene = spice::mld::parsing::Sa3dBlenderIrBuilder{}.build(reparsed);
    const auto mesh = std::find_if(scene.meshes.begin(), scene.meshes.end(), [](const auto& candidate) {
        return candidate.label.starts_with("GOBJ_");
    });
    ASSERT_NE(mesh, scene.meshes.end());
    ASSERT_FALSE(mesh->triangleSets.empty());
    ASSERT_FALSE(mesh->triangleSets[0].corners.empty());
    EXPECT_TRUE(mesh->triangleSets[0].corners[0].hasColor);

    auto invalid = reparsed;
    auto& invalidVertices = invalid.groundResources.at(address).gobj->nodes[0].streamMesh.vertices;
    invalidVertices[0].diffuseColor.reset();
    EXPECT_FALSE(MldFileWriter{}.write(invalid).ok());
}

TEST(MldCanonical, WriterPreservesAklzSourceAndSupportsDreamcastProjection) {
    const auto source = makeGrndMld();
    const auto compressed = spice::compression::aklz::compress(source);
    ASSERT_TRUE(compressed.ok());
    const auto compressedFile = MldParser{}.parseBytes(compressed.bytes);
    const auto preserved = MldFileWriter{}.write(compressedFile);
    ASSERT_TRUE(preserved.ok());
    EXPECT_EQ(preserved.bytes, compressed.bytes);

    const auto file = MldParser{}.parseBytes(source);
    const auto converted = MldFileWriter{}.write(file, spice::mld::exporting::MldWriteOptions{
        .platform = spice::mld::model::TargetPlatform::Dreamcast,
        .compressAklz = false,
    });
    ASSERT_TRUE(converted.ok());
    const auto reparsed = MldParser{}.parseBytes(converted.bytes);
    EXPECT_EQ(reparsed.endian, Endian::Little);
    const auto address = reparsed.entries[0].entry.groundAddresses->values[0];
    ASSERT_TRUE(reparsed.groundResources.at(address).grnd.has_value());
    EXPECT_EQ(reparsed.groundResources.at(address).grnd->mesh.indices.size(), 3U);
}

TEST(MldCanonical, WriterRejectsRelocationReferencedByUnknownRange) {
    auto bytes = makeBaseMld();
    writeU32(bytes, 0x150U, 0x108U);
    auto file = MldParser{}.parseBytes(bytes);
    file.entries[0].entry.functionParameters->values.resize(40U, 1U);
    const auto written = MldFileWriter{}.write(file);
    EXPECT_FALSE(written.ok());
    EXPECT_TRUE(written.bytes.empty());
}

TEST(MldCanonical, WriterRejectsReplacedReadOnlySa3dMotion) {
    auto file = MldParser{}.parseBytes(makeBaseMld());
    const auto original = std::make_shared<const spice::modeling::Animation::Motion>();
    spice::mld::model::MldMotionResource resource{};
    resource.sourceAddress = 0x200U;
    resource.blockOffset = 0x200U;
    resource.blockSize = 0x20U;
    resource.variants.push_back(spice::mld::model::MldMotionVariant{
        .motion = std::make_shared<const spice::modeling::Animation::Motion>(),
        .originalMotion = original,
    });
    file.motionResources.emplace(resource.sourceAddress, std::move(resource));

    const auto written = MldFileWriter{}.write(file);
    EXPECT_FALSE(written.ok());
    ASSERT_FALSE(written.diagnostics.empty());
    EXPECT_NE(written.diagnostics.front().message.find("read-only"), std::string::npos);
}

TEST(MldCanonical, CompatibilityParseMatchesExplicitProjection) {
    const auto bytes = makeGrndMld();
    const MldParser parser{};
    const auto file = parser.parseBytes(bytes);
    const auto projected = parser.project(file);
    const auto compatibility = parser.parse(bytes);
    ASSERT_EQ(projected.entryList.size(), compatibility.entryList.size());
    ASSERT_EQ(projected.world.grndSurfaces.size(), compatibility.world.grndSurfaces.size());
    ASSERT_TRUE(projected.blenderIrScene.has_value());
    ASSERT_TRUE(compatibility.blenderIrScene.has_value());
    EXPECT_EQ(
        spice::mld::exporting::BlenderIrJsonExporter{}.toJson(*projected.blenderIrScene),
        spice::mld::exporting::BlenderIrJsonExporter{}.toJson(*compatibility.blenderIrScene));
}

TEST(MldDocument, ImportsNeutralEditableStructureAndWritesWithExplicitTarget) {
    const auto source = makeBaseMld();
    auto imported = spice::mld::MldDocumentImporter::importBytes(source);
    ASSERT_TRUE(imported.ok());
    ASSERT_TRUE(imported.document.has_value());
    EXPECT_EQ(imported.receipt.platform, spice::mld::MldPlatform::GameCube);
    EXPECT_EQ(imported.receipt.wrapper, spice::mld::MldWrapper::Raw);
    ASSERT_EQ(imported.document->entries.size(), 1U);
    EXPECT_TRUE(imported.document->entries.front().id);
    EXPECT_EQ(imported.document->allocateEntryId().value, 2U);
    EXPECT_FALSE(imported.document->layout.empty());

    const auto projection = spice::mld::MldBlenderIrProjector::project(*imported.document);
    EXPECT_TRUE(projection.ok());

    imported.document->entries.front().functionName = "edited";
    const spice::mld::MldWriteTarget target{
        .platform = spice::mld::MldPlatform::GameCube,
        .wrapper = spice::mld::MldWrapper::Raw,
    };
    const auto validation = spice::mld::MldDocumentValidator::validate(
        *imported.document, target, &imported.receipt);
    ASSERT_TRUE(validation.ok());
    const auto written = spice::mld::MldDocumentWriter::write(
        *imported.document, target, &imported.receipt);
    ASSERT_TRUE(written.ok());

    const auto reparsed = spice::mld::MldDocumentImporter::importBytes(written.bytes);
    ASSERT_TRUE(reparsed.ok());
    ASSERT_TRUE(reparsed.document.has_value());
    EXPECT_EQ(reparsed.document->entries.front().functionName, "edited");
}

TEST(MldDocument, RequiresReceiptForOpaquePreservingWrites) {
    spice::mld::MldDocument document{};
    document.entries.push_back({ .id = spice::mld::MldEntryId{ 1U } });
    document.opaqueMembers.push_back({
        .id = spice::mld::MldOpaqueMemberId{ 1U },
        .role = "unknown",
        .payload = { { 1U, 2U, 3U } },
    });
    document.layout.push_back(document.entries.front().id);
    document.layout.push_back(document.opaqueMembers.front().id);
    const auto result = spice::mld::MldDocumentWriter::write(document, {
        .platform = spice::mld::MldPlatform::Dreamcast,
        .wrapper = spice::mld::MldWrapper::Raw,
    });
    EXPECT_FALSE(result.ok());
}

TEST(MldDocument, ConstructivelyWritesFullyDecodedContentWithoutAReceipt) {
    spice::mld::MldDocument document{};
    spice::mld::MldEntry entry{};
    entry.id = spice::mld::MldEntryId{ 1U };
    entry.entryId = 42U;
    entry.tableId = -7;
    entry.functionName = "constructive";
    document.entries.push_back(entry);
    document.layout.push_back(entry.id);

    const auto dreamcast = spice::mld::MldDocumentWriter::write(document, {
        .platform = spice::mld::MldPlatform::Dreamcast,
        .wrapper = spice::mld::MldWrapper::Raw,
    });
    ASSERT_TRUE(dreamcast.ok());
    const auto dreamcastParsed = spice::mld::MldDocumentImporter::importBytes(dreamcast.bytes);
    ASSERT_TRUE(dreamcastParsed.ok());
    ASSERT_TRUE(dreamcastParsed.document.has_value());
    ASSERT_EQ(dreamcastParsed.document->entries.size(), 1U);
    EXPECT_EQ(dreamcastParsed.document->entries.front().entryId, 42U);
    EXPECT_EQ(dreamcastParsed.document->entries.front().tableId, -7);
    EXPECT_EQ(dreamcastParsed.document->entries.front().functionName, "constructive");

    const auto gameCube = spice::mld::MldDocumentWriter::write(document, {
        .platform = spice::mld::MldPlatform::GameCube,
        .wrapper = spice::mld::MldWrapper::Aklz,
    });
    ASSERT_TRUE(gameCube.ok());
    const auto gameCubeParsed = spice::mld::MldDocumentImporter::importBytes(gameCube.bytes);
    ASSERT_TRUE(gameCubeParsed.ok());
    ASSERT_TRUE(gameCubeParsed.document.has_value());
    ASSERT_EQ(gameCubeParsed.document->entries.size(), 1U);
    EXPECT_EQ(gameCubeParsed.document->entries.front().entryId, 42U);
    EXPECT_EQ(gameCubeParsed.document->entries.front().tableId, -7);
    EXPECT_EQ(gameCubeParsed.document->entries.front().functionName, "constructive");
}

TEST(MldDocument, EditsSourceNeutralGrndContentAndRebuildsIt) {
    auto imported = spice::mld::MldDocumentImporter::importBytes(makeGrndMld());
    ASSERT_TRUE(imported.ok());
    ASSERT_TRUE(imported.document.has_value());
    ASSERT_EQ(imported.document->grounds.size(), 1U);
    auto* ground = std::get_if<spice::mld::MldGrndDocument>(&imported.document->grounds.front().payload);
    ASSERT_NE(ground, nullptr);
    ASSERT_EQ(ground->mesh.indices.size(), 3U);
    const auto originalIndices = ground->mesh.indices;
    ground->mesh.indices.insert(ground->mesh.indices.end(), originalIndices.begin(), originalIndices.end());
    ground->mesh.triangleMetadata.push_back(ground->mesh.triangleMetadata.front());
    ASSERT_FALSE(ground->cells.empty());
    ground->cells.front().triangleIndices.push_back(1U);

    const auto written = spice::mld::MldDocumentWriter::write(*imported.document, {
        .platform = spice::mld::MldPlatform::GameCube,
        .wrapper = spice::mld::MldWrapper::Raw,
    }, &imported.receipt);
    ASSERT_TRUE(written.ok());
    const auto reparsed = spice::mld::MldDocumentImporter::importBytes(written.bytes);
    ASSERT_TRUE(reparsed.ok());
    ASSERT_TRUE(reparsed.document.has_value());
    ASSERT_EQ(reparsed.document->grounds.size(), 1U);
    const auto* rebuilt = std::get_if<spice::mld::MldGrndDocument>(&reparsed.document->grounds.front().payload);
    ASSERT_NE(rebuilt, nullptr);
    EXPECT_EQ(rebuilt->mesh.indices.size(), 6U);
}

TEST(MldDocument, ConstructivelyWritesAndRelocatesEditedTextureLists) {
    spice::mld::MldDocument document{};
    spice::mld::MldEntry entry{};
    entry.id = spice::mld::MldEntryId{ 1U };
    entry.entryId = 9U;
    entry.textureList = spice::mld::MldTextureListId{ 1U };
    document.entries.push_back(entry);
    document.textureLists.push_back({
        .id = spice::mld::MldTextureListId{ 1U },
        .names = { "first", "second" },
    });
    document.layout = { entry.id, spice::mld::MldTextureListId{ 1U } };

    const auto initial = spice::mld::MldDocumentWriter::write(document, {
        .platform = spice::mld::MldPlatform::Dreamcast,
        .wrapper = spice::mld::MldWrapper::Raw,
    });
    ASSERT_TRUE(initial.ok());
    auto imported = spice::mld::MldDocumentImporter::importBytes(initial.bytes);
    ASSERT_TRUE(imported.ok());
    ASSERT_TRUE(imported.document.has_value());
    ASSERT_EQ(imported.document->textureLists.size(), 1U);
    EXPECT_EQ(imported.document->textureLists.front().names,
        (std::vector<std::string>{ "first", "second" }));

    imported.document->textureLists.front().names.front() = std::string(80U, 'x');
    const auto edited = spice::mld::MldDocumentWriter::write(*imported.document, {
        .platform = spice::mld::MldPlatform::Dreamcast,
        .wrapper = spice::mld::MldWrapper::Raw,
    }, &imported.receipt);
    ASSERT_TRUE(edited.ok());
    const auto reparsed = spice::mld::MldDocumentImporter::importBytes(edited.bytes);
    ASSERT_TRUE(reparsed.ok());
    ASSERT_TRUE(reparsed.document.has_value());
    ASSERT_EQ(reparsed.document->textureLists.size(), 1U);
    EXPECT_EQ(reparsed.document->textureLists.front().names.front(), std::string(80U, 'x'));
    EXPECT_EQ(reparsed.document->textureLists.front().names.back(), "second");
}

TEST(MldMotionFrameProjector, ReportsIntrinsicSlotFailuresWithoutInferringAnOwner) {
    spice::mld::MldDocument document{};
    document.entries.push_back({
        .id = spice::mld::MldEntryId{ 1U },
        .motionSlots = {
            std::nullopt,
            spice::mld::MldMotionId{ 99U },
            spice::mld::MldMotionId{ 1U },
            spice::mld::MldMotionId{ 2U },
        },
    });
    document.motions.push_back({
        .id = spice::mld::MldMotionId{ 1U },
        .payload = spice::mld::MldOpaquePayload{ { 1U } },
    });
    document.motions.push_back({
        .id = spice::mld::MldMotionId{ 2U },
        .payload = spice::mld::MldDecodedMotion{ .kind = spice::modeling::MotionKind::Node },
    });

    const auto absent = spice::mld::MldMotionFrameProjector::project(
        document, spice::mld::MldEntryId{ 2U });
    EXPECT_FALSE(absent.entryId.has_value());
    EXPECT_TRUE(absent.slots.empty());
    EXPECT_FALSE(absent.ok());

    const auto projected = spice::mld::MldMotionFrameProjector::project(
        document, spice::mld::MldEntryId{ 1U });
    ASSERT_EQ(projected.slots.size(), 4U);
    EXPECT_EQ(projected.slots[0].status, spice::mld::MldMotionFrameSlotStatus::EmptySlot);
    EXPECT_EQ(projected.slots[1].status, spice::mld::MldMotionFrameSlotStatus::MissingMotion);
    EXPECT_EQ(projected.slots[2].status, spice::mld::MldMotionFrameSlotStatus::OpaqueMotion);
    EXPECT_EQ(projected.slots[3].status, spice::mld::MldMotionFrameSlotStatus::NoDecodedVariants);
    EXPECT_FALSE(projected.ok());
}
