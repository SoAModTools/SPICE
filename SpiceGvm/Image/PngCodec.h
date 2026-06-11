#pragma once

#include "../Model/GvmTextureModel.h"

#include <filesystem>

namespace spice::gvm::image {

[[nodiscard]] model::RgbaImage readPngRgba8(const std::filesystem::path& path);
void writePngRgba8(const std::filesystem::path& path, const model::RgbaImage& image);

} // namespace spice::gvm::image
