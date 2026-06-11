#include "PngCodec.h"

#include "../../third-party/lodepng/lodepng.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace spice::gvm::image {

model::RgbaImage readPngRgba8(const std::filesystem::path& path) {
    std::vector<unsigned char> pixels{};
    unsigned width = 0U;
    unsigned height = 0U;
    const auto error = lodepng::decode(pixels, width, height, path.string(), LCT_RGBA, 8U);
    if (error != 0U) {
        throw std::runtime_error("PNG decode failed for " + path.string() + ": " + lodepng_error_text(error));
    }

    model::RgbaImage image{};
    image.width = width;
    image.height = height;
    image.rgba8.assign(pixels.begin(), pixels.end());
    return image;
}

void writePngRgba8(const std::filesystem::path& path, const model::RgbaImage& image) {
    if (image.width == 0U || image.height == 0U) {
        throw std::runtime_error("PNG encode failed: image has empty dimensions");
    }
    const auto expectedSize = static_cast<std::size_t>(image.width) * image.height * 4U;
    if (image.rgba8.size() != expectedSize) {
        throw std::runtime_error("PNG encode failed: RGBA8 buffer size does not match dimensions");
    }

    const auto error = lodepng::encode(path.string(), image.rgba8, image.width, image.height, LCT_RGBA, 8U);
    if (error != 0U) {
        throw std::runtime_error("PNG encode failed for " + path.string() + ": " + lodepng_error_text(error));
    }
}

} // namespace spice::gvm::image
