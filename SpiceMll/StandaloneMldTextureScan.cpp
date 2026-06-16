#include "StandaloneMldTextureScan.h"

#include "../Compression/Aklz.h"
#include "../SpiceCore/Binary/EndianReader.h"
#include "../SpiceGvm/Parsing/GvmParser.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace spice::mll {
namespace {

using spice::core::Endian;
using spice::core::EndianReader;

constexpr std::uint32_t kMldIndexEntryStride = 0x68U;
constexpr std::uint32_t kTextureRecordStride = 0x2cU;
constexpr std::uint32_t kTextureRecordNameSize = 0x10U;
constexpr std::uint32_t kExpectedFileWord24 = 0x80000000U;

struct MldHeaderCandidate {
    Endian endian{ Endian::Big };
    std::uint32_t entryCount{ 0U };
    std::uint32_t indexTableOffset{ 0U };
    std::uint32_t functionParametersOffset{ 0U };
    std::uint32_t realDataOffset{ 0U };
    std::uint32_t textureTableOffset{ 0U };
};

std::uint32_t clampSize(std::size_t size) {
    return static_cast<std::uint32_t>(
        std::min<std::size_t>(size, std::numeric_limits<std::uint32_t>::max()));
}

bool canReadRange(std::size_t size, std::uint32_t offset, std::uint32_t length) {
    return offset <= size && length <= size - offset;
}

std::uint32_t readU32BeUnchecked(std::span<const std::uint8_t> bytes, std::uint32_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
        static_cast<std::uint32_t>(bytes[offset + 3U]);
}

std::uint32_t readU32LeUnchecked(std::span<const std::uint8_t> bytes, std::uint32_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
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

std::string makeSignature(std::span<const std::uint8_t> bytes, std::uint32_t offset) {
    if (!canReadRange(bytes.size(), offset, 4U)) {
        return {};
    }

    std::string signature{};
    signature.reserve(4U);
    for (std::uint32_t i = 0U; i < 4U; ++i) {
        const auto value = bytes[static_cast<std::size_t>(offset) + i];
        signature.push_back(value >= 0x20U && value <= 0x7eU ? static_cast<char>(value) : '.');
    }
    return signature;
}

std::optional<std::uint32_t> readChunkPayloadSize(std::span<const std::uint8_t> bytes, std::uint32_t offset) {
    if (!canReadRange(bytes.size(), offset, 8U)) {
        return std::nullopt;
    }

    const auto remaining = static_cast<std::uint32_t>(bytes.size() - offset);
    const auto le = readU32LeUnchecked(bytes, offset + 4U);
    if (le >= 8U && le <= remaining - 8U) {
        return le;
    }
    const auto be = readU32BeUnchecked(bytes, offset + 4U);
    if (be >= 8U && be <= remaining - 8U) {
        return be;
    }
    return std::nullopt;
}

bool isTextureChunkStart(std::span<const std::uint8_t> bytes, std::uint32_t offset) {
    return matchesTag(bytes, offset, "GBIX") ||
        matchesTag(bytes, offset, "GCIX") ||
        matchesTag(bytes, offset, "GVRT");
}

std::optional<std::uint32_t> computeGvrSourceSize(std::span<const std::uint8_t> bytes, std::uint32_t offset) {
    if (!isTextureChunkStart(bytes, offset)) {
        return std::nullopt;
    }

    if (matchesTag(bytes, offset, "GVRT")) {
        const auto gvrtPayloadSize = readChunkPayloadSize(bytes, offset);
        if (!gvrtPayloadSize.has_value()) {
            return std::nullopt;
        }
        return 8U + *gvrtPayloadSize;
    }

    const auto indexPayloadSize = readChunkPayloadSize(bytes, offset);
    if (!indexPayloadSize.has_value()) {
        return std::nullopt;
    }
    const auto gvrtOffset64 = static_cast<std::uint64_t>(offset) + 8U + *indexPayloadSize;
    if (gvrtOffset64 > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    const auto gvrtOffset = static_cast<std::uint32_t>(gvrtOffset64);
    if (!matchesTag(bytes, gvrtOffset, "GVRT")) {
        return std::nullopt;
    }
    const auto gvrtPayloadSize = readChunkPayloadSize(bytes, gvrtOffset);
    if (!gvrtPayloadSize.has_value()) {
        return std::nullopt;
    }
    const auto sourceEnd = static_cast<std::uint64_t>(gvrtOffset) + 8U + *gvrtPayloadSize;
    if (sourceEnd > bytes.size()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(sourceEnd - offset);
}

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool isMldPath(const std::filesystem::path& path) {
    return toLowerCopy(path.extension().string()) == ".mld";
}

std::string boolText(bool value) {
    return value ? "true" : "false";
}

std::string endianText(Endian endian) {
    return endian == Endian::Little ? "little" : "big";
}

std::string csvEscape(std::string_view value) {
    const bool needsQuotes = value.find_first_of(",\"\r\n") != std::string_view::npos;
    if (!needsQuotes) {
        return std::string(value);
    }

    std::string escaped{};
    escaped.reserve(value.size() + 2U);
    escaped.push_back('"');
    for (const char ch : value) {
        if (ch == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

void appendJsonEscaped(std::ostream& out, std::string_view value) {
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20U) {
                constexpr char kHex[] = "0123456789abcdef";
                out << "\\u00"
                    << kHex[(static_cast<unsigned char>(ch) >> 4U) & 0x0fU]
                    << kHex[static_cast<unsigned char>(ch) & 0x0fU];
            } else {
                out << ch;
            }
            break;
        }
    }
}

void writeJsonString(std::ostream& out, std::string_view value) {
    out << '"';
    appendJsonEscaped(out, value);
    out << '"';
}

std::string joinDiagnostics(const std::vector<std::string>& diagnostics) {
    std::ostringstream out{};
    for (std::size_t i = 0U; i < diagnostics.size(); ++i) {
        if (i != 0U) {
            out << " | ";
        }
        out << diagnostics[i];
    }
    return out.str();
}

std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::ostringstream message{};
        message << "Could not open file: " << path.string();
        throw std::runtime_error(message.str());
    }

    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0) {
        std::ostringstream message{};
        message << "Could not determine file size: " << path.string();
        throw std::runtime_error(message.str());
    }
    in.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!in) {
            std::ostringstream message{};
            message << "Could not read full file: " << path.string();
            throw std::runtime_error(message.str());
        }
    }
    return bytes;
}

void writeTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::ostringstream message{};
        message << "Could not open output file: " << path.string();
        throw std::runtime_error(message.str());
    }
    out << text;
    if (!out.good()) {
        std::ostringstream message{};
        message << "Could not write output file: " << path.string();
        throw std::runtime_error(message.str());
    }
}

std::vector<std::filesystem::path> collectMldPaths(const std::filesystem::path& inputPath, bool& inputWasDirectory) {
    std::error_code ec{};
    inputWasDirectory = std::filesystem::is_directory(inputPath, ec);
    ec.clear();
    const bool inputWasFile = std::filesystem::is_regular_file(inputPath, ec);
    if (!inputWasDirectory && !inputWasFile) {
        std::ostringstream message{};
        message << "Input path is not a file or directory: " << inputPath.string();
        throw std::runtime_error(message.str());
    }

    std::vector<std::filesystem::path> paths{};
    if (inputWasFile) {
        if (isMldPath(inputPath)) {
            paths.push_back(inputPath);
        }
        return paths;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             inputPath,
             std::filesystem::directory_options::skip_permission_denied)) {
        std::error_code entryEc{};
        if (!entry.is_regular_file(entryEc) || entryEc) {
            continue;
        }
        if (isMldPath(entry.path())) {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

std::string relativePathString(
    const std::filesystem::path& path,
    const std::filesystem::path& inputPath,
    bool inputWasDirectory) {
    if (!inputWasDirectory) {
        return path.filename().generic_string();
    }

    std::error_code ec{};
    const auto relative = std::filesystem::relative(path, inputPath, ec);
    if (!ec && !relative.empty()) {
        return relative.generic_string();
    }
    return path.filename().generic_string();
}

std::optional<MldHeaderCandidate> readMldHeaderCandidate(std::span<const std::uint8_t> bytes, Endian endian) {
    if (bytes.size() < 0x14U) {
        return std::nullopt;
    }

    const EndianReader reader(bytes, endian);
    return MldHeaderCandidate{
        .endian = endian,
        .entryCount = reader.read_u32(0x00U),
        .indexTableOffset = reader.read_u32(0x04U),
        .functionParametersOffset = reader.read_u32(0x08U),
        .realDataOffset = reader.read_u32(0x0CU),
        .textureTableOffset = reader.read_u32(0x10U),
    };
}

bool plausibleHeader(std::span<const std::uint8_t> bytes, const MldHeaderCandidate& header) {
    constexpr std::uint32_t kHardEntryCap = 1U << 16U;
    if (header.entryCount == 0U || header.entryCount > kHardEntryCap) {
        return false;
    }
    const std::uint64_t tableEnd = static_cast<std::uint64_t>(header.indexTableOffset) +
        static_cast<std::uint64_t>(header.entryCount) * kMldIndexEntryStride;
    if (tableEnd > bytes.size()) {
        return false;
    }
    const auto offsetInFile = [&](std::uint32_t offset) {
        return offset == 0U || offset < bytes.size();
    };
    return offsetInFile(header.indexTableOffset) &&
        offsetInFile(header.functionParametersOffset) &&
        offsetInFile(header.realDataOffset) &&
        offsetInFile(header.textureTableOffset);
}

std::optional<MldHeaderCandidate> detectMldHeader(std::span<const std::uint8_t> bytes) {
    const auto big = readMldHeaderCandidate(bytes, Endian::Big);
    const auto little = readMldHeaderCandidate(bytes, Endian::Little);
    const bool bigOk = big.has_value() && plausibleHeader(bytes, *big);
    const bool littleOk = little.has_value() && plausibleHeader(bytes, *little);
    if (bigOk && littleOk) {
        return big->entryCount <= little->entryCount ? big : little;
    }
    if (bigOk) {
        return big;
    }
    if (littleOk) {
        return little;
    }
    return std::nullopt;
}

std::string readSlotString(std::span<const std::uint8_t> bytes, std::uint32_t offset, std::uint32_t length) {
    std::string value{};
    if (!canReadRange(bytes.size(), offset, length)) {
        return value;
    }
    value.reserve(length);
    for (std::uint32_t i = 0U; i < length; ++i) {
        const auto byte = bytes[static_cast<std::size_t>(offset) + i];
        if (byte == 0U) {
            break;
        }
        value.push_back(static_cast<char>(byte));
    }
    while (!value.empty() && value.front() == ' ') {
        value.erase(value.begin());
    }
    while (!value.empty() && value.back() == ' ') {
        value.pop_back();
    }
    return value;
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

std::uint32_t expectedPaddingSize(std::uint32_t textureCount) {
    const auto tableBytes = 4U + textureCount * kTextureRecordStride;
    return (0x20U - (tableBytes % 0x20U)) % 0x20U;
}

StandaloneMldTextureFileScan scanFile(
    const std::filesystem::path& path,
    const std::filesystem::path& inputPath,
    bool inputWasDirectory) {
    StandaloneMldTextureFileScan file{};
    file.relativePath = relativePathString(path, inputPath, inputWasDirectory);
    file.absolutePath = std::filesystem::absolute(path).string();

    const auto rawBytes = readFileBytes(path);
    file.rawSize = clampSize(rawBytes.size());

    std::vector<std::uint8_t> decodedStorage{};
    std::span<const std::uint8_t> bytes(rawBytes.data(), rawBytes.size());
    if (spice::compression::aklz::isAklz(bytes)) {
        file.sourceWasCompressedAklz = true;
        const auto decoded = spice::compression::aklz::decompress(bytes);
        if (!decoded.ok()) {
            file.diagnostics.push_back("AKLZ decompression failed: " +
                std::string(spice::compression::aklz::errorToString(decoded.error)));
            return file;
        }
        decodedStorage = decoded.bytes;
        bytes = std::span<const std::uint8_t>(decodedStorage.data(), decodedStorage.size());
    }
    file.decodedSize = clampSize(bytes.size());

    const auto header = detectMldHeader(bytes);
    if (!header.has_value()) {
        file.diagnostics.push_back("No plausible standalone MLD header detected.");
        return file;
    }

    file.headerPlausible = true;
    file.endian = endianText(header->endian);
    file.entryCount = header->entryCount;
    file.indexTableOffset = header->indexTableOffset;
    file.functionParametersOffset = header->functionParametersOffset;
    file.realDataOffset = header->realDataOffset;
    file.textureTableOffset = header->textureTableOffset;
    file.textureTableOffsetInBounds = header->textureTableOffset != 0U &&
        header->textureTableOffset < bytes.size();
    if (!file.textureTableOffsetInBounds) {
        return file;
    }

    const EndianReader reader(bytes, header->endian);
    if (!canReadRange(bytes.size(), file.textureTableOffset, sizeof(std::uint32_t))) {
        file.diagnostics.push_back("Texture table count is out of bounds.");
        return file;
    }

    file.textureTablePresent = true;
    file.textureTableDeclaredCount = reader.read_u32(file.textureTableOffset);
    constexpr std::uint32_t kHardTextureCountCap = 4096U;
    if (file.textureTableDeclaredCount > kHardTextureCountCap) {
        file.diagnostics.push_back("Texture table declared count exceeds hard scan cap.");
        return file;
    }
    const std::uint64_t recordsStart = static_cast<std::uint64_t>(file.textureTableOffset) + sizeof(std::uint32_t);
    const std::uint64_t recordsByteSize = static_cast<std::uint64_t>(file.textureTableDeclaredCount) * kTextureRecordStride;
    const std::uint64_t recordsEnd = recordsStart + recordsByteSize;
    file.expectedPaddingSize = expectedPaddingSize(file.textureTableDeclaredCount);
    const auto expectedFirstTextureOffset64 = recordsEnd + file.expectedPaddingSize;
    if (expectedFirstTextureOffset64 > std::numeric_limits<std::uint32_t>::max()) {
        file.diagnostics.push_back("Expected first texture offset exceeds 32-bit offset range.");
        return file;
    }
    file.firstTextureOffset = static_cast<std::uint32_t>(expectedFirstTextureOffset64);
    file.textureTableRecordsFit =
        recordsStart <= bytes.size() &&
        recordsByteSize <= bytes.size() - recordsStart &&
        file.firstTextureOffset <= bytes.size();
    if (!file.textureTableRecordsFit) {
        file.diagnostics.push_back("Texture table records and expected padding do not fit in file.");
        return file;
    }

    file.trailingPaddingSize = file.expectedPaddingSize;
    file.trailingPaddingMatchesExpected = true;

    std::vector<spice::gvm::model::GvrTexture> orderedTextures{};
    orderedTextures.reserve(file.textureTableDeclaredCount);
    std::uint32_t textureCursor = file.firstTextureOffset;
    for (std::uint32_t textureIndex = 0U; textureIndex < file.textureTableDeclaredCount; ++textureIndex) {
        const auto sourceSize = computeGvrSourceSize(bytes, textureCursor);
        if (!sourceSize.has_value()) {
            std::ostringstream message{};
            message << "Expected texture " << textureIndex << " missing or invalid at offset " << textureCursor << ".";
            file.diagnostics.push_back(message.str());
            break;
        }

        auto texture = spice::gvm::parsing::parseGvrTexture(
            bytes.subspan(textureCursor, *sourceSize),
            textureCursor,
            spice::gvm::parsing::ParseOptions{
                .decodeBaseLevel = false,
                .keepRawEncodedPayload = false,
            });
        texture.sourceOffset = textureCursor;
        texture.sourceSize = *sourceSize;
        orderedTextures.push_back(std::move(texture));
        textureCursor += *sourceSize;
    }

    file.textureChunkCount = clampSize(orderedTextures.size());
    file.hasTextureChunks = !orderedTextures.empty();
    file.textureTableCountMatchesTextureCount = file.textureTableDeclaredCount == file.textureChunkCount;

    file.entries.reserve(file.textureTableDeclaredCount);
    for (std::uint32_t entryIndex = 0U; entryIndex < file.textureTableDeclaredCount; ++entryIndex) {
        const auto entryOffset = file.textureTableOffset +
            static_cast<std::uint32_t>(sizeof(std::uint32_t)) +
            entryIndex * kTextureRecordStride;
        StandaloneMldTextureEntryScan entry{};
        entry.entryIndex = entryIndex;
        entry.entryOffset = entryOffset;
        entry.name = readSlotString(bytes, entryOffset, kTextureRecordNameSize);
        entry.nameEmpty = entry.name.empty();
        entry.namePrintable = isPrintableSlotString(entry.name);
        if (entry.nameEmpty) {
            ++file.emptyNameCount;
        }
        if (entry.namePrintable) {
            ++file.printableNameCount;
        }
        entry.word10 = reader.read_u32(entryOffset + 0x10U);
        entry.word14 = reader.read_u32(entryOffset + 0x14U);
        entry.word18 = reader.read_u32(entryOffset + 0x18U);
        entry.word1c = reader.read_u32(entryOffset + 0x1CU);
        entry.word20 = reader.read_u32(entryOffset + 0x20U);
        entry.word24 = reader.read_u32(entryOffset + 0x24U);
        entry.word28 = reader.read_u32(entryOffset + 0x28U);
        if (entry.word10 != 0U) {
            ++file.word10NonZeroCount;
        }
        if (entry.word14 != 0U) {
            ++file.word14NonZeroCount;
        }
        if (entry.word18 != 0U) {
            ++file.word18NonZeroCount;
        }
        if (entry.word1c != 0U) {
            ++file.word1cNonZeroCount;
        }
        if (entry.word20 != 0U) {
            ++file.word20NonZeroCount;
        }
        if (entry.word24 != kExpectedFileWord24) {
            ++file.word24Not80000000Count;
        }
        if (entryIndex < orderedTextures.size()) {
            const auto& texture = orderedTextures[entryIndex];
            entry.orderTexturePresent = true;
            entry.orderTextureIndex = entryIndex;
            entry.orderTextureOffset = clampSize(texture.sourceOffset);
            entry.orderTextureStartTag = makeSignature(bytes, entry.orderTextureOffset);
            entry.orderTextureSourceSize = clampSize(texture.sourceSize);
            entry.orderTextureParsed = texture.width != 0U &&
                texture.height != 0U &&
                spice::gvm::model::to_string(texture.textureFormat) != "Unknown";
            entry.orderTextureDecoded =
                texture.decodedBaseLevel.has_value() && !texture.decodedBaseLevel->rgba8.empty();
            entry.orderTextureHasGlobalIndex = texture.hasGlobalIndex;
            entry.orderTextureGlobalIndex = texture.globalIndex;
            entry.orderTextureRawFlags = texture.rawFlags;
            entry.orderTextureRawDataFormat = texture.rawDataFormat;
            entry.orderTextureFormat = spice::gvm::model::to_string(texture.textureFormat);
            entry.orderTexturePaletteFormat = spice::gvm::model::to_string(texture.paletteFormat);
            entry.orderTextureWidth = texture.width;
            entry.orderTextureHeight = texture.height;
            if (entry.orderTextureParsed) {
                ++file.parsedTextureCount;
            }
            if (entry.orderTextureDecoded) {
                ++file.decodedTextureCount;
            }
            if (entry.word28 != entry.orderTextureSourceSize) {
                ++file.word28SourceSizeMismatchCount;
            }
        }
        file.entries.push_back(std::move(entry));
    }
    return file;
}

} // namespace

StandaloneMldTextureCorpusScan scanStandaloneMldTextures(const std::filesystem::path& inputPath) {
    StandaloneMldTextureCorpusScan corpus{};
    corpus.inputPath = inputPath.string();
    const auto paths = collectMldPaths(inputPath, corpus.inputWasDirectory);
    corpus.files.reserve(paths.size());
    for (const auto& path : paths) {
        corpus.files.push_back(scanFile(path, inputPath, corpus.inputWasDirectory));
    }
    return corpus;
}

StandaloneMldTextureFeedbackSummary summarizeStandaloneMldTextureFeedback(
    const StandaloneMldTextureCorpusScan& corpus) {
    StandaloneMldTextureFeedbackSummary summary{};
    summary.fileCount = corpus.files.size();
    for (const auto& file : corpus.files) {
        if (file.sourceWasCompressedAklz) {
            ++summary.compressedFileCount;
        }
        if (file.headerPlausible) {
            ++summary.plausibleHeaderFileCount;
        }
        if (file.hasTextureChunks) {
            ++summary.textureChunkFileCount;
        }
        if (file.textureTablePresent) {
            ++summary.textureTableFileCount;
        }
        if (file.textureTableRecordsFit) {
            ++summary.recordsFitFileCount;
        }
        if (file.textureTableCountMatchesTextureCount) {
            ++summary.countMatchesTextureCountFileCount;
        }
        if (file.trailingPaddingMatchesExpected) {
            ++summary.paddingMatchesFileCount;
        }
        summary.totalTextureChunks += file.textureChunkCount;
        summary.totalTextureTableEntries += file.entries.size();
        summary.totalPrintableNames += file.printableNameCount;
        summary.totalEmptyNames += file.emptyNameCount;
        summary.totalWord10NonZero += file.word10NonZeroCount;
        summary.totalWord14NonZero += file.word14NonZeroCount;
        summary.totalWord18NonZero += file.word18NonZeroCount;
        summary.totalWord1cNonZero += file.word1cNonZeroCount;
        summary.totalWord20NonZero += file.word20NonZeroCount;
        summary.totalWord24Not80000000 += file.word24Not80000000Count;
        summary.totalWord28SourceSizeMismatches += file.word28SourceSizeMismatchCount;
        summary.totalParsedTextures += file.parsedTextureCount;
        summary.totalDecodedTextures += file.decodedTextureCount;
        summary.totalDiagnostics += file.diagnostics.size();
    }
    return summary;
}

std::string formatStandaloneMldTextureJson(const StandaloneMldTextureCorpusScan& corpus) {
    const auto summary = summarizeStandaloneMldTextureFeedback(corpus);
    std::ostringstream out{};
    out << "{\n";
    out << "  \"inputPath\": ";
    writeJsonString(out, corpus.inputPath);
    out << ",\n";
    out << "  \"inputWasDirectory\": " << boolText(corpus.inputWasDirectory) << ",\n";
    out << "  \"fileCount\": " << summary.fileCount << ",\n";
    out << "  \"compressedFileCount\": " << summary.compressedFileCount << ",\n";
    out << "  \"plausibleHeaderFileCount\": " << summary.plausibleHeaderFileCount << ",\n";
    out << "  \"textureChunkFileCount\": " << summary.textureChunkFileCount << ",\n";
    out << "  \"textureTableFileCount\": " << summary.textureTableFileCount << ",\n";
    out << "  \"recordsFitFileCount\": " << summary.recordsFitFileCount << ",\n";
    out << "  \"countMatchesTextureCountFileCount\": " << summary.countMatchesTextureCountFileCount << ",\n";
    out << "  \"paddingMatchesFileCount\": " << summary.paddingMatchesFileCount << ",\n";
    out << "  \"totalTextureChunks\": " << summary.totalTextureChunks << ",\n";
    out << "  \"totalTextureTableEntries\": " << summary.totalTextureTableEntries << ",\n";
    out << "  \"totalPrintableNames\": " << summary.totalPrintableNames << ",\n";
    out << "  \"totalEmptyNames\": " << summary.totalEmptyNames << ",\n";
    out << "  \"totalWord10NonZero\": " << summary.totalWord10NonZero << ",\n";
    out << "  \"totalWord14NonZero\": " << summary.totalWord14NonZero << ",\n";
    out << "  \"totalWord18NonZero\": " << summary.totalWord18NonZero << ",\n";
    out << "  \"totalWord1cNonZero\": " << summary.totalWord1cNonZero << ",\n";
    out << "  \"totalWord20NonZero\": " << summary.totalWord20NonZero << ",\n";
    out << "  \"totalWord24Not80000000\": " << summary.totalWord24Not80000000 << ",\n";
    out << "  \"totalWord28SourceSizeMismatches\": " << summary.totalWord28SourceSizeMismatches << ",\n";
    out << "  \"totalParsedTextures\": " << summary.totalParsedTextures << ",\n";
    out << "  \"totalDecodedTextures\": " << summary.totalDecodedTextures << ",\n";
    out << "  \"totalDiagnostics\": " << summary.totalDiagnostics << "\n";
    out << "}\n";
    return out.str();
}

std::string formatStandaloneMldTextureFilesCsv(const StandaloneMldTextureCorpusScan& corpus) {
    std::ostringstream out{};
    out << "path,absolutePath,sourceWasCompressedAklz,rawSize,decodedSize,headerPlausible,endian,entryCount,"
           "indexTableOffset,functionParametersOffset,realDataOffset,textureTableOffset,textureTableOffsetInBounds,"
           "hasTextureChunks,textureChunkCount,firstTextureOffset,textureTableDeclaredCount,textureTablePresent,"
           "textureTableRecordsFit,textureTableCountMatchesTextureCount,expectedPaddingSize,trailingPaddingSize,"
           "trailingPaddingMatchesExpected,printableNameCount,emptyNameCount,word10NonZeroCount,word14NonZeroCount,"
           "word18NonZeroCount,word1cNonZeroCount,word20NonZeroCount,word24Not80000000Count,"
           "word28SourceSizeMismatchCount,parsedTextureCount,decodedTextureCount,diagnostics\n";
    for (const auto& file : corpus.files) {
        out << csvEscape(file.relativePath) << ","
            << csvEscape(file.absolutePath) << ","
            << boolText(file.sourceWasCompressedAklz) << ","
            << file.rawSize << ","
            << file.decodedSize << ","
            << boolText(file.headerPlausible) << ","
            << csvEscape(file.endian) << ","
            << file.entryCount << ","
            << file.indexTableOffset << ","
            << file.functionParametersOffset << ","
            << file.realDataOffset << ","
            << file.textureTableOffset << ","
            << boolText(file.textureTableOffsetInBounds) << ","
            << boolText(file.hasTextureChunks) << ","
            << file.textureChunkCount << ","
            << file.firstTextureOffset << ","
            << file.textureTableDeclaredCount << ","
            << boolText(file.textureTablePresent) << ","
            << boolText(file.textureTableRecordsFit) << ","
            << boolText(file.textureTableCountMatchesTextureCount) << ","
            << file.expectedPaddingSize << ","
            << file.trailingPaddingSize << ","
            << boolText(file.trailingPaddingMatchesExpected) << ","
            << file.printableNameCount << ","
            << file.emptyNameCount << ","
            << file.word10NonZeroCount << ","
            << file.word14NonZeroCount << ","
            << file.word18NonZeroCount << ","
            << file.word1cNonZeroCount << ","
            << file.word20NonZeroCount << ","
            << file.word24Not80000000Count << ","
            << file.word28SourceSizeMismatchCount << ","
            << file.parsedTextureCount << ","
            << file.decodedTextureCount << ","
            << csvEscape(joinDiagnostics(file.diagnostics)) << "\n";
    }
    return out.str();
}

std::string formatStandaloneMldTextureEntriesCsv(const StandaloneMldTextureCorpusScan& corpus) {
    std::ostringstream out{};
    out << "filePath,entryIndex,entryOffset,name,namePrintable,nameEmpty,word10,word14,word18,word1c,word20,word24,"
           "word28,orderTexturePresent,orderTextureIndex,orderTextureOffset,orderTextureStartTag,orderTextureSourceSize,orderTextureParsed,"
           "orderTextureDecoded,orderTextureHasGlobalIndex,orderTextureGlobalIndex,orderTextureRawFlags,"
           "orderTextureRawDataFormat,orderTextureFormat,orderTexturePaletteFormat,orderTextureWidth,orderTextureHeight\n";
    for (const auto& file : corpus.files) {
        for (const auto& entry : file.entries) {
            out << csvEscape(file.relativePath) << ","
                << entry.entryIndex << ","
                << entry.entryOffset << ","
                << csvEscape(entry.name) << ","
                << boolText(entry.namePrintable) << ","
                << boolText(entry.nameEmpty) << ","
                << entry.word10 << ","
                << entry.word14 << ","
                << entry.word18 << ","
                << entry.word1c << ","
                << entry.word20 << ","
                << entry.word24 << ","
                << entry.word28 << ","
                << boolText(entry.orderTexturePresent) << ","
                << entry.orderTextureIndex << ","
                << entry.orderTextureOffset << ","
                << csvEscape(entry.orderTextureStartTag) << ","
                << entry.orderTextureSourceSize << ","
                << boolText(entry.orderTextureParsed) << ","
                << boolText(entry.orderTextureDecoded) << ","
                << boolText(entry.orderTextureHasGlobalIndex) << ","
                << entry.orderTextureGlobalIndex << ","
                << entry.orderTextureRawFlags << ","
                << entry.orderTextureRawDataFormat << ","
                << csvEscape(entry.orderTextureFormat) << ","
                << csvEscape(entry.orderTexturePaletteFormat) << ","
                << entry.orderTextureWidth << ","
                << entry.orderTextureHeight << "\n";
        }
    }
    return out.str();
}

StandaloneMldTextureWriteResult writeStandaloneMldTextureArtifacts(
    const StandaloneMldTextureCorpusScan& corpus,
    const std::filesystem::path& outputDir) {
    std::filesystem::create_directories(outputDir);

    StandaloneMldTextureWriteResult result{};
    result.jsonPath = outputDir / "standalone_mld_texture_scan.json";
    result.filesCsvPath = outputDir / "standalone_mld_texture_files.csv";
    result.entriesCsvPath = outputDir / "standalone_mld_texture_entries.csv";
    writeTextFile(result.jsonPath, formatStandaloneMldTextureJson(corpus));
    writeTextFile(result.filesCsvPath, formatStandaloneMldTextureFilesCsv(corpus));
    writeTextFile(result.entriesCsvPath, formatStandaloneMldTextureEntriesCsv(corpus));
    return result;
}

} // namespace spice::mll
