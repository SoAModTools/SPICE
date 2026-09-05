#pragma once

#include "File/AnimationFile.h"

#include <cstdint>
#include <span>

namespace spice::modeling::Testing::Slice8 {

using AnimationFile = ::spice::modeling::File::AnimationFile;

[[nodiscard]] inline bool CheckIsAnimationFile(std::span<const std::byte> data) {
    return ::spice::modeling::File::AnimationFile::check_is_animation_file(data);
}

[[nodiscard]] inline AnimationFile ReadAnimationFile(std::span<const std::byte> data,
                                                     std::uint32_t nodeCount,
                                                     bool shortRot = false) {
    return ::spice::modeling::File::AnimationFile::read_from_bytes(data, nodeCount, shortRot);
}

} // namespace spice::modeling::Testing::Slice8
