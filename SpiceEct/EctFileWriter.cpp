#include "EctFileWriter.h"

#include "../Compression/Aklz.h"
#include "../SpiceCore/Binary/EndianWriter.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace spice::ect {
namespace {

using spice::core::Endian;
using spice::core::EndianWriter;

constexpr std::size_t kEncounterTableSize = 0x84U;
constexpr std::size_t kIndexedHeaderSize = 0x08U;
constexpr std::size_t kIndexRecordSize = 0x20U;
constexpr std::size_t kIndexTitleSize = 0x14U;
constexpr std::uint32_t kIndexedPayloadSize =
    static_cast<std::uint32_t>(kOverworldTablesPerEntry * kEncounterTableSize);

void addDiagnostic(
    EctWriteResult& result,
    DiagnosticSeverity severity,
    std::string message,
    std::optional<std::size_t> offset = std::nullopt) {
    result.diagnostics.push_back(EctDiagnostic{ severity, std::move(message), offset });
}

bool hasErrors(const EctWriteResult& result) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const EctDiagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

bool isValidTitle(const std::string& title) {
    if (title.empty() || title.size() > kIndexTitleSize) {
        return false;
    }
    return std::all_of(title.begin(), title.end(), [](unsigned char value) {
        return value >= 0x20U && value <= 0x7EU;
    });
}

void writeTable(EndianWriter& writer, const EctEncounterTable& table) {
    writer.write_u16(table.stage);
    writer.write_u16(table.overallEncounterRate);
    for (const auto& encounter : table.encounters) {
        writer.write_u16(encounter.encounterId);
        writer.write_u16(encounter.encounterRate);
    }
}

std::vector<std::uint8_t> writeFlat(
    const EctFlatContent& content,
    Endian endian,
    EctWriteResult& result) {
    if (content.tables.empty()) {
        addDiagnostic(result, DiagnosticSeverity::Error, "Cannot write an empty flat ECT file.");
        return {};
    }
    if (content.tables.size() > std::numeric_limits<std::uint32_t>::max() / kEncounterTableSize) {
        addDiagnostic(result, DiagnosticSeverity::Error, "Flat ECT output exceeds 32-bit size limits.");
        return {};
    }

    EndianWriter writer(endian);
    writer.reserve(content.tables.size() * kEncounterTableSize);
    for (const auto& table : content.tables) {
        writeTable(writer, table);
    }
    return writer.take_data();
}

std::vector<std::uint8_t> writeOverworld(
    const EctOverworldContent& content,
    Endian endian,
    EctWriteResult& result) {
    if (content.entries.empty()) {
        addDiagnostic(result, DiagnosticSeverity::Error, "Cannot write an empty A099 ECT file.");
        return {};
    }
    if (content.entries.size() > std::numeric_limits<std::uint16_t>::max()) {
        addDiagnostic(result, DiagnosticSeverity::Error, "A099 ECT contains more than 65535 entries.");
        return {};
    }

    const auto maxSize = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
    if (content.entries.size() >
        (maxSize - kIndexedHeaderSize) / (kIndexRecordSize + kIndexedPayloadSize)) {
        addDiagnostic(result, DiagnosticSeverity::Error, "A099 ECT output exceeds 32-bit offset limits.");
        return {};
    }

    for (std::size_t i = 0; i < content.entries.size(); ++i) {
        if (!isValidTitle(content.entries[i].title)) {
            addDiagnostic(
                result,
                DiagnosticSeverity::Error,
                "A099 entry title must contain 1-20 printable ASCII bytes.",
                i);
        }
    }
    if (hasErrors(result)) {
        return {};
    }

    const auto indexBytes = content.entries.size() * kIndexRecordSize;
    const auto firstPayloadOffset = kIndexedHeaderSize + indexBytes;
    EndianWriter writer(endian);
    writer.reserve(firstPayloadOffset + content.entries.size() * kIndexedPayloadSize);

    writer.write_u16(0U);
    writer.write_u16(0xFFFFU);
    writer.write_u16(static_cast<std::uint16_t>(content.entries.size()));
    writer.write_u16(0xFFFFU);

    auto payloadOffset = firstPayloadOffset;
    for (const auto& entry : content.entries) {
        for (std::size_t i = 0; i < kIndexTitleSize; ++i) {
            writer.write_u8(i < entry.title.size()
                    ? static_cast<std::uint8_t>(entry.title[i])
                    : 0U);
        }
        writer.write_u32(static_cast<std::uint32_t>(payloadOffset));
        writer.write_u32(kIndexedPayloadSize);
        writer.write_u32(0xFFFFFFFFU);
        payloadOffset += kIndexedPayloadSize;
    }

    for (const auto& entry : content.entries) {
        for (const auto& table : entry.tables) {
            writeTable(writer, table);
        }
    }
    return writer.take_data();
}

} // namespace

bool EctWriteResult::ok() const noexcept {
    return !bytes.empty() && !hasErrors(*this);
}

EctWriteResult EctFileWriter::write(
    const EctFile& file,
    EctTargetPlatform targetPlatform) const {
    EctWriteResult result{};
    const auto endian = targetPlatform == EctTargetPlatform::GameCube
        ? Endian::Big
        : Endian::Little;

    std::vector<std::uint8_t> decoded;
    if (const auto* flat = std::get_if<EctFlatContent>(&file.content)) {
        decoded = writeFlat(*flat, endian, result);
    } else {
        decoded = writeOverworld(std::get<EctOverworldContent>(file.content), endian, result);
    }
    if (hasErrors(result)) {
        return result;
    }

    if (targetPlatform == EctTargetPlatform::Dreamcast) {
        result.bytes = std::move(decoded);
        return result;
    }

    const auto compressed = spice::compression::aklz::compress(decoded);
    if (!compressed.ok()) {
        addDiagnostic(
            result,
            DiagnosticSeverity::Error,
            std::string("AKLZ compression failed: ") +
                std::string(spice::compression::aklz::errorToString(compressed.error)));
        return result;
    }
    result.bytes = compressed.bytes;
    return result;
}

} // namespace spice::ect
