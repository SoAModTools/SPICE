#pragma once

#include "Animation/Motion.h"
#include "File/FileHeaders.h"
#include "File/NinjaMotionBlock.h"
#include "File/NJBlockUtility.h"
#include "Structs/EndianStackReader.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace spice::modeling::File {

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

    [[nodiscard]] static bool check_is_animation_file(std::span<const std::byte> data,
                                                       std::uint32_t address = 0) {
        return check_is_nj_animation_file(data, address);
    }

    [[nodiscard]] static bool check_is_nj_animation_file(std::span<const std::byte> data,
                                                          std::uint32_t address = 0) {
        const auto blocks = NJBlockUtility::GetBlockAddresses(data, address);
        return NJBlockUtility::FindBlockAddress(blocks, FileHeaders::AnimationBlockHeaders).has_value();
    }

    [[nodiscard]] static NinjaMotionBlock parse_structure(std::span<const std::byte> data,
                                                           std::uint32_t address = 0) {
        NinjaMotionBlock result{};
        auto payload = NJBlockUtility::TryGetBlockPayload(
            data, address, FileHeaders::AnimationBlockHeaders);
        if (!payload.has_value()) {
            auto scan = NJBlockUtility::ScanBlocks(data, address);
            const auto block = std::find_if(scan.blocks.begin(), scan.blocks.end(), [&](const auto& item) {
                return item.offset == address;
            });
            if (block != scan.blocks.end()) {
                payload.emplace(std::move(scan), *block, data);
            }
        }
        if (!payload.has_value()) {
            result.diagnostics.push_back({true, "NJ animation block not found", address});
            return result;
        }

        const auto blockEnd64 = static_cast<std::uint64_t>(payload->block.offset) +
            8ULL + static_cast<std::uint64_t>(payload->block.size);
        if (blockEnd64 > data.size() || blockEnd64 > std::numeric_limits<std::uint32_t>::max()) {
            result.diagnostics.push_back({true, "animation block size overruns buffer", payload->block.offset});
            return result;
        }
        const auto blockEnd = static_cast<std::uint32_t>(blockEnd64);
        result.block_range = {
            payload->block.offset,
            static_cast<std::uint32_t>(8U + payload->block.size),
        };
        result.chunk_header_range = {payload->block.offset, 8U};
        result.payload_range = {payload->data_address, payload->block.size};
        result.header.raw_tag = payload->block.header;
        result.header.kind = kind_for_tag(payload->block.header);
        result.header.payload_size = payload->block.size;
        if (payload->block.size < Animation::Motion::StructSize) {
            result.diagnostics.push_back({
                true, "motion payload is smaller than the 16-byte header", payload->data_address});
            return result;
        }
        result.motion_header_range = {payload->data_address, Animation::Motion::StructSize};

        try {
            result.header.raw_mdata_offset = payload->reader.read_u32(payload->data_address);
            result.header.frame_count = payload->reader.read_u32(payload->data_address + 4U);
            result.header.channel_mask = static_cast<Animation::KeyframeAttributes>(
                payload->reader.read_u16(payload->data_address + 8U));
            result.header.raw_style = payload->reader.read_u16(payload->data_address + 10U);
            result.header.reserved = payload->reader.read_u32(payload->data_address + 12U);
        } catch (const std::exception&) {
            result.diagnostics.push_back({true, "truncated motion header", payload->data_address});
            return result;
        }

        result.header.encoded_lane_count = static_cast<std::uint8_t>(result.header.raw_style & 0xFU);
        result.header.interpolation_mode = static_cast<Animation::InterpolationMode>(
            (result.header.raw_style >> 6U) & 0x3U);
        const auto channelCount = Animation::channel_count(result.header.channel_mask);
        if ((static_cast<std::uint16_t>(result.header.channel_mask) & 0xC000U) != 0U) {
            result.diagnostics.push_back({
                true, "motion channel mask contains unknown high bits", payload->data_address + 8U});
        }
        if (result.header.encoded_lane_count != static_cast<std::uint8_t>(channelCount)) {
            result.diagnostics.push_back({
                true, "style lane count does not match channel mask", payload->data_address + 10U});
        }

        if (result.header.raw_mdata_offset != 0U) {
            const auto resolved64 = static_cast<std::uint64_t>(payload->data_address) +
                result.header.raw_mdata_offset;
            if (resolved64 >= blockEnd) {
                result.diagnostics.push_back({
                    true, "mdata pointer is outside the motion payload", payload->data_address});
            } else {
                result.header.resolved_mdata_offset = static_cast<std::uint32_t>(resolved64);
            }
        } else if (channelCount != 0) {
            result.diagnostics.push_back({
                true, "motion with channels has a null mdata pointer", payload->data_address});
        }

        const auto pof = std::find_if(
            payload->scan.blocks.begin(), payload->scan.blocks.end(), [&](const auto& item) {
                return item.offset == blockEnd && item.header == FileHeaders::POF0;
            });
        if (pof == payload->scan.blocks.end()) {
            result.diagnostics.push_back({true, "motion block is not followed by POF0", blockEnd});
        } else {
            parse_pof0(data, payload->reader, payload->data_address, payload->block.size, *pof, result);
        }

        const bool hasError = std::any_of(
            result.diagnostics.begin(), result.diagnostics.end(), [](const auto& item) {
                return item.error;
            });
        result.status = hasError ? NinjaMotionParseStatus::Partial : NinjaMotionParseStatus::Complete;
        return result;
    }

    [[nodiscard]] static AnimationFile read_from_bytes(std::span<const std::byte> data,
                                                       std::uint32_t nodeCount,
                                                       bool shortRot = false,
                                                       std::uint32_t address = 0) {
        return read(data, nodeCount, shortRot, address);
    }

    [[nodiscard]] static AnimationFile read_from_bytes(
        std::span<const std::byte> data,
        const Animation::MotionTargetLayout& targetLayout,
        Animation::EulerRecordWidth eulerWidth = Animation::EulerRecordWidth::Full32,
        std::uint32_t address = 0) {
        return read(data, targetLayout, eulerWidth, address);
    }

    [[nodiscard]] static AnimationProbeResult probe_from_bytes(std::span<const std::byte> data,
                                                               std::uint32_t nodeCount,
                                                               bool shortRot = false,
                                                               std::uint32_t address = 0) {
        Animation::MotionTargetLayout layout{};
        layout.lanes.reserve(nodeCount);
        for (std::uint32_t node = 0; node < nodeCount; ++node) {
            layout.lanes.push_back(Animation::MotionTargetLane{.node_index = node});
        }
        return probe_from_bytes(
            data,
            layout,
            shortRot ? Animation::EulerRecordWidth::Short16 : Animation::EulerRecordWidth::Full32,
            address);
    }

    [[nodiscard]] static AnimationProbeResult probe_from_bytes(
        std::span<const std::byte> data,
        const Animation::MotionTargetLayout& targetLayout,
        Animation::EulerRecordWidth eulerWidth = Animation::EulerRecordWidth::Full32,
        std::uint32_t address = 0) {
        AnimationProbeResult result{};
        result.node_count = targetLayout.lane_count();
        result.short_rot = eulerWidth == Animation::EulerRecordWidth::Short16;

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
        if (blockEnd64 > data.size() || blockEnd64 > std::numeric_limits<std::uint32_t>::max()) {
            result.failure_reason = "animation block size overruns buffer";
            return result;
        }
        const auto blockEnd = static_cast<std::uint32_t>(blockEnd64);
        if (motionAddress + Animation::Motion::StructSize > blockEnd) {
            result.failure_reason = "motion header overruns animation block";
            return result;
        }

        result.declared_frame_count = reader.read_u32(motionAddress + 4U);
        result.keyframe_attributes = static_cast<Animation::KeyframeAttributes>(
            reader.read_u16(motionAddress + 8U));
        const auto encodedChannelBits = static_cast<std::uint16_t>(
            reader.read_u16(motionAddress + 10U) & 0xFU);
        const auto channels = Animation::channel_count(result.keyframe_attributes);
        if (encodedChannelBits != static_cast<std::uint16_t>(channels)) {
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
        for (const auto& lane : targetLayout.lanes) {
            const auto tableBytes = static_cast<std::uint32_t>(channels * 8);
            if (keyframeTable > blockEnd || tableBytes > blockEnd - keyframeTable) {
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
                if (!setAddress.has_value() && frameCount != 0U) {
                    result.failure_reason = "keyframe count has a null set pointer";
                    return result;
                }
                if (setAddress.has_value() && *setAddress >= blockEnd) {
                    result.failure_reason = "keyframe set pointer is outside the animation block";
                    return result;
                }
                if (setAddress.has_value() && frameCount > 0U) {
                    const auto entrySize = probe_entry_size(flag, result.short_rot);
                    const auto setEnd64 = static_cast<std::uint64_t>(*setAddress) +
                        static_cast<std::uint64_t>(entrySize) * frameCount;
                    if (entrySize == 0U || *setAddress > blockEnd || setEnd64 > blockEnd) {
                        result.failure_reason = "keyframe set overruns animation block";
                        return result;
                    }
                    consumedEnd = std::max(consumedEnd, static_cast<std::uint32_t>(setEnd64));

                    if (flag == Animation::KeyframeAttributes::Vertex ||
                        flag == Animation::KeyframeAttributes::Normal) {
                        const auto expectedCount = flag == Animation::KeyframeAttributes::Vertex
                            ? lane.vertex_count : lane.normal_count;
                        if (!expectedCount.has_value()) {
                            result.failure_reason = flag == Animation::KeyframeAttributes::Vertex
                                ? "target layout lacks vertex count"
                                : "target layout lacks normal count";
                            return result;
                        }
                        for (std::uint32_t key = 0; key < frameCount; ++key) {
                            const auto arrayPointer = Animation::read_motion_pointer(
                                reader, *setAddress + key * 8U + 4U, payload->image_base);
                            if (!arrayPointer.has_value()) {
                                if (*expectedCount != 0U) {
                                    result.failure_reason = "shape key has a null vector-array pointer";
                                    return result;
                                }
                                continue;
                            }
                            if (*expectedCount == 0U) {
                                result.failure_reason = "shape key targets an object lane with no vertices";
                                return result;
                            }
                            const auto arrayEnd64 = static_cast<std::uint64_t>(*arrayPointer) +
                                static_cast<std::uint64_t>(*expectedCount) * 12ULL;
                            if (*arrayPointer > blockEnd || arrayEnd64 > blockEnd) {
                                result.failure_reason = "shape vector array overruns animation block";
                                return result;
                            }
                            consumedEnd = std::max(
                                consumedEnd, static_cast<std::uint32_t>(arrayEnd64));
                        }
                    }
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

    [[nodiscard]] static AnimationFile read(
        std::span<const std::byte> data,
        const Animation::MotionTargetLayout& targetLayout,
        Animation::EulerRecordWidth eulerWidth = Animation::EulerRecordWidth::Full32,
        std::uint32_t address = 0) {
        if (!check_is_nj_animation_file(data, address)) {
            throw std::runtime_error("File is not an animation file");
        }
        const auto payload = NJBlockUtility::RequireBlockPayload(
            data, address, FileHeaders::AnimationBlockHeaders, "NJ animation block not found");
        AnimationFile result;
        result.animation_block_address = payload.block.offset;
        result.animation = Animation::Motion::read(
            payload.reader, payload.data_address, targetLayout, payload.image_base, eulerWidth);
        return result;
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
    [[nodiscard]] static constexpr NinjaMotionKind kind_for_tag(std::uint32_t tag) {
        switch (tag) {
        case FileHeaders::NMDM:
            return NinjaMotionKind::Node;
        case FileHeaders::NSSM:
            return NinjaMotionKind::Shape;
        case FileHeaders::NCAM:
            return NinjaMotionKind::Camera;
        default:
            return NinjaMotionKind::Unknown;
        }
    }

    static void parse_pof0(std::span<const std::byte> data,
                           const spice::modeling::Structs::EndianStackReader& reader,
                           std::uint32_t payloadOffset,
                           std::uint32_t payloadSize,
                           const NJBlockInfo& pof,
                           NinjaMotionBlock& result) {
        const auto end64 = static_cast<std::uint64_t>(pof.offset) + 8ULL + pof.size;
        if (end64 > data.size() || end64 > std::numeric_limits<std::uint32_t>::max()) {
            result.diagnostics.push_back({true, "POF0 block overruns input", pof.offset});
            return;
        }
        result.pof0_range = NinjaMotionRange{
            pof.offset,
            static_cast<std::uint32_t>(8U + pof.size),
        };
        const auto encodedStart = pof.offset + 8U;
        const auto encodedEnd = static_cast<std::uint32_t>(end64);
        std::uint32_t cursor = encodedStart;
        std::uint64_t pointerWords = 0;
        while (cursor < encodedEnd) {
            const auto first = static_cast<std::uint8_t>(data[cursor]);
            const auto kind = static_cast<std::uint8_t>(first & 0xC0U);
            std::uint32_t width = 0;
            std::uint32_t deltaWords = 0;
            if (kind == 0x40U) {
                width = 1U;
                deltaWords = first & 0x3FU;
            } else if (kind == 0x80U) {
                width = 2U;
                if (cursor + width > encodedEnd) {
                    result.diagnostics.push_back({true, "truncated two-byte POF0 delta", cursor});
                    return;
                }
                deltaWords = (static_cast<std::uint32_t>(first & 0x3FU) << 8U) |
                    static_cast<std::uint8_t>(data[cursor + 1U]);
            } else if (kind == 0xC0U) {
                width = 4U;
                if (cursor + width > encodedEnd) {
                    result.diagnostics.push_back({true, "truncated four-byte POF0 delta", cursor});
                    return;
                }
                deltaWords = (static_cast<std::uint32_t>(first & 0x3FU) << 24U) |
                    (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[cursor + 1U])) << 16U) |
                    (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[cursor + 2U])) << 8U) |
                    static_cast<std::uint8_t>(data[cursor + 3U]);
            } else if (first == 0U) {
                ++cursor;
                continue;
            } else {
                result.diagnostics.push_back({true, "invalid POF0 delta prefix", cursor});
                ++cursor;
                continue;
            }

            pointerWords += deltaWords;
            const auto pointerOffset64 = pointerWords * 4ULL;
            if (pointerOffset64 + 4ULL > payloadSize ||
                pointerOffset64 > std::numeric_limits<std::uint32_t>::max()) {
                result.diagnostics.push_back({
                    true, "POF0 pointer field is outside motion payload", cursor});
                cursor += width;
                continue;
            }
            const auto pointerOffset = static_cast<std::uint32_t>(pointerOffset64);
            const auto rawPointer = reader.read_u32(payloadOffset + pointerOffset);
            std::optional<std::uint32_t> resolved{};
            if (rawPointer != 0U) {
                const auto resolved64 = static_cast<std::uint64_t>(payloadOffset) + rawPointer;
                if (resolved64 >= static_cast<std::uint64_t>(payloadOffset) + payloadSize ||
                    resolved64 > std::numeric_limits<std::uint32_t>::max()) {
                    result.diagnostics.push_back({
                        true,
                        "POF0 pointer target is outside motion payload",
                        payloadOffset + pointerOffset,
                    });
                } else {
                    resolved = static_cast<std::uint32_t>(resolved64);
                }
            }
            result.relocations.push_back(NinjaPof0Relocation{
                .encoded_range = {cursor, width},
                .pointer_field_offset = pointerOffset,
                .raw_pointer = rawPointer,
                .resolved_payload_offset = resolved,
            });
            cursor += width;
        }
    }

    [[nodiscard]] static constexpr std::uint32_t probe_entry_size(
        Animation::KeyframeAttributes type,
        bool shortRot) {
        if (type == Animation::KeyframeAttributes::EulerRotation && shortRot) {
            return 8U;
        }
        return Animation::keyframe_entry_size(type);
    }
};

} // namespace spice::modeling::File
