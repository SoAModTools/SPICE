#include "EctParser.h"

#include "../Compression/Aklz.h"
#include "../SpiceRoot/Binary/Alignment.h"
#include "../SpiceRoot/Binary/EndianReader.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <utility>

namespace spice::ect {
namespace {

using spice::root::Endian;
using spice::root::EndianReader;

constexpr std::size_t kEncounterTableSize = 0x84U;
constexpr std::size_t kEncounterEntrySize = 0x04U;
constexpr std::size_t kIndexedHeaderSize = 0x08U;
constexpr std::size_t kIndexRecordSize = 0x20U;
constexpr std::size_t kIndexTitleSize = 0x14U;
constexpr std::size_t kIndexDataOffsetInRecord = 0x14U;
constexpr std::size_t kIndexDataSizeInRecord = 0x18U;
constexpr std::size_t kIndexTailInRecord = 0x1CU;
constexpr std::uint32_t kIndexedPayloadSize =
    static_cast<std::uint32_t>(kOverworldTablesPerEntry * kEncounterTableSize);

void addDiagnostic(
    std::vector<EctDiagnostic>& diagnostics,
    DiagnosticSeverity severity,
    std::string message,
    std::optional<std::size_t> offset = std::nullopt) {
    diagnostics.push_back(EctDiagnostic{ severity, std::move(message), offset });
}

bool hasErrors(const std::vector<EctDiagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const EctDiagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

bool isPrintableAscii(std::uint8_t value) {
    return value >= 0x20U && value <= 0x7EU;
}

std::optional<std::string> readAndValidateTitle(
    std::span<const std::uint8_t> bytes,
    std::size_t recordOffset,
    std::vector<EctDiagnostic>& diagnostics) {
    std::string title;
    bool sawTerminator = false;
    for (std::size_t i = 0; i < kIndexTitleSize; ++i) {
        const auto value = bytes[recordOffset + i];
        if (value == 0U) {
            sawTerminator = true;
            continue;
        }
        if (sawTerminator) {
            addDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                "A099 title contains nonzero bytes after its terminator.",
                recordOffset + i);
            return std::nullopt;
        }
        if (!isPrintableAscii(value)) {
            addDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                "A099 title contains a non-printable ASCII byte.",
                recordOffset + i);
            return std::nullopt;
        }
        title.push_back(static_cast<char>(value));
    }

    if (title.empty()) {
        addDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "A099 title is empty.",
            recordOffset);
        return std::nullopt;
    }
    return title;
}

EctEncounterTable parseTable(const EndianReader& reader, std::size_t offset) {
    EctEncounterTable table{};
    table.stage = reader.read_u16(offset);
    table.overallEncounterRate = reader.read_u16(offset + 0x02U);

    auto entryOffset = offset + 0x04U;
    for (auto& encounter : table.encounters) {
        encounter.encounterId = reader.read_u16(entryOffset);
        encounter.encounterRate = reader.read_u16(entryOffset + 0x02U);
        entryOffset += kEncounterEntrySize;
    }
    return table;
}

std::optional<EctFile> parseFlat(
    std::span<const std::uint8_t> bytes,
    Endian endian,
    std::vector<EctDiagnostic>& diagnostics) {
    if (bytes.empty()) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Error, "Flat ECT file is empty.");
        return std::nullopt;
    }
    if (bytes.size() % kEncounterTableSize != 0U) {
        addDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "Flat ECT decoded size is not a multiple of 0x84.",
            bytes.size());
        return std::nullopt;
    }

    EndianReader reader(bytes, endian);
    EctFlatContent content{};
    const auto tableCount = bytes.size() / kEncounterTableSize;
    content.tables.reserve(tableCount);
    for (std::size_t i = 0; i < tableCount; ++i) {
        content.tables.push_back(parseTable(reader, i * kEncounterTableSize));
    }
    return EctFile{ EctContent{ std::move(content) } };
}

struct PayloadSpan {
    std::size_t begin{ 0U };
    std::size_t end{ 0U };
    std::size_t recordIndex{ 0U };
};

std::optional<EctFile> parseOverworld(
    std::span<const std::uint8_t> bytes,
    Endian endian,
    std::vector<EctDiagnostic>& diagnostics) {
    if (bytes.size() < kIndexedHeaderSize) {
        addDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "A099 ECT file is smaller than its 0x08-byte header.");
        return std::nullopt;
    }

    EndianReader reader(bytes, endian);
    if (reader.read_u16(0x00U) != 0U ||
        reader.read_u16(0x02U) != 0xFFFFU ||
        reader.read_u16(0x06U) != 0xFFFFU) {
        addDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "A099 ECT header does not contain the canonical 0, 0xFFFF, count, 0xFFFF words.",
            0U);
        return std::nullopt;
    }

    const auto entryCount = reader.read_u16(0x04U);
    if (entryCount == 0U) {
        addDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "A099 ECT contains no index entries.",
            0x04U);
        return std::nullopt;
    }

    const auto indexBytes = static_cast<std::size_t>(entryCount) * kIndexRecordSize;
    const auto indexEnd = kIndexedHeaderSize + indexBytes;
    if (!spice::root::bounds_contains(bytes.size(), kIndexedHeaderSize, indexBytes)) {
        addDiagnostic(
            diagnostics,
            DiagnosticSeverity::Error,
            "A099 index record table extends beyond decoded bytes.",
            kIndexedHeaderSize);
        return std::nullopt;
    }

    EctOverworldContent content{};
    content.entries.reserve(entryCount);
    std::vector<PayloadSpan> spans;
    spans.reserve(entryCount);

    for (std::size_t i = 0; i < entryCount; ++i) {
        const auto recordOffset = kIndexedHeaderSize + i * kIndexRecordSize;
        const auto title = readAndValidateTitle(bytes, recordOffset, diagnostics);
        const auto dataOffset = static_cast<std::size_t>(
            reader.read_u32(recordOffset + kIndexDataOffsetInRecord));
        const auto dataSize = reader.read_u32(recordOffset + kIndexDataSizeInRecord);
        const auto tail = reader.read_u32(recordOffset + kIndexTailInRecord);

        if (!title.has_value()) {
            continue;
        }
        if (dataSize != kIndexedPayloadSize) {
            addDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                "A099 index entry payload size is not the canonical 0x420 bytes.",
                recordOffset + kIndexDataSizeInRecord);
            continue;
        }
        if (tail != 0xFFFFFFFFU) {
            addDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                "A099 index entry trailing word is not 0xFFFFFFFF.",
                recordOffset + kIndexTailInRecord);
            continue;
        }
        if (dataOffset < indexEnd || !spice::root::bounds_contains(bytes.size(), dataOffset, dataSize)) {
            addDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                "A099 index entry payload span is outside decoded payload bytes.",
                recordOffset + kIndexDataOffsetInRecord);
            continue;
        }

        EctOverworldEntry entry{};
        entry.title = *title;
        for (std::size_t tableIndex = 0; tableIndex < entry.tables.size(); ++tableIndex) {
            entry.tables[tableIndex] = parseTable(
                reader,
                dataOffset + tableIndex * kEncounterTableSize);
        }
        content.entries.push_back(std::move(entry));
        spans.push_back(PayloadSpan{ dataOffset, dataOffset + dataSize, i });
    }

    std::sort(spans.begin(), spans.end(), [](const PayloadSpan& left, const PayloadSpan& right) {
        return left.begin < right.begin;
    });
    for (std::size_t i = 1; i < spans.size(); ++i) {
        if (spans[i].begin < spans[i - 1U].end) {
            std::ostringstream message;
            message << "A099 index entry payload overlaps record " << spans[i - 1U].recordIndex << '.';
            addDiagnostic(
                diagnostics,
                DiagnosticSeverity::Error,
                message.str(),
                spans[i].begin);
        }
    }

    if (hasErrors(diagnostics)) {
        return std::nullopt;
    }
    return EctFile{ EctContent{ std::move(content) } };
}

std::string lowercaseFilename(const std::filesystem::path& path) {
    auto filename = path.filename().string();
    std::transform(filename.begin(), filename.end(), filename.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return filename;
}

} // namespace

bool EctParseResult::ok() const noexcept {
    return file.has_value() && !hasErrors(diagnostics);
}

EctParseResult EctParser::parse(std::span<const std::uint8_t> bytes, EctLayout layout) {
    EctParseResult result{};
    std::vector<std::uint8_t> decodedStorage;
    auto decodedBytes = bytes;
    auto endian = Endian::Little;

    if (spice::compression::aklz::isAklz(bytes)) {
        const auto decoded = spice::compression::aklz::decompress(bytes);
        if (!decoded.ok()) {
            addDiagnostic(
                result.diagnostics,
                DiagnosticSeverity::Error,
                std::string("AKLZ decompression failed: ") +
                    std::string(spice::compression::aklz::errorToString(decoded.error)));
            return result;
        }
        decodedStorage = decoded.bytes;
        decodedBytes = decodedStorage;
        endian = Endian::Big;
    }

    if (layout == EctLayout::Flat) {
        result.file = parseFlat(decodedBytes, endian, result.diagnostics);
    } else {
        result.file = parseOverworld(decodedBytes, endian, result.diagnostics);
    }
    return result;
}

EctParseResult EctParser::parseFile(const std::filesystem::path& path) {
    EctParseResult result{};
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        addDiagnostic(
            result.diagnostics,
            DiagnosticSeverity::Error,
            std::string("Could not open ECT file: ") + path.string());
        return result;
    }

    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0) {
        addDiagnostic(
            result.diagnostics,
            DiagnosticSeverity::Error,
            std::string("Could not determine ECT file size: ") + path.string());
        return result;
    }
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input) {
            addDiagnostic(
                result.diagnostics,
                DiagnosticSeverity::Error,
                std::string("Could not read full ECT file: ") + path.string());
            return result;
        }
    }

    const auto layout = lowercaseFilename(path) == "a099a.ect"
        ? EctLayout::OverworldIndexed
        : EctLayout::Flat;
    return parse(bytes, layout);
}

} // namespace spice::ect
