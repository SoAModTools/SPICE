#pragma once

#include "Mesh/Chunk/PolyChunk.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace spice::modeling::Mesh::Chunk::PolyChunks {

struct VolumePolygonRecord {
    std::vector<std::uint16_t> indices{};
    std::vector<std::uint16_t> user_words{};
};

struct VolumeStripRecord {
    std::int16_t signed_index_count = 0;
    std::vector<std::uint16_t> indices{};
    std::vector<std::vector<std::uint16_t>> triangle_user_words{};

    [[nodiscard]] bool reversed() const { return signed_index_count < 0; }

    [[nodiscard]] std::vector<std::array<std::uint16_t, 3>> canonical_triangles() const {
        std::vector<std::array<std::uint16_t, 3>> result{};
        if (indices.size() < 3U) {
            return result;
        }
        result.reserve(indices.size() - 2U);
        for (std::size_t triangle = 0; triangle + 2U < indices.size(); ++triangle) {
            std::array<std::uint16_t, 3> value{
                indices[triangle], indices[triangle + 1U], indices[triangle + 2U]};
            if (((triangle & 1U) != 0U) != reversed()) {
                std::swap(value[0], value[1]);
            }
            result.push_back(value);
        }
        return result;
    }
};

class VolumeChunk : public SizedPolyChunk {
public:
    std::uint32_t source_address = 0;
    std::uint16_t count_and_user = 0;
    std::uint16_t polygon_count = 0;
    std::uint8_t polygon_attribute_count = 0;
    std::uint16_t declared_size_words = 0;
    std::vector<VolumePolygonRecord> polygons{};
    std::vector<VolumeStripRecord> strips{};
    std::vector<std::uint16_t> trailing_padding_words{};

    explicit VolumeChunk(PolyChunkType chunkType = PolyChunkType::Volume_Polygon3)
        : SizedPolyChunk(chunkType) {}

    [[nodiscard]] std::uint16_t size() const override {
        return declared_size_words;
    }

    [[nodiscard]] static VolumeChunk read(const spice::modeling::Structs::EndianStackReader& reader,
                                          std::uint32_t& address) {
        const std::uint32_t start = address;
        auto fail = [start](PolyChunkType type, const std::string& reason) -> void {
            throw std::runtime_error(
                "volume chunk at offset " + std::to_string(start) +
                " type " + std::to_string(static_cast<unsigned>(type)) + ": " + reason);
        };

        std::uint16_t header = 0;
        std::uint16_t sizeWords = 0;
        std::uint16_t header2 = 0;
        try {
            header = reader.read_u16(start);
            sizeWords = reader.read_u16(start + 2U);
            header2 = reader.read_u16(start + 4U);
        } catch (const std::exception&) {
            fail(PolyChunkType::Null, "truncated header");
        }

        const auto type = static_cast<PolyChunkType>(header & 0xFFu);
        if (type != PolyChunkType::Volume_Polygon3 &&
            type != PolyChunkType::Volume_Polygon4 &&
            type != PolyChunkType::Volume_Strip) {
            fail(type, "invalid volume chunk type");
        }
        if (sizeWords < 1U) {
            fail(type, "declared size does not include count word");
        }

        const std::uint64_t declaredEnd64 = static_cast<std::uint64_t>(start) + 4ULL +
            static_cast<std::uint64_t>(sizeWords) * 2ULL;
        if (declaredEnd64 > reader.size() || declaredEnd64 > std::numeric_limits<std::uint32_t>::max()) {
            fail(type, "declared size overruns input");
        }
        const auto declaredEnd = static_cast<std::uint32_t>(declaredEnd64);

        VolumeChunk result(type);
        result.source_address = start;
        result.attributes = static_cast<std::uint8_t>(header >> 8U);
        result.count_and_user = header2;
        result.polygon_count = static_cast<std::uint16_t>(header2 & 0x3FFFu);
        result.polygon_attribute_count = static_cast<std::uint8_t>(header2 >> 14U);
        result.declared_size_words = sizeWords;

        std::uint32_t cursor = start + 6U;
        auto require = [&](std::uint64_t byteCount, const char* what) {
            if (static_cast<std::uint64_t>(cursor) + byteCount > declaredEnd) {
                fail(type, std::string(what) + " overruns declared size");
            }
        };
        auto readU16 = [&]() {
            require(2U, "record");
            const auto value = reader.read_u16(cursor);
            cursor += 2U;
            return value;
        };

        if (type == PolyChunkType::Volume_Polygon3 || type == PolyChunkType::Volume_Polygon4) {
            const std::size_t indexCount = type == PolyChunkType::Volume_Polygon3 ? 3U : 4U;
            result.polygons.reserve(result.polygon_count);
            for (std::uint16_t polygon = 0; polygon < result.polygon_count; ++polygon) {
                VolumePolygonRecord record{};
                record.indices.reserve(indexCount);
                record.user_words.reserve(result.polygon_attribute_count);
                for (std::size_t index = 0; index < indexCount; ++index) {
                    record.indices.push_back(readU16());
                }
                for (std::uint8_t user = 0; user < result.polygon_attribute_count; ++user) {
                    record.user_words.push_back(readU16());
                }
                result.polygons.push_back(std::move(record));
            }
        } else {
            result.strips.reserve(result.polygon_count);
            for (std::uint16_t stripIndex = 0; stripIndex < result.polygon_count; ++stripIndex) {
                require(2U, "strip count");
                const auto signedCount = reader.read_i16(cursor);
                cursor += 2U;
                if (signedCount == std::numeric_limits<std::int16_t>::min()) {
                    fail(type, "strip count cannot be INT16_MIN");
                }
                const auto indexCount = static_cast<std::uint32_t>(signedCount < 0 ? -signedCount : signedCount);
                if (indexCount < 3U) {
                    fail(type, "strip contains fewer than three indices");
                }

                VolumeStripRecord record{};
                record.signed_index_count = signedCount;
                record.indices.reserve(indexCount);
                for (std::uint32_t index = 0; index < 2U; ++index) {
                    record.indices.push_back(readU16());
                }
                record.triangle_user_words.reserve(indexCount - 2U);
                for (std::uint32_t index = 2U; index < indexCount; ++index) {
                    record.indices.push_back(readU16());
                    std::vector<std::uint16_t> words{};
                    words.reserve(result.polygon_attribute_count);
                    for (std::uint8_t user = 0; user < result.polygon_attribute_count; ++user) {
                        words.push_back(readU16());
                    }
                    record.triangle_user_words.push_back(std::move(words));
                }
                result.strips.push_back(std::move(record));
            }
        }

        if (cursor < declaredEnd) {
            if (type != PolyChunkType::Volume_Polygon3 || declaredEnd - cursor != 2U) {
                fail(type, "computed record end does not match the declared size");
            }
            const auto padding = readU16();
            if (padding != 0U) {
                fail(type, "nonzero data remains after the declared records");
            }
            result.trailing_padding_words.push_back(padding);
        }
        address = declaredEnd;
        return result;
    }
};

} // namespace spice::modeling::Mesh::Chunk::PolyChunks
