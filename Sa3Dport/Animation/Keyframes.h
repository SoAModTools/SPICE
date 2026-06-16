#pragma once

#include "Animation/Enums.h"
#include "Structs/BAMSFHelper.h"
#include "Structs/Color.h"
#include "Structs/ColorIOType.h"
#include "Structs/EndianIOExtensions.h"
#include "Structs/EndianStackReader.h"
#include "Structs/FloatIOType.h"
#include "Structs/LabeledArray.h"
#include "Structs/MathHelper.h"
#include "Structs/Quaternion.h"
#include "Structs/Vector2.h"
#include "Structs/Vector3.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

namespace Sa3Dport::Animation {

struct Spotlight {
    static constexpr std::uint32_t StructSize = 16;

    float near_distance = 0.0f;
    float far_distance = 0.0f;
    float inside_angle = 0.0f;
    float outside_angle = 0.0f;

    [[nodiscard]] static Spotlight read(const Structs::EndianStackReader& reader, std::uint32_t address) {
        return Spotlight{
            .near_distance = reader.read_float(address),
            .far_distance = reader.read_float(address + 4U),
            .inside_angle = Structs::MathHelper::bams_to_rad(reader.read_i32(address + 8U)),
            .outside_angle = Structs::MathHelper::bams_to_rad(reader.read_i32(address + 12U)),
        };
    }
};

struct Frame {
    float frame_time = 0.0f;
    std::optional<Structs::Vector3> position{};
    std::optional<Structs::Vector3> euler_rotation{};
    std::optional<Structs::Vector3> scale{};
    std::optional<Structs::Vector3> vector{};
    std::optional<Structs::LabeledArray<Structs::Vector3>> vertex{};
    std::optional<Structs::LabeledArray<Structs::Vector3>> normal{};
    std::optional<Structs::Vector3> target{};
    std::optional<float> roll{};
    std::optional<float> angle{};
    std::optional<Structs::Color> light_color{};
    std::optional<float> intensity{};
    std::optional<Spotlight> spot{};
    std::optional<Structs::Vector2> point{};
    std::optional<Structs::Quaternion> quaternion_rotation{};
};

struct KeyframeChannelSummary {
    KeyframeAttributes type = KeyframeAttributes::None;
    std::uint32_t pointer = 0;
    std::uint32_t count = 0;
    std::uint32_t first_frame = 0;
    std::uint32_t last_frame = 0;
};

struct Keyframes {
    KeyframeAttributes type = KeyframeAttributes::None;
    std::uint32_t keyframe_count = 0;
    std::vector<KeyframeChannelSummary> channels;
    std::map<std::uint32_t, Structs::Vector3> position{};
    std::map<std::uint32_t, Structs::Vector3> euler_rotation{};
    std::map<std::uint32_t, Structs::Vector3> scale{};
    std::map<std::uint32_t, Structs::Vector3> vector{};
    std::map<std::uint32_t, Structs::LabeledArray<Structs::Vector3>> vertex{};
    std::map<std::uint32_t, Structs::LabeledArray<Structs::Vector3>> normal{};
    std::map<std::uint32_t, Structs::Vector3> target{};
    std::map<std::uint32_t, float> roll{};
    std::map<std::uint32_t, float> angle{};
    std::map<std::uint32_t, Structs::Color> light_color{};
    std::map<std::uint32_t, float> intensity{};
    std::map<std::uint32_t, Spotlight> spot{};
    std::map<std::uint32_t, Structs::Vector2> point{};
    std::map<std::uint32_t, Structs::Quaternion> quaternion_rotation{};

    [[nodiscard]] bool has_keyframes() const {
        return type != KeyframeAttributes::None;
    }

    [[nodiscard]] Frame get_frame_at(float frame) const;
};

[[nodiscard]] inline constexpr std::uint32_t keyframe_entry_size(KeyframeAttributes type) {
    switch (type) {
    case KeyframeAttributes::Position:
    case KeyframeAttributes::Scale:
    case KeyframeAttributes::Vector:
    case KeyframeAttributes::Target:
        return 16;
    case KeyframeAttributes::EulerRotation:
        return 16;
    case KeyframeAttributes::Vertex:
    case KeyframeAttributes::Normal:
    case KeyframeAttributes::Roll:
    case KeyframeAttributes::Angle:
    case KeyframeAttributes::LightColor:
    case KeyframeAttributes::Intensity:
        return 8;
    case KeyframeAttributes::Spot:
        return 24;
    case KeyframeAttributes::Point:
        return 12;
    case KeyframeAttributes::QuaternionRotation:
        return 20;
    case KeyframeAttributes::None:
        return 0;
    }
    return 0;
}

[[nodiscard]] inline std::optional<std::uint32_t> read_motion_pointer(
    const Structs::EndianStackReader& reader,
    std::uint32_t address,
    std::uint32_t imageBase) {
    const std::uint32_t raw = reader.read_u32(address);
    if (raw == 0) {
        return std::nullopt;
    }
    return raw - imageBase;
}

[[nodiscard]] inline std::uint32_t read_frame_at(
    const Structs::EndianStackReader& reader,
    std::uint32_t address,
    KeyframeAttributes type,
    bool shortRot) {
    if (type == KeyframeAttributes::EulerRotation && shortRot) {
        return reader.read_u16(address);
    }
    return reader.read_u32(address);
}

template <class T>
inline void update_summary(Keyframes& result,
                           KeyframeAttributes type,
                           std::uint32_t pointer,
                           const std::map<std::uint32_t, T>& values) {
    if (values.empty()) {
        return;
    }

    result.type |= type;
    result.keyframe_count = std::max(result.keyframe_count, values.rbegin()->first + 1U);
    result.channels.push_back(KeyframeChannelSummary{
        .type = type,
        .pointer = pointer,
        .count = static_cast<std::uint32_t>(values.size()),
        .first_frame = values.begin()->first,
        .last_frame = values.rbegin()->first,
    });
}

[[nodiscard]] inline std::map<std::uint32_t, Structs::Vector3> read_vector3_set(
    const Structs::EndianStackReader& reader,
    std::uint32_t address,
    std::uint32_t count,
    Structs::FloatIOType type) {
    std::map<std::uint32_t, Structs::Vector3> result;
    for (std::uint32_t i = 0; i < count; ++i) {
        const bool shortFrame = type == Structs::FloatIOType::BAMS16 ||
            type == Structs::FloatIOType::BAMS16F ||
            type == Structs::FloatIOType::Short;
        const std::uint32_t frame = shortFrame ? reader.read_u16(address) : reader.read_u32(address);
        address += shortFrame ? 2U : 4U;
        result.emplace(frame, Structs::EndianIOExtensions::read_vector3(reader, static_cast<std::size_t>(address), type));
        address += static_cast<std::uint32_t>(3 * Structs::byte_size(type));
    }
    return result;
}

[[nodiscard]] inline std::map<std::uint32_t, Structs::LabeledArray<Structs::Vector3>> read_vector3_array_set(
    const Structs::EndianStackReader& reader,
    std::uint32_t address,
    std::uint32_t count,
    std::string_view labelPrefix,
    std::uint32_t imageBase,
    Structs::BaseLUT& lut) {
    std::map<std::uint32_t, Structs::LabeledArray<Structs::Vector3>> result;
    using Structs::FloatIOType;
    if (count == 0) {
        return result;
    }

    const std::uint32_t keyframeTableAddress = address;
    std::map<std::uint32_t, std::uint32_t> frameAddresses;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t frame = reader.read_u32(address);
        const auto vectorAddress = read_motion_pointer(reader, address + 4U, imageBase);
        if (vectorAddress.has_value()) {
            frameAddresses.emplace(frame, *vectorAddress);
        }
        address += 8U;
    }

    std::vector<std::uint32_t> addresses;
    addresses.reserve(frameAddresses.size());
    for (const auto& [_, vectorAddress] : frameAddresses) {
        addresses.push_back(vectorAddress);
    }
    std::sort(addresses.begin(), addresses.end());
    addresses.erase(std::unique(addresses.begin(), addresses.end()), addresses.end());

    if (addresses.empty()) {
        return result;
    }

    std::uint32_t smallestSize = (keyframeTableAddress - addresses.back()) / 12U;
    for (std::size_t i = 1; i < addresses.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            smallestSize = std::min(smallestSize, (addresses[i] - addresses[j]) / 12U);
        }
    }

    for (const auto& [frame, vectorAddress] : frameAddresses) {
        result.emplace(frame, lut.get_add_labeled_value<Structs::Vector3>(vectorAddress, labelPrefix, [&]() {
            std::vector<Structs::Vector3> values;
            values.reserve(smallestSize);
            std::uint32_t vectorRead = vectorAddress;
            for (std::uint32_t i = 0; i < smallestSize; ++i) {
                values.push_back(Structs::EndianIOExtensions::read_vector3(reader, vectorRead, FloatIOType::Float));
            }
            return values;
        }));
    }

    return result;
}

[[nodiscard]] inline std::map<std::uint32_t, Structs::Vector2> read_vector2_set(
    const Structs::EndianStackReader& reader,
    std::uint32_t address,
    std::uint32_t count,
    Structs::FloatIOType type) {
    std::map<std::uint32_t, Structs::Vector2> result;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t frame = reader.read_u32(address);
        address += 4U;
        result.emplace(frame, Structs::EndianIOExtensions::read_vector2(reader, static_cast<std::size_t>(address), type));
        address += static_cast<std::uint32_t>(2 * Structs::byte_size(type));
    }
    return result;
}

[[nodiscard]] inline std::map<std::uint32_t, Structs::Color> read_color_set(
    const Structs::EndianStackReader& reader,
    std::uint32_t address,
    std::uint32_t count,
    Structs::ColorIOType type) {
    std::map<std::uint32_t, Structs::Color> result;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t frame = reader.read_u32(address);
        address += 4U;
        result.emplace(frame, Structs::EndianIOExtensions::read_color(reader, static_cast<std::size_t>(address), type));
        address += static_cast<std::uint32_t>(Structs::byte_size(type));
    }
    return result;
}

[[nodiscard]] inline std::map<std::uint32_t, float> read_float_set(
    const Structs::EndianStackReader& reader,
    std::uint32_t address,
    std::uint32_t count,
    bool bams) {
    std::map<std::uint32_t, float> result;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t frame = reader.read_u32(address);
        const float value = bams
            ? Structs::BAMSFHelper::BAMSFToRad(reader.read_i32(address + 4U))
            : reader.read_float(address + 4U);
        result.emplace(frame, value);
        address += 8U;
    }
    return result;
}

[[nodiscard]] inline std::map<std::uint32_t, Spotlight> read_spot_set(
    const Structs::EndianStackReader& reader,
    std::uint32_t address,
    std::uint32_t count) {
    std::map<std::uint32_t, Spotlight> result;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t frame = reader.read_u32(address);
        result.emplace(frame, Spotlight::read(reader, address + 4U));
        address += 8U + Spotlight::StructSize;
    }
    return result;
}

[[nodiscard]] inline std::map<std::uint32_t, Structs::Quaternion> read_quaternion_set(
    const Structs::EndianStackReader& reader,
    std::uint32_t address,
    std::uint32_t count) {
    std::map<std::uint32_t, Structs::Quaternion> result;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t frame = reader.read_u32(address);
        address += 4U;
        result.emplace(frame, Structs::EndianIOExtensions::read_quaternion(reader, static_cast<std::size_t>(address)));
        address += 16U;
    }
    return result;
}

inline void read_channel_values(Keyframes& result,
                                const Structs::EndianStackReader& reader,
                                std::uint32_t setAddress,
                                std::uint32_t frameCount,
                                KeyframeAttributes flag,
                                std::uint32_t imageBase,
                                bool shortRot,
                                Structs::BaseLUT& lut) {
    using Structs::FloatIOType;
    switch (flag) {
    case KeyframeAttributes::Position:
        result.position = read_vector3_set(reader, setAddress, frameCount, FloatIOType::Float);
        update_summary(result, flag, setAddress, result.position);
        break;
    case KeyframeAttributes::EulerRotation:
        result.euler_rotation = read_vector3_set(reader, setAddress, frameCount, shortRot ? FloatIOType::BAMS16F : FloatIOType::BAMS32F);
        update_summary(result, flag, setAddress, result.euler_rotation);
        break;
    case KeyframeAttributes::Scale:
        result.scale = read_vector3_set(reader, setAddress, frameCount, FloatIOType::Float);
        update_summary(result, flag, setAddress, result.scale);
        break;
    case KeyframeAttributes::Vector:
        result.vector = read_vector3_set(reader, setAddress, frameCount, FloatIOType::Float);
        update_summary(result, flag, setAddress, result.vector);
        break;
    case KeyframeAttributes::Vertex:
        result.vertex = read_vector3_array_set(reader, setAddress, frameCount, "vertex_", imageBase, lut);
        update_summary(result, flag, setAddress, result.vertex);
        break;
    case KeyframeAttributes::Normal:
        result.normal = read_vector3_array_set(reader, setAddress, frameCount, "normal_", imageBase, lut);
        update_summary(result, flag, setAddress, result.normal);
        break;
    case KeyframeAttributes::Target:
        result.target = read_vector3_set(reader, setAddress, frameCount, FloatIOType::Float);
        update_summary(result, flag, setAddress, result.target);
        break;
    case KeyframeAttributes::Roll:
        result.roll = read_float_set(reader, setAddress, frameCount, true);
        update_summary(result, flag, setAddress, result.roll);
        break;
    case KeyframeAttributes::Angle:
        result.angle = read_float_set(reader, setAddress, frameCount, true);
        update_summary(result, flag, setAddress, result.angle);
        break;
    case KeyframeAttributes::LightColor:
        result.light_color = read_color_set(reader, setAddress, frameCount, Structs::ColorIOType::ARGB8_32);
        update_summary(result, flag, setAddress, result.light_color);
        break;
    case KeyframeAttributes::Intensity:
        result.intensity = read_float_set(reader, setAddress, frameCount, false);
        update_summary(result, flag, setAddress, result.intensity);
        break;
    case KeyframeAttributes::Spot:
        result.spot = read_spot_set(reader, setAddress, frameCount);
        update_summary(result, flag, setAddress, result.spot);
        break;
    case KeyframeAttributes::Point:
        result.point = read_vector2_set(reader, setAddress, frameCount, FloatIOType::Float);
        update_summary(result, flag, setAddress, result.point);
        break;
    case KeyframeAttributes::QuaternionRotation:
        result.quaternion_rotation = read_quaternion_set(reader, setAddress, frameCount);
        update_summary(result, flag, setAddress, result.quaternion_rotation);
        break;
    case KeyframeAttributes::None:
        break;
    }
}

[[nodiscard]] inline Keyframes read_keyframes(
    const Structs::EndianStackReader& reader,
    std::uint32_t& address,
    KeyframeAttributes type,
    std::uint32_t imageBase,
    bool shortRot,
    Structs::BaseLUT& lut) {
    const int channels = channel_count(type);
    std::uint32_t keyframePointerArray = address;
    std::uint32_t keyframeCountArray = address + static_cast<std::uint32_t>(4 * channels);

    Keyframes result;
    for (const KeyframeAttributes flag : kKeyframeAttributeOrder) {
        if (!has_flag(type, flag)) {
            continue;
        }

        const auto setAddress = read_motion_pointer(reader, keyframePointerArray, imageBase);
        if (setAddress.has_value()) {
            const std::uint32_t frameCount = reader.read_u32(keyframeCountArray);
            if (frameCount > 0) {
                read_channel_values(result, reader, *setAddress, frameCount, flag, imageBase, shortRot, lut);
            }
        }

        keyframePointerArray += 4;
        keyframeCountArray += 4;
    }

    address = keyframeCountArray;
    return result;
}

[[nodiscard]] inline Keyframes read_keyframes(
    const Structs::EndianStackReader& reader,
    std::uint32_t& address,
    KeyframeAttributes type,
    std::uint32_t imageBase,
    bool shortRot = false) {
    Structs::BaseLUT lut;
    return read_keyframes(reader, address, type, imageBase, shortRot, lut);
}

[[nodiscard]] inline Frame Keyframes::get_frame_at(float frame) const {
    auto getExact = [](const auto& values, std::uint32_t frameIndex) -> std::optional<typename std::decay_t<decltype(values)>::mapped_type> {
        const auto found = values.find(frameIndex);
        if (found == values.end()) {
            return std::nullopt;
        }
        return found->second;
    };

    const auto frameIndex = static_cast<std::uint32_t>(frame);
    Frame result{};
    result.frame_time = frame;
    result.position = getExact(position, frameIndex);
    result.euler_rotation = getExact(euler_rotation, frameIndex);
    result.scale = getExact(scale, frameIndex);
    result.vector = getExact(vector, frameIndex);
    result.vertex = getExact(vertex, frameIndex);
    result.normal = getExact(normal, frameIndex);
    result.target = getExact(target, frameIndex);
    result.roll = getExact(roll, frameIndex);
    result.angle = getExact(angle, frameIndex);
    result.light_color = getExact(light_color, frameIndex);
    result.intensity = getExact(intensity, frameIndex);
    result.spot = getExact(spot, frameIndex);
    result.point = getExact(point, frameIndex);
    result.quaternion_rotation = getExact(quaternion_rotation, frameIndex);
    return result;
}

} // namespace Sa3Dport::Animation
