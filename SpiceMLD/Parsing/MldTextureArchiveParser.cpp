#include "MldTextureArchiveParser.h"

#include "../../SpiceGvm/SpiceGvm.h"
#include "../../SpicePvm/SpicePvm.h"
#include "../../SpiceRoot/Binary/EndianReader.h"

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

void addDiagnostic(std::vector<model::MldDiagnostic>& diagnostics,
    const model::MldDiagnostic::Severity severity,
    std::string message,
    const std::size_t offset,
    const model::MldResourceKind kind = model::MldResourceKind::TextureArchive)
{
    diagnostics.push_back(model::MldDiagnostic{
        .severity = severity,
        .message = std::move(message),
        .sourceOffset = static_cast<std::uint32_t>(std::min<std::size_t>(offset, UINT32_MAX)),
        .scope = model::MldDiagnosticScope::Resource,
        .resourceKind = kind,
    });
}

model::MldDiagnostic::Severity convertSeverity(
    const spice::pvm::model::DiagnosticSeverity severity)
{
    switch (severity) {
    case spice::pvm::model::DiagnosticSeverity::Information:
        return model::MldDiagnostic::Severity::Info;
    case spice::pvm::model::DiagnosticSeverity::Warning:
        return model::MldDiagnostic::Severity::Warning;
    default:
        return model::MldDiagnostic::Severity::Error;
    }
}

void finalizeArchiveStatus(model::MldTextureArchive& archive)
{
    if (archive.status == model::MldResourceStatus::Failed)
        return;
    if (archive.entries.empty()) {
        archive.status = model::MldResourceStatus::Empty;
        return;
    }
    archive.status = model::MldResourceStatus::Complete;
    for (const auto& entry : archive.entries) {
        if (entry.status == model::MldResourceStatus::Failed) {
            archive.status = model::MldResourceStatus::Failed;
            return;
        }
        if (entry.status != model::MldResourceStatus::Complete)
            archive.status = model::MldResourceStatus::Partial;
    }
    if (archive.status == model::MldResourceStatus::Complete &&
        std::any_of(archive.diagnostics.begin(), archive.diagnostics.end(), [](const auto& diagnostic) {
            return diagnostic.severity != model::MldDiagnostic::Severity::Info;
        }))
        archive.status = model::MldResourceStatus::Partial;
}

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
    const spice::root::Endian endian, std::size_t& tableEnd)
{
    const spice::root::EndianReader reader(bytes, endian);
    const auto count = reader.try_read_u32(textureTableOffset);
    if (!count.has_value()) {
        addDiagnostic(out.diagnostics, model::MldDiagnostic::Severity::Error,
            "Texture archive record count is unreadable.", textureTableOffset);
        return false;
    }
    if (*count > kHardCap) {
        addDiagnostic(out.diagnostics, model::MldDiagnostic::Severity::Error,
            "Texture archive record count exceeds the safety cap.", textureTableOffset);
        return false;
    }
    if (*count > (std::numeric_limits<std::size_t>::max() - 4U) / kRecordSize) {
        addDiagnostic(out.diagnostics, model::MldDiagnostic::Severity::Error,
            "Texture archive record table size overflows addressable input.", textureTableOffset);
        return false;
    }
    const auto tableSize = 4U + static_cast<std::size_t>(*count) * kRecordSize;
    if (textureTableOffset > bytes.size() || tableSize > bytes.size() - textureTableOffset) {
        addDiagnostic(out.diagnostics, model::MldDiagnostic::Severity::Error,
            "Texture archive record table overruns file bounds.", textureTableOffset);
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
    for (const auto& message : archive.diagnostics)
        addDiagnostic(out.diagnostics, model::MldDiagnostic::Severity::Warning,
            message, textureTableOffset);

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
        for (const auto& message : texture.diagnostics)
            addDiagnostic(entry.diagnostics, model::MldDiagnostic::Severity::Warning,
                message, texture.sourceOffset, model::MldResourceKind::TextureArchiveEntry);
        if (texture.decodedBaseLevel.has_value()) {
            entry.decoded = !texture.decodedBaseLevel->rgba8.empty();
            entry.width = static_cast<std::uint16_t>(texture.decodedBaseLevel->width);
            entry.height = static_cast<std::uint16_t>(texture.decodedBaseLevel->height);
            entry.rgba8 = texture.decodedBaseLevel->rgba8;
        }
        entry.status = entry.decoded
            ? model::MldResourceStatus::Complete
            : model::MldResourceStatus::Partial;
        cursor = std::max(cursor, texture.sourceOffset + texture.sourceSize);
        out.archiveEndOffset = std::max(out.archiveEndOffset, cursor);
    }
    if (archive.textures.size() != out.entries.size())
        addDiagnostic(out.diagnostics, model::MldDiagnostic::Severity::Warning,
            "Texture record count does not match the number of GVR chunks.", textureTableOffset);
    addDiagnostic(out.diagnostics, model::MldDiagnostic::Severity::Info,
        "Texture archive parse extracted " + std::to_string(count) + " GVR texture chunk(s).",
        textureTableOffset);
    finalizeArchiveStatus(out);
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
            entry.status = model::MldResourceStatus::Failed;
            addDiagnostic(entry.diagnostics, model::MldDiagnostic::Severity::Error,
                "Dreamcast texture record block size is zero or outside file bounds.", cursor,
                model::MldResourceKind::TextureArchiveEntry);
            addDiagnostic(out.diagnostics, model::MldDiagnostic::Severity::Warning,
                "Dreamcast texture archive contains an invalid record block size.", cursor);
            break;
        }

        std::size_t prefixSize = 0U;
        if (entry.rawRecordWord1 == kDreamcastAlignFlag)
            prefixSize = (32U - (cursor & 31U)) & 31U;
        else if (entry.rawRecordWord1 != 0U)
            addDiagnostic(entry.diagnostics, model::MldDiagnostic::Severity::Warning,
                "Dreamcast texture record has an unknown alignment control word.", cursor,
                model::MldResourceKind::TextureArchiveEntry);
        if (prefixSize > bytes.size() - cursor || blockSize > bytes.size() - cursor - prefixSize) {
            entry.status = model::MldResourceStatus::Failed;
            addDiagnostic(entry.diagnostics, model::MldDiagnostic::Severity::Error,
                "Dreamcast texture alignment prefix and declared block exceed file bounds.", cursor,
                model::MldResourceKind::TextureArchiveEntry);
            break;
        }

        const auto textureOffset = cursor + prefixSize;
        const auto blockEnd = textureOffset + blockSize;
        copyBytes(entry.alignmentPrefixBytes, bytes, cursor, textureOffset);
        entry.encodedDataOffset = textureOffset;
        entry.encodedDataSize = blockSize;
        copyBytes(entry.encodedData, bytes, textureOffset, blockEnd);
        auto texture = spice::pvm::parsing::parsePvrTexture(bytes, textureOffset);
        if (texture.status == spice::pvm::model::ParseStatus::Failed ||
            texture.sourceRange.end() > blockEnd) {
            entry.status = model::MldResourceStatus::Partial;
            addDiagnostic(entry.diagnostics, model::MldDiagnostic::Severity::Warning,
                "Dreamcast texture record does not contain a bounded PVR texture at its expected offset.",
                textureOffset, model::MldResourceKind::TextureArchiveEntry);
            addDiagnostic(out.diagnostics, model::MldDiagnostic::Severity::Warning,
                "Dreamcast texture archive contains an undecodable record.", textureOffset);
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
            addDiagnostic(entry.diagnostics, convertSeverity(diagnostic.severity),
                diagnostic.message, diagnostic.offset, model::MldResourceKind::TextureArchiveEntry);

        const auto decoded = spice::pvm::decoding::decodePvrTexture(texture);
        for (const auto& diagnostic : decoded.diagnostics)
            addDiagnostic(entry.diagnostics, convertSeverity(diagnostic.severity),
                diagnostic.message, diagnostic.offset, model::MldResourceKind::TextureArchiveEntry);
        if (decoded.status != spice::pvm::model::ParseStatus::Failed && !decoded.mipLevels.empty()) {
            entry.decoded = true;
            entry.rgba8 = decoded.mipLevels.front().image.pixels;
        }
        entry.status = entry.decoded
            ? model::MldResourceStatus::Complete
            : model::MldResourceStatus::Partial;
        ++parsedCount;
        cursor = blockEnd;
        out.archiveEndOffset = std::max(out.archiveEndOffset, cursor);
    }
    addDiagnostic(out.diagnostics, model::MldDiagnostic::Severity::Info,
        "Texture archive parse extracted " + std::to_string(parsedCount) + " PVR texture chunk(s).",
        out.tableOffset);
    finalizeArchiveStatus(out);
}

} // namespace

model::MldTextureArchive parseMldTextureArchive(const std::span<const std::uint8_t> bytes,
    const std::size_t textureTableOffset, const spice::root::Endian endian)
{
    model::MldTextureArchive out;
    out.status = model::MldResourceStatus::Failed;
    out.tableOffset = textureTableOffset;
    out.archiveStartOffset = textureTableOffset;
    out.archiveEndOffset = textureTableOffset;
    std::size_t tableEnd = textureTableOffset;
    if (!initializeRecords(out, bytes, textureTableOffset, endian, tableEnd))
        return out;
    out.status = model::MldResourceStatus::Complete;
    if (endian == spice::root::Endian::Little)
        populatePvrArchive(out, bytes, tableEnd);
    else
        populateGvrArchive(out, bytes, textureTableOffset, tableEnd);
    return out;
}

} // namespace spice::mld::parsing
