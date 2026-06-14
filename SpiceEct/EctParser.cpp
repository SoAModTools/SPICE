#include "EctParser.h"

#include "../Compression/Aklz.h"
#include "../SpiceCore/Binary/EndianReader.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace spice::ect {
namespace {

using spice::core::Endian;
using spice::core::EndianReader;

constexpr std::uint32_t kEncounterTableSize = 0x84U;
constexpr std::uint32_t kEncounterEntryCount = 32U;
constexpr std::uint32_t kEncounterEntrySize = 0x04U;
constexpr std::uint32_t kIndexedHeaderSize = 0x08U;
constexpr std::uint32_t kIndexRecordSize = 0x20U;
constexpr std::uint32_t kIndexTitleMaxLength = 0x14U;
constexpr std::uint32_t kIndexDataOffsetInRecord = 0x14U;
constexpr std::uint32_t kIndexedTablesPerEntry = 8U;

void addDiagnostic(std::vector<EctDiagnostic>& diagnostics,
    DiagnosticSeverity severity,
    std::string message,
    std::uint32_t offset = 0U) {
    diagnostics.push_back(EctDiagnostic{ severity, std::move(message), offset });
}

bool canReadRange(std::size_t size, std::uint32_t offset, std::uint32_t length) {
    return offset <= size && length <= size - offset;
}

std::uint32_t clampSize(std::size_t size) {
    return static_cast<std::uint32_t>(
        std::min<std::size_t>(size, std::numeric_limits<std::uint32_t>::max()));
}

bool isDamTitle(const std::string& title) {
    return title.size() >= 3U && title[0] == 'd' && title[1] == 'a' && title[2] == 'm';
}

bool isPrintableTitleByte(std::uint8_t value) {
    return value == 0U || (value >= 0x20U && value <= 0x7eU);
}

std::string readIndexTitle(std::span<const std::uint8_t> bytes, std::uint32_t recordOffset) {
    std::string title;
    for (std::uint32_t i = 0; i < kIndexTitleMaxLength; ++i) {
        const auto value = bytes[static_cast<std::size_t>(recordOffset) + i];
        if (value == 0U) {
            break;
        }
        title.push_back(static_cast<char>(value));
    }
    return title;
}

bool looksLikeIndexedContainer(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kIndexedHeaderSize) {
        return false;
    }

    EndianReader reader(bytes, Endian::Big);
    const auto indexCount = reader.read_u16(0x04U);
    if (indexCount == 0U) {
        return false;
    }

    const auto indexBytes = static_cast<std::uint64_t>(indexCount) * kIndexRecordSize;
    const auto indexEnd = static_cast<std::uint64_t>(kIndexedHeaderSize) + indexBytes;
    if (indexEnd > bytes.size()) {
        return false;
    }

    bool sawParsedEntry = false;
    for (std::uint16_t i = 0; i < indexCount; ++i) {
        const auto recordOffset = kIndexedHeaderSize + static_cast<std::uint32_t>(i) * kIndexRecordSize;
        bool titleHasNonNull = false;
        for (std::uint32_t j = 0; j < kIndexTitleMaxLength; ++j) {
            const auto value = bytes[static_cast<std::size_t>(recordOffset) + j];
            if (!isPrintableTitleByte(value)) {
                return false;
            }
            titleHasNonNull = titleHasNonNull || value != 0U;
        }

        const auto title = readIndexTitle(bytes, recordOffset);
        const auto dataOffset = reader.read_u32(recordOffset + kIndexDataOffsetInRecord);
        if (title.empty() && !titleHasNonNull) {
            continue;
        }
        if (isDamTitle(title)) {
            continue;
        }
        if (!canReadRange(bytes.size(), dataOffset, kIndexedTablesPerEntry * kEncounterTableSize)) {
            return false;
        }
        sawParsedEntry = true;
    }

    return sawParsedEntry;
}

EctEncounterTable parseTable(std::span<const std::uint8_t> bytes,
    std::uint32_t offset,
    std::size_t tableIndex) {
    EndianReader reader(bytes, Endian::Big);
    EctEncounterTable table{};
    table.tableIndex = tableIndex;
    table.decodedOffset = offset;
    table.stage = reader.read_u16(offset);
    table.overallEncounterRate = reader.read_u16(offset + 0x02U);
    table.encounters.reserve(kEncounterEntryCount);

    auto entryOffset = offset + 0x04U;
    for (std::uint32_t i = 0; i < kEncounterEntryCount; ++i) {
        EctEncounterEntry entry{};
        entry.entryIndex = i;
        entry.encounterId = reader.read_u16(entryOffset);
        entry.encounterRate = reader.read_u16(entryOffset + 0x02U);
        table.encounters.push_back(entry);
        entryOffset += kEncounterEntrySize;
    }

    return table;
}

void parseFlatTables(std::span<const std::uint8_t> bytes, EctFile& file) {
    file.layout = EctLayout::FlatTables;
    if (bytes.empty()) {
        addDiagnostic(file.diagnostics, DiagnosticSeverity::Error, "Flat ECT file is empty.");
        return;
    }
    if (bytes.size() % kEncounterTableSize != 0U) {
        addDiagnostic(file.diagnostics,
            DiagnosticSeverity::Error,
            "Flat ECT decoded size is not a multiple of 0x84.",
            clampSize(bytes.size()));
        return;
    }

    const auto tableCount = bytes.size() / kEncounterTableSize;
    file.tables.reserve(tableCount);
    for (std::size_t i = 0; i < tableCount; ++i) {
        const auto offset = static_cast<std::uint32_t>(i * kEncounterTableSize);
        file.tables.push_back(parseTable(bytes, offset, file.tables.size()));
    }
}

void parseIndexedContainer(std::span<const std::uint8_t> bytes, EctFile& file) {
    file.layout = EctLayout::IndexedContainer;
    if (bytes.size() < kIndexedHeaderSize) {
        addDiagnostic(file.diagnostics,
            DiagnosticSeverity::Error,
            "Indexed ECT decoded size is smaller than the 0x08-byte header.");
        return;
    }

    EndianReader reader(bytes, Endian::Big);
    const auto indexCount = reader.read_u16(0x04U);
    const auto indexBytes = static_cast<std::uint64_t>(indexCount) * kIndexRecordSize;
    const auto indexEnd = static_cast<std::uint64_t>(kIndexedHeaderSize) + indexBytes;
    if (indexEnd > bytes.size()) {
        addDiagnostic(file.diagnostics,
            DiagnosticSeverity::Error,
            "Indexed ECT record table extends beyond decoded bytes.",
            kIndexedHeaderSize);
        return;
    }

    file.indexEntries.reserve(indexCount);
    for (std::uint16_t i = 0; i < indexCount; ++i) {
        const auto recordOffset = kIndexedHeaderSize + static_cast<std::uint32_t>(i) * kIndexRecordSize;
        EctIndexEntry entry{};
        entry.rowIndex = i;
        entry.title = readIndexTitle(bytes, recordOffset);
        entry.dataOffset = reader.read_u32(recordOffset + kIndexDataOffsetInRecord);
        entry.skippedDam = isDamTitle(entry.title);

        if (!entry.skippedDam) {
            const auto tableBytes = kIndexedTablesPerEntry * kEncounterTableSize;
            if (!canReadRange(bytes.size(), entry.dataOffset, tableBytes)) {
                addDiagnostic(file.diagnostics,
                    DiagnosticSeverity::Error,
                    "Indexed ECT entry table span extends beyond decoded bytes.",
                    entry.dataOffset);
                file.indexEntries.push_back(std::move(entry));
                continue;
            }

            entry.firstParsedTableIndex = file.tables.size();
            entry.tableCount = kIndexedTablesPerEntry;
            for (std::uint32_t table = 0; table < kIndexedTablesPerEntry; ++table) {
                const auto tableOffset = entry.dataOffset + table * kEncounterTableSize;
                file.tables.push_back(parseTable(bytes, tableOffset, file.tables.size()));
            }
        }

        file.indexEntries.push_back(std::move(entry));
    }
}

std::span<const std::uint8_t> decodeIfNeeded(std::span<const std::uint8_t> input,
    EctFile& file,
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

bool sourcePathContains099(const std::string& sourcePath) {
    const auto filename = std::filesystem::path(sourcePath).filename().string();
    return filename.find("099") != std::string::npos;
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

bool EctFile::ok() const {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const EctDiagnostic& diagnostic) {
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

const char* toString(EctLayoutHint hint) {
    switch (hint) {
    case EctLayoutHint::Auto:
        return "auto";
    case EctLayoutHint::FlatTables:
        return "flat-tables";
    case EctLayoutHint::IndexedContainer:
        return "indexed-container";
    }
    return "unknown";
}

const char* toString(EctLayout layout) {
    switch (layout) {
    case EctLayout::Unknown:
        return "unknown";
    case EctLayout::FlatTables:
        return "flat-tables";
    case EctLayout::IndexedContainer:
        return "indexed-container";
    }
    return "unknown";
}

EctFile EctParser::parse(std::span<const std::uint8_t> bytes,
    std::string sourcePath,
    EctParseOptions options) {
    EctFile file{};
    file.sourcePath = std::move(sourcePath);
    file.rawSize = clampSize(bytes.size());

    std::vector<std::uint8_t> decodedStorage;
    const auto decodedBytes = decodeIfNeeded(bytes, file, decodedStorage);
    file.decodedSize = clampSize(decodedBytes.size());
    if (!file.ok()) {
        return file;
    }

    auto layoutHint = options.layoutHint;
    if (layoutHint == EctLayoutHint::Auto) {
        layoutHint = sourcePathContains099(file.sourcePath) || looksLikeIndexedContainer(decodedBytes)
            ? EctLayoutHint::IndexedContainer
            : EctLayoutHint::FlatTables;
    }

    if (layoutHint == EctLayoutHint::IndexedContainer) {
        parseIndexedContainer(decodedBytes, file);
    } else {
        parseFlatTables(decodedBytes, file);
    }

    return file;
}

EctFile EctParser::parseFile(const std::filesystem::path& path, EctParseOptions options) {
    return parse(readFileBytes(path), path.string(), options);
}

} // namespace spice::ect
