#pragma once

#include "File/ModelFile.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace spice::modeling::Testing::Slice6 {

using ModelFile = File::ModelFile;

inline bool CheckIsModelFile(std::span<const std::byte> data, std::uint32_t address = 0) {
    return File::ModelFile::check_is_model_file(data, address);
}

inline ModelFile ReadModelFile(std::span<const std::byte> data, std::uint32_t address = 0) {
    return File::ModelFile::read_from_bytes(data, address);
}

} // namespace spice::modeling::Testing::Slice6
