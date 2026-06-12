#include "SmlParser.h"

#include "../Compression/Aklz.h"
#include "../SpiceCore/Binary/EndianReader.h"

#include <algorithm>
#include <limits>

namespace spice::sstsml {
namespace {

using spice::core::Endian;
using spice::core::EndianReader;

constexpr std::uint32_t kSmlRecordsOffset = 0x08U;
constexpr std::uint32_t kSmlRecordStride = 0x10U;

void addDiagnostic(std::vector<ParseDiagnostic>& diagnostics,
    DiagnosticSeverity severity,
    std::string message,
    std::uint32_t offset = 0U) {
    diagnostics.push_back(ParseDiagnostic{ severity, std::move(message), offset });
}

bool canReadRange(std::size_t size, std::uint32_t offset, std::uint32_t length) {
    return offset <= size && length <= size - offset;
}

std::vector<std::uint8_t> copyRange(std::span<const std::uint8_t> bytes,
    std::uint32_t offset,
    std::uint32_t length) {
    if (!canReadRange(bytes.size(), offset, length)) {
        return {};
    }
    return std::vector<std::uint8_t>(bytes.begin() + offset, bytes.begin() + offset + length);
}

std::span<const std::uint8_t> decodeIfNeeded(std::span<const std::uint8_t> input,
    SmlParseResult& result,
    std::vector<std::uint8_t>& decodedStorage) {
    if (!spice::compression::aklz::isAklz(input)) {
        return input;
    }

    result.sourceWasCompressedAklz = true;
    const auto decoded = spice::compression::aklz::decompress(input);
    if (!decoded.ok()) {
        addDiagnostic(result.diagnostics,
            DiagnosticSeverity::Error,
            std::string("AKLZ decompression failed: ") +
                std::string(spice::compression::aklz::errorToString(decoded.error)));
        return {};
    }

    decodedStorage = decoded.bytes;
    return decodedStorage;
}

} // namespace

bool SmlParseResult::ok() const {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const ParseDiagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

SmlParseResult SmlParser::parse(std::span<const std::uint8_t> bytes, std::string sourcePath) {
    SmlParseResult result{};
    result.sourcePath = std::move(sourcePath);

    std::vector<std::uint8_t> decodedStorage;
    const auto decodedBytes = decodeIfNeeded(bytes, result, decodedStorage);
    result.decodedSize = static_cast<std::uint32_t>(
        std::min<std::size_t>(decodedBytes.size(), std::numeric_limits<std::uint32_t>::max()));

    EndianReader reader(decodedBytes, Endian::Big);
    const auto header0 = reader.try_read_u32(0x00U);
    const auto recordCountWord = reader.try_read_u32(0x04U);
    const auto count = reader.try_read_u16(0x04U);
    if (!header0.has_value() || !recordCountWord.has_value() || !count.has_value()) {
        addDiagnostic(result.diagnostics, DiagnosticSeverity::Error, "SML is too small for header");
        return result;
    }

    result.rawHeader0 = *header0;
    result.rawRecordCountWord = *recordCountWord;
    result.recordCount = *count;

    const std::uint64_t tableEnd =
        static_cast<std::uint64_t>(kSmlRecordsOffset) +
        static_cast<std::uint64_t>(result.recordCount) * kSmlRecordStride;
    if (tableEnd > decodedBytes.size()) {
        addDiagnostic(result.diagnostics,
            DiagnosticSeverity::Error,
            "SML record table extends beyond decoded file",
            kSmlRecordsOffset);
        return result;
    }

    result.records.reserve(static_cast<std::size_t>(result.recordCount));
    for (std::uint32_t i = 0U; i < result.recordCount; ++i) {
        const std::uint32_t recordOffset = kSmlRecordsOffset + (i * kSmlRecordStride);
        SmlRecord record{};
        record.index = i;
        record.recordOffset = recordOffset;
        record.rawWord0 = reader.read_u32(recordOffset + 0x00U);
        record.embeddedMldOffset = reader.read_u32(recordOffset + 0x04U);
        record.embeddedMldSize = reader.read_u32(recordOffset + 0x08U);
        record.rawWord12 = reader.read_u32(recordOffset + 0x0CU);
        record.embeddedMldInBounds =
            canReadRange(decodedBytes.size(), record.embeddedMldOffset, record.embeddedMldSize);
        if (record.embeddedMldInBounds) {
            record.embeddedMldBytes =
                copyRange(decodedBytes, record.embeddedMldOffset, record.embeddedMldSize);
        } else {
            addDiagnostic(result.diagnostics,
                DiagnosticSeverity::Error,
                "SML embedded MLD span is out of bounds",
                record.embeddedMldOffset);
        }
        result.records.push_back(std::move(record));
    }

    return result;
}

} // namespace spice::sstsml
