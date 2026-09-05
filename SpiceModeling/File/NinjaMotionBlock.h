#pragma once

#include "../Animation/Enums.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace spice::modeling::File {

enum class NinjaMotionKind {
    Node,
    Shape,
    Camera,
    Unknown,
};

enum class NinjaMotionParseStatus {
    Complete,
    Partial,
    Failed,
};

struct NinjaMotionRange {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

struct NinjaMotionDiagnostic {
    bool error = false;
    std::string message{};
    std::optional<std::uint32_t> offset{};
};

struct NinjaPof0Relocation {
    NinjaMotionRange encoded_range{};
    std::uint32_t pointer_field_offset = 0;
    std::uint32_t raw_pointer = 0;
    std::optional<std::uint32_t> resolved_payload_offset{};
};

struct NinjaMotionHeader {
    std::uint32_t raw_tag = 0;
    NinjaMotionKind kind = NinjaMotionKind::Unknown;
    std::uint32_t payload_size = 0;
    std::uint32_t raw_mdata_offset = 0;
    std::optional<std::uint32_t> resolved_mdata_offset{};
    std::uint32_t frame_count = 0;
    Animation::KeyframeAttributes channel_mask = Animation::KeyframeAttributes::None;
    std::uint16_t raw_style = 0;
    std::uint32_t reserved = 0;
    std::uint8_t encoded_lane_count = 0;
    Animation::InterpolationMode interpolation_mode = Animation::InterpolationMode::Linear;
};

struct NinjaMotionBlock {
    NinjaMotionParseStatus status = NinjaMotionParseStatus::Failed;
    NinjaMotionHeader header{};
    NinjaMotionRange chunk_header_range{};
    NinjaMotionRange motion_header_range{};
    NinjaMotionRange block_range{};
    NinjaMotionRange payload_range{};
    std::optional<NinjaMotionRange> pof0_range{};
    std::vector<NinjaPof0Relocation> relocations{};
    std::vector<NinjaMotionDiagnostic> diagnostics{};
};

} // namespace spice::modeling::File
