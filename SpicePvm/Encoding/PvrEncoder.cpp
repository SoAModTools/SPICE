#include "PvrEncoder.h"
#include "../../SpiceRoot/Binary/EndianWriter.h"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <map>
#include <numeric>
#include <ranges>
#include <utility>

namespace spice::pvm::encoding {
namespace {

using model::DataLayout;
using model::DiagnosticSeverity;
using model::ParseStatus;
using model::PixelFormat;

using PackedVector = std::array<std::uint16_t, 4>;

struct PhysicalLevel {
    const model::RgbaImage* image = nullptr;
    std::size_t offset = 0;
    std::size_t byteSize = 0;
};

struct UniqueVector {
    PackedVector words{};
    std::array<std::uint8_t, 16> components{};
    std::uint64_t count = 0;
};

void addError(PvrEncodeResult& result, const std::size_t offset, std::string message)
{
    result.diagnostics.push_back({DiagnosticSeverity::Error, offset, std::move(message)});
}

bool checkedMul(const std::size_t left, const std::size_t right, std::size_t& result)
{
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

bool isPowerOfTwo(const std::uint32_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

bool isMipmapped(const DataLayout layout)
{
    return layout == DataLayout::TwiddledMipmaps ||
        layout == DataLayout::VqMipmaps ||
        layout == DataLayout::SmallVqMipmaps ||
        layout == DataLayout::TwiddledMipmapsDma;
}

bool isVq(const DataLayout layout)
{
    return layout == DataLayout::Vq || layout == DataLayout::VqMipmaps ||
        layout == DataLayout::SmallVq || layout == DataLayout::SmallVqMipmaps;
}

bool isTwiddled(const DataLayout layout)
{
    return layout == DataLayout::Twiddled || layout == DataLayout::TwiddledMipmaps ||
        layout == DataLayout::TwiddledMipmapsDma;
}

std::size_t twiddle(const std::uint32_t value)
{
    std::size_t result = 0;
    for (unsigned bit = 0; bit < 16; ++bit)
        result |= static_cast<std::size_t>((value >> bit) & 1U) << (bit * 2U);
    return result;
}

std::size_t rectangleTwiddledIndex(
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t width,
    const std::uint32_t height)
{
    const auto tileSize = std::min(width, height);
    const auto tileX = x / tileSize;
    const auto tileY = y / tileSize;
    const auto tilesPerRow = width / tileSize;
    const auto tileIndex = static_cast<std::size_t>(tileY) * tilesPerRow + tileX;
    const auto withinTile = (twiddle(x % tileSize) << 1U) | twiddle(y % tileSize);
    return tileIndex * static_cast<std::size_t>(tileSize) * tileSize + withinTile;
}

std::uint8_t quantize(const std::uint8_t value, const std::uint32_t maximum)
{
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(value) * maximum + 127U) / 255U);
}

std::uint16_t packColor(const std::uint8_t* rgba, const PixelFormat format)
{
    switch (format) {
    case PixelFormat::Argb1555:
        return static_cast<std::uint16_t>((rgba[3] >= 128U ? 0x8000U : 0U) |
            (static_cast<std::uint16_t>(quantize(rgba[0], 31U)) << 10U) |
            (static_cast<std::uint16_t>(quantize(rgba[1], 31U)) << 5U) |
            quantize(rgba[2], 31U));
    case PixelFormat::Rgb565:
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(quantize(rgba[0], 31U)) << 11U) |
            (static_cast<std::uint16_t>(quantize(rgba[1], 63U)) << 5U) |
            quantize(rgba[2], 31U));
    case PixelFormat::Argb4444:
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(quantize(rgba[3], 15U)) << 12U) |
            (static_cast<std::uint16_t>(quantize(rgba[0], 15U)) << 8U) |
            (static_cast<std::uint16_t>(quantize(rgba[1], 15U)) << 4U) |
            quantize(rgba[2], 15U));
    default:
        return 0;
    }
}

std::array<std::uint8_t, 4> unpackColor(const std::uint16_t value, const PixelFormat format)
{
    switch (format) {
    case PixelFormat::Argb1555:
        return {
            static_cast<std::uint8_t>(((value >> 10U) & 0x1FU) * 255U / 31U),
            static_cast<std::uint8_t>(((value >> 5U) & 0x1FU) * 255U / 31U),
            static_cast<std::uint8_t>((value & 0x1FU) * 255U / 31U),
            static_cast<std::uint8_t>((value & 0x8000U) != 0 ? 255U : 0U),
        };
    case PixelFormat::Rgb565:
        return {
            static_cast<std::uint8_t>(((value >> 11U) & 0x1FU) * 255U / 31U),
            static_cast<std::uint8_t>(((value >> 5U) & 0x3FU) * 255U / 63U),
            static_cast<std::uint8_t>((value & 0x1FU) * 255U / 31U),
            255U,
        };
    case PixelFormat::Argb4444:
        return {
            static_cast<std::uint8_t>(((value >> 8U) & 0x0FU) * 17U),
            static_cast<std::uint8_t>(((value >> 4U) & 0x0FU) * 17U),
            static_cast<std::uint8_t>((value & 0x0FU) * 17U),
            static_cast<std::uint8_t>(((value >> 12U) & 0x0FU) * 17U),
        };
    default:
        return {};
    }
}

void appendU16(std::vector<std::uint8_t>& out, const std::uint16_t value)
{
    spice::root::append_u16(out, value, spice::root::Endian::Little);
}

void appendU32(std::vector<std::uint8_t>& out, const std::uint32_t value)
{
    spice::root::append_u32(out, value, spice::root::Endian::Little);
}

void appendTag(std::vector<std::uint8_t>& out, const char (&tag)[5])
{
    out.insert(out.end(), tag, tag + 4);
}

bool validateImage(const model::RgbaImage& image, PvrEncodeResult& result)
{
    if (image.width == 0 || image.height == 0) {
        addError(result, 0, "PVR image dimensions must be non-zero");
        return false;
    }
    if (image.width > std::numeric_limits<std::uint16_t>::max() ||
        image.height > std::numeric_limits<std::uint16_t>::max()) {
        addError(result, 0, "PVR image dimensions exceed the 16-bit header fields");
        return false;
    }
    std::size_t pixels = 0;
    std::size_t bytes = 0;
    if (!checkedMul(image.width, image.height, pixels) || !checkedMul(pixels, 4, bytes) ||
        image.pixels.size() != bytes) {
        addError(result, 0, "RGBA image byte count is inconsistent with its dimensions");
        return false;
    }
    return true;
}

model::RgbaImage downsample(const model::RgbaImage& source)
{
    model::RgbaImage out;
    out.width = std::max<std::uint32_t>(source.width / 2U, 1U);
    out.height = std::max<std::uint32_t>(source.height / 2U, 1U);
    out.pixels.resize(static_cast<std::size_t>(out.width) * out.height * 4U);
    for (std::uint32_t y = 0; y < out.height; ++y) {
        for (std::uint32_t x = 0; x < out.width; ++x) {
            for (std::size_t channel = 0; channel < 4; ++channel) {
                std::uint32_t sum = 0;
                std::uint32_t count = 0;
                for (std::uint32_t dy = 0; dy < 2; ++dy) {
                    const auto sy = std::min(y * 2U + dy, source.height - 1U);
                    for (std::uint32_t dx = 0; dx < 2; ++dx) {
                        const auto sx = std::min(x * 2U + dx, source.width - 1U);
                        sum += source.pixels[(static_cast<std::size_t>(sy) * source.width + sx) * 4U + channel];
                        ++count;
                    }
                }
                out.pixels[(static_cast<std::size_t>(y) * out.width + x) * 4U + channel] =
                    static_cast<std::uint8_t>((sum + count / 2U) / count);
            }
        }
    }
    return out;
}

std::size_t smallVqCodebookEntries(const std::uint32_t size, const bool mipmapped)
{
    if (mipmapped) {
        if (size == 16) return 16;
        if (size == 32) return 64;
        if (size == 64) return 256;
        return 0;
    }
    if (size == 16) return 16;
    if (size == 32) return 32;
    if (size == 64) return 128;
    return 0;
}

PackedVector makeVector(const model::RgbaImage& image, const std::uint32_t x,
    const std::uint32_t y, const PixelFormat format)
{
    PackedVector vector{};
    if (image.width == 1U) {
        const auto word = packColor(image.pixels.data(), format);
        vector.fill(word);
        return vector;
    }
    for (std::uint32_t blockX = 0; blockX < 2; ++blockX) {
        for (std::uint32_t blockY = 0; blockY < 2; ++blockY) {
            const auto pixel = (static_cast<std::size_t>(y + blockY) * image.width + x + blockX) * 4U;
            vector[blockX * 2U + blockY] = packColor(image.pixels.data() + pixel, format);
        }
    }
    return vector;
}

std::vector<UniqueVector> collectUniqueVectors(
    const std::vector<model::RgbaImage>& images, const PixelFormat format)
{
    std::map<PackedVector, std::uint64_t> histogram;
    for (const auto& image : images) {
        if (image.width == 1U) {
            ++histogram[makeVector(image, 0, 0, format)];
            continue;
        }
        for (std::uint32_t y = 0; y < image.height; y += 2U)
            for (std::uint32_t x = 0; x < image.width; x += 2U)
                ++histogram[makeVector(image, x, y, format)];
    }

    std::vector<UniqueVector> unique;
    unique.reserve(histogram.size());
    for (const auto& [words, count] : histogram) {
        UniqueVector item;
        item.words = words;
        item.count = count;
        for (std::size_t color = 0; color < 4; ++color) {
            const auto rgba = unpackColor(words[color], format);
            std::copy(rgba.begin(), rgba.end(), item.components.begin() + static_cast<std::ptrdiff_t>(color * 4U));
        }
        unique.push_back(item);
    }
    return unique;
}

struct QuantizedCodebook {
    std::vector<PackedVector> entries;
    std::map<PackedVector, std::uint8_t> indices;
};

QuantizedCodebook quantizeVectors(
    const std::vector<UniqueVector>& unique, const std::size_t targetEntries, const PixelFormat format)
{
    QuantizedCodebook result;
    if (unique.size() <= targetEntries) {
        result.entries.reserve(targetEntries);
        for (std::size_t i = 0; i < unique.size(); ++i) {
            result.entries.push_back(unique[i].words);
            result.indices.emplace(unique[i].words, static_cast<std::uint8_t>(i));
        }
        const PackedVector filler = result.entries.empty() ? PackedVector{} : result.entries.back();
        result.entries.resize(targetEntries, filler);
        return result;
    }

    std::vector<std::vector<std::size_t>> buckets(1);
    buckets.front().resize(unique.size());
    std::iota(buckets.front().begin(), buckets.front().end(), 0U);

    while (buckets.size() < targetEntries) {
        std::size_t chosen = buckets.size();
        std::size_t chosenDimension = 0;
        std::uint64_t chosenScore = 0;
        for (std::size_t bucketIndex = 0; bucketIndex < buckets.size(); ++bucketIndex) {
            const auto& bucket = buckets[bucketIndex];
            if (bucket.size() < 2)
                continue;
            std::uint64_t weight = 0;
            std::uint8_t bestRange = 0;
            std::size_t bestDimension = 0;
            for (const auto index : bucket)
                weight += unique[index].count;
            for (std::size_t dimension = 0; dimension < 16; ++dimension) {
                std::uint8_t minimum = 255;
                std::uint8_t maximum = 0;
                for (const auto index : bucket) {
                    minimum = std::min(minimum, unique[index].components[dimension]);
                    maximum = std::max(maximum, unique[index].components[dimension]);
                }
                const auto range = static_cast<std::uint8_t>(maximum - minimum);
                if (range > bestRange) {
                    bestRange = range;
                    bestDimension = dimension;
                }
            }
            const auto score = weight * (static_cast<std::uint64_t>(bestRange) + 1U);
            if (chosen == buckets.size() || score > chosenScore) {
                chosen = bucketIndex;
                chosenDimension = bestDimension;
                chosenScore = score;
            }
        }
        if (chosen == buckets.size())
            break;

        auto sorted = std::move(buckets[chosen]);
        std::sort(sorted.begin(), sorted.end(), [&](const std::size_t left, const std::size_t right) {
            if (unique[left].components[chosenDimension] != unique[right].components[chosenDimension])
                return unique[left].components[chosenDimension] < unique[right].components[chosenDimension];
            return unique[left].words < unique[right].words;
        });
        std::uint64_t totalWeight = 0;
        for (const auto index : sorted)
            totalWeight += unique[index].count;
        std::uint64_t accumulated = 0;
        std::size_t split = 1;
        for (; split < sorted.size(); ++split) {
            accumulated += unique[sorted[split - 1U]].count;
            if (accumulated * 2U >= totalWeight)
                break;
        }
        split = std::clamp<std::size_t>(split, 1U, sorted.size() - 1U);
        buckets[chosen] = {sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(split)};
        buckets.emplace_back(sorted.begin() + static_cast<std::ptrdiff_t>(split), sorted.end());
    }

    result.entries.reserve(targetEntries);
    for (std::size_t bucketIndex = 0; bucketIndex < buckets.size(); ++bucketIndex) {
        const auto& bucket = buckets[bucketIndex];
        std::array<std::uint64_t, 16> sums{};
        std::uint64_t weight = 0;
        for (const auto index : bucket) {
            weight += unique[index].count;
            for (std::size_t component = 0; component < 16; ++component)
                sums[component] += static_cast<std::uint64_t>(unique[index].components[component]) * unique[index].count;
        }
        PackedVector center{};
        for (std::size_t color = 0; color < 4; ++color) {
            std::array<std::uint8_t, 4> rgba{};
            for (std::size_t channel = 0; channel < 4; ++channel)
                rgba[channel] = static_cast<std::uint8_t>((sums[color * 4U + channel] + weight / 2U) / weight);
            center[color] = packColor(rgba.data(), format);
        }
        result.entries.push_back(center);
        for (const auto index : bucket)
            result.indices.emplace(unique[index].words, static_cast<std::uint8_t>(bucketIndex));
    }
    const PackedVector filler = result.entries.empty() ? PackedVector{} : result.entries.back();
    result.entries.resize(targetEntries, filler);
    return result;
}

} // namespace

bool PvrEncodeResult::ok() const noexcept
{
    return status == ParseStatus::Complete;
}

PvrEncodeResult encodePvrTexture(const model::RgbaImage& image, const PvrEncodeOptions& options)
{
    return encodePvrTexture(std::span<const model::RgbaImage>(&image, 1U), options);
}

PvrEncodeResult encodePvrTexture(
    const std::span<const model::RgbaImage> mipImages,
    const PvrEncodeOptions& options)
{
    PvrEncodeResult result;
    if (mipImages.empty()) {
        addError(result, 0, "At least one RGBA image is required to encode a PVR texture");
        return result;
    }
    if (options.pixelFormat == PixelFormat::Unknown) {
        addError(result, 0, "Unknown PVR pixel formats cannot be encoded");
        return result;
    }
    if (options.dataLayout == DataLayout::Unknown) {
        addError(result, 0, "Unknown PVR data layouts cannot be encoded");
        return result;
    }
    for (const auto& image : mipImages)
        if (!validateImage(image, result))
            return result;

    const auto& base = mipImages.front();
    if (options.dataLayout == DataLayout::RectangleTwiddled &&
        (!isPowerOfTwo(base.width) || !isPowerOfTwo(base.height))) {
        addError(result, 0, "Rectangle-twiddled PVR dimensions must be powers of two");
        return result;
    }
    if (options.dataLayout != DataLayout::Rectangle &&
        options.dataLayout != DataLayout::RectangleTwiddled &&
        (base.width != base.height || !isPowerOfTwo(base.width))) {
        addError(result, 0, "Twiddled and VQ PVR textures must be square powers of two");
        return result;
    }
    if (isVq(options.dataLayout) && !isMipmapped(options.dataLayout) && base.width < 2U) {
        addError(result, 0, "A non-mipmapped VQ texture must be at least 2x2");
        return result;
    }

    std::vector<model::RgbaImage> images(mipImages.begin(), mipImages.end());
    if (isMipmapped(options.dataLayout) && images.size() == 1U && options.generateMipmaps) {
        while (images.back().width > 1U)
            images.push_back(downsample(images.back()));
    }

    if (!isMipmapped(options.dataLayout) && images.size() != 1U) {
        addError(result, 0, "A non-mipmapped PVR layout accepts exactly one image");
        return result;
    }
    if (isMipmapped(options.dataLayout)) {
        std::uint32_t expected = base.width;
        const std::size_t expectedLevels = std::bit_width(base.width);
        if (images.size() != expectedLevels) {
            addError(result, 0, "Mipmapped PVR input must contain the complete chain through 1x1");
            return result;
        }
        for (const auto& image : images) {
            if (image.width != expected || image.height != expected) {
                addError(result, 0, "PVR mip images must be supplied largest-to-smallest with halved square dimensions");
                return result;
            }
            expected = std::max<std::uint32_t>(expected / 2U, 1U);
        }
    }

    std::size_t codebookEntries = 0;
    if (options.dataLayout == DataLayout::Vq || options.dataLayout == DataLayout::VqMipmaps)
        codebookEntries = 256U;
    else if (options.dataLayout == DataLayout::SmallVq || options.dataLayout == DataLayout::SmallVqMipmaps)
        codebookEntries = smallVqCodebookEntries(base.width, isMipmapped(options.dataLayout));
    if (isVq(options.dataLayout) && codebookEntries == 0) {
        addError(result, 0, "Small VQ dimensions must be 16x16, 32x32, or 64x64");
        return result;
    }

    std::vector<std::uint8_t> payload;
    std::vector<PhysicalLevel> physical;
    std::size_t prefixSize = 0;
    if (options.dataLayout == DataLayout::TwiddledMipmaps)
        prefixSize = 2U;
    else if (options.dataLayout == DataLayout::TwiddledMipmapsDma)
        prefixSize = 6U;
    payload.resize(prefixSize, 0U);

    QuantizedCodebook codebook;
    if (isVq(options.dataLayout)) {
        const auto unique = collectUniqueVectors(images, options.pixelFormat);
        codebook = quantizeVectors(unique, codebookEntries, options.pixelFormat);
        for (const auto& vector : codebook.entries)
            for (const auto word : vector)
                appendU16(payload, word);
    }
    const auto codebookBytes = payload.size() - prefixSize;

    const auto appendDirectLevel = [&](const model::RgbaImage& image) {
        const auto levelOffset = payload.size();
        std::vector<std::uint16_t> words(static_cast<std::size_t>(image.width) * image.height);
        for (std::uint32_t y = 0; y < image.height; ++y) {
            for (std::uint32_t x = 0; x < image.width; ++x) {
                const auto pixel = (static_cast<std::size_t>(y) * image.width + x) * 4U;
                const auto destination = options.dataLayout == DataLayout::RectangleTwiddled
                    ? rectangleTwiddledIndex(x, y, image.width, image.height)
                    : (isTwiddled(options.dataLayout)
                        ? ((twiddle(x) << 1U) | twiddle(y))
                        : static_cast<std::size_t>(y) * image.width + x);
                words[destination] = packColor(image.pixels.data() + pixel, options.pixelFormat);
            }
        }
        for (const auto word : words)
            appendU16(payload, word);
        physical.push_back({&image, levelOffset, payload.size() - levelOffset});
    };

    const auto appendVqLevel = [&](const model::RgbaImage& image) {
        const auto levelOffset = payload.size();
        const auto indices = std::max<std::size_t>(static_cast<std::size_t>(image.width) * image.height / 4U, 1U);
        std::vector<std::uint8_t> stream(indices, 0U);
        if (image.width == 1U) {
            stream[0] = codebook.indices.at(makeVector(image, 0, 0, options.pixelFormat));
        } else {
            for (std::uint32_t y = 0; y < image.height; y += 2U) {
                for (std::uint32_t x = 0; x < image.width; x += 2U) {
                    const auto destination = (twiddle(x >> 1U) << 1U) | twiddle(y >> 1U);
                    stream[destination] = codebook.indices.at(makeVector(image, x, y, options.pixelFormat));
                }
            }
        }
        payload.insert(payload.end(), stream.begin(), stream.end());
        physical.push_back({&image, levelOffset, stream.size()});
    };

    if (isMipmapped(options.dataLayout)) {
        for (auto iterator = images.rbegin(); iterator != images.rend(); ++iterator) {
            if (isVq(options.dataLayout)) appendVqLevel(*iterator);
            else appendDirectLevel(*iterator);
        }
    } else {
        if (isVq(options.dataLayout)) appendVqLevel(images.front());
        else appendDirectLevel(images.front());
    }

    if (isVq(options.dataLayout) && isMipmapped(options.dataLayout) &&
        options.alignVqMipmapsTo32Bytes) {
        payload.resize((payload.size() + 31U) & ~std::size_t{31U}, 0U);
    }

    if (payload.size() > std::numeric_limits<std::uint32_t>::max() - 8U) {
        addError(result, 0, "Encoded PVR payload exceeds the 32-bit PVRT size field");
        return result;
    }

    if (options.includeGlobalIndex) {
        if (options.gbixTrailingBytes.size() > std::numeric_limits<std::uint32_t>::max() - 4U) {
            addError(result, 0, "GBIX trailing metadata exceeds the 32-bit chunk-size field");
            return result;
        }
        appendTag(result.bytes, "GBIX");
        appendU32(result.bytes, static_cast<std::uint32_t>(4U + options.gbixTrailingBytes.size()));
        appendU32(result.bytes, options.globalIndex);
        result.bytes.insert(result.bytes.end(), options.gbixTrailingBytes.begin(), options.gbixTrailingBytes.end());
        result.gbixRange = model::ByteRange{0U, 12U + options.gbixTrailingBytes.size()};
    }
    const auto pvrtOffset = result.bytes.size();
    appendTag(result.bytes, "PVRT");
    appendU32(result.bytes, static_cast<std::uint32_t>(payload.size() + 8U));
    result.bytes.push_back(static_cast<std::uint8_t>(options.pixelFormat));
    result.bytes.push_back(static_cast<std::uint8_t>(options.dataLayout));
    result.bytes.insert(result.bytes.end(), options.pvrtUnknownHeader.begin(), options.pvrtUnknownHeader.end());
    appendU16(result.bytes, static_cast<std::uint16_t>(base.width));
    appendU16(result.bytes, static_cast<std::uint16_t>(base.height));
    const auto textureDataOffset = result.bytes.size();
    result.bytes.insert(result.bytes.end(), payload.begin(), payload.end());

    result.sourceRange = {0U, result.bytes.size()};
    result.pvrtRange = {pvrtOffset, result.bytes.size() - pvrtOffset};
    result.textureDataRange = {textureDataOffset, payload.size()};
    result.codebookRange = {textureDataOffset + prefixSize, codebookBytes};
    result.mipSourceRanges.reserve(physical.size());
    if (isMipmapped(options.dataLayout)) {
        for (auto iterator = physical.rbegin(); iterator != physical.rend(); ++iterator)
            result.mipSourceRanges.push_back({textureDataOffset + iterator->offset, iterator->byteSize});
    } else {
        result.mipSourceRanges.push_back({textureDataOffset + physical.front().offset, physical.front().byteSize});
    }
    result.status = ParseStatus::Complete;
    return result;
}

} // namespace spice::pvm::encoding
