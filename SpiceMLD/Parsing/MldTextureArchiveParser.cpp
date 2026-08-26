#include "MldTextureArchiveParser.h"

#include "../../SpiceGvm/SpiceGvm.h"
#include "../../SpicePvm/SpicePvm.h"
#include "../../SpiceCore/Binary/EndianReader.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>

namespace spice::mld::parsing {
namespace {

constexpr std::size_t kRecordSize = 44U;
constexpr std::size_t kNameSize = 32U;
constexpr std::size_t kHardCap = 4096U;
constexpr std::uint32_t kDreamcastAlignFlag = 0x80000000U;

std::string readFixedAsciiName(std::span<const std::uint8_t> bytes,
    const std::size_t offset, const std::size_t maxLength)
{
    std::string out;
    if (offset >= bytes.size()) return out;
    const auto end = std::min(bytes.size(), offset + maxLength);
    for (std::size_t i = offset; i < end; ++i) {
        const auto ch = bytes[i];
        if (ch == 0U || std::isprint(static_cast<unsigned char>(ch)) == 0) break;
        out.push_back(static_cast<char>(ch));
    }
    return out;
}

bool isMipmapped(const spice::pvm::model::DataLayout layout)
{
    using spice::pvm::model::DataLayout;
    return layout == DataLayout::TwiddledMipmaps || layout == DataLayout::VqMipmaps ||
        layout == DataLayout::SmallVqMipmaps || layout == DataLayout::TwiddledMipmapsDma;
}

void copyBytes(std::vector<std::uint8_t>& destination,
    const std::span<const std::uint8_t> bytes, const std::size_t begin, const std::size_t end)
{
    if (begin <= end && end <= bytes.size())
        destination.assign(bytes.begin() + static_cast<std::ptrdiff_t>(begin),
            bytes.begin() + static_cast<std::ptrdiff_t>(end));
}

bool initializeRecords(model::MldTextureArchive& out,
    const std::span<const std::uint8_t> bytes, const std::size_t textureTableOffset,
    const spice::core::Endian endian, std::size_t& tableEnd)
{
    const spice::core::EndianReader reader(bytes, endian);
    const auto count = reader.try_read_u32(textureTableOffset);
    if (!count.has_value()) {
        out.diagnostics.push_back("Texture archive record count is unreadable.");
        return false;
    }
    if (*count > kHardCap) {
        out.diagnostics.push_back("Texture archive record count exceeds the safety cap.");
        return false;
    }
    if (*count > (std::numeric_limits<std::size_t>::max() - 4U) / kRecordSize) {
        out.diagnostics.push_back("Texture archive record table size overflows addressable input.");
        return false;
    }
    const auto tableSize = 4U + static_cast<std::size_t>(*count) * kRecordSize;
    if (textureTableOffset > bytes.size() || tableSize > bytes.size() - textureTableOffset) {
        out.diagnostics.push_back("Texture archive record table overruns file bounds.");
        return false;
    }
    tableEnd = textureTableOffset + tableSize;
    copyBytes(out.archivePrefixBytes, bytes, textureTableOffset, tableEnd);
    out.archiveEndOffset = tableEnd;
    out.entries.resize(*count);
    for (std::size_t i = 0; i < out.entries.size(); ++i) {
        auto& entry = out.entries[i];
        const auto record = textureTableOffset + 4U + i * kRecordSize;
        entry.archiveTextureIndex = static_cast<std::uint32_t>(i);
        entry.textureName = readFixedAsciiName(bytes, record, kNameSize);
        entry.rawRecordWord0 = reader.try_read_u32(record + 32U).value_or(0U);
        entry.rawRecordWord1 = reader.try_read_u32(record + 36U).value_or(0U);
        entry.declaredBlockSize = reader.try_read_u32(record + 40U).value_or(0U);
    }
    return true;
}

void populateGvrArchive(model::MldTextureArchive& out, const std::span<const std::uint8_t> bytes,
    const std::size_t textureTableOffset, const std::size_t tableEnd)
{
    spice::gvm::parsing::ParseOptions options{};
    options.decodeBaseLevel = true;
    options.keepRawEncodedPayload = false;
    auto archive = spice::gvm::parsing::parseGvmArchive(bytes, textureTableOffset, options);
    out.diagnostics.insert(out.diagnostics.end(), archive.diagnostics.begin(), archive.diagnostics.end());

    std::size_t cursor = tableEnd;
    const auto count = std::min(out.entries.size(), archive.textures.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto& texture = archive.textures[i];
        auto& entry = out.entries[i];
        entry.encoding = model::MldTextureEncoding::Gvr;
        entry.archiveOffset = std::min(cursor, texture.sourceOffset);
        entry.encodedDataOffset = texture.sourceOffset;
        entry.encodedDataSize = texture.sourceSize;
        if (cursor <= texture.sourceOffset)
            copyBytes(entry.alignmentPrefixBytes, bytes, cursor, texture.sourceOffset);
        entry.hasGlobalIndex = texture.hasGlobalIndex;
        entry.globalIndex = texture.globalIndex;
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
        copyBytes(entry.encodedData, bytes, texture.sourceOffset,
            std::min(bytes.size(), texture.sourceOffset + texture.sourceSize));
        entry.diagnostics = texture.diagnostics;
        if (texture.decodedBaseLevel.has_value()) {
            entry.decoded = !texture.decodedBaseLevel->rgba8.empty();
            entry.width = static_cast<std::uint16_t>(texture.decodedBaseLevel->width);
            entry.height = static_cast<std::uint16_t>(texture.decodedBaseLevel->height);
            entry.rgba8 = texture.decodedBaseLevel->rgba8;
        }
        cursor = std::max(cursor, texture.sourceOffset + texture.sourceSize);
        out.archiveEndOffset = std::max(out.archiveEndOffset, cursor);
    }
    if (archive.textures.size() != out.entries.size())
        out.diagnostics.push_back("Texture record count does not match the number of GVR chunks.");
    out.diagnostics.push_back("Texture archive parse extracted " + std::to_string(count) + " GVR texture chunk(s).");
}

void populatePvrArchive(model::MldTextureArchive& out, const std::span<const std::uint8_t> bytes,
    const std::size_t tableEnd)
{
    std::size_t cursor = tableEnd;
    std::size_t parsedCount = 0;
    for (auto& entry : out.entries) {
        entry.encoding = model::MldTextureEncoding::Pvr;
        entry.archiveOffset = cursor;
        const auto blockSize = static_cast<std::size_t>(entry.declaredBlockSize);
        if (blockSize == 0U || cursor > bytes.size()) {
            entry.diagnostics.push_back("Dreamcast texture record block size is zero or outside file bounds.");
            out.diagnostics.push_back("Dreamcast texture archive contains an invalid record block size.");
            break;
        }

        std::size_t prefixSize = 0U;
        if (entry.rawRecordWord1 == kDreamcastAlignFlag)
            prefixSize = (32U - (cursor & 31U)) & 31U;
        else if (entry.rawRecordWord1 != 0U)
            entry.diagnostics.push_back("Dreamcast texture record has an unknown alignment control word.");
        if (prefixSize > bytes.size() - cursor || blockSize > bytes.size() - cursor - prefixSize) {
            entry.diagnostics.push_back("Dreamcast texture alignment prefix and declared block exceed file bounds.");
            break;
        }

        const auto textureOffset = cursor + prefixSize;
        const auto blockEnd = textureOffset + blockSize;
        copyBytes(entry.alignmentPrefixBytes, bytes, cursor, textureOffset);
        auto texture = spice::pvm::parsing::parsePvrTexture(bytes, textureOffset);
        if (texture.status == spice::pvm::model::ParseStatus::Failed ||
            texture.sourceRange.end() > blockEnd) {
            entry.diagnostics.push_back("Dreamcast texture record does not contain a bounded PVR texture at its expected offset.");
            out.diagnostics.push_back("Dreamcast texture archive contains an undecodable record.");
            cursor = blockEnd;
            out.archiveEndOffset = std::max(out.archiveEndOffset, cursor);
            continue;
        }

        entry.encodedDataOffset = texture.sourceRange.offset;
        entry.encodedDataSize = texture.sourceRange.size;
        copyBytes(entry.encodedData, bytes, texture.sourceRange.offset, texture.sourceRange.end());
        copyBytes(entry.trailingBlockBytes, bytes, texture.sourceRange.end(), blockEnd);
        entry.hasGlobalIndex = texture.globalIndex.has_value();
        entry.globalIndex = texture.globalIndex.value_or(0U);
        entry.pixelFormat = texture.rawPixelFormat;
        entry.dataFormat = texture.rawDataLayout;
        entry.sourceFormat = spice::pvm::model::toString(texture.pixelFormat);
        entry.sourcePaletteFormat = spice::pvm::model::toString(texture.dataLayout);
        entry.hasMipmaps = isMipmapped(texture.dataLayout);
        entry.hasInternalPalette = false;
        entry.width = texture.width;
        entry.height = texture.height;
        entry.imageDataOffset = texture.textureDataRange.offset;
        entry.imageDataSize = texture.textureDataRange.size;
        for (const auto& diagnostic : texture.diagnostics)
            entry.diagnostics.push_back(diagnostic.message);

        const auto decoded = spice::pvm::decoding::decodePvrTexture(texture);
        for (const auto& diagnostic : decoded.diagnostics)
            entry.diagnostics.push_back(diagnostic.message);
        if (decoded.status != spice::pvm::model::ParseStatus::Failed && !decoded.mipLevels.empty()) {
            entry.decoded = true;
            entry.rgba8 = decoded.mipLevels.front().image.pixels;
        }
        ++parsedCount;
        cursor = blockEnd;
        out.archiveEndOffset = std::max(out.archiveEndOffset, cursor);
    }
    out.diagnostics.push_back("Texture archive parse extracted " + std::to_string(parsedCount) + " PVR texture chunk(s).");
}

} // namespace

model::MldTextureArchive parseMldTextureArchive(const std::span<const std::uint8_t> bytes,
    const std::size_t textureTableOffset, const spice::core::Endian endian)
{
    model::MldTextureArchive out;
    out.tableOffset = textureTableOffset;
    out.archiveStartOffset = textureTableOffset;
    out.archiveEndOffset = textureTableOffset;
    std::size_t tableEnd = textureTableOffset;
    if (!initializeRecords(out, bytes, textureTableOffset, endian, tableEnd))
        return out;
    if (endian == spice::core::Endian::Little)
        populatePvrArchive(out, bytes, tableEnd);
    else
        populateGvrArchive(out, bytes, textureTableOffset, tableEnd);
    return out;
}

} // namespace spice::mld::parsing
