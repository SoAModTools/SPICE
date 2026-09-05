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

using namespace spice::modeling::Testing::Slice5;
namespace S = spice::modeling::Structs;

void WriteU16(std::vector<std::byte>& data, std::uint32_t offset, std::uint16_t value) {
    if (data.size() < offset + 2u) {
        data.resize(offset + 2u);
    }
    data[offset] = std::byte(value & 0xFFu);
    data[offset + 1u] = std::byte((value >> 8u) & 0xFFu);
}

void WriteU16Endian(
    std::vector<std::byte>& data,
    std::uint32_t offset,
    std::uint16_t value,
    S::Endian endian) {
    WriteU16(data, offset, endian == S::Endian::Little
        ? value
        : static_cast<std::uint16_t>((value << 8u) | (value >> 8u)));
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

TEST(SpiceModelingStage5, ReadsNullDelimitedPolyChunkArray) {
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

TEST(SpiceModelingStage5, ReadsMaterialColorsAndSpecularExponent) {
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

TEST(SpiceModelingStage5, ReadsStripChunkAndAdvancesBySizedContent) {
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

TEST(SpiceModelingStage5, ChunkAttachRoutesPolyPointerIntoParsedChunks) {
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

TEST(SpiceModelingStage5, ChunkBufferContextSharesVertexCacheAcrossAttaches) {
    ChunkAttach vertexAttach{};
    spice::modeling::Mesh::Chunk::VertexChunk vertexChunk{};
    vertexChunk.type = spice::modeling::Mesh::Chunk::VertexChunkType::Blank;
    vertexChunk.index_offset = 0;
    vertexChunk.vertices.resize(3);
    vertexChunk.vertices[0].position = {1.0f, 2.0f, 3.0f};
    vertexChunk.vertices[1].position = {4.0f, 5.0f, 6.0f};
    vertexChunk.vertices[1].diffuse = S::Color(0x12, 0x34, 0x56, 0x78);
    vertexChunk.vertices[2].position = {7.0f, 8.0f, 9.0f};
    vertexAttach.vertex_chunks.push_back(vertexChunk);

    ChunkAttach stripAttach{};
    auto strip = std::make_shared<StripChunk>(PolyChunkType::Strip_Blank);
    spice::modeling::Mesh::Chunk::Structs::ChunkStrip chunkStrip{};
    chunkStrip.corners.resize(3);
    chunkStrip.corners[0].index = 0;
    chunkStrip.corners[1].index = 1;
    chunkStrip.corners[2].index = 2;
    strip->strips.push_back(chunkStrip);
    stripAttach.poly_chunks.push_back(strip);

    spice::modeling::Mesh::Converters::ChunkBufferContext context{};
    const auto vertexMeshes = spice::modeling::Mesh::Converters::buffer_chunk_attach(vertexAttach, {}, context);
    const auto stripMeshes = spice::modeling::Mesh::Converters::buffer_chunk_attach(stripAttach, stripAttach.poly_chunks, context);

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

TEST(SpiceModelingStage5, ReadsVolumeTrianglesAndQuadsWithAllUserWordCountsAndEndians) {
    for (const auto endian : {S::Endian::Little, S::Endian::Big}) {
        for (const auto type : {PolyChunkType::Volume_Polygon3, PolyChunkType::Volume_Polygon4}) {
            for (std::uint16_t userCount = 0; userCount <= 3; ++userCount) {
                const std::uint16_t indexCount = type == PolyChunkType::Volume_Polygon3 ? 3U : 4U;
                const std::uint16_t sizeWords = static_cast<std::uint16_t>(1U + indexCount + userCount);
                std::vector<std::byte> data(4U + sizeWords * 2U);
                WriteU16Endian(data, 0U,
                    static_cast<std::uint16_t>(type) | 0xA500U, endian);
                WriteU16Endian(data, 2U, sizeWords, endian);
                WriteU16Endian(data, 4U, static_cast<std::uint16_t>(1U | (userCount << 14U)), endian);
                std::uint32_t cursor = 6U;
                for (std::uint16_t i = 0; i < indexCount; ++i, cursor += 2U) {
                    WriteU16Endian(data, cursor, static_cast<std::uint16_t>(0x100U + i), endian);
                }
                for (std::uint16_t i = 0; i < userCount; ++i, cursor += 2U) {
                    WriteU16Endian(data, cursor, static_cast<std::uint16_t>(0xA000U + i), endian);
                }

                S::EndianStackReader reader(data, endian);
                std::uint32_t address = 0;
                const auto chunk = std::dynamic_pointer_cast<
                    spice::modeling::Mesh::Chunk::PolyChunks::VolumeChunk>(ReadPolyChunk(reader, address));
                ASSERT_NE(chunk, nullptr);
                EXPECT_EQ(address, data.size());
                EXPECT_EQ(chunk->source_address, 0U);
                EXPECT_EQ(chunk->declared_size_words, sizeWords);
                EXPECT_EQ(chunk->attributes, 0xA5U);
                EXPECT_EQ(chunk->count_and_user,
                    static_cast<std::uint16_t>(1U | (userCount << 14U)));
                EXPECT_EQ(chunk->polygon_attribute_count, userCount);
                ASSERT_EQ(chunk->polygons.size(), 1U);
                EXPECT_EQ(chunk->polygons[0].indices.size(), indexCount);
                EXPECT_EQ(chunk->polygons[0].indices.front(), 0x100U);
                EXPECT_EQ(chunk->polygons[0].user_words.size(), userCount);
            }
        }
    }
}

TEST(SpiceModelingStage5, ReadsVolumeStripWithEveryUserWordCountAndEndian) {
    for (const auto endian : {S::Endian::Little, S::Endian::Big}) {
        for (std::uint16_t userCount = 0U; userCount <= 3U; ++userCount) {
            const auto sizeWords = static_cast<std::uint16_t>(5U + userCount);
            std::vector<std::byte> data(4U + sizeWords * 2U);
            WriteU16Endian(data, 0U,
                static_cast<std::uint16_t>(PolyChunkType::Volume_Strip) | 0x3C00U, endian);
            WriteU16Endian(data, 2U, sizeWords, endian);
            WriteU16Endian(data, 4U,
                static_cast<std::uint16_t>(1U | (userCount << 14U)), endian);
            WriteU16Endian(data, 6U, 3U, endian);
            WriteU16Endian(data, 8U, 0x10U, endian);
            WriteU16Endian(data, 10U, 0x11U, endian);
            WriteU16Endian(data, 12U, 0x12U, endian);
            for (std::uint16_t user = 0; user < userCount; ++user) {
                WriteU16Endian(data, 14U + user * 2U,
                    static_cast<std::uint16_t>(0xA100U + user), endian);
            }

            S::EndianStackReader reader(data, endian);
            std::uint32_t address = 0U;
            const auto volume = std::dynamic_pointer_cast<
                spice::modeling::Mesh::Chunk::PolyChunks::VolumeChunk>(ReadPolyChunk(reader, address));
            ASSERT_NE(volume, nullptr);
            EXPECT_EQ(address, data.size());
            EXPECT_EQ(volume->attributes, 0x3CU);
            ASSERT_EQ(volume->strips.size(), 1U);
            EXPECT_EQ(volume->strips[0].indices,
                (std::vector<std::uint16_t>{0x10U, 0x11U, 0x12U}));
            ASSERT_EQ(volume->strips[0].triangle_user_words.size(), 1U);
            EXPECT_EQ(volume->strips[0].triangle_user_words[0].size(), userCount);
        }
    }
}

TEST(SpiceModelingStage5, ReadsPositiveAndNegativeVolumeStripsWithCanonicalWinding) {
    constexpr std::uint16_t sizeWords = 15U;
    std::vector<std::byte> data(4U + sizeWords * 2U + 2U);
    WriteHeader(data, 0U, PolyChunkType::Volume_Strip);
    WriteU16(data, 2U, sizeWords);
    WriteU16(data, 4U, static_cast<std::uint16_t>(2U | (1U << 14U)));
    std::uint32_t cursor = 6U;
    for (const std::int16_t signedCount : {std::int16_t(4), std::int16_t(-4)}) {
        WriteU16(data, cursor, static_cast<std::uint16_t>(signedCount));
        cursor += 2U;
        WriteU16(data, cursor, 0U); cursor += 2U;
        WriteU16(data, cursor, 1U); cursor += 2U;
        WriteU16(data, cursor, 2U); cursor += 2U;
        WriteU16(data, cursor, 0xA1U); cursor += 2U;
        WriteU16(data, cursor, 3U); cursor += 2U;
        WriteU16(data, cursor, 0xA2U); cursor += 2U;
    }
    WriteHeader(data, cursor, PolyChunkType::End);

    S::EndianStackReader reader(data, S::Endian::Little);
    const auto chunks = ReadPolyChunks(reader, 0U);
    ASSERT_EQ(chunks.size(), 1U);
    const auto volume = std::dynamic_pointer_cast<
        spice::modeling::Mesh::Chunk::PolyChunks::VolumeChunk>(*chunks[0]);
    ASSERT_NE(volume, nullptr);
    ASSERT_EQ(volume->strips.size(), 2U);
    EXPECT_EQ(volume->strips[0].signed_index_count, 4);
    EXPECT_EQ(volume->strips[1].signed_index_count, -4);
    ASSERT_EQ(volume->strips[0].triangle_user_words.size(), 2U);
    EXPECT_EQ(volume->strips[0].triangle_user_words[1][0], 0xA2U);

    const auto positive = volume->strips[0].canonical_triangles();
    const auto negative = volume->strips[1].canonical_triangles();
    ASSERT_EQ(positive.size(), 2U);
    EXPECT_EQ(positive[0], (std::array<std::uint16_t, 3>{0U, 1U, 2U}));
    EXPECT_EQ(positive[1], (std::array<std::uint16_t, 3>{2U, 1U, 3U}));
    EXPECT_EQ(negative[0], (std::array<std::uint16_t, 3>{1U, 0U, 2U}));
    EXPECT_EQ(negative[1], (std::array<std::uint16_t, 3>{1U, 2U, 3U}));

    ChunkAttach attach{};
    attach.poly_chunks.push_back(*chunks[0]);
    spice::modeling::Mesh::Converters::ChunkBufferContext context{};
    EXPECT_TRUE(spice::modeling::Mesh::Converters::buffer_chunk_attach(
        attach, attach.poly_chunks, context).empty());
    ASSERT_EQ(attach.poly_chunks.size(), 1U);
}

TEST(SpiceModelingStage5, PreservesBoundedZeroPaddingInsideDeclaredVolumeSize) {
    std::vector<std::byte> data(14U);
    WriteHeader(data, 0U, PolyChunkType::Volume_Polygon3);
    WriteU16(data, 2U, 5U);
    WriteU16(data, 4U, 1U);
    WriteU16(data, 6U, 3U);
    WriteU16(data, 8U, 4U);
    WriteU16(data, 10U, 5U);
    WriteU16(data, 12U, 0U);

    S::EndianStackReader reader(data, S::Endian::Little);
    std::uint32_t address = 0;
    const auto volume = std::dynamic_pointer_cast<
        spice::modeling::Mesh::Chunk::PolyChunks::VolumeChunk>(ReadPolyChunk(reader, address));
    ASSERT_NE(volume, nullptr);
    EXPECT_EQ(address, data.size());
    EXPECT_EQ(volume->trailing_padding_words, (std::vector<std::uint16_t>{0U}));
}

TEST(SpiceModelingStage5, ReadsBigEndianVolumeStripUserWords) {
    std::vector<std::byte> data(20U);
    WriteU16Endian(data, 0U, static_cast<std::uint16_t>(PolyChunkType::Volume_Strip), S::Endian::Big);
    WriteU16Endian(data, 2U, 8U, S::Endian::Big);
    WriteU16Endian(data, 4U, static_cast<std::uint16_t>(1U | (3U << 14U)), S::Endian::Big);
    WriteU16Endian(data, 6U, static_cast<std::uint16_t>(-3), S::Endian::Big);
    WriteU16Endian(data, 8U, 0x10U, S::Endian::Big);
    WriteU16Endian(data, 10U, 0x11U, S::Endian::Big);
    WriteU16Endian(data, 12U, 0x12U, S::Endian::Big);
    WriteU16Endian(data, 14U, 0xA1U, S::Endian::Big);
    WriteU16Endian(data, 16U, 0xA2U, S::Endian::Big);
    WriteU16Endian(data, 18U, 0xA3U, S::Endian::Big);

    S::EndianStackReader reader(data, S::Endian::Big);
    std::uint32_t address = 0;
    const auto volume = std::dynamic_pointer_cast<
        spice::modeling::Mesh::Chunk::PolyChunks::VolumeChunk>(ReadPolyChunk(reader, address));
    ASSERT_NE(volume, nullptr);
    ASSERT_EQ(volume->strips.size(), 1U);
    EXPECT_EQ(volume->strips[0].signed_index_count, -3);
    EXPECT_EQ(volume->strips[0].indices, (std::vector<std::uint16_t>{0x10U, 0x11U, 0x12U}));
    ASSERT_EQ(volume->strips[0].triangle_user_words.size(), 1U);
    EXPECT_EQ(volume->strips[0].triangle_user_words[0],
        (std::vector<std::uint16_t>{0xA1U, 0xA2U, 0xA3U}));
}

TEST(SpiceModelingStage5, RejectsMalformedVolumeRecordsWithinDeclaredBounds) {
    for (const auto& data : {
        std::vector<std::byte>(2U),
        std::vector<std::byte>(6U),
        std::vector<std::byte>(8U)}) {
        auto malformed = data;
        if (malformed.size() >= 2U) WriteHeader(malformed, 0U, PolyChunkType::Volume_Polygon3);
        if (malformed.size() >= 4U) WriteU16(malformed, 2U, 0xFFFFU);
        if (malformed.size() >= 6U && malformed.size() == 8U) {
            WriteU16(malformed, 2U, 2U);
            WriteU16(malformed, 4U, 1U);
        }
        S::EndianStackReader malformedReader(malformed, S::Endian::Little);
        std::uint32_t malformedAddress = 0U;
        EXPECT_THROW(ReadPolyChunk(malformedReader, malformedAddress), std::runtime_error);
    }

    for (const std::uint16_t signedCount : {std::uint16_t(0x8000U), std::uint16_t(2U)}) {
        std::vector<std::byte> data(8U);
        WriteHeader(data, 0U, PolyChunkType::Volume_Strip);
        WriteU16(data, 2U, 2U);
        WriteU16(data, 4U, 1U);
        WriteU16(data, 6U, signedCount);
        S::EndianStackReader reader(data, S::Endian::Little);
        std::uint32_t address = 0;
        EXPECT_THROW(ReadPolyChunk(reader, address), std::runtime_error);
    }

    std::vector<std::byte> mismatch(14U);
    WriteHeader(mismatch, 0U, PolyChunkType::Volume_Polygon3);
    WriteU16(mismatch, 2U, 5U);
    WriteU16(mismatch, 4U, 1U);
    WriteU16(mismatch, 6U, 0U);
    WriteU16(mismatch, 8U, 1U);
    WriteU16(mismatch, 10U, 2U);
    WriteU16(mismatch, 12U, 0xFFFFU);
    S::EndianStackReader reader(mismatch, S::Endian::Little);
    std::uint32_t address = 0;
    EXPECT_THROW(ReadPolyChunk(reader, address), std::runtime_error);

    std::vector<std::byte> excessivePadding(16U);
    WriteHeader(excessivePadding, 0U, PolyChunkType::Volume_Polygon3);
    WriteU16(excessivePadding, 2U, 6U);
    WriteU16(excessivePadding, 4U, 1U);
    WriteU16(excessivePadding, 6U, 0U);
    WriteU16(excessivePadding, 8U, 1U);
    WriteU16(excessivePadding, 10U, 2U);
    S::EndianStackReader excessivePaddingReader(excessivePadding, S::Endian::Little);
    address = 0U;
    EXPECT_THROW(ReadPolyChunk(excessivePaddingReader, address), std::runtime_error);
}

} // namespace
