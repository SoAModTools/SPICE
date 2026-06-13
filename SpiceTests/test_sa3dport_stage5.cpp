#include "gtest/gtest.h"

#include "Mesh/Converters/ChunkBufferConverter.h"
#include "Testing/Slice5TestApi.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using namespace Sa3Dport::Testing::Slice5;
namespace S = Sa3Dport::Structs;

void WriteU16(std::vector<std::byte>& data, std::uint32_t offset, std::uint16_t value) {
    if (data.size() < offset + 2u) {
        data.resize(offset + 2u);
    }
    data[offset] = std::byte(value & 0xFFu);
    data[offset + 1u] = std::byte((value >> 8u) & 0xFFu);
}

void WriteU32(std::vector<std::byte>& data, std::uint32_t offset, std::uint32_t value) {
    if (data.size() < offset + 4u) {
        data.resize(offset + 4u);
    }
    data[offset] = std::byte(value & 0xFFu);
    data[offset + 1u] = std::byte((value >> 8u) & 0xFFu);
    data[offset + 2u] = std::byte((value >> 16u) & 0xFFu);
    data[offset + 3u] = std::byte((value >> 24u) & 0xFFu);
}

void WriteF32(std::vector<std::byte>& data, std::uint32_t offset, float value) {
    WriteU32(data, offset, std::bit_cast<std::uint32_t>(value));
}

void WriteVec3(std::vector<std::byte>& data, std::uint32_t offset, S::Vector3 value) {
    WriteF32(data, offset, value.x);
    WriteF32(data, offset + 4u, value.y);
    WriteF32(data, offset + 8u, value.z);
}

void WriteHeader(std::vector<std::byte>& data, std::uint32_t offset, PolyChunkType type, std::uint8_t attributes = 0) {
    WriteU16(data, offset, static_cast<std::uint16_t>(static_cast<std::uint8_t>(type) | (attributes << 8u)));
}

TEST(Sa3DportStage5, ReadsNullDelimitedPolyChunkArray) {
    std::vector<std::byte> data(0x20);
    WriteHeader(data, 0, PolyChunkType::Null);
    WriteHeader(data, 2, PolyChunkType::BlendAlpha, 0x2B);
    WriteHeader(data, 4, PolyChunkType::TextureID, 0xF3);
    WriteU16(data, 6, 0xA123);
    WriteHeader(data, 8, PolyChunkType::End);

    S::EndianStackReader reader(data, S::Endian::Little);
    const auto chunks = ReadPolyChunks(reader, 0);

    ASSERT_EQ(chunks.size(), 3u);
    EXPECT_FALSE(chunks[0].has_value());
    ASSERT_TRUE(chunks[1].has_value());
    EXPECT_EQ((*chunks[1])->type, PolyChunkType::BlendAlpha);
    const auto bits = std::dynamic_pointer_cast<BitsChunk>(*chunks[1]);
    ASSERT_NE(bits, nullptr);
    EXPECT_EQ(bits->source_alpha(), 5u);
    EXPECT_EQ(bits->destination_alpha(), 3u);

    const auto texture = std::dynamic_pointer_cast<TextureChunk>(*chunks[2]);
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->texture_id(), 0x123u);
    EXPECT_TRUE(texture->super_sample());
    EXPECT_EQ(texture->filter_mode(), 2u);
    EXPECT_TRUE(texture->mirror_u());
    EXPECT_TRUE(texture->mirror_v());
    EXPECT_TRUE(texture->clamp_u());
    EXPECT_TRUE(texture->clamp_v());
}

TEST(Sa3DportStage5, ReadsMaterialColorsAndSpecularExponent) {
    std::vector<std::byte> data(0x20);
    WriteHeader(data, 0, PolyChunkType::Material_DiffuseAmbientSpecular, 0x39);
    WriteU16(data, 2, 6);
    WriteU32(data, 4, 0x44332211);
    WriteU32(data, 8, 0x88776655);
    WriteU32(data, 12, 0xCCBBAA19);

    S::EndianStackReader reader(data, S::Endian::Little);
    std::uint32_t address = 0;
    const auto chunk = ReadPolyChunk(reader, address);
    const auto material = std::dynamic_pointer_cast<MaterialChunk>(chunk);

    ASSERT_NE(material, nullptr);
    EXPECT_EQ(address, 16u);
    ASSERT_TRUE(material->diffuse.has_value());
    ASSERT_TRUE(material->ambient.has_value());
    ASSERT_TRUE(material->specular.has_value());
    EXPECT_EQ(material->diffuse->argb(), 0x44332211u);
    EXPECT_EQ(material->ambient->argb(), 0x88776655u);
    EXPECT_EQ(material->specular_exponent, 0xCCu);
    EXPECT_EQ(material->specular->argb(), 0xFFBBAA19u);
    EXPECT_EQ(material->source_alpha(), 7u);
    EXPECT_EQ(material->destination_alpha(), 1u);
}

TEST(Sa3DportStage5, ReadsStripChunkAndAdvancesBySizedContent) {
    std::vector<std::byte> data(0x40);
    WriteHeader(data, 0, PolyChunkType::Strip_Tex, 0x98);
    WriteU16(data, 2, 9);
    WriteU16(data, 4, static_cast<std::uint16_t>(1u | (1u << 14u)));
    WriteU16(data, 6, 3);
    WriteU16(data, 8, 4);
    WriteU16(data, 10, 256);
    WriteU16(data, 12, 128);
    WriteU16(data, 14, 5);
    WriteU16(data, 16, 512);
    WriteU16(data, 18, 256);
    WriteU16(data, 20, 6);
    WriteU16(data, 22, 768);
    WriteU16(data, 24, 384);
    WriteU16(data, 26, 0xBEEF);

    S::EndianStackReader reader(data, S::Endian::Little);
    std::uint32_t address = 0;
    const auto chunk = ReadPolyChunk(reader, address);
    const auto strip = std::dynamic_pointer_cast<StripChunk>(chunk);

    ASSERT_NE(strip, nullptr);
    EXPECT_EQ(address, 28u);
    EXPECT_EQ(strip->byte_size(), 28u);
    EXPECT_EQ(strip->texcoord_count(), 1);
    EXPECT_FALSE(strip->has_hd_texcoords());
    EXPECT_FALSE(strip->has_normals());
    EXPECT_FALSE(strip->has_colors());
    EXPECT_TRUE(strip->use_alpha());
    EXPECT_TRUE(strip->no_alpha_test());
    ASSERT_EQ(strip->strips.size(), 1u);
    ASSERT_EQ(strip->strips[0].corners.size(), 3u);
    EXPECT_EQ(strip->strips[0].corners[2].index, 6u);
    EXPECT_FLOAT_EQ(strip->strips[0].corners[2].texcoord.x, 3.0f);
    EXPECT_EQ(strip->strips[0].corners[2].attributes1, 0xBEEFu);
}

TEST(Sa3DportStage5, ChunkAttachRoutesPolyPointerIntoParsedChunks) {
    std::vector<std::byte> data(0x80);
    WriteU32(data, 0, 0);
    WriteU32(data, 4, 0x40);
    WriteVec3(data, 8, {1.0f, 2.0f, 3.0f});
    WriteF32(data, 20, 4.0f);
    WriteHeader(data, 0x40, PolyChunkType::BlendAlpha, 0x12);
    WriteHeader(data, 0x42, PolyChunkType::End);

    S::EndianStackReader reader(data, S::Endian::Little);
    AttachReadContext context;
    const auto attach = ReadChunkAttach(reader, 0, context);

    ASSERT_NE(attach, nullptr);
    EXPECT_EQ(attach->poly_chunks_address, 0x40u);
    EXPECT_FALSE(attach->poly_chunks_deferred);
    ASSERT_EQ(attach->poly_chunks.size(), 1u);
    ASSERT_TRUE(attach->poly_chunks[0].has_value());
    EXPECT_EQ((*attach->poly_chunks[0])->type, PolyChunkType::BlendAlpha);
}

TEST(Sa3DportStage5, ChunkBufferContextSharesVertexCacheAcrossAttaches) {
    ChunkAttach vertexAttach{};
    Sa3Dport::Mesh::Chunk::VertexChunk vertexChunk{};
    vertexChunk.type = Sa3Dport::Mesh::Chunk::VertexChunkType::Blank;
    vertexChunk.index_offset = 0;
    vertexChunk.vertices.resize(3);
    vertexChunk.vertices[0].position = {1.0f, 2.0f, 3.0f};
    vertexChunk.vertices[1].position = {4.0f, 5.0f, 6.0f};
    vertexChunk.vertices[1].diffuse = S::Color(0x12, 0x34, 0x56, 0x78);
    vertexChunk.vertices[2].position = {7.0f, 8.0f, 9.0f};
    vertexAttach.vertex_chunks.push_back(vertexChunk);

    ChunkAttach stripAttach{};
    auto strip = std::make_shared<StripChunk>(PolyChunkType::Strip_Blank);
    Sa3Dport::Mesh::Chunk::Structs::ChunkStrip chunkStrip{};
    chunkStrip.corners.resize(3);
    chunkStrip.corners[0].index = 0;
    chunkStrip.corners[1].index = 1;
    chunkStrip.corners[2].index = 2;
    strip->strips.push_back(chunkStrip);
    stripAttach.poly_chunks.push_back(strip);

    Sa3Dport::Mesh::Converters::ChunkBufferContext context{};
    const auto vertexMeshes = Sa3Dport::Mesh::Converters::buffer_chunk_attach(vertexAttach, {}, context);
    const auto stripMeshes = Sa3Dport::Mesh::Converters::buffer_chunk_attach(stripAttach, stripAttach.poly_chunks, context);

    ASSERT_EQ(vertexMeshes.size(), 1u);
    ASSERT_FALSE(vertexMeshes[0].has_corners());
    ASSERT_EQ(stripMeshes.size(), 1u);
    ASSERT_TRUE(stripMeshes[0].has_corners());

    const auto triangles = stripMeshes[0].corner_triangle_list();
    ASSERT_EQ(triangles.size(), 3u);
    EXPECT_EQ(triangles[0].vertex_index, 0u);
    EXPECT_FLOAT_EQ(triangles[0].normal.y, 1.0f);
    EXPECT_EQ(triangles[1].vertex_index, 1u);
    EXPECT_EQ(triangles[1].color.argb(), 0x78123456u);
}

TEST(Sa3DportStage5, RejectsVolumeChunksForThisSlice) {
    std::vector<std::byte> data(4);
    WriteHeader(data, 0, PolyChunkType::Volume_Polygon3);

    S::EndianStackReader reader(data, S::Endian::Little);
    std::uint32_t address = 0;
    EXPECT_THROW(ReadPolyChunk(reader, address), std::runtime_error);
}

} // namespace
