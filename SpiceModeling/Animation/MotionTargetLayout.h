#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace spice::modeling::Animation {

enum class EulerRecordWidth {
    Full32,
    Short16,
};

struct MotionTargetLane {
    std::uint32_t node_index = 0;
    std::optional<std::uint32_t> vertex_count{};
    std::optional<std::uint32_t> normal_count{};

    [[nodiscard]] bool operator==(const MotionTargetLane&) const = default;
};

struct MotionTargetLayout {
    std::vector<MotionTargetLane> lanes{};

    [[nodiscard]] std::uint32_t lane_count() const {
        return static_cast<std::uint32_t>(lanes.size());
    }

    [[nodiscard]] bool operator==(const MotionTargetLayout&) const = default;
};

} // namespace spice::modeling::Animation
