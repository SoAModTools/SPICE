#pragma once

#include "Animation/Motion.h"
#include "File/FileHeaders.h"
#include "File/NJBlockUtility.h"
#include "Structs/EndianStackReader.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

namespace Sa3Dport::File {

struct AnimationProbeResult {
    bool valid = false;
    bool short_rot = false;
    std::uint32_t node_count = 0;
    std::uint32_t declared_frame_count = 0;
    Animation::KeyframeAttributes keyframe_attributes = Animation::KeyframeAttributes::None;
    std::uint32_t consumed_end = 0;
    std::string failure_reason{};
};

class AnimationFile {
public:
    Animation::Motion animation;
    std::optional<std::uint32_t> animation_block_address;

    [[nodiscard]] static bool check_is_animation_file(std::span<const std::byte> data, std::uint32_t address = 0) {
        return check_is_nj_animation_file(data, address);
    }

    [[nodiscard]] static bool check_is_nj_animation_file(std::span<const std::byte> data, std::uint32_t address = 0) {
        const auto blocks = NJBlockUtility::GetBlockAddresses(data, address);
        return NJBlockUtility::FindBlockAddress(blocks, FileHeaders::AnimationBlockHeaders).has_value();
    }

    [[nodiscard]] static AnimationFile read_from_bytes(std::span<const std::byte> data,
                                                       std::uint32_t nodeCount,
                                                       bool shortRot = false,
                                                       std::uint32_t address = 0) {
        return read(data, nodeCount, shortRot, address);
    }

    [[nodiscard]] static AnimationProbeResult probe_from_bytes(std::span<const std::byte> data,
                                                               std::uint32_t nodeCount,
                                                               bool shortRot = false,
                                                               std::uint32_t address = 0) {
        AnimationProbeResult result{};
        result.node_count = nodeCount;
        result.short_rot = shortRot;

        const auto payload = NJBlockUtility::TryGetBlockPayload(
            data, address, FileHeaders::AnimationBlockHeaders);
        if (!payload.has_value()) {
            result.failure_reason = "NJ animation block not found";
            return result;
        }

        const auto& reader = payload->reader;
        const auto motionAddress = payload->data_address;
        const auto blockEnd64 = static_cast<std::uint64_t>(payload->block.offset) +
            8ULL + static_cast<std::uint64_t>(payload->block.size);
        if (blockEnd64 > data.size()) {
            result.failure_reason = "animation block size overruns buffer";
            return result;
        }
        const auto blockEnd = static_cast<std::uint32_t>(blockEnd64);
        if (motionAddress + Animation::Motion::StructSize > blockEnd) {
            result.failure_reason = "motion header overruns animation block";
            return result;
        }

        result.declared_frame_count = reader.read_u32(motionAddress + 4U);
        result.keyframe_attributes = static_cast<Animation::KeyframeAttributes>(reader.read_u16(motionAddress + 8U));
        const auto encodedChannelBits = static_cast<std::uint16_t>(reader.read_u16(motionAddress + 10U) & 0xFU);
        const auto channels = Animation::channel_count(result.keyframe_attributes);
        if (encodedChannelBits != 0U && encodedChannelBits != static_cast<std::uint16_t>(channels)) {
            result.failure_reason = "motion channel count does not match keyframe attributes";
            return result;
        }
        if (channels == 0) {
            result.valid = true;
            result.consumed_end = motionAddress + Animation::Motion::StructSize;
            return result;
        }

        const auto keyframeAddress = Animation::read_motion_pointer(
            reader, motionAddress, payload->image_base);
        if (!keyframeAddress.has_value()) {
            result.failure_reason = "motion keyframe table pointer is null";
            return result;
        }

        std::uint32_t keyframeTable = *keyframeAddress;
        std::uint32_t consumedEnd = keyframeTable;
        for (std::uint32_t node = 0; node < nodeCount; ++node) {
            const auto tableBytes = static_cast<std::uint32_t>(channels * 8);
            if (keyframeTable > blockEnd || keyframeTable + tableBytes > blockEnd) {
                result.failure_reason = "node keyframe table overruns animation block";
                return result;
            }

            std::uint32_t keyframePointerArray = keyframeTable;
            std::uint32_t keyframeCountArray = keyframeTable + static_cast<std::uint32_t>(4 * channels);
            for (const Animation::KeyframeAttributes flag : Animation::kKeyframeAttributeOrder) {
                if (!Animation::has_flag(result.keyframe_attributes, flag)) {
                    continue;
                }

                const auto setAddress = Animation::read_motion_pointer(
                    reader, keyframePointerArray, payload->image_base);
                const auto frameCount = reader.read_u32(keyframeCountArray);
                if (setAddress.has_value() && frameCount > 0U) {
                    const auto entrySize = probe_entry_size(flag, shortRot);
                    const auto setEnd64 = static_cast<std::uint64_t>(*setAddress) +
                        static_cast<std::uint64_t>(entrySize) * static_cast<std::uint64_t>(frameCount);
                    if (entrySize == 0U || *setAddress > blockEnd || setEnd64 > blockEnd) {
                        result.failure_reason = "keyframe set overruns animation block";
                        return result;
                    }
                    consumedEnd = std::max(consumedEnd, static_cast<std::uint32_t>(setEnd64));
                }

                keyframePointerArray += 4U;
                keyframeCountArray += 4U;
            }

            keyframeTable += tableBytes;
            consumedEnd = std::max(consumedEnd, keyframeTable);
        }

        result.valid = true;
        result.consumed_end = consumedEnd;
        return result;
    }

    [[nodiscard]] static AnimationFile read(std::span<const std::byte> data,
                                            std::uint32_t nodeCount,
                                            bool shortRot = false,
                                            std::uint32_t address = 0) {
        if (!check_is_nj_animation_file(data, address)) {
            throw std::runtime_error("File is not an animation file");
        }
        return read_nj(data, nodeCount, shortRot, address);
    }

    [[nodiscard]] static AnimationFile read_nj(std::span<const std::byte> data,
                                               std::uint32_t nodeCount,
                                               bool shortRot = false,
                                               std::uint32_t address = 0) {
        const auto payload = NJBlockUtility::RequireBlockPayload(
            data, address, FileHeaders::AnimationBlockHeaders, "NJ animation block not found");

        AnimationFile result;
        result.animation_block_address = payload.block.offset;
        result.animation = Animation::Motion::read(
            payload.reader, payload.data_address, nodeCount, payload.image_base, shortRot);
        return result;
    }

private:
    [[nodiscard]] static constexpr std::uint32_t probe_entry_size(
        Animation::KeyframeAttributes type,
        bool shortRot) {
        if (type == Animation::KeyframeAttributes::EulerRotation && shortRot) {
            return 8U;
        }
        return Animation::keyframe_entry_size(type);
    }
};

} // namespace Sa3Dport::File
