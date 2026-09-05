#pragma once

#include "Structs/Color.h"
#include "Structs/Vector2.h"
#include "Structs/Vector3.h"

#include <cstdint>

namespace spice::modeling::Mesh::Chunk::Structs {

struct ChunkCorner {
    std::uint16_t index = 0;
    spice::modeling::Structs::Vector2 texcoord {};
    spice::modeling::Structs::Vector2 texcoord2 {};
    spice::modeling::Structs::Vector3 normal = spice::modeling::Structs::Vector3::unit_y();
    spice::modeling::Structs::Color color = spice::modeling::Structs::Color(0xFF, 0xFF, 0xFF, 0xFF);
    std::uint16_t attributes1 = 0;
    std::uint16_t attributes2 = 0;
    std::uint16_t attributes3 = 0;

    [[nodiscard]] bool operator==(const ChunkCorner&) const = default;
};

} // namespace spice::modeling::Mesh::Chunk::Structs
