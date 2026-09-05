#pragma once

#include "Structs/Color.h"
#include "Structs/Vector3.h"

#include <cstdint>

namespace spice::modeling::Mesh::Chunk::Structs {

struct ChunkVertex {
    spice::modeling::Structs::Vector3 position {};
    spice::modeling::Structs::Vector3 normal = spice::modeling::Structs::Vector3::unit_y();
    spice::modeling::Structs::Color diffuse = spice::modeling::Structs::Color(0xFF, 0xFF, 0xFF, 0xFF);
    spice::modeling::Structs::Color specular = spice::modeling::Structs::Color(0xFF, 0xFF, 0xFF, 0xFF);
    std::uint32_t attributes = 0;

    [[nodiscard]] std::uint16_t index() const {
        return static_cast<std::uint16_t>(attributes & 0xFFFFu);
    }

    void set_index(std::uint16_t value) {
        attributes = (attributes & ~0xFFFFu) | value;
    }

    [[nodiscard]] float weight() const {
        return static_cast<float>(attributes >> 16) / 255.0f;
    }

    [[nodiscard]] bool operator==(const ChunkVertex&) const = default;
};

} // namespace spice::modeling::Mesh::Chunk::Structs
