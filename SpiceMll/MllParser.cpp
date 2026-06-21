#include "MllParser.h"

#include "../Compression/Aklz.h"
#include "../SpiceBin/BinParser.h"
#include "../SpiceGvm/Parsing/GvmParser.h"
#include "../SpiceCore/Binary/EndianReader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace spice::mll {
namespace {

using spice::core::Endian;
using spice::core::EndianReader;

constexpr std::uint32_t kMllRecordsOffset = 0x08U;
constexpr std::uint32_t kMllRecordStride = 0x20U;
constexpr std::uint32_t kMllNameLength = 0x14U;
constexpr std::uint32_t kMldIndexEntryStride = 0x68U;
constexpr std::uint32_t kMldFunctionNameOffset = 0x24U;
constexpr std::uint32_t kMldFunctionNameLength = 0x14U;
constexpr std::uint32_t kMldObjectListSampleLimit = 8U;
constexpr std::uint32_t kPreTextureTableCountSize = sizeof(std::uint32_t);
constexpr std::array<std::uint32_t, 3U> kMldObjectListFieldOffsets{ 0x14U, 0x18U, 0x1CU };
constexpr std::array<std::uint32_t, 6U> kMldCountedListFieldOffsets{ 0x08U, 0x0CU, 0x10U, 0x14U, 0x18U, 0x1CU };
constexpr std::array<std::string_view, 13U> kInterestingBlockTags{
    "GOBJ",
    "GRND",
    "POF0",
    "NJCM",
    "GJCM",
    "NJTL",
    "GJTL",
    "NJBM",
    "NJLI",
    "NJTX",
    "NMDM",
    "GCIX",
    "GVRT",
};

void addDiagnostic(std::vector<MllDiagnostic>& diagnostics,
    DiagnosticSeverity severity,
    std::string message,
    std::uint32_t offset = 0U) {
    diagnostics.push_back(MllDiagnostic{ severity, std::move(message), offset });
}

bool canReadRange(std::size_t size, std::uint32_t offset, std::uint32_t length) {
    return offset <= size && length <= size - offset;
}

bool endsWithCaseInsensitive(std::string_view text, std::string_view suffix) {
    if (text.size() < suffix.size()) {
        return false;
    }

    const auto start = text.size() - suffix.size();
    for (std::size_t i = 0U; i < suffix.size(); ++i) {
        const auto lhs = static_cast<unsigned char>(text[start + i]);
        const auto rhs = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(lhs) != std::tolower(rhs)) {
            return false;
        }
    }
    return true;
}

std::uint32_t readU32LeUnchecked(std::span<const std::uint8_t> bytes, std::uint32_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

std::uint32_t clampSize(std::size_t size) {
    return static_cast<std::uint32_t>(
        std::min<std::size_t>(size, std::numeric_limits<std::uint32_t>::max()));
}

std::string makeSignature(std::span<const std::uint8_t> bytes, std::uint32_t offset) {
    if (!canReadRange(bytes.size(), offset, 4U)) {
        return {};
    }

    std::string signature;
    signature.reserve(4U);
    for (std::size_t i = 0; i < 4U; ++i) {
        const auto value = bytes[static_cast<std::size_t>(offset) + i];
        if (value < 0x20U || value > 0x7eU) {
            signature.push_back('.');
        } else {
            signature.push_back(static_cast<char>(value));
        }
    }
    return signature;
}

std::string makeHexBytes(std::span<const std::uint8_t> bytes, std::uint32_t offset, std::uint32_t maxLength) {
    if (offset >= bytes.size()) {
        return {};
    }
    const auto readableLength = std::min<std::uint32_t>(
        maxLength,
        static_cast<std::uint32_t>(bytes.size() - offset));

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::uint32_t i = 0U; i < readableLength; ++i) {
        out << std::setw(2) << static_cast<unsigned int>(bytes[static_cast<std::size_t>(offset) + i]);
    }
    return out.str();
}

bool matchesTag(std::span<const std::uint8_t> bytes, std::uint32_t offset, std::string_view tag) {
    if (!canReadRange(bytes.size(), offset, static_cast<std::uint32_t>(tag.size()))) {
        return false;
    }
    for (std::uint32_t i = 0U; i < tag.size(); ++i) {
        if (bytes[static_cast<std::size_t>(offset) + i] != static_cast<std::uint8_t>(tag[i])) {
            return false;
        }
    }
    return true;
}

bool hasParseFailureDiagnostic(const std::vector<std::string>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.find("parse failed") != std::string::npos;
    });
}

std::string joinDiagnostics(const std::vector<std::string>& diagnostics) {
    std::ostringstream out;
    for (std::size_t i = 0U; i < diagnostics.size(); ++i) {
        if (i != 0U) {
            out << " | ";
        }
        out << diagnostics[i];
    }
    return out.str();
}

std::string countedListReferenceText(
    std::uint32_t entryIndex,
    std::uint32_t fieldOffset,
    std::uint32_t listOffset,
    std::uint32_t valueIndex) {
    std::ostringstream out;
    out << "entry=" << entryIndex
        << ";field=0x" << std::hex << fieldOffset
        << ";list=0x" << listOffset
        << ";valueIndex=" << std::dec << valueIndex;
    return out.str();
}

std::string readMemberName(std::span<const std::uint8_t> bytes, std::uint32_t recordOffset) {
    std::string name;
    for (std::uint32_t i = 0; i < kMllNameLength; ++i) {
        const auto value = bytes[static_cast<std::size_t>(recordOffset) + i];
        if (value == 0U) {
            break;
        }
        name.push_back(static_cast<char>(value));
    }
    return name;
}

enum class FixedAsciiShape {
    Empty,
    Printable,
    Suspicious,
};

FixedAsciiShape classifyFixedAscii(std::span<const std::uint8_t> bytes, std::uint32_t offset, std::uint32_t length) {
    if (!canReadRange(bytes.size(), offset, length)) {
        return FixedAsciiShape::Suspicious;
    }

    bool sawPrintable = false;
    bool sawTerminator = false;
    for (std::uint32_t i = 0U; i < length; ++i) {
        const auto value = bytes[static_cast<std::size_t>(offset) + i];
        if (value == 0U) {
            sawTerminator = true;
            continue;
        }
        if (sawTerminator || std::isprint(static_cast<unsigned char>(value)) == 0) {
            return FixedAsciiShape::Suspicious;
        }
        sawPrintable = true;
    }

    return sawPrintable ? FixedAsciiShape::Printable : FixedAsciiShape::Empty;
}

bool countedU32ListLooksPlausible(
    std::span<const std::uint8_t> payload,
    const EndianReader& reader,
    std::uint32_t listOffset) {
    if (listOffset == 0U || listOffset % sizeof(std::uint32_t) != 0U) {
        return false;
    }
    if (!canReadRange(payload.size(), listOffset, sizeof(std::uint32_t))) {
        return false;
    }

    const auto count = reader.read_u32(listOffset);
    constexpr std::uint32_t kHardCountCap = 65536U;
    if (count > kHardCountCap) {
        return false;
    }

    const auto entriesOffset = static_cast<std::uint64_t>(listOffset) + sizeof(std::uint32_t);
    const auto entriesSize = static_cast<std::uint64_t>(count) * sizeof(std::uint32_t);
    return entriesOffset <= payload.size() && entriesSize <= payload.size() - entriesOffset;
}

struct CountedListValueReference {
    std::uint32_t entryIndex{ 0U };
    std::uint32_t fieldOffset{ 0U };
    std::uint32_t listOffset{ 0U };
    std::uint32_t valueIndex{ 0U };
    std::uint32_t value{ 0U };
};

std::vector<CountedListValueReference> collectCountedListValueReferences(
    std::span<const std::uint8_t> payload,
    const MllEmbeddedMldHeaderProbe& header) {
    std::vector<CountedListValueReference> references{};
    if (!header.plausible) {
        return references;
    }

    EndianReader reader(payload, Endian::Big);
    for (std::uint32_t entryIndex = 0U; entryIndex < header.entryCount; ++entryIndex) {
        const auto entryOffset = header.indexTableOffset + entryIndex * kMldIndexEntryStride;
        if (!canReadRange(payload.size(), entryOffset, kMldIndexEntryStride)) {
            continue;
        }
        for (const auto fieldOffset : kMldCountedListFieldOffsets) {
            const auto listOffset = reader.read_u32(entryOffset + fieldOffset);
            if (!countedU32ListLooksPlausible(payload, reader, listOffset)) {
                continue;
            }

            const auto count = reader.read_u32(listOffset);
            const auto valuesOffset = listOffset + static_cast<std::uint32_t>(sizeof(std::uint32_t));
            references.reserve(references.size() + count);
            for (std::uint32_t valueIndex = 0U; valueIndex < count; ++valueIndex) {
                const auto valueOffset = valuesOffset + valueIndex * static_cast<std::uint32_t>(sizeof(std::uint32_t));
                const auto value = reader.read_u32(valueOffset);
                if (value == 0U) {
                    continue;
                }
                references.push_back(CountedListValueReference{
                    .entryIndex = entryIndex,
                    .fieldOffset = fieldOffset,
                    .listOffset = listOffset,
                    .valueIndex = valueIndex,
                    .value = value,
                });
            }
        }
    }
    return references;
}

bool declaredSizePlausible(std::string_view tag, std::uint32_t remaining, std::uint32_t declaredSize) {
    if (declaredSize == 0U) {
        return false;
    }
    if (tag == "GRND" || tag == "GOBJ") {
        return declaredSize >= 8U && declaredSize <= remaining;
    }
    if (tag == "NJTL" || tag == "GJTL") {
        return declaredSize <= remaining && declaredSize <= remaining - std::min<std::uint32_t>(remaining, 8U);
    }
    return declaredSize <= remaining || (remaining >= 8U && declaredSize <= remaining - 8U);
}

std::vector<MllEmbeddedBlockProbe> probeEmbeddedBlocks(
    std::span<const std::uint8_t> payload,
    const MllEmbeddedMldHeaderProbe& header) {
    std::vector<MllEmbeddedBlockProbe> probes{};
    const auto references = collectCountedListValueReferences(payload, header);
    if (payload.size() < 4U) {
        return probes;
    }

    EndianReader reader(payload, Endian::Big);
    for (std::uint32_t offset = 0U; offset <= payload.size() - 4U; ++offset) {
        std::string_view matchedTag{};
        for (const auto tag : kInterestingBlockTags) {
            if (matchesTag(payload, offset, tag)) {
                matchedTag = tag;
                break;
            }
        }
        if (matchedTag.empty()) {
            continue;
        }

        MllEmbeddedBlockProbe probe{};
        probe.blockOffset = offset;
        probe.tag = std::string(matchedTag);
        probe.offsetAligned = offset % sizeof(std::uint32_t) == 0U;
        probe.atHeaderRealDataOffset = header.plausible && offset == header.realDataOffset;
        probe.atHeaderTextureTableOffset = header.plausible && offset == header.textureTableOffset;
        probe.bytes32Hex = makeHexBytes(payload, offset, 32U);

        if (canReadRange(payload.size(), offset + 4U, sizeof(std::uint32_t))) {
            probe.declaredSizeBe = reader.read_u32(offset + 4U);
            probe.declaredSizeLe = readU32LeUnchecked(payload, offset + 4U);
            const auto remaining = static_cast<std::uint32_t>(payload.size() - offset);
            probe.declaredSizeBePlausible =
                declaredSizePlausible(matchedTag, remaining, probe.declaredSizeBe);
            probe.declaredSizeLePlausible =
                declaredSizePlausible(matchedTag, remaining, probe.declaredSizeLe);
        }

        for (const auto& reference : references) {
            if (reference.value == offset) {
                ++probe.exactCountedListReferenceCount;
                if (probe.firstExactCountedListReference.empty()) {
                    probe.firstExactCountedListReference = countedListReferenceText(
                        reference.entryIndex,
                        reference.fieldOffset,
                        reference.listOffset,
                        reference.valueIndex);
                }
            }
            if (reference.value <= std::numeric_limits<std::uint32_t>::max() - 8U &&
                reference.value + 8U == offset) {
                ++probe.plus8CountedListReferenceCount;
            }
            if (offset <= std::numeric_limits<std::uint32_t>::max() - 8U &&
                reference.value == offset + 8U) {
                ++probe.minus8CountedListReferenceCount;
            }
        }

        probes.push_back(std::move(probe));
    }
    return probes;
}

std::vector<MllEmbeddedGvrTextureProbe> probeEmbeddedGvrTextures(std::span<const std::uint8_t> payload) {
    std::vector<MllEmbeddedGvrTextureProbe> probes{};
    if (payload.size() < 0x18U) {
        return probes;
    }

    for (std::uint32_t offset = 0U; offset <= payload.size() - 4U; ++offset) {
        if (!matchesTag(payload, offset, "GCIX")) {
            continue;
        }

        const auto gvrtOffset64 = static_cast<std::uint64_t>(offset) + 0x10U;
        if (gvrtOffset64 > std::numeric_limits<std::uint32_t>::max()) {
            continue;
        }
        const auto gvrtOffset = static_cast<std::uint32_t>(gvrtOffset64);
        if (!matchesTag(payload, gvrtOffset, "GVRT")) {
            continue;
        }

        MllEmbeddedGvrTextureProbe probe{};
        probe.textureIndex = static_cast<std::uint32_t>(probes.size());
        probe.gcixOffset = offset;
        probe.gvrtOffset = gvrtOffset;
        probe.pairDistance = probe.gvrtOffset - probe.gcixOffset;
        if (canReadRange(payload.size(), probe.gcixOffset + 4U, sizeof(std::uint32_t))) {
            probe.gcixPayloadSizeLe = readU32LeUnchecked(payload, probe.gcixOffset + 4U);
        }
        if (canReadRange(payload.size(), probe.gvrtOffset + 4U, sizeof(std::uint32_t))) {
            probe.gvrtPayloadSizeLe = readU32LeUnchecked(payload, probe.gvrtOffset + 4U);
        }

        const auto recordEnd = static_cast<std::uint64_t>(probe.gvrtOffset) + 8U + probe.gvrtPayloadSizeLe;
        probe.recordInBounds = recordEnd <= payload.size();
        const auto safeRecordEnd = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(recordEnd, payload.size()));
        probe.sourceSize = safeRecordEnd >= probe.gcixOffset ? safeRecordEnd - probe.gcixOffset : 0U;

        if (probe.sourceSize != 0U) {
            probe.parseAttempted = true;
            const auto textureBytes = payload.subspan(probe.gcixOffset, probe.sourceSize);
            const auto parsed = spice::gvm::parsing::parseGvrTexture(
                textureBytes,
                probe.gcixOffset,
                spice::gvm::parsing::ParseOptions{
                    .decodeBaseLevel = true,
                    .keepRawEncodedPayload = false,
                });
            probe.parseHasFailureDiagnostics = hasParseFailureDiagnostic(parsed.diagnostics);
            probe.hasGlobalIndex = parsed.hasGlobalIndex;
            probe.globalIndex = parsed.globalIndex;
            probe.rawFlags = parsed.rawFlags;
            probe.rawDataFormat = parsed.rawDataFormat;
            probe.textureFormat = spice::gvm::model::to_string(parsed.textureFormat);
            probe.paletteFormat = spice::gvm::model::to_string(parsed.paletteFormat);
            probe.hasMipmaps = parsed.hasMipmaps;
            probe.hasInternalPalette = parsed.hasInternalPalette;
            probe.width = parsed.width;
            probe.height = parsed.height;
            probe.imageDataOffset = static_cast<std::uint32_t>(
                std::min<std::size_t>(parsed.imageDataOffset, std::numeric_limits<std::uint32_t>::max()));
            probe.imageDataSize = static_cast<std::uint32_t>(
                std::min<std::size_t>(parsed.imageDataSize, std::numeric_limits<std::uint32_t>::max()));
            probe.decodedBaseLevelPresent =
                parsed.decodedBaseLevel.has_value() && !parsed.decodedBaseLevel->rgba8.empty();
            if (parsed.decodedBaseLevel.has_value()) {
                probe.decodedRgba8Size = static_cast<std::uint32_t>(std::min<std::size_t>(
                    parsed.decodedBaseLevel->rgba8.size(),
                    std::numeric_limits<std::uint32_t>::max()));
            }
            probe.diagnosticCount = static_cast<std::uint32_t>(std::min<std::size_t>(
                parsed.diagnostics.size(),
                std::numeric_limits<std::uint32_t>::max()));
            probe.diagnosticsJoined = joinDiagnostics(parsed.diagnostics);
        }

        probes.push_back(std::move(probe));
    }
    return probes;
}

std::string joinU32Preview(const std::vector<std::uint32_t>& values, std::size_t limit) {
    std::ostringstream out;
    const auto count = std::min(values.size(), limit);
    for (std::size_t i = 0U; i < count; ++i) {
        if (i != 0U) {
            out << ";";
        }
        out << values[i];
    }
    if (values.size() > limit) {
        out << ";...";
    }
    return out.str();
}

std::string joinStringPreview(const std::vector<std::string>& values, std::size_t limit) {
    std::ostringstream out;
    const auto count = std::min(values.size(), limit);
    for (std::size_t i = 0U; i < count; ++i) {
        if (i != 0U) {
            out << ";";
        }
        out << values[i];
    }
    if (values.size() > limit) {
        out << ";...";
    }
    return out.str();
}

std::string trimAsciiSpaces(std::string value) {
    while (!value.empty() && value.front() == ' ') {
        value.erase(value.begin());
    }
    while (!value.empty() && value.back() == ' ') {
        value.pop_back();
    }
    return value;
}

std::string readSlotString(std::span<const std::uint8_t> payload, std::uint32_t offset, std::uint32_t length) {
    std::string value{};
    if (!canReadRange(payload.size(), offset, length)) {
        return value;
    }
    value.reserve(length);
    for (std::uint32_t i = 0U; i < length; ++i) {
        const auto byte = payload[offset + i];
        if (byte == 0U) {
            break;
        }
        value.push_back(static_cast<char>(byte));
    }
    return trimAsciiSpaces(value);
}

bool isPrintableSlotString(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char ch) {
        const auto byte = static_cast<unsigned char>(ch);
        return byte >= 0x20U && byte <= 0x7eU;
    });
}

bool tryParseTrailingDecimal(std::string_view value, std::uint32_t& parsed) {
    if (value.empty() || !std::isdigit(static_cast<unsigned char>(value.back()))) {
        return false;
    }
    std::size_t firstDigit = value.size() - 1U;
    while (firstDigit > 0U && std::isdigit(static_cast<unsigned char>(value[firstDigit - 1U]))) {
        --firstDigit;
    }

    std::uint64_t result = 0U;
    for (std::size_t i = firstDigit; i < value.size(); ++i) {
        result = result * 10U + static_cast<std::uint32_t>(value[i] - '0');
        if (result > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
    }
    parsed = static_cast<std::uint32_t>(result);
    return true;
}

std::set<std::uint32_t> collectGlobalIndexes(const std::vector<MllEmbeddedGvrTextureProbe>& textures) {
    std::set<std::uint32_t> indexes{};
    for (const auto& texture : textures) {
        if (texture.hasGlobalIndex) {
            indexes.insert(texture.globalIndex);
        }
    }
    return indexes;
}

std::int32_t offsetDelta(std::uint32_t offset, std::uint32_t target) {
    const auto delta = static_cast<std::int64_t>(offset) - static_cast<std::int64_t>(target);
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(
        delta,
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max()));
}

bool offsetInsideSpan(std::uint32_t offset, std::uint32_t start, std::uint32_t end) {
    return offset >= start && offset < end;
}

std::string extractPrintableStringsNearTextures(
    std::span<const std::uint8_t> payload,
    const MllEmbeddedMldHeaderProbe& header,
    std::uint32_t firstTextureOffset) {
    if (payload.empty() || firstTextureOffset == 0U || firstTextureOffset > payload.size()) {
        return {};
    }

    std::uint32_t start = firstTextureOffset > 0x100U ? firstTextureOffset - 0x100U : 0U;
    if (header.plausible && header.textureTableOffset != 0U && header.textureTableOffset < firstTextureOffset) {
        start = std::min(start, header.textureTableOffset);
    }
    const auto end = firstTextureOffset;

    std::vector<std::string> strings{};
    std::string current{};
    const auto flush = [&]() {
        if (current.size() >= 3U) {
            strings.push_back(current);
        }
        current.clear();
    };

    for (std::uint32_t offset = start; offset < end; ++offset) {
        const auto value = payload[offset];
        if (value >= 0x20U && value <= 0x7eU) {
            current.push_back(static_cast<char>(value));
            continue;
        }
        flush();
        if (strings.size() >= 16U) {
            break;
        }
    }
    flush();

    std::ostringstream out;
    const auto count = std::min<std::size_t>(strings.size(), 16U);
    for (std::size_t i = 0U; i < count; ++i) {
        if (i != 0U) {
            out << ";";
        }
        out << strings[i];
    }
    return out.str();
}

std::vector<std::uint32_t> collectIndexTexturePointers(
    std::span<const std::uint8_t> payload,
    const MllEmbeddedMldHeaderProbe& header) {
    std::vector<std::uint32_t> pointers{};
    if (!header.plausible) {
        return pointers;
    }

    EndianReader reader(payload, Endian::Big);
    pointers.reserve(header.entryCount);
    for (std::uint32_t entryIndex = 0U; entryIndex < header.entryCount; ++entryIndex) {
        const auto entryOffset = header.indexTableOffset + entryIndex * kMldIndexEntryStride;
        if (!canReadRange(payload.size(), entryOffset, kMldIndexEntryStride)) {
            continue;
        }
        pointers.push_back(reader.read_u32(entryOffset + 0x20U));
    }
    return pointers;
}

MllTextureTableProbe probeTextureTable(
    std::span<const std::uint8_t> payload,
    const MllEmbeddedMldHeaderProbe& header,
    const std::vector<MllEmbeddedGvrTextureProbe>& textures) {
    MllTextureTableProbe probe{};
    probe.textureCount = static_cast<std::uint32_t>(std::min<std::size_t>(
        textures.size(),
        std::numeric_limits<std::uint32_t>::max()));
    probe.hasTextures = !textures.empty();
    if (textures.empty()) {
        return probe;
    }

    auto sorted = textures;
    std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) {
        return left.gcixOffset < right.gcixOffset;
    });

    probe.firstTextureOffset = sorted.front().gcixOffset;
    probe.lastTextureEndOffset = sorted.front().gcixOffset + sorted.front().sourceSize;
    probe.clusterCount = 1U;
    probe.allRecordsInBounds = true;
    probe.allTexturesParsed = true;
    probe.allTexturesDecoded = true;
    probe.allTexturesHaveGlobalIndex = true;

    std::vector<std::uint32_t> globalIndexes{};
    globalIndexes.reserve(sorted.size());
    std::uint32_t currentClusterEnd = probe.lastTextureEndOffset;
    for (const auto& texture : sorted) {
        const auto textureEnd = texture.gcixOffset + texture.sourceSize;
        probe.lastTextureEndOffset = std::max(probe.lastTextureEndOffset, textureEnd);
        probe.allRecordsInBounds = probe.allRecordsInBounds && texture.recordInBounds;
        probe.allTexturesParsed = probe.allTexturesParsed &&
            texture.parseAttempted &&
            texture.recordInBounds &&
            !texture.parseHasFailureDiagnostics &&
            texture.width != 0U &&
            texture.height != 0U &&
            texture.textureFormat != "Unknown";
        probe.allTexturesDecoded = probe.allTexturesDecoded && texture.decodedBaseLevelPresent;
        probe.allTexturesHaveGlobalIndex = probe.allTexturesHaveGlobalIndex && texture.hasGlobalIndex;
        if (texture.hasGlobalIndex) {
            globalIndexes.push_back(texture.globalIndex);
        }

        if (texture.gcixOffset > currentClusterEnd) {
            ++probe.clusterCount;
            probe.largestGapBetweenRecords =
                std::max(probe.largestGapBetweenRecords, texture.gcixOffset - currentClusterEnd);
        }
        currentClusterEnd = std::max(currentClusterEnd, textureEnd);
    }
    probe.textureSpanSize = probe.lastTextureEndOffset - probe.firstTextureOffset;
    probe.globalIndexSequencePreview = joinU32Preview(globalIndexes, 32U);

    if (!globalIndexes.empty()) {
        const auto [minIt, maxIt] = std::minmax_element(globalIndexes.begin(), globalIndexes.end());
        probe.globalIndexMin = *minIt;
        probe.globalIndexMax = *maxIt;
        const std::set<std::uint32_t> uniqueIndexes(globalIndexes.begin(), globalIndexes.end());
        probe.uniqueGlobalIndexCount = static_cast<std::uint32_t>(uniqueIndexes.size());
        probe.duplicateGlobalIndexCount = static_cast<std::uint32_t>(globalIndexes.size() - uniqueIndexes.size());
        if (!uniqueIndexes.empty()) {
            const auto denseSpan = static_cast<std::uint64_t>(probe.globalIndexMax) - probe.globalIndexMin + 1U;
            probe.missingGlobalIndexCount = denseSpan > uniqueIndexes.size()
                ? static_cast<std::uint32_t>(denseSpan - uniqueIndexes.size())
                : 0U;
        }
        probe.globalIndexSequenceDense =
            probe.duplicateGlobalIndexCount == 0U && probe.missingGlobalIndexCount == 0U;
        probe.globalIndexSequenceStartsAtZero = probe.globalIndexMin == 0U;
    }

    if (header.plausible) {
        probe.headerTextureTableOffsetNonZero = header.textureTableOffset != 0U;
        probe.headerTextureTableOffsetAtFirstTexture = header.textureTableOffset == probe.firstTextureOffset;
        probe.headerTextureTableOffsetInsideTextureSpan =
            offsetInsideSpan(header.textureTableOffset, probe.firstTextureOffset, probe.lastTextureEndOffset);
        probe.headerTextureTableOffsetBeforeFirstTexture =
            header.textureTableOffset != 0U && header.textureTableOffset < probe.firstTextureOffset;
        probe.headerTextureTableOffsetDeltaToFirstTexture =
            offsetDelta(header.textureTableOffset, probe.firstTextureOffset);

        probe.headerRealDataOffsetNonZero = header.realDataOffset != 0U;
        probe.headerRealDataOffsetAtFirstTexture = header.realDataOffset == probe.firstTextureOffset;
        probe.headerRealDataOffsetInsideTextureSpan =
            offsetInsideSpan(header.realDataOffset, probe.firstTextureOffset, probe.lastTextureEndOffset);
        probe.headerRealDataOffsetBeforeFirstTexture =
            header.realDataOffset != 0U && header.realDataOffset < probe.firstTextureOffset;
        probe.headerRealDataOffsetDeltaToFirstTexture =
            offsetDelta(header.realDataOffset, probe.firstTextureOffset);
    }

    const auto texturePointers = collectIndexTexturePointers(payload, header);
    probe.indexTexturePointerCount = static_cast<std::uint32_t>(texturePointers.size());
    std::set<std::uint32_t> uniqueNonZeroTexturePointers{};
    std::vector<std::uint32_t> nonZeroTexturePointers{};
    for (const auto pointer : texturePointers) {
        if (pointer == 0U) {
            continue;
        }
        ++probe.nonZeroIndexTexturePointerCount;
        uniqueNonZeroTexturePointers.insert(pointer);
        nonZeroTexturePointers.push_back(pointer);
        if (pointer == probe.firstTextureOffset) {
            ++probe.indexTexturePointerAtFirstTextureCount;
        }
        if (offsetInsideSpan(pointer, probe.firstTextureOffset, probe.lastTextureEndOffset)) {
            ++probe.indexTexturePointerInsideTextureSpanCount;
        }
        if (pointer < probe.firstTextureOffset) {
            ++probe.indexTexturePointerBeforeFirstTextureCount;
        }
    }
    probe.uniqueIndexTexturePointerCount = static_cast<std::uint32_t>(uniqueNonZeroTexturePointers.size());
    probe.indexTexturePointerValuesPreview = joinU32Preview(nonZeroTexturePointers, 32U);
    probe.nearbyPrintableStrings = extractPrintableStringsNearTextures(payload, header, probe.firstTextureOffset);
    return probe;
}

MllPreTextureTableProbe probePreTextureTable(
    std::span<const std::uint8_t> payload,
    const MllEmbeddedMldHeaderProbe& header,
    const MllTextureTableProbe& textureProbe,
    const std::vector<MllEmbeddedGvrTextureProbe>& textures) {
    MllPreTextureTableProbe probe{};
    if (!header.plausible ||
        !textureProbe.hasTextures ||
        header.textureTableOffset == 0U ||
        header.textureTableOffset >= textureProbe.firstTextureOffset) {
        return probe;
    }

    probe.present = true;
    probe.tableOffset = header.textureTableOffset;
    probe.tableEndOffset = textureProbe.firstTextureOffset;
    probe.tableSize = probe.tableEndOffset - probe.tableOffset;
    probe.spanInBounds = canReadRange(payload.size(), probe.tableOffset, probe.tableSize);
    probe.spanAlignedTo20 = probe.tableSize % 0x20U == 0U;
    if (!probe.spanInBounds) {
        return probe;
    }

    EndianReader reader(payload, Endian::Big);
    if (!canReadRange(payload.size(), probe.tableOffset, kPreTextureTableCountSize)) {
        return probe;
    }
    probe.declaredEntryCount = reader.read_u32(probe.tableOffset);
    probe.declaredCountMatchesTextureCount = probe.declaredEntryCount == textureProbe.textureCount;
    const auto recordsByteSize = static_cast<std::uint64_t>(probe.declaredEntryCount) * probe.entryStride;
    const auto recordsStart = static_cast<std::uint64_t>(probe.tableOffset) + kPreTextureTableCountSize;
    const auto recordsEnd = recordsStart + recordsByteSize;
    probe.recordsFit =
        recordsStart <= payload.size() &&
        recordsByteSize <= payload.size() - recordsStart &&
        recordsEnd <= probe.tableEndOffset;
    if (!probe.recordsFit) {
        return probe;
    }
    probe.entryCount = probe.declaredEntryCount;
    probe.trailingPaddingSize = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(probe.tableEndOffset) - recordsEnd);
    probe.entries.reserve(probe.entryCount);
    const auto globalIndexes = collectGlobalIndexes(textures);
    auto orderedTextures = textures;
    std::sort(orderedTextures.begin(), orderedTextures.end(), [](const auto& left, const auto& right) {
        return left.gcixOffset < right.gcixOffset;
    });
    std::vector<std::string> names{};
    names.reserve(std::min<std::uint32_t>(probe.entryCount, 32U));

    for (std::uint32_t entryIndex = 0U; entryIndex < probe.entryCount; ++entryIndex) {
        const auto entryOffset = probe.tableOffset + kPreTextureTableCountSize + entryIndex * probe.entryStride;
        if (!canReadRange(payload.size(), entryOffset, probe.entryStride)) {
            break;
        }

        MllPreTextureTableEntryProbe entry{};
        entry.entryIndex = entryIndex;
        entry.entryOffset = entryOffset;
        entry.name = readSlotString(payload, entryOffset, 0x10U);
        entry.nameEmpty = entry.name.empty();
        entry.namePrintable = isPrintableSlotString(entry.name);
        if (entry.nameEmpty) {
            ++probe.emptyNameCount;
        }
        if (entry.namePrintable) {
            ++probe.printableNameCount;
            names.push_back(entry.name);
        }

        std::uint32_t suffix = 0U;
        const bool hasTrailingDecimal = tryParseTrailingDecimal(entry.name, suffix);
        if (hasTrailingDecimal && globalIndexes.find(suffix) != globalIndexes.end()) {
            entry.nameMatchesKnownTextureGlobalIndex = true;
            entry.matchedGlobalIndex = suffix;
            ++probe.textureNameMatchCount;
        }

        entry.word10 = reader.read_u32(entryOffset + 0x10U);
        entry.word14 = reader.read_u32(entryOffset + 0x14U);
        entry.word18 = reader.read_u32(entryOffset + 0x18U);
        entry.word1c = reader.read_u32(entryOffset + 0x1CU);
        entry.word20 = reader.read_u32(entryOffset + 0x20U);
        entry.word24 = reader.read_u32(entryOffset + 0x24U);
        entry.word28 = reader.read_u32(entryOffset + 0x28U);
        entry.bytes32Hex = makeHexBytes(payload, entryOffset, probe.entryStride);
        if (entryIndex < orderedTextures.size()) {
            const auto& texture = orderedTextures[entryIndex];
            entry.orderTexturePresent = true;
            entry.orderTextureIndex = texture.textureIndex;
            entry.orderTextureGcixOffset = texture.gcixOffset;
            entry.orderTextureGvrtOffset = texture.gvrtOffset;
            entry.orderTextureSourceSize = texture.sourceSize;
            entry.orderTextureHasGlobalIndex = texture.hasGlobalIndex;
            entry.orderTextureGlobalIndex = texture.globalIndex;
            entry.orderTextureRawFlags = texture.rawFlags;
            entry.orderTextureRawDataFormat = texture.rawDataFormat;
            entry.orderTextureFormat = texture.textureFormat;
            entry.orderTexturePaletteFormat = texture.paletteFormat;
            entry.orderTextureHasMipmaps = texture.hasMipmaps;
            entry.orderTextureHasInternalPalette = texture.hasInternalPalette;
            entry.orderTextureWidth = texture.width;
            entry.orderTextureHeight = texture.height;
            entry.orderTextureImageDataSize = texture.imageDataSize;
            entry.orderTextureDecoded = texture.decodedBaseLevelPresent;
            entry.nameSuffixMatchesOrderTextureGlobalIndex =
                hasTrailingDecimal && texture.hasGlobalIndex && suffix == texture.globalIndex;
        }
        probe.entries.push_back(std::move(entry));
    }
    probe.entryNamePreview = joinStringPreview(names, 32U);
    return probe;
}

MllEmbeddedMldHeaderProbe probeEmbeddedMldHeader(std::span<const std::uint8_t> payload) {
    MllEmbeddedMldHeaderProbe probe{};
    if (payload.size() < 0x14U) {
        return probe;
    }

    EndianReader reader(payload, Endian::Big);
    probe.entryCount = reader.read_u32(0x00U);
    probe.indexTableOffset = reader.read_u32(0x04U);
    probe.functionParametersOffset = reader.read_u32(0x08U);
    probe.realDataOffset = reader.read_u32(0x0CU);
    probe.textureTableOffset = reader.read_u32(0x10U);

    if (probe.entryCount == 0U || probe.entryCount > 4096U) {
        return probe;
    }
    const std::uint64_t tableEnd =
        static_cast<std::uint64_t>(probe.indexTableOffset) +
        static_cast<std::uint64_t>(probe.entryCount) * kMldIndexEntryStride;
    if (tableEnd > payload.size()) {
        return probe;
    }

    const auto offsetInPayload = [&](std::uint32_t offset) {
        return offset == 0U || offset < payload.size();
    };
    probe.plausible =
        offsetInPayload(probe.indexTableOffset) &&
        offsetInPayload(probe.functionParametersOffset) &&
        offsetInPayload(probe.realDataOffset) &&
        offsetInPayload(probe.textureTableOffset);

    if (!probe.plausible) {
        return probe;
    }

    for (std::uint32_t entryIndex = 0U; entryIndex < probe.entryCount; ++entryIndex) {
        const auto entryOffset = probe.indexTableOffset + entryIndex * kMldIndexEntryStride;
        const auto nameShape = classifyFixedAscii(
            payload,
            entryOffset + kMldFunctionNameOffset,
            kMldFunctionNameLength);
        switch (nameShape) {
        case FixedAsciiShape::Empty:
            ++probe.emptyFunctionNameCount;
            break;
        case FixedAsciiShape::Printable:
            ++probe.printableFunctionNameCount;
            break;
        case FixedAsciiShape::Suspicious:
            ++probe.suspiciousFunctionNameCount;
            break;
        }

        for (const auto fieldOffset : kMldCountedListFieldOffsets) {
            const auto listOffset = reader.read_u32(entryOffset + fieldOffset);
            if (listOffset == 0U) {
                continue;
            }
            ++probe.nonZeroCountedListFieldCount;
            if (countedU32ListLooksPlausible(payload, reader, listOffset)) {
                ++probe.plausibleCountedListFieldCount;
            }
        }
    }

    probe.indexEntryShapePlausible =
        probe.suspiciousFunctionNameCount == 0U &&
        (probe.printableFunctionNameCount != 0U || probe.plausibleCountedListFieldCount != 0U);
    return probe;
}

std::uint32_t readableU32CountFrom(std::size_t size, std::uint32_t offset) {
    if (offset > size) {
        return 0U;
    }
    return static_cast<std::uint32_t>((size - offset) / sizeof(std::uint32_t));
}

MllEmbeddedMldObjectListTargetSample probeEmbeddedMldObjectListTarget(
    std::span<const std::uint8_t> payload,
    const EndianReader& reader,
    std::uint32_t sampleIndex,
    std::uint32_t pointerOffset,
    std::uint32_t entryOffset,
    std::uint32_t listOffset) {
    MllEmbeddedMldObjectListTargetSample sample{};
    sample.sampleIndex = sampleIndex;
    sample.pointerOffset = pointerOffset;
    sample.targetOffset = reader.read_u32(pointerOffset);
    sample.pointerNonZero = sample.targetOffset != 0U;
    if (!sample.pointerNonZero) {
        return sample;
    }

    sample.targetOffsetAligned = sample.targetOffset % sizeof(std::uint32_t) == 0U;
    sample.targetInBounds = canReadRange(payload.size(), sample.targetOffset, sizeof(std::uint32_t));
    sample.targetLooksPlausible = sample.targetOffsetAligned && sample.targetInBounds;
    const auto resolveRelative = [&](std::uint32_t base, std::uint32_t value, std::uint32_t& resolvedOffset) {
        const auto resolved = static_cast<std::uint64_t>(base) + value;
        if (resolved > std::numeric_limits<std::uint32_t>::max()) {
            resolvedOffset = 0U;
            return false;
        }
        resolvedOffset = static_cast<std::uint32_t>(resolved);
        return resolvedOffset % sizeof(std::uint32_t) == 0U &&
            canReadRange(payload.size(), resolvedOffset, sizeof(std::uint32_t));
    };
    sample.listBaseTargetLooksPlausible =
        resolveRelative(listOffset, sample.targetOffset, sample.listBaseTargetOffset);
    sample.entryBaseTargetLooksPlausible =
        resolveRelative(entryOffset, sample.targetOffset, sample.entryBaseTargetOffset);
    sample.pointerBaseTargetLooksPlausible =
        resolveRelative(pointerOffset, sample.targetOffset, sample.pointerBaseTargetOffset);
    if (!sample.targetLooksPlausible) {
        return sample;
    }

    sample.targetWord0 = reader.read_u32(sample.targetOffset);
    sample.targetSignature = makeSignature(payload, sample.targetOffset);
    sample.targetBytes16Hex = makeHexBytes(payload, sample.targetOffset, 16U);
    return sample;
}

std::vector<MllEmbeddedMldObjectListProbe> probeEmbeddedMldObjectLists(
    std::span<const std::uint8_t> payload,
    const MllEmbeddedMldHeaderProbe& header) {
    std::vector<MllEmbeddedMldObjectListProbe> probes{};
    if (!header.plausible) {
        return probes;
    }

    EndianReader reader(payload, Endian::Big);
    probes.reserve(static_cast<std::size_t>(header.entryCount) * kMldObjectListFieldOffsets.size());

    for (std::uint32_t entryIndex = 0U; entryIndex < header.entryCount; ++entryIndex) {
        const auto entryOffset = header.indexTableOffset + entryIndex * kMldIndexEntryStride;
        if (!canReadRange(payload.size(), entryOffset, kMldIndexEntryStride)) {
            continue;
        }

        for (const auto fieldOffset : kMldObjectListFieldOffsets) {
            MllEmbeddedMldObjectListProbe probe{};
            probe.entryIndex = entryIndex;
            probe.entryOffset = entryOffset;
            probe.fieldOffset = fieldOffset;
            probe.listOffset = reader.read_u32(entryOffset + fieldOffset);
            probe.listOffsetNonZero = probe.listOffset != 0U;
            probe.listOffsetAligned = probe.listOffset % sizeof(std::uint32_t) == 0U;
            if (!probe.listOffsetNonZero) {
                probes.push_back(std::move(probe));
                continue;
            }
            if (!probe.listOffsetAligned) {
                probes.push_back(std::move(probe));
                continue;
            }

            probe.listHeaderInBounds = canReadRange(payload.size(), probe.listOffset, sizeof(std::uint32_t));
            if (!probe.listHeaderInBounds) {
                probes.push_back(std::move(probe));
                continue;
            }

            probe.listBytes32Hex = makeHexBytes(payload, probe.listOffset, 32U);
            probe.declaredCount = reader.read_u32(probe.listOffset);
            constexpr std::uint32_t kPointerSize = sizeof(std::uint32_t);
            const std::uint32_t entriesOffset = probe.listOffset + kPointerSize;
            const auto entriesByteSize = static_cast<std::uint64_t>(probe.declaredCount) * kPointerSize;
            probe.listEntriesInBounds =
                static_cast<std::uint64_t>(entriesOffset) <= payload.size() &&
                entriesByteSize <= payload.size() - entriesOffset;
            probe.listLooksPlausible = probe.listEntriesInBounds;
            if (!probe.listLooksPlausible) {
                probes.push_back(std::move(probe));
                continue;
            }

            const auto readableCount = readableU32CountFrom(payload.size(), entriesOffset);
            probe.sampledPointerCount = std::min({
                probe.declaredCount,
                readableCount,
                kMldObjectListSampleLimit,
            });
            probe.targetSamples.reserve(probe.sampledPointerCount);
            for (std::uint32_t sampleIndex = 0U; sampleIndex < probe.sampledPointerCount; ++sampleIndex) {
                const auto pointerOffset = entriesOffset + sampleIndex * kPointerSize;
                auto sample = probeEmbeddedMldObjectListTarget(
                    payload,
                    reader,
                    sampleIndex,
                    pointerOffset,
                    entryOffset,
                    probe.listOffset);
                if (sample.pointerNonZero) {
                    ++probe.nonNullSampledPointerCount;
                }
                probe.targetSamples.push_back(std::move(sample));
            }

            probes.push_back(std::move(probe));
        }
    }

    return probes;
}

bool indexedBinProbeLooksPlausible(const spice::bin::BinIndexedTableProbe& probe) {
    return probe.present &&
        probe.count != 0U &&
        probe.offsetTableInBounds &&
        probe.offsetsInBounds &&
        probe.offsetsMonotonic;
}

MllPayloadKind classifyPayload(std::span<const std::uint8_t> payload,
    std::string_view memberName,
    const std::string& signature,
    const MllEmbeddedMldHeaderProbe& embeddedMldHeader,
    const MllTextureTableProbe& textureTableProbe,
    const MllPreTextureTableProbe& preTextureTableProbe,
    const std::vector<MllEmbeddedGvrTextureProbe>& embeddedGvrTextureProbes,
    const spice::bin::BinIndexedTableProbe& indexedBinTableProbe) {
    if (payload.empty()) {
        return MllPayloadKind::Empty;
    }
    if (spice::compression::aklz::isAklz(payload)) {
        return MllPayloadKind::AklzCompressed;
    }
    const bool memberNameLooksBin = endsWithCaseInsensitive(memberName, ".bin");
    const bool indexedBinLooksPlausible = indexedBinProbeLooksPlausible(indexedBinTableProbe);
    if (indexedBinLooksPlausible && (memberNameLooksBin || !embeddedMldHeader.indexEntryShapePlausible)) {
        return MllPayloadKind::IndexedBin;
    }

    const bool memberNameLooksMld = endsWithCaseInsensitive(memberName, ".mld");
    const bool hasMldTextureEvidence =
        textureTableProbe.hasTextures ||
        preTextureTableProbe.present ||
        !embeddedGvrTextureProbes.empty();
    if (embeddedMldHeader.plausible &&
        (embeddedMldHeader.indexEntryShapePlausible || memberNameLooksMld || hasMldTextureEvidence)) {
        return MllPayloadKind::MldFile;
    }
    if (signature == "POF0") {
        return MllPayloadKind::Pof0;
    }
    if (signature.size() == 4U && signature[0] == 'N' && signature[1] == 'J') {
        return MllPayloadKind::NinjaChunk;
    }
    return MllPayloadKind::Unknown;
}

std::span<const std::uint8_t> decodeIfNeeded(std::span<const std::uint8_t> input,
    MllFile& file,
    std::vector<std::uint8_t>& decodedStorage) {
    if (!spice::compression::aklz::isAklz(input)) {
        return input;
    }

    file.sourceWasCompressedAklz = true;
    const auto decoded = spice::compression::aklz::decompress(input);
    if (!decoded.ok()) {
        addDiagnostic(file.diagnostics,
            DiagnosticSeverity::Error,
            std::string("AKLZ decompression failed: ") +
                std::string(spice::compression::aklz::errorToString(decoded.error)));
        return {};
    }

    decodedStorage = decoded.bytes;
    return decodedStorage;
}

std::uint16_t inferMemberCountFromFirstMemberOffset(std::uint32_t firstMemberOffset,
    std::size_t decodedSize) {
    if (firstMemberOffset <= kMllRecordsOffset || firstMemberOffset > decodedSize) {
        return 0U;
    }
    const auto tableBytes = firstMemberOffset - kMllRecordsOffset;
    if (tableBytes % kMllRecordStride != 0U) {
        return 0U;
    }
    const auto count = tableBytes / kMllRecordStride;
    if (count > std::numeric_limits<std::uint16_t>::max()) {
        return 0U;
    }
    return static_cast<std::uint16_t>(count);
}

bool rangeIntersects(std::uint32_t offset,
    std::uint32_t size,
    std::uint32_t otherOffset,
    std::uint32_t otherSize) {
    const auto end = static_cast<std::uint64_t>(offset) + size;
    const auto otherEnd = static_cast<std::uint64_t>(otherOffset) + otherSize;
    return offset < otherEnd && otherOffset < end;
}

void classifyTableShape(MllFile& file) {
    const bool hasErrors = std::any_of(file.diagnostics.begin(), file.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
    if (hasErrors) {
        file.tableShape = MllTableShape::MalformedMemberSpans;
        return;
    }
    if (file.memberCountInferredFromFirstMemberOffset != 0U &&
        file.memberCountInferredFromFirstMemberOffset != file.selectedMemberCount) {
        file.tableShape = MllTableShape::FirstMemberCountCandidate;
        return;
    }
    file.tableShape = MllTableShape::Normal;
}

std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::ostringstream message;
        message << "Could not open file: " << path.string();
        throw std::runtime_error(message.str());
    }

    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0) {
        std::ostringstream message;
        message << "Could not determine file size: " << path.string();
        throw std::runtime_error(message.str());
    }
    in.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!in) {
            std::ostringstream message;
            message << "Could not read full file: " << path.string();
            throw std::runtime_error(message.str());
        }
    }
    return bytes;
}

} // namespace

bool MllFile::ok() const {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const MllDiagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

const char* toString(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::Info:
        return "info";
    case DiagnosticSeverity::Warning:
        return "warning";
    case DiagnosticSeverity::Error:
        return "error";
    }
    return "unknown";
}

const char* toString(MllPayloadKind kind) {
    switch (kind) {
    case MllPayloadKind::Empty:
        return "empty";
    case MllPayloadKind::Unknown:
        return "unknown";
    case MllPayloadKind::AklzCompressed:
        return "aklz";
    case MllPayloadKind::IndexedBin:
        return "indexed-bin";
    case MllPayloadKind::MldFile:
        return "mld";
    case MllPayloadKind::NinjaChunk:
        return "ninja-chunk";
    case MllPayloadKind::Pof0:
        return "pof0";
    }
    return "unknown";
}

const char* toString(MllMemberCountSource source) {
    switch (source) {
    case MllMemberCountSource::HeaderU16At04:
        return "header-u16-at-04";
    case MllMemberCountSource::FirstMemberOffset:
        return "first-member-offset";
    case MllMemberCountSource::Unresolved:
        return "unresolved";
    }
    return "unknown";
}

const char* toString(MllTableShape shape) {
    switch (shape) {
    case MllTableShape::Normal:
        return "normal";
    case MllTableShape::FirstMemberCountCandidate:
        return "first-member-count-candidate";
    case MllTableShape::MalformedMemberSpans:
        return "malformed-member-spans";
    }
    return "unknown";
}

MllFile MllParser::parse(std::span<const std::uint8_t> bytes, std::string sourcePath) {
    MllFile file{};
    file.sourcePath = std::move(sourcePath);
    file.rawSize = clampSize(bytes.size());

    std::vector<std::uint8_t> decodedStorage;
    const auto decodedBytes = decodeIfNeeded(bytes, file, decodedStorage);
    file.decodedSize = clampSize(decodedBytes.size());
    file.originalDecodedBytes.assign(decodedBytes.begin(), decodedBytes.end());
    if (!file.ok()) {
        return file;
    }

    if (decodedBytes.size() < kMllRecordsOffset) {
        addDiagnostic(file.diagnostics,
            DiagnosticSeverity::Error,
            "MLL decoded size is smaller than the 0x08-byte provisional header.");
        classifyTableShape(file);
        return file;
    }

    EndianReader reader(decodedBytes, Endian::Big);
    file.headerWord0 = reader.read_u32(0x00U);
    file.countWord = reader.read_u32(0x04U);
    file.signedMemberCountCandidate = reader.read_i16(0x04U);
    file.memberCountCandidate = reader.read_u16(0x04U);

    const bool headerCountTableFits = file.memberCountCandidate != 0U &&
        canReadRange(decodedBytes.size(),
            kMllRecordsOffset,
            static_cast<std::uint32_t>(file.memberCountCandidate) * kMllRecordStride);

    std::uint16_t inferredCount = 0U;
    if (decodedBytes.size() >= kMllRecordsOffset + kMllRecordStride) {
        const auto firstRecordPayloadOffset = reader.read_u32(kMllRecordsOffset + 0x14U);
        inferredCount = inferMemberCountFromFirstMemberOffset(firstRecordPayloadOffset, decodedBytes.size());
    }

    if (headerCountTableFits) {
        file.selectedMemberCount = file.memberCountCandidate;
        file.memberCountSource = MllMemberCountSource::HeaderU16At04;
    } else if (inferredCount != 0U) {
        file.selectedMemberCount = inferredCount;
        file.memberCountSource = MllMemberCountSource::FirstMemberOffset;
        addDiagnostic(file.diagnostics,
            DiagnosticSeverity::Warning,
            "MLL member count was inferred from the first member offset; header count hypothesis did not fit.",
            0x04U);
    } else {
        file.selectedMemberCount = file.memberCountCandidate;
        file.memberCountSource = MllMemberCountSource::Unresolved;
        addDiagnostic(file.diagnostics,
            DiagnosticSeverity::Error,
            "MLL member count could not be resolved from the provisional header or first member offset.",
            0x04U);
        classifyTableShape(file);
        return file;
    }

    file.recordsOffset = kMllRecordsOffset;
    file.recordStride = kMllRecordStride;
    file.memberTableEndOffset = kMllRecordsOffset +
        static_cast<std::uint32_t>(file.selectedMemberCount) * kMllRecordStride;
    file.memberTableInBounds = canReadRange(decodedBytes.size(), file.recordsOffset, file.memberTableEndOffset - file.recordsOffset);
    if (!file.memberTableInBounds) {
        addDiagnostic(file.diagnostics,
            DiagnosticSeverity::Error,
            "MLL provisional member table extends beyond decoded bytes.",
            file.recordsOffset);
        classifyTableShape(file);
        return file;
    }

    file.members.reserve(file.selectedMemberCount);
    for (std::uint32_t i = 0U; i < file.selectedMemberCount; ++i) {
        const auto recordOffset = file.recordsOffset + i * file.recordStride;
        MllMember member{};
        member.index = i;
        member.recordOffset = recordOffset;
        member.name = readMemberName(decodedBytes, recordOffset);
        member.payloadOffset = reader.read_u32(recordOffset + 0x14U);
        member.payloadSize = reader.read_u32(recordOffset + 0x18U);
        member.rawWord1c = reader.read_u32(recordOffset + 0x1CU);
        if (member.payloadOffset != 0U &&
            (file.firstMemberOffset == 0U || member.payloadOffset < file.firstMemberOffset)) {
            file.firstMemberOffset = member.payloadOffset;
        }

        member.payloadInBounds = canReadRange(decodedBytes.size(), member.payloadOffset, member.payloadSize);
        member.payloadOverlapsMemberTable = member.payloadInBounds &&
            member.payloadSize != 0U &&
            rangeIntersects(member.payloadOffset, member.payloadSize, file.recordsOffset, file.memberTableEndOffset - file.recordsOffset);
        if (member.payloadInBounds) {
            member.payloadSignature = makeSignature(decodedBytes, member.payloadOffset);
            const auto payload = decodedBytes.subspan(member.payloadOffset, member.payloadSize);
            member.embeddedMldHeader = probeEmbeddedMldHeader(payload);
            member.embeddedMldObjectListProbes =
                probeEmbeddedMldObjectLists(payload, member.embeddedMldHeader);
            member.embeddedBlockProbes = probeEmbeddedBlocks(payload, member.embeddedMldHeader);
            member.embeddedGvrTextureProbes = probeEmbeddedGvrTextures(payload);
            member.textureTableProbe =
                probeTextureTable(payload, member.embeddedMldHeader, member.embeddedGvrTextureProbes);
            member.preTextureTableProbe =
                probePreTextureTable(
                    payload,
                    member.embeddedMldHeader,
                    member.textureTableProbe,
                    member.embeddedGvrTextureProbes);
            if (endsWithCaseInsensitive(member.name, ".bin") ||
                !member.embeddedMldHeader.indexEntryShapePlausible) {
                member.indexedBinTableProbe = spice::bin::probeIndexedTable(payload);
            }
            member.payloadKind = classifyPayload(
                payload,
                member.name,
                member.payloadSignature,
                member.embeddedMldHeader,
                member.textureTableProbe,
                member.preTextureTableProbe,
                member.embeddedGvrTextureProbes,
                member.indexedBinTableProbe);
        } else {
            addDiagnostic(file.diagnostics,
                DiagnosticSeverity::Error,
                "MLL member payload span is out of bounds.",
                recordOffset);
        }
        if (member.payloadOverlapsMemberTable) {
            addDiagnostic(file.diagnostics,
                DiagnosticSeverity::Warning,
                "MLL member payload overlaps the provisional member table.",
                recordOffset);
        }

        file.members.push_back(std::move(member));
    }

    file.memberCountInferredFromFirstMemberOffset =
        inferMemberCountFromFirstMemberOffset(file.firstMemberOffset, decodedBytes.size());
    file.memberCountMatchesFirstMemberOffset =
        file.memberCountInferredFromFirstMemberOffset == file.selectedMemberCount;
    classifyTableShape(file);
    file.supported = file.ok() &&
        file.tableShape == MllTableShape::Normal &&
        file.memberCountSource == MllMemberCountSource::HeaderU16At04 &&
        !file.members.empty();
    return file;
}

MllFile MllParser::parseFile(const std::filesystem::path& path) {
    return parse(readFileBytes(path), path.string());
}

} // namespace spice::mll
