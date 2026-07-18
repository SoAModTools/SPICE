#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace spice::mld::model {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct ColorRgba8 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;
};

struct SpotlightValue {
    float nearDistance = 0.0f;
    float farDistance = 0.0f;
    float insideAngle = 0.0f;
    float outsideAngle = 0.0f;
};

struct Transform {
    Vec3 position{};
    Vec3 rotationRaw{};
    Quat rotation{};
    Vec3 scale{ 1.0f, 1.0f, 1.0f };
};

struct MeshVertex {
    Vec3 position{};
    Vec3 normal{};
    bool hasNormal = true;
    std::optional<std::uint32_t> rawUserAttributesU32{};
    float u = 0.0f;
    float v = 0.0f;
};

struct TriangleMetadata {
    std::array<std::uint16_t, 3> rawU16{};
};

struct MeshData {
    std::vector<MeshVertex> vertices{};
    std::vector<std::uint32_t> indices{};
    std::vector<TriangleMetadata> triangleMetadata{};
};

} // namespace spice::mld::model
