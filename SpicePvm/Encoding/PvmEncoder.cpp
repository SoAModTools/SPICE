#include "PvmEncoder.h"

#include "../Parsing/PvmParser.h"

#include <bit>
#include <limits>
#include <utility>

namespace spice::pvm::encoding {
namespace {

constexpr std::uint16_t kPvmGlobalIndex = 0x0001;
constexpr std::uint16_t kPvmDimensions = 0x0002;
constexpr std::uint16_t kPvmFormats = 0x0004;
constexpr std::uint16_t kPvmFilenames = 0x0008;
constexpr std::uint16_t kWritablePvmFlags = 0x000F;

void addError(PvmEncodeResult& result, const std::size_t offset, std::string message)
{
    result.diagnostics.push_back({model::DiagnosticSeverity::Error, offset, std::move(message)});
}

void appendU16(std::vector<std::uint8_t>& out, const std::uint16_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void appendU32(std::vector<std::uint8_t>& out, const std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

std::size_t entrySize(const std::uint16_t flags)
{
    std::size_t size = 2U;
    if ((flags & kPvmFilenames) != 0) size += 28U;
    if ((flags & kPvmFormats) != 0) size += 2U;
    if ((flags & kPvmDimensions) != 0) size += 2U;
    if ((flags & kPvmGlobalIndex) != 0) size += 4U;
    return size;
}

std::optional<std::uint8_t> dimensionNibble(const std::uint16_t value)
{
    if (value < 4U || !std::has_single_bit(value))
        return std::nullopt;
    const auto exponent = std::countr_zero(value);
    if (exponent < 2U || exponent - 2U > 0x0FU)
        return std::nullopt;
    return static_cast<std::uint8_t>(exponent - 2U);
}

} // namespace

bool PvmEncodeResult::ok() const noexcept
{
    return status == model::ParseStatus::Complete;
}

PvmEncodeResult encodePvmArchive(
    const std::span<const PvmEncodeEntry> entries,
    const PvmEncodeOptions& options)
{
    PvmEncodeResult result;
    if (entries.empty()) {
        addError(result, 0, "A PVM archive must contain at least one texture");
        return result;
    }
    if (entries.size() > std::numeric_limits<std::uint16_t>::max()) {
        addError(result, 0, "PVM texture count exceeds the 16-bit header field");
        return result;
    }
    if ((options.flags & ~kWritablePvmFlags) != 0) {
        addError(result, 0, "PVM encoding only supports the promoted 0x000F entry flags");
        return result;
    }

    struct ValidatedEntry {
        const PvmEncodeEntry* input = nullptr;
        model::PvrTexture texture;
        std::uint32_t globalIndex = 0;
        std::uint16_t dimensions = 0;
    };
    std::vector<ValidatedEntry> validated;
    validated.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& input = entries[i];
        if (input.pvrBytes.empty()) {
            addError(result, i, "PVM entry contains no encoded PVR bytes");
            continue;
        }
        auto texture = parsing::parsePvrTexture(input.pvrBytes);
        if (texture.status == model::ParseStatus::Failed || texture.sourceRange.offset != 0U ||
            texture.sourceRange.size != input.pvrBytes.size()) {
            addError(result, i, "PVM entry is not exactly one structurally valid PVR texture");
            continue;
        }
        if ((options.flags & kPvmFilenames) != 0 && input.name.size() > 27U) {
            addError(result, i, "PVM filenames must fit in 27 bytes plus a terminator");
            continue;
        }

        ValidatedEntry item;
        item.input = &input;
        item.texture = std::move(texture);
        if ((options.flags & kPvmGlobalIndex) != 0) {
            const auto global = input.globalIndex.has_value() ? input.globalIndex : item.texture.globalIndex;
            if (!global.has_value()) {
                addError(result, i, "PVM global-index metadata was requested but the entry has no global index");
                continue;
            }
            item.globalIndex = *global;
        }
        if ((options.flags & kPvmDimensions) != 0) {
            const auto width = dimensionNibble(item.texture.width);
            const auto height = dimensionNibble(item.texture.height);
            if (!width.has_value() || !height.has_value()) {
                addError(result, i, "PVM entry dimensions cannot be represented by the PVMH exponent field");
                continue;
            }
            item.dimensions = static_cast<std::uint16_t>(*width | (*height << 4U) |
                (static_cast<std::uint16_t>(input.dimensionUnknownByte) << 8U));
        }
        validated.push_back(std::move(item));
    }
    if (validated.size() != entries.size())
        return result;

    const auto recordSize = entrySize(options.flags);
    const auto tableBytes = recordSize * entries.size();
    if (tableBytes > std::numeric_limits<std::uint32_t>::max() - 4U ||
        options.headerPadding.size() > std::numeric_limits<std::uint32_t>::max() - 4U - tableBytes) {
        addError(result, 0, "PVMH chunk exceeds its 32-bit payload-size field");
        return result;
    }
    const auto pvmhPayloadSize = 4U + tableBytes + options.headerPadding.size();

    result.bytes.insert(result.bytes.end(), {'P', 'V', 'M', 'H'});
    appendU32(result.bytes, static_cast<std::uint32_t>(pvmhPayloadSize));
    appendU16(result.bytes, options.flags);
    appendU16(result.bytes, static_cast<std::uint16_t>(entries.size()));
    for (const auto& item : validated) {
        appendU16(result.bytes, item.input->archiveIndex);
        if ((options.flags & kPvmFilenames) != 0) {
            const auto begin = result.bytes.size();
            result.bytes.insert(result.bytes.end(), item.input->name.begin(), item.input->name.end());
            result.bytes.resize(begin + 28U, 0U);
        }
        if ((options.flags & kPvmFormats) != 0) {
            result.bytes.push_back(item.texture.rawPixelFormat);
            result.bytes.push_back(item.texture.rawDataLayout);
        }
        if ((options.flags & kPvmDimensions) != 0)
            appendU16(result.bytes, item.dimensions);
        if ((options.flags & kPvmGlobalIndex) != 0)
            appendU32(result.bytes, item.globalIndex);
    }
    result.bytes.insert(result.bytes.end(), options.headerPadding.begin(), options.headerPadding.end());
    result.pvmhRange = {0U, result.bytes.size()};
    result.bytes.insert(result.bytes.end(), options.interstitialMetadata.begin(), options.interstitialMetadata.end());

    result.textureRanges.reserve(entries.size());
    for (const auto& entry : entries) {
        result.textureRanges.push_back({result.bytes.size(), entry.pvrBytes.size()});
        result.bytes.insert(result.bytes.end(), entry.pvrBytes.begin(), entry.pvrBytes.end());
    }
    result.status = model::ParseStatus::Complete;
    return result;
}

} // namespace spice::pvm::encoding
