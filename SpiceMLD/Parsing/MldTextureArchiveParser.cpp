#include "MldTextureArchiveParser.h"

#include "../../SpiceGvm/SpiceGvm.h"

#include "../../SpiceCore/Binary/EndianReader.h"
#include "../common/ByteUtils.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace spice::mld::parsing {
namespace {

[[nodiscard]] std::string readFixedAsciiName(std::span<const std::uint8_t> bytes,
    const std::size_t offset,
    const std::size_t maxLength) {
    std::string out{};
    if (offset >= bytes.size()) {
        return out;
    }

    const auto end = std::min(bytes.size(), offset + maxLength);
    for (std::size_t i = offset; i < end; ++i) {
        const auto ch = bytes[i];
        if (ch == 0U) {
            break;
        }
        if (std::isprint(static_cast<unsigned char>(ch)) == 0) {
            break;
        }
        out.push_back(static_cast<char>(ch));
    }
    return out;
}

[[nodiscard]] std::vector<std::string> parseTextureArchiveNames(std::span<const std::uint8_t> bytes,
    const std::size_t textureTableOffset,
    const spice::core::Endian endian,
    std::vector<std::string>& diagnostics) {
    std::vector<std::string> names{};
    const spice::core::EndianReader reader(bytes, endian);
    const auto count = reader.try_read_u32(textureTableOffset);
    if (!count.has_value()) {
        diagnostics.push_back("Texture archive name table count is unreadable.");
        return names;
    }

    constexpr std::size_t recordSize = 44U;
    constexpr std::size_t nameSize = 32U;
    constexpr std::size_t hardCap = 4096U;
    if (*count == 0U || *count > hardCap) {
        diagnostics.push_back("Texture archive name table count is suspicious: " + std::to_string(*count) + ".");
        return names;
    }

    const std::size_t tableBegin = textureTableOffset + 4U;
    const std::size_t requiredEnd = tableBegin + (static_cast<std::size_t>(*count) * recordSize);
    if (requiredEnd > bytes.size()) {
        diagnostics.push_back("Texture archive name table overruns file bounds.");
        return names;
    }

    names.reserve(*count);
    for (std::uint32_t i = 0; i < *count; ++i) {
        names.push_back(readFixedAsciiName(bytes, tableBegin + (static_cast<std::size_t>(i) * recordSize), nameSize));
    }
    diagnostics.push_back("Texture archive name table extracted " + std::to_string(names.size()) + " texture name(s).");
    return names;
}

} // namespace

model::MldTextureArchive parseMldTextureArchive(std::span<const std::uint8_t> bytes,
    const std::size_t textureTableOffset,
    const spice::core::Endian endian) {
    model::MldTextureArchive out{};
    out.tableOffset = textureTableOffset;
    out.archiveStartOffset = textureTableOffset;
    out.archiveEndOffset = textureTableOffset;
    const auto archiveNames = parseTextureArchiveNames(bytes, textureTableOffset, endian, out.diagnostics);

    spice::gvm::parsing::ParseOptions options{};
    options.decodeBaseLevel = true;
    options.keepRawEncodedPayload = false;
    auto archive = spice::gvm::parsing::parseGvmArchive(bytes, textureTableOffset, options);

    out.diagnostics.insert(out.diagnostics.end(), archive.diagnostics.begin(), archive.diagnostics.end());
    if (!archive.textures.empty()) {
        const auto firstTexture = std::min_element(archive.textures.begin(), archive.textures.end(),
            [](const spice::gvm::model::GvrTexture& left, const spice::gvm::model::GvrTexture& right) {
                return left.sourceOffset < right.sourceOffset;
            });
        const auto firstTextureOffset = firstTexture->sourceOffset;
        if (firstTextureOffset >= textureTableOffset && firstTextureOffset <= bytes.size()) {
            out.archivePrefixBytes.assign(
                bytes.begin() + static_cast<std::ptrdiff_t>(textureTableOffset),
                bytes.begin() + static_cast<std::ptrdiff_t>(firstTextureOffset));
        }
        for (const auto& texture : archive.textures) {
            if (texture.sourceOffset <= bytes.size()) {
                out.archiveEndOffset = std::max(out.archiveEndOffset,
                    std::min(bytes.size(), texture.sourceOffset + texture.sourceSize));
            }
        }
    } else {
        const spice::core::EndianReader reader(bytes, endian);
        const auto count = reader.try_read_u32(textureTableOffset).value_or(0U);
        constexpr std::size_t recordSize = 44U;
        const auto prefixSize = 4U + static_cast<std::size_t>(count) * recordSize;
        if (textureTableOffset <= bytes.size() && prefixSize <= bytes.size() - textureTableOffset) {
            out.archivePrefixBytes.assign(
                bytes.begin() + static_cast<std::ptrdiff_t>(textureTableOffset),
                bytes.begin() + static_cast<std::ptrdiff_t>(textureTableOffset + prefixSize));
            out.archiveEndOffset = textureTableOffset + prefixSize;
        }
    }

    out.entries.reserve(archive.textures.size());
    for (std::size_t i = 0; i < archive.textures.size(); ++i) {
        const auto& texture = archive.textures[i];

        model::MldTextureEntry entry{};
        entry.archiveTextureIndex = static_cast<std::uint32_t>(i);
        entry.archiveOffset = texture.sourceOffset;
        entry.gvrDataOffset = texture.sourceOffset;
        entry.gvrDataSize = texture.sourceSize;
        entry.hasGlobalIndex = texture.hasGlobalIndex;
        entry.globalIndex = texture.globalIndex;
        if (i < archiveNames.size()) {
            entry.textureName = archiveNames[i];
        }
        entry.pixelFormat = texture.rawFlags;
        entry.dataFormat = texture.rawDataFormat;
        entry.sourceFormat = spice::gvm::model::to_string(texture.textureFormat);
        entry.sourcePaletteFormat = spice::gvm::model::to_string(texture.paletteFormat);
        entry.hasMipmaps = texture.hasMipmaps;
        entry.hasInternalPalette = texture.hasInternalPalette;
        entry.width = texture.width;
        entry.height = texture.height;
        entry.imageDataOffset = texture.imageDataOffset;
        entry.imageDataSize = texture.imageDataSize;
        entry.paletteDataSize = texture.paletteData.size();
        if (texture.sourceOffset < bytes.size()) {
            const auto safeSize = std::min(texture.sourceSize, bytes.size() - texture.sourceOffset);
            entry.gvrData.assign(
                bytes.begin() + static_cast<std::ptrdiff_t>(texture.sourceOffset),
                bytes.begin() + static_cast<std::ptrdiff_t>(texture.sourceOffset + safeSize));
        }
        entry.diagnostics = texture.diagnostics;

        if (texture.decodedBaseLevel.has_value()) {
            entry.decoded = !texture.decodedBaseLevel->rgba8.empty();
            entry.width = static_cast<std::uint16_t>(texture.decodedBaseLevel->width);
            entry.height = static_cast<std::uint16_t>(texture.decodedBaseLevel->height);
            entry.rgba8 = texture.decodedBaseLevel->rgba8;
        }

        out.entries.push_back(std::move(entry));
    }

    out.diagnostics.push_back("Texture archive parse extracted " + std::to_string(out.entries.size()) + " GVR texture chunk(s).");
    return out;
}

} // namespace spice::mld::parsing
