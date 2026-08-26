#include "PvmParser.h"

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <string_view>

namespace spice::pvm::parsing {
namespace {

using model::Diagnostic;
using model::DiagnosticSeverity;
using model::ParseStatus;

constexpr std::uint16_t kPvmGlobalIndex = 0x0001;
constexpr std::uint16_t kPvmDimensions = 0x0002;
constexpr std::uint16_t kPvmFormats = 0x0004;
constexpr std::uint16_t kPvmFilenames = 0x0008;
constexpr std::uint16_t kKnownPvmFlags = 0x011F;

bool hasBytes(const std::span<const std::uint8_t> bytes, const std::size_t offset, const std::size_t count)
{
    return offset <= bytes.size() && count <= bytes.size() - offset;
}

bool checkedAdd(const std::size_t left, const std::size_t right, std::size_t& result)
{
    if (right > std::numeric_limits<std::size_t>::max() - left)
        return false;
    result = left + right;
    return true;
}

bool isTag(const std::span<const std::uint8_t> bytes, const std::size_t offset, const std::string_view tag)
{
    return tag.size() == 4 && hasBytes(bytes, offset, 4) &&
        std::equal(tag.begin(), tag.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

std::uint16_t readU16(const std::span<const std::uint8_t> bytes, const std::size_t offset)
{
    return static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
}

std::uint32_t readU32(const std::span<const std::uint8_t> bytes, const std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

void addDiagnostic(
    std::vector<Diagnostic>& diagnostics,
    const DiagnosticSeverity severity,
    const std::size_t offset,
    std::string message)
{
    diagnostics.push_back({severity, offset, std::move(message)});
}

bool hasErrors(const std::vector<Diagnostic>& diagnostics)
{
    return std::ranges::any_of(diagnostics, [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

bool hasWarnings(const std::vector<Diagnostic>& diagnostics)
{
    return std::ranges::any_of(diagnostics, [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Warning;
    });
}

std::vector<std::uint8_t> copyRange(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset,
    const std::size_t size)
{
    if (!hasBytes(bytes, offset, size))
        return {};
    return {bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(offset + size)};
}

std::size_t pvmEntrySize(const std::uint16_t flags)
{
    std::size_t size = 2;
    if ((flags & kPvmFilenames) != 0) size += 28;
    if ((flags & kPvmFormats) != 0) size += 2;
    if ((flags & kPvmDimensions) != 0) size += 2;
    if ((flags & kPvmGlobalIndex) != 0) size += 4;
    return size;
}

std::optional<std::uint16_t> dimensionFromNibble(const std::uint8_t nibble)
{
    const unsigned exponent = static_cast<unsigned>(nibble) + 2U;
    if (exponent >= 16U)
        return std::nullopt;
    return static_cast<std::uint16_t>(1U << exponent);
}

void finalizeTextureStatus(model::PvrTexture& texture)
{
    if (hasErrors(texture.diagnostics))
        texture.status = ParseStatus::Failed;
    else if (hasWarnings(texture.diagnostics))
        texture.status = ParseStatus::Partial;
    else
        texture.status = ParseStatus::Complete;
}

} // namespace

model::PvrTexture parsePvrTexture(
    const std::span<const std::uint8_t> bytes,
    const std::size_t sourceOffset)
{
    model::PvrTexture texture;
    texture.sourceRange.offset = sourceOffset;

    if (!hasBytes(bytes, sourceOffset, 4)) {
        addDiagnostic(texture.diagnostics, DiagnosticSeverity::Error, sourceOffset,
            "PVR input is truncated before a chunk identifier");
        return texture;
    }

    std::size_t pvrtOffset = sourceOffset;
    if (isTag(bytes, sourceOffset, "GBIX")) {
        if (!hasBytes(bytes, sourceOffset, 8)) {
            addDiagnostic(texture.diagnostics, DiagnosticSeverity::Error, sourceOffset,
                "GBIX header is truncated");
            return texture;
        }

        const std::size_t gbixPayloadSize = readU32(bytes, sourceOffset + 4);
        std::size_t gbixSize = 0;
        if (!checkedAdd(8, gbixPayloadSize, gbixSize) || !hasBytes(bytes, sourceOffset, gbixSize)) {
            addDiagnostic(texture.diagnostics, DiagnosticSeverity::Error, sourceOffset + 4,
                "GBIX chunk size exceeds the available input");
            return texture;
        }
        texture.gbixRange = model::ByteRange{sourceOffset, gbixSize};
        if (gbixPayloadSize >= 4)
            texture.globalIndex = readU32(bytes, sourceOffset + 8);
        else
            addDiagnostic(texture.diagnostics, DiagnosticSeverity::Warning, sourceOffset + 4,
                "GBIX payload is too small to contain a global index");
        if (!checkedAdd(sourceOffset, gbixSize, pvrtOffset)) {
            addDiagnostic(texture.diagnostics, DiagnosticSeverity::Error, sourceOffset + 4,
                "GBIX end offset overflows addressable input");
            return texture;
        }
    }

    if (!isTag(bytes, pvrtOffset, "PVRT")) {
        addDiagnostic(texture.diagnostics, DiagnosticSeverity::Error, pvrtOffset,
            texture.gbixRange.has_value() ? "GBIX is not immediately followed by PVRT" :
                                            "Expected a PVRT chunk");
        return texture;
    }
    if (!hasBytes(bytes, pvrtOffset, 16)) {
        addDiagnostic(texture.diagnostics, DiagnosticSeverity::Error, pvrtOffset,
            "PVRT header is truncated");
        return texture;
    }

    const std::size_t pvrtPayloadSize = readU32(bytes, pvrtOffset + 4);
    std::size_t pvrtSize = 0;
    if (!checkedAdd(8, pvrtPayloadSize, pvrtSize) || !hasBytes(bytes, pvrtOffset, pvrtSize)) {
        addDiagnostic(texture.diagnostics, DiagnosticSeverity::Error, pvrtOffset + 4,
            "PVRT chunk size exceeds the available input");
        return texture;
    }
    if (pvrtPayloadSize < 8) {
        addDiagnostic(texture.diagnostics, DiagnosticSeverity::Error, pvrtOffset + 4,
            "PVRT payload is smaller than its eight-byte texture header");
        return texture;
    }

    texture.pvrtRange = {pvrtOffset, pvrtSize};
    texture.rawPixelFormat = bytes[pvrtOffset + 8];
    texture.rawDataLayout = bytes[pvrtOffset + 9];
    texture.pixelFormat = model::pixelFormatFromRaw(texture.rawPixelFormat);
    texture.dataLayout = model::dataLayoutFromRaw(texture.rawDataLayout);
    texture.pvrtUnknownHeader = copyRange(bytes, pvrtOffset + 10, 2);
    texture.width = readU16(bytes, pvrtOffset + 12);
    texture.height = readU16(bytes, pvrtOffset + 14);
    texture.textureDataRange = {pvrtOffset + 16, pvrtPayloadSize - 8};

    std::size_t sourceEnd = 0;
    if (!checkedAdd(pvrtOffset, pvrtSize, sourceEnd)) {
        addDiagnostic(texture.diagnostics, DiagnosticSeverity::Error, pvrtOffset + 4,
            "PVRT end offset overflows addressable input");
        return texture;
    }
    texture.sourceRange.size = sourceEnd - sourceOffset;
    texture.sourceBytes = copyRange(bytes, sourceOffset, texture.sourceRange.size);

    if (texture.width == 0 || texture.height == 0)
        addDiagnostic(texture.diagnostics, DiagnosticSeverity::Error, pvrtOffset + 12,
            "PVRT dimensions must be non-zero");
    if (texture.pixelFormat == model::PixelFormat::Unknown)
        addDiagnostic(texture.diagnostics, DiagnosticSeverity::Warning, pvrtOffset + 8,
            "PVRT pixel format is retained but is not supported for decoding");
    if (texture.dataLayout == model::DataLayout::Unknown)
        addDiagnostic(texture.diagnostics, DiagnosticSeverity::Warning, pvrtOffset + 9,
            "PVRT data layout is retained but is not supported for decoding");

    finalizeTextureStatus(texture);
    return texture;
}

model::PvrScanResult scanPvrTextures(
    const std::span<const std::uint8_t> bytes,
    const std::size_t startOffset,
    const std::optional<std::size_t> expectedCount)
{
    model::PvrScanResult result;
    if (startOffset > bytes.size()) {
        addDiagnostic(result.diagnostics, DiagnosticSeverity::Error, startOffset,
            "PVR scan start offset is beyond the input");
        return result;
    }

    std::size_t cursor = startOffset;
    while (hasBytes(bytes, cursor, 4)) {
        if (isTag(bytes, cursor, "GBIX") || isTag(bytes, cursor, "PVRT")) {
            auto texture = parsePvrTexture(bytes, cursor);
            if (texture.status != ParseStatus::Failed) {
                const std::size_t next = texture.sourceRange.end();
                result.textures.push_back(std::move(texture));
                cursor = next > cursor ? next : cursor + 1;
                continue;
            }
        }
        ++cursor;
    }

    if (expectedCount.has_value() && result.textures.size() != *expectedCount) {
        addDiagnostic(result.diagnostics, DiagnosticSeverity::Error, startOffset,
            "PVR scan found " + std::to_string(result.textures.size()) +
            " texture(s), expected " + std::to_string(*expectedCount));
    }
    if (result.textures.empty()) {
        addDiagnostic(result.diagnostics, DiagnosticSeverity::Error, startOffset,
            "PVR scan did not find a structurally valid texture");
    }

    if (hasErrors(result.diagnostics))
        result.status = result.textures.empty() ? ParseStatus::Failed : ParseStatus::Partial;
    else if (std::ranges::any_of(result.textures, [](const model::PvrTexture& texture) {
                 return texture.status != ParseStatus::Complete;
             }))
        result.status = ParseStatus::Partial;
    else
        result.status = ParseStatus::Complete;
    return result;
}

model::PvmArchive parsePvmArchive(
    const std::span<const std::uint8_t> bytes,
    const std::size_t sourceOffset)
{
    model::PvmArchive archive;
    archive.sourceRange.offset = sourceOffset;

    if (!isTag(bytes, sourceOffset, "PVMH")) {
        addDiagnostic(archive.diagnostics, DiagnosticSeverity::Error, sourceOffset,
            "Expected a PVMH archive header");
        return archive;
    }
    if (!hasBytes(bytes, sourceOffset, 12)) {
        addDiagnostic(archive.diagnostics, DiagnosticSeverity::Error, sourceOffset,
            "PVMH header is truncated");
        return archive;
    }

    const std::size_t payloadSize = readU32(bytes, sourceOffset + 4);
    std::size_t pvmhSize = 0;
    if (!checkedAdd(8, payloadSize, pvmhSize) || !hasBytes(bytes, sourceOffset, pvmhSize)) {
        addDiagnostic(archive.diagnostics, DiagnosticSeverity::Error, sourceOffset + 4,
            "PVMH chunk size exceeds the available input");
        return archive;
    }
    if (payloadSize < 4) {
        addDiagnostic(archive.diagnostics, DiagnosticSeverity::Error, sourceOffset + 4,
            "PVMH payload is smaller than its flags and count fields");
        return archive;
    }

    archive.pvmhRange = {sourceOffset, pvmhSize};
    archive.flags = readU16(bytes, sourceOffset + 8);
    archive.declaredTextureCount = readU16(bytes, sourceOffset + 10);
    if ((archive.flags & ~kKnownPvmFlags) != 0)
        addDiagnostic(archive.diagnostics, DiagnosticSeverity::Warning, sourceOffset + 8,
            "PVMH contains unknown flag bits that were preserved");

    const std::size_t entrySize = pvmEntrySize(archive.flags);
    std::size_t tableBytes = 0;
    if (archive.declaredTextureCount != 0 &&
        entrySize > std::numeric_limits<std::size_t>::max() / archive.declaredTextureCount) {
        addDiagnostic(archive.diagnostics, DiagnosticSeverity::Error, sourceOffset + 10,
            "PVMH entry table size overflows addressable input");
        return archive;
    }
    tableBytes = entrySize * archive.declaredTextureCount;
    const std::size_t tableOffset = sourceOffset + 12;
    const std::size_t pvmhEnd = archive.pvmhRange.end();
    if (tableOffset > pvmhEnd || tableBytes > pvmhEnd - tableOffset) {
        addDiagnostic(archive.diagnostics, DiagnosticSeverity::Error, sourceOffset + 10,
            "PVMH texture count does not fit inside the declared header chunk");
        return archive;
    }

    archive.entries.reserve(archive.declaredTextureCount);
    std::size_t cursor = tableOffset;
    for (std::size_t i = 0; i < archive.declaredTextureCount; ++i) {
        model::PvmEntry entry;
        entry.metadataRange = {cursor, entrySize};
        entry.archiveIndex = readU16(bytes, cursor);
        cursor += 2;

        if ((archive.flags & kPvmFilenames) != 0) {
            entry.rawName = copyRange(bytes, cursor, 28);
            const auto terminator = std::ranges::find(entry.rawName, std::uint8_t{0});
            entry.name.assign(entry.rawName.begin(), terminator);
            cursor += 28;
        }
        if ((archive.flags & kPvmFormats) != 0) {
            entry.rawPixelFormat = bytes[cursor++];
            entry.rawDataLayout = bytes[cursor++];
        }
        if ((archive.flags & kPvmDimensions) != 0) {
            entry.rawDimensions = readU16(bytes, cursor);
            const auto width = dimensionFromNibble(static_cast<std::uint8_t>(*entry.rawDimensions & 0x0F));
            const auto height = dimensionFromNibble(static_cast<std::uint8_t>((*entry.rawDimensions >> 4) & 0x0F));
            entry.declaredWidth = width;
            entry.declaredHeight = height;
            if (!width.has_value() || !height.has_value())
                addDiagnostic(entry.diagnostics, DiagnosticSeverity::Warning, cursor,
                    "PVMH dimension exponent cannot be represented as a 16-bit dimension");
            const std::uint8_t unknownDimensionByte = bytes[cursor + 1];
            if (unknownDimensionByte != 0)
                entry.unknownMetadata.push_back(unknownDimensionByte);
            cursor += 2;
        }
        if ((archive.flags & kPvmGlobalIndex) != 0) {
            entry.globalIndex = readU32(bytes, cursor);
            cursor += 4;
        }
        archive.entries.push_back(std::move(entry));
    }

    archive.headerPadding = copyRange(bytes, cursor, pvmhEnd - cursor);
    auto scan = scanPvrTextures(bytes, pvmhEnd, archive.declaredTextureCount);
    for (const auto& diagnostic : scan.diagnostics)
        archive.diagnostics.push_back(diagnostic);

    const std::size_t pairedCount = std::min(archive.entries.size(), scan.textures.size());
    for (std::size_t i = 0; i < pairedCount; ++i) {
        auto& entry = archive.entries[i];
        auto& texture = scan.textures[i];
        if (entry.archiveIndex != i)
            addDiagnostic(entry.diagnostics, DiagnosticSeverity::Warning, entry.metadataRange.offset,
                "PVMH archive index is not sequential");
        if (entry.rawPixelFormat.has_value() && *entry.rawPixelFormat != texture.rawPixelFormat)
            addDiagnostic(entry.diagnostics, DiagnosticSeverity::Error, entry.metadataRange.offset,
                "PVMH pixel format does not match the paired PVRT texture");
        if (entry.rawDataLayout.has_value() && *entry.rawDataLayout != texture.rawDataLayout)
            addDiagnostic(entry.diagnostics, DiagnosticSeverity::Error, entry.metadataRange.offset,
                "PVMH data layout does not match the paired PVRT texture");
        if (entry.declaredWidth.has_value() && *entry.declaredWidth != texture.width)
            addDiagnostic(entry.diagnostics, DiagnosticSeverity::Error, entry.metadataRange.offset,
                "PVMH width does not match the paired PVRT texture");
        if (entry.declaredHeight.has_value() && *entry.declaredHeight != texture.height)
            addDiagnostic(entry.diagnostics, DiagnosticSeverity::Error, entry.metadataRange.offset,
                "PVMH height does not match the paired PVRT texture");
        if (entry.globalIndex.has_value() && texture.globalIndex.has_value() &&
            *entry.globalIndex != *texture.globalIndex)
            addDiagnostic(entry.diagnostics, DiagnosticSeverity::Error, entry.metadataRange.offset,
                "PVMH global index does not match the paired GBIX chunk");
        entry.texture = std::move(texture);
    }
    for (std::size_t i = pairedCount; i < scan.textures.size(); ++i)
        archive.unpairedTextures.push_back(std::move(scan.textures[i]));

    if (pairedCount != 0 || !archive.unpairedTextures.empty()) {
        const std::size_t firstTextureOffset = pairedCount != 0
            ? archive.entries.front().texture->sourceRange.offset
            : archive.unpairedTextures.front().sourceRange.offset;
        archive.interstitialMetadata = copyRange(bytes, pvmhEnd, firstTextureOffset - pvmhEnd);
    }

    std::size_t archiveEnd = pvmhEnd;
    for (const auto& entry : archive.entries) {
        if (entry.texture.has_value())
            archiveEnd = std::max(archiveEnd, entry.texture->sourceRange.end());
        if (hasErrors(entry.diagnostics))
            addDiagnostic(archive.diagnostics, DiagnosticSeverity::Error, entry.metadataRange.offset,
                "One or more PVMH entry fields do not match the paired texture");
    }
    for (const auto& texture : archive.unpairedTextures)
        archiveEnd = std::max(archiveEnd, texture.sourceRange.end());
    archive.sourceRange.size = archiveEnd - sourceOffset;
    archive.sourceBytes = copyRange(bytes, sourceOffset, archive.sourceRange.size);

    if (hasErrors(archive.diagnostics))
        archive.status = archive.entries.empty() ? ParseStatus::Failed : ParseStatus::Partial;
    else if (hasWarnings(archive.diagnostics) ||
        std::ranges::any_of(archive.entries, [](const model::PvmEntry& entry) {
            return hasWarnings(entry.diagnostics);
        }))
        archive.status = ParseStatus::Partial;
    else
        archive.status = ParseStatus::Complete;
    return archive;
}

} // namespace spice::pvm::parsing
