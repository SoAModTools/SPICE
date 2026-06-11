#include "GvrEncoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

namespace spice::gvm::encoding {
namespace {

struct Rgba {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

void appendTag(std::vector<std::uint8_t>& out, const char a, const char b, const char c, const char d) {
    out.push_back(static_cast<std::uint8_t>(a));
    out.push_back(static_cast<std::uint8_t>(b));
    out.push_back(static_cast<std::uint8_t>(c));
    out.push_back(static_cast<std::uint8_t>(d));
}

void appendU32BE(std::vector<std::uint8_t>& out, const std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void appendU32LE(std::vector<std::uint8_t>& out, const std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

void appendU16BE(std::vector<std::uint8_t>& out, const std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void validateImage(const model::RgbaImage& image) {
    if (image.width == 0U || image.height == 0U) {
        throw std::runtime_error("cannot encode GVR with empty dimensions");
    }
    if (image.width > 0xFFFFU || image.height > 0xFFFFU) {
        throw std::runtime_error("cannot encode GVR larger than 65535x65535");
    }
    const auto expectedSize = static_cast<std::size_t>(image.width) * image.height * 4U;
    if (image.rgba8.size() != expectedSize) {
        throw std::runtime_error("cannot encode GVR: pixel buffer size does not match dimensions");
    }
}

std::uint8_t channelAt(const model::RgbaImage& image,
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t channel) {
    if (x >= image.width || y >= image.height) {
        return 0U;
    }
    const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4U + channel;
    return image.rgba8[offset];
}

Rgba pixelAt(const model::RgbaImage& image, const std::uint32_t x, const std::uint32_t y) {
    if (x >= image.width || y >= image.height) {
        return {};
    }
    const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4U;
    return Rgba{
        .r = image.rgba8[offset + 0U],
        .g = image.rgba8[offset + 1U],
        .b = image.rgba8[offset + 2U],
        .a = image.rgba8[offset + 3U],
    };
}

void appendRgba(std::vector<std::uint8_t>& out, const Rgba px) {
    out.push_back(px.r);
    out.push_back(px.g);
    out.push_back(px.b);
    out.push_back(px.a);
}

std::uint16_t packRgb5A3(Rgba px);
std::uint16_t packRgb565(Rgba px);
std::uint8_t quantize4(std::uint8_t value);

std::vector<model::RgbaImage> makeMipChain(const model::RgbaImage& base, const bool generateMipmaps) {
    std::vector<model::RgbaImage> levels{};
    levels.push_back(base);
    if (!generateMipmaps) {
        return levels;
    }

    while (levels.back().width > 1U || levels.back().height > 1U) {
        const auto& previous = levels.back();
        model::RgbaImage next{};
        next.width = std::max<std::uint32_t>(1U, previous.width / 2U);
        next.height = std::max<std::uint32_t>(1U, previous.height / 2U);
        next.rgba8.reserve(static_cast<std::size_t>(next.width) * next.height * 4U);

        for (std::uint32_t y = 0; y < next.height; ++y) {
            for (std::uint32_t x = 0; x < next.width; ++x) {
                std::uint32_t r = 0;
                std::uint32_t g = 0;
                std::uint32_t b = 0;
                std::uint32_t a = 0;
                std::uint32_t count = 0;
                for (std::uint32_t dy = 0; dy < 2U; ++dy) {
                    for (std::uint32_t dx = 0; dx < 2U; ++dx) {
                        const auto sx = std::min(previous.width - 1U, x * 2U + dx);
                        const auto sy = std::min(previous.height - 1U, y * 2U + dy);
                        const auto px = pixelAt(previous, sx, sy);
                        r += px.r;
                        g += px.g;
                        b += px.b;
                        a += px.a;
                        ++count;
                    }
                }
                next.rgba8.push_back(static_cast<std::uint8_t>(r / count));
                next.rgba8.push_back(static_cast<std::uint8_t>(g / count));
                next.rgba8.push_back(static_cast<std::uint8_t>(b / count));
                next.rgba8.push_back(static_cast<std::uint8_t>(a / count));
            }
        }
        levels.push_back(std::move(next));
    }
    return levels;
}

std::vector<std::uint8_t> swizzleRgba8(const model::RgbaImage& image) {
    std::vector<std::uint8_t> out{};
    const auto blocksX = (image.width + 3U) / 4U;
    const auto blocksY = (image.height + 3U) / 4U;
    out.reserve(static_cast<std::size_t>(blocksX) * blocksY * 64U);

    for (std::uint32_t by = 0; by < image.height; by += 4U) {
        for (std::uint32_t bx = 0; bx < image.width; bx += 4U) {
            for (std::uint32_t y = 0; y < 4U; ++y) {
                for (std::uint32_t x = 0; x < 4U; ++x) {
                    out.push_back(channelAt(image, bx + x, by + y, 3U));
                    out.push_back(channelAt(image, bx + x, by + y, 0U));
                }
            }
            for (std::uint32_t y = 0; y < 4U; ++y) {
                for (std::uint32_t x = 0; x < 4U; ++x) {
                    out.push_back(channelAt(image, bx + x, by + y, 1U));
                    out.push_back(channelAt(image, bx + x, by + y, 2U));
                }
            }
        }
    }
    return out;
}

std::vector<std::uint8_t> swizzleRgb5A3(const model::RgbaImage& image) {
    std::vector<std::uint8_t> out{};
    const auto blocksX = (image.width + 3U) / 4U;
    const auto blocksY = (image.height + 3U) / 4U;
    out.reserve(static_cast<std::size_t>(blocksX) * blocksY * 32U);

    for (std::uint32_t by = 0; by < image.height; by += 4U) {
        for (std::uint32_t bx = 0; bx < image.width; bx += 4U) {
            for (std::uint32_t y = 0; y < 4U; ++y) {
                for (std::uint32_t x = 0; x < 4U; ++x) {
                    appendU16BE(out, packRgb5A3(pixelAt(image, bx + x, by + y)));
                }
            }
        }
    }
    return out;
}

std::uint8_t intensity(const Rgba px) {
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(px.r) * 30U +
        static_cast<std::uint32_t>(px.g) * 59U +
        static_cast<std::uint32_t>(px.b) * 11U + 50U) / 100U);
}

std::vector<std::uint8_t> swizzleI4(const model::RgbaImage& image) {
    std::vector<std::uint8_t> out{};
    const auto blocksX = (image.width + 7U) / 8U;
    const auto blocksY = (image.height + 7U) / 8U;
    out.reserve(static_cast<std::size_t>(blocksX) * blocksY * 32U);

    for (std::uint32_t by = 0; by < image.height; by += 8U) {
        for (std::uint32_t bx = 0; bx < image.width; bx += 8U) {
            for (std::uint32_t y = 0; y < 8U; ++y) {
                for (std::uint32_t x = 0; x < 8U; x += 2U) {
                    const auto hi = quantize4(intensity(pixelAt(image, bx + x, by + y)));
                    const auto lo = quantize4(intensity(pixelAt(image, bx + x + 1U, by + y)));
                    out.push_back(static_cast<std::uint8_t>((hi << 4U) | lo));
                }
            }
        }
    }
    return out;
}

std::vector<std::uint8_t> swizzleI8(const model::RgbaImage& image) {
    std::vector<std::uint8_t> out{};
    const auto blocksX = (image.width + 7U) / 8U;
    const auto blocksY = (image.height + 3U) / 4U;
    out.reserve(static_cast<std::size_t>(blocksX) * blocksY * 32U);

    for (std::uint32_t by = 0; by < image.height; by += 4U) {
        for (std::uint32_t bx = 0; bx < image.width; bx += 8U) {
            for (std::uint32_t y = 0; y < 4U; ++y) {
                for (std::uint32_t x = 0; x < 8U; ++x) {
                    out.push_back(intensity(pixelAt(image, bx + x, by + y)));
                }
            }
        }
    }
    return out;
}

std::vector<std::uint8_t> swizzleIA4(const model::RgbaImage& image) {
    std::vector<std::uint8_t> out{};
    const auto blocksX = (image.width + 7U) / 8U;
    const auto blocksY = (image.height + 3U) / 4U;
    out.reserve(static_cast<std::size_t>(blocksX) * blocksY * 32U);

    for (std::uint32_t by = 0; by < image.height; by += 4U) {
        for (std::uint32_t bx = 0; bx < image.width; bx += 8U) {
            for (std::uint32_t y = 0; y < 4U; ++y) {
                for (std::uint32_t x = 0; x < 8U; ++x) {
                    const auto px = pixelAt(image, bx + x, by + y);
                    out.push_back(static_cast<std::uint8_t>((quantize4(px.a) << 4U) | quantize4(intensity(px))));
                }
            }
        }
    }
    return out;
}

std::vector<std::uint8_t> swizzleIA8(const model::RgbaImage& image) {
    std::vector<std::uint8_t> out{};
    const auto blocksX = (image.width + 3U) / 4U;
    const auto blocksY = (image.height + 3U) / 4U;
    out.reserve(static_cast<std::size_t>(blocksX) * blocksY * 32U);

    for (std::uint32_t by = 0; by < image.height; by += 4U) {
        for (std::uint32_t bx = 0; bx < image.width; bx += 4U) {
            for (std::uint32_t y = 0; y < 4U; ++y) {
                for (std::uint32_t x = 0; x < 4U; ++x) {
                    const auto px = pixelAt(image, bx + x, by + y);
                    out.push_back(px.a);
                    out.push_back(intensity(px));
                }
            }
        }
    }
    return out;
}

std::vector<std::uint8_t> swizzleRgb565(const model::RgbaImage& image) {
    std::vector<std::uint8_t> out{};
    const auto blocksX = (image.width + 3U) / 4U;
    const auto blocksY = (image.height + 3U) / 4U;
    out.reserve(static_cast<std::size_t>(blocksX) * blocksY * 32U);

    for (std::uint32_t by = 0; by < image.height; by += 4U) {
        for (std::uint32_t bx = 0; bx < image.width; bx += 4U) {
            for (std::uint32_t y = 0; y < 4U; ++y) {
                for (std::uint32_t x = 0; x < 4U; ++x) {
                    appendU16BE(out, packRgb565(pixelAt(image, bx + x, by + y)));
                }
            }
        }
    }
    return out;
}

std::uint8_t expand5(const std::uint8_t value) {
    return static_cast<std::uint8_t>((value << 3U) | (value >> 2U));
}

std::uint8_t expand6(const std::uint8_t value) {
    return static_cast<std::uint8_t>((value << 2U) | (value >> 4U));
}

std::uint8_t quantize5(const std::uint8_t value) {
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(value) * 31U + 127U) / 255U);
}

std::uint8_t quantize6(const std::uint8_t value) {
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(value) * 63U + 127U) / 255U);
}

std::uint8_t quantize4(const std::uint8_t value) {
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(value) * 15U + 127U) / 255U);
}

std::uint8_t quantize3(const std::uint8_t value) {
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(value) * 7U + 127U) / 255U);
}

std::uint16_t packRgb565(const Rgba px) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(quantize5(px.r)) << 11U) |
        (static_cast<std::uint16_t>(quantize6(px.g)) << 5U) |
        static_cast<std::uint16_t>(quantize5(px.b)));
}

Rgba unpackRgb565(const std::uint16_t px) {
    return Rgba{
        .r = expand5(static_cast<std::uint8_t>((px >> 11U) & 0x1FU)),
        .g = expand6(static_cast<std::uint8_t>((px >> 5U) & 0x3FU)),
        .b = expand5(static_cast<std::uint8_t>(px & 0x1FU)),
        .a = 255U,
    };
}

std::uint16_t packRgb5A3(const Rgba px) {
    if (px.a < 255U) {
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(quantize3(px.a)) << 12U) |
            (static_cast<std::uint16_t>(quantize4(px.r)) << 8U) |
            (static_cast<std::uint16_t>(quantize4(px.g)) << 4U) |
            static_cast<std::uint16_t>(quantize4(px.b)));
    }
    return static_cast<std::uint16_t>(
        0x8000U |
        (static_cast<std::uint16_t>(quantize5(px.r)) << 10U) |
        (static_cast<std::uint16_t>(quantize5(px.g)) << 5U) |
        static_cast<std::uint16_t>(quantize5(px.b)));
}

Rgba unpackRgb5A3(const std::uint16_t px) {
    if ((px & 0x8000U) != 0U) {
        return Rgba{
            .r = expand5(static_cast<std::uint8_t>((px >> 10U) & 0x1FU)),
            .g = expand5(static_cast<std::uint8_t>((px >> 5U) & 0x1FU)),
            .b = expand5(static_cast<std::uint8_t>(px & 0x1FU)),
            .a = 255U,
        };
    }
    const auto expand4 = [](const std::uint8_t value) {
        return static_cast<std::uint8_t>((value << 4U) | value);
    };
    return Rgba{
        .r = expand4(static_cast<std::uint8_t>((px >> 8U) & 0x0FU)),
        .g = expand4(static_cast<std::uint8_t>((px >> 4U) & 0x0FU)),
        .b = expand4(static_cast<std::uint8_t>(px & 0x0FU)),
        .a = static_cast<std::uint8_t>(((px >> 12U) & 0x7U) * 255U / 7U),
    };
}

std::uint32_t colorDistance(const Rgba a, const Rgba b, const bool includeAlpha) {
    const int dr = static_cast<int>(a.r) - static_cast<int>(b.r);
    const int dg = static_cast<int>(a.g) - static_cast<int>(b.g);
    const int db = static_cast<int>(a.b) - static_cast<int>(b.b);
    const int da = includeAlpha ? static_cast<int>(a.a) - static_cast<int>(b.a) : 0;
    return static_cast<std::uint32_t>(dr * dr + dg * dg + db * db + da * da);
}

std::array<Rgba, 4> cmprPalette(const std::uint16_t c0, const std::uint16_t c1) {
    std::array<Rgba, 4> colors{ unpackRgb565(c0), unpackRgb565(c1), {}, {} };
    if (c0 > c1) {
        colors[2] = Rgba{
            .r = static_cast<std::uint8_t>((2U * colors[0].r + colors[1].r) / 3U),
            .g = static_cast<std::uint8_t>((2U * colors[0].g + colors[1].g) / 3U),
            .b = static_cast<std::uint8_t>((2U * colors[0].b + colors[1].b) / 3U),
            .a = 255U,
        };
        colors[3] = Rgba{
            .r = static_cast<std::uint8_t>((colors[0].r + 2U * colors[1].r) / 3U),
            .g = static_cast<std::uint8_t>((colors[0].g + 2U * colors[1].g) / 3U),
            .b = static_cast<std::uint8_t>((colors[0].b + 2U * colors[1].b) / 3U),
            .a = 255U,
        };
    } else {
        colors[2] = Rgba{
            .r = static_cast<std::uint8_t>((colors[0].r + colors[1].r) / 2U),
            .g = static_cast<std::uint8_t>((colors[0].g + colors[1].g) / 2U),
            .b = static_cast<std::uint8_t>((colors[0].b + colors[1].b) / 2U),
            .a = 255U,
        };
        colors[3] = Rgba{ .r = 0U, .g = 0U, .b = 0U, .a = 0U };
    }
    return colors;
}

std::uint16_t luminance565(const Rgba px) {
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(px.r) * 30U +
        static_cast<std::uint32_t>(px.g) * 59U +
        static_cast<std::uint32_t>(px.b) * 11U);
}

void appendCmprSubblock(std::vector<std::uint8_t>& out,
    const model::RgbaImage& image,
    const std::uint32_t originX,
    const std::uint32_t originY) {
    bool hasTransparent = false;
    bool hasOpaque = false;
    Rgba minPx{ .r = 255U, .g = 255U, .b = 255U, .a = 255U };
    Rgba maxPx{ .r = 0U, .g = 0U, .b = 0U, .a = 255U };
    std::uint16_t minLum = std::numeric_limits<std::uint16_t>::max();
    std::uint16_t maxLum = 0U;

    for (std::uint32_t y = 0; y < 4U; ++y) {
        for (std::uint32_t x = 0; x < 4U; ++x) {
            const auto px = pixelAt(image, originX + x, originY + y);
            if (px.a < 128U) {
                hasTransparent = true;
                continue;
            }
            hasOpaque = true;
            const auto lum = luminance565(px);
            if (lum < minLum) {
                minLum = lum;
                minPx = px;
            }
            if (lum >= maxLum) {
                maxLum = lum;
                maxPx = px;
            }
        }
    }

    if (!hasOpaque) {
        appendU16BE(out, 0U);
        appendU16BE(out, 0U);
        appendU32BE(out, 0xFFFFFFFFU);
        return;
    }

    auto c0 = packRgb565(maxPx);
    auto c1 = packRgb565(minPx);
    if (hasTransparent) {
        if (c0 > c1) {
            std::swap(c0, c1);
        }
    } else if (c0 <= c1) {
        std::swap(c0, c1);
        if (c0 <= c1) {
            c0 = 0xFFFFU;
            c1 = 0U;
        }
    }

    const auto palette = cmprPalette(c0, c1);
    std::uint32_t codes = 0U;
    for (std::uint32_t y = 0; y < 4U; ++y) {
        for (std::uint32_t x = 0; x < 4U; ++x) {
            const auto px = pixelAt(image, originX + x, originY + y);
            std::uint32_t index = 0U;
            if (hasTransparent && px.a < 128U) {
                index = 3U;
            } else {
                std::uint32_t bestDistance = std::numeric_limits<std::uint32_t>::max();
                const std::uint32_t paletteLimit = hasTransparent ? 3U : 4U;
                for (std::uint32_t i = 0; i < paletteLimit; ++i) {
                    const auto distance = colorDistance(px, palette[i], false);
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        index = i;
                    }
                }
            }
            const auto shift = 30U - ((y * 4U + x) * 2U);
            codes |= index << shift;
        }
    }

    appendU16BE(out, c0);
    appendU16BE(out, c1);
    appendU32BE(out, codes);
}

std::vector<std::uint8_t> swizzleCmpr(const model::RgbaImage& image) {
    std::vector<std::uint8_t> out{};
    const auto blocksX = (image.width + 7U) / 8U;
    const auto blocksY = (image.height + 7U) / 8U;
    out.reserve(static_cast<std::size_t>(blocksX) * blocksY * 32U);

    for (std::uint32_t by = 0; by < image.height; by += 8U) {
        for (std::uint32_t bx = 0; bx < image.width; bx += 8U) {
            appendCmprSubblock(out, image, bx, by);
            appendCmprSubblock(out, image, bx + 4U, by);
            appendCmprSubblock(out, image, bx, by + 4U);
            appendCmprSubblock(out, image, bx + 4U, by + 4U);
        }
    }
    return out;
}

std::uint16_t packPaletteEntry(const Rgba px, const model::PaletteFormat format) {
    switch (format) {
    case model::PaletteFormat::IA8:
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(px.a) << 8U) | intensity(px));
    case model::PaletteFormat::RGB565:
        return packRgb565(px);
    case model::PaletteFormat::RGB5A3:
        return packRgb5A3(px);
    case model::PaletteFormat::None:
    default:
        break;
    }
    throw std::runtime_error("indexed GVR encoding requires a palette format");
}

Rgba unpackPaletteEntry(const std::uint16_t px, const model::PaletteFormat format) {
    switch (format) {
    case model::PaletteFormat::IA8:
        return Rgba{
            .r = static_cast<std::uint8_t>(px & 0xFFU),
            .g = static_cast<std::uint8_t>(px & 0xFFU),
            .b = static_cast<std::uint8_t>(px & 0xFFU),
            .a = static_cast<std::uint8_t>((px >> 8U) & 0xFFU),
        };
    case model::PaletteFormat::RGB565:
        return unpackRgb565(px);
    case model::PaletteFormat::RGB5A3:
        return unpackRgb5A3(px);
    case model::PaletteFormat::None:
    default:
        break;
    }
    return {};
}

struct IndexedPalette {
    std::vector<std::uint16_t> entries{};
    std::map<std::uint16_t, std::uint16_t> exactIndexByPacked{};
    model::PaletteFormat format = model::PaletteFormat::None;
};

IndexedPalette buildPalette(const model::RgbaImage& image,
    const model::PaletteFormat format,
    const std::size_t capacity) {
    std::map<std::uint16_t, std::size_t> counts{};
    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            ++counts[packPaletteEntry(pixelAt(image, x, y), format)];
        }
    }

    std::vector<std::pair<std::uint16_t, std::size_t>> ranked(counts.begin(), counts.end());
    std::sort(ranked.begin(), ranked.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.second != rhs.second) {
            return lhs.second > rhs.second;
        }
        return lhs.first < rhs.first;
    });

    IndexedPalette palette{};
    palette.format = format;
    palette.entries.reserve(capacity);
    for (std::size_t i = 0; i < capacity; ++i) {
        palette.entries.push_back(i < ranked.size() ? ranked[i].first : ranked.empty() ? 0U : ranked.front().first);
        if (i < ranked.size()) {
            palette.exactIndexByPacked[ranked[i].first] = static_cast<std::uint16_t>(i);
        }
    }
    return palette;
}

std::uint16_t closestPaletteIndex(const Rgba px, const IndexedPalette& palette) {
    const auto packed = packPaletteEntry(px, palette.format);
    if (const auto exact = palette.exactIndexByPacked.find(packed); exact != palette.exactIndexByPacked.end()) {
        return exact->second;
    }
    std::uint16_t best = 0U;
    std::uint32_t bestDistance = std::numeric_limits<std::uint32_t>::max();
    for (std::size_t i = 0; i < palette.entries.size(); ++i) {
        const auto distance = colorDistance(px, unpackPaletteEntry(palette.entries[i], palette.format), true);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = static_cast<std::uint16_t>(i);
        }
    }
    return best;
}

std::vector<std::uint8_t> swizzleCi4(const model::RgbaImage& image, const IndexedPalette& palette) {
    std::vector<std::uint8_t> out{};
    const auto blocksX = (image.width + 7U) / 8U;
    const auto blocksY = (image.height + 7U) / 8U;
    out.reserve(static_cast<std::size_t>(blocksX) * blocksY * 32U);

    for (std::uint32_t by = 0; by < image.height; by += 8U) {
        for (std::uint32_t bx = 0; bx < image.width; bx += 8U) {
            for (std::uint32_t y = 0; y < 8U; ++y) {
                for (std::uint32_t x = 0; x < 8U; x += 2U) {
                    const auto hi = closestPaletteIndex(pixelAt(image, bx + x, by + y), palette);
                    const auto lo = closestPaletteIndex(pixelAt(image, bx + x + 1U, by + y), palette);
                    out.push_back(static_cast<std::uint8_t>((hi << 4U) | lo));
                }
            }
        }
    }
    return out;
}

std::vector<std::uint8_t> swizzleCi8(const model::RgbaImage& image, const IndexedPalette& palette) {
    std::vector<std::uint8_t> out{};
    const auto blocksX = (image.width + 7U) / 8U;
    const auto blocksY = (image.height + 3U) / 4U;
    out.reserve(static_cast<std::size_t>(blocksX) * blocksY * 32U);

    for (std::uint32_t by = 0; by < image.height; by += 4U) {
        for (std::uint32_t bx = 0; bx < image.width; bx += 8U) {
            for (std::uint32_t y = 0; y < 4U; ++y) {
                for (std::uint32_t x = 0; x < 8U; ++x) {
                    out.push_back(static_cast<std::uint8_t>(closestPaletteIndex(pixelAt(image, bx + x, by + y), palette) & 0xFFU));
                }
            }
        }
    }
    return out;
}

std::vector<std::uint8_t> swizzleCi14X2(const model::RgbaImage& image, const IndexedPalette& palette) {
    std::vector<std::uint8_t> out{};
    const auto blocksX = (image.width + 3U) / 4U;
    const auto blocksY = (image.height + 3U) / 4U;
    out.reserve(static_cast<std::size_t>(blocksX) * blocksY * 32U);

    for (std::uint32_t by = 0; by < image.height; by += 4U) {
        for (std::uint32_t bx = 0; bx < image.width; bx += 4U) {
            for (std::uint32_t y = 0; y < 4U; ++y) {
                for (std::uint32_t x = 0; x < 4U; ++x) {
                    appendU16BE(out, static_cast<std::uint16_t>(closestPaletteIndex(pixelAt(image, bx + x, by + y), palette) & 0x3FFFU));
                }
            }
        }
    }
    return out;
}

std::uint8_t rawDataFormatFor(const model::TextureFormat format) {
    switch (format) {
    case model::TextureFormat::I4: return 0x00U;
    case model::TextureFormat::I8: return 0x01U;
    case model::TextureFormat::IA4: return 0x02U;
    case model::TextureFormat::IA8: return 0x03U;
    case model::TextureFormat::RGB565: return 0x04U;
    case model::TextureFormat::RGB5A3: return 0x05U;
    case model::TextureFormat::RGBA8: return 0x06U;
    case model::TextureFormat::CI4: return 0x08U;
    case model::TextureFormat::CI8: return 0x09U;
    case model::TextureFormat::CI14X2: return 0x0AU;
    case model::TextureFormat::CMPR: return 0x0EU;
    default: break;
    }
    throw std::runtime_error("unsupported GVR encode texture format");
}

bool usesPalette(const model::TextureFormat format) {
    return format == model::TextureFormat::CI4 ||
        format == model::TextureFormat::CI8 ||
        format == model::TextureFormat::CI14X2;
}

std::size_t paletteCapacity(const model::TextureFormat format) {
    switch (format) {
    case model::TextureFormat::CI4: return 16U;
    case model::TextureFormat::CI8: return 256U;
    case model::TextureFormat::CI14X2: return 16384U;
    default: break;
    }
    return 0U;
}

std::uint8_t paletteFlagBits(const model::PaletteFormat format) {
    switch (format) {
    case model::PaletteFormat::IA8: return 0x00U;
    case model::PaletteFormat::RGB565: return 0x10U;
    case model::PaletteFormat::RGB5A3: return 0x20U;
    case model::PaletteFormat::None:
    default:
        break;
    }
    throw std::runtime_error("indexed GVR encoding requires IA8, RGB565, or RGB5A3 palette format");
}

std::uint8_t rawFlagsFor(const EncodeOptions& options) {
    std::uint8_t flags = options.generateMipmaps ? 0x01U : 0x00U;
    if (usesPalette(options.textureFormat)) {
        flags = static_cast<std::uint8_t>(flags | paletteFlagBits(options.paletteFormat));
        if (options.textureFormat == model::TextureFormat::CI4) {
            flags = static_cast<std::uint8_t>(flags | 0x08U);
        }
    }
    return flags;
}

std::vector<std::uint8_t> encodeImageLevels(const std::vector<model::RgbaImage>& levels,
    const EncodeOptions& options,
    const IndexedPalette* palette) {
    std::vector<std::uint8_t> out{};
    for (const auto& level : levels) {
        switch (options.textureFormat) {
        case model::TextureFormat::I4: {
            const auto bytes = swizzleI4(level);
            out.insert(out.end(), bytes.begin(), bytes.end());
            break;
        }
        case model::TextureFormat::I8: {
            const auto bytes = swizzleI8(level);
            out.insert(out.end(), bytes.begin(), bytes.end());
            break;
        }
        case model::TextureFormat::IA4: {
            const auto bytes = swizzleIA4(level);
            out.insert(out.end(), bytes.begin(), bytes.end());
            break;
        }
        case model::TextureFormat::IA8: {
            const auto bytes = swizzleIA8(level);
            out.insert(out.end(), bytes.begin(), bytes.end());
            break;
        }
        case model::TextureFormat::RGB565: {
            const auto bytes = swizzleRgb565(level);
            out.insert(out.end(), bytes.begin(), bytes.end());
            break;
        }
        case model::TextureFormat::RGBA8: {
            const auto bytes = swizzleRgba8(level);
            out.insert(out.end(), bytes.begin(), bytes.end());
            break;
        }
        case model::TextureFormat::RGB5A3: {
            const auto bytes = swizzleRgb5A3(level);
            out.insert(out.end(), bytes.begin(), bytes.end());
            break;
        }
        case model::TextureFormat::CMPR: {
            const auto bytes = swizzleCmpr(level);
            out.insert(out.end(), bytes.begin(), bytes.end());
            break;
        }
        case model::TextureFormat::CI4: {
            const auto bytes = swizzleCi4(level, *palette);
            out.insert(out.end(), bytes.begin(), bytes.end());
            break;
        }
        case model::TextureFormat::CI8: {
            const auto bytes = swizzleCi8(level, *palette);
            out.insert(out.end(), bytes.begin(), bytes.end());
            break;
        }
        case model::TextureFormat::CI14X2: {
            const auto bytes = swizzleCi14X2(level, *palette);
            out.insert(out.end(), bytes.begin(), bytes.end());
            break;
        }
        default:
            throw std::runtime_error("unsupported GVR encode texture format");
        }
    }
    return out;
}

} // namespace

std::vector<std::uint8_t> encodeGvr(const model::RgbaImage& image, const EncodeOptions& options) {
    validateImage(image);

    std::vector<std::uint8_t> out{};
    if (options.hasGlobalIndex) {
        appendTag(out, 'G', 'C', 'I', 'X');
        appendU32LE(out, 8U);
        appendU32BE(out, options.globalIndex);
        appendU32BE(out, 0U);
    }

    const auto levels = makeMipChain(image, options.generateMipmaps);
    IndexedPalette palette{};
    const IndexedPalette* palettePtr = nullptr;
    std::vector<std::uint8_t> paletteData{};
    if (usesPalette(options.textureFormat)) {
        palette = buildPalette(image, options.paletteFormat, paletteCapacity(options.textureFormat));
        palettePtr = &palette;
        paletteData.reserve(palette.entries.size() * 2U);
        for (const auto entry : palette.entries) {
            appendU16BE(paletteData, entry);
        }
    }

    const auto imageData = encodeImageLevels(levels, options, palettePtr);
    const auto payloadSize = static_cast<std::uint32_t>(8U + paletteData.size() + imageData.size());
    appendTag(out, 'G', 'V', 'R', 'T');
    appendU32LE(out, payloadSize);
    out.push_back(0U);
    out.push_back(0U);
    out.push_back(rawFlagsFor(options));
    out.push_back(rawDataFormatFor(options.textureFormat));
    appendU16BE(out, static_cast<std::uint16_t>(image.width));
    appendU16BE(out, static_cast<std::uint16_t>(image.height));
    out.insert(out.end(), paletteData.begin(), paletteData.end());
    out.insert(out.end(), imageData.begin(), imageData.end());
    return out;
}

std::vector<std::uint8_t> encodeRgba8Gvr(const model::RgbaImage& image, const EncodeOptions& options) {
    auto rgbaOptions = options;
    if (rgbaOptions.textureFormat != model::TextureFormat::RGBA8) {
        throw std::runtime_error("encodeRgba8Gvr requires RGBA8 texture format");
    }
    return encodeGvr(image, rgbaOptions);
}

} // namespace spice::gvm::encoding
