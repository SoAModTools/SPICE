#include "BinParser.h"
#include "../Compression/Aklz.h"
#include "../SpiceRoot/Binary/Alignment.h"
#include "../SpiceRoot/Binary/EndianReader.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace spice::bin {
namespace {

constexpr std::uint32_t kIndexedRecordSampleLimit = 8U;
constexpr std::uint32_t kU32Size = sizeof(std::uint32_t);

void addDiagnostic(BinFile& file, DiagnosticSeverity severity, std::string message, std::uint32_t offset = 0U)
{
    file.diagnostics.push_back(BinDiagnostic{ severity, std::move(message), offset });
}

std::string makeHexBytes(std::span<const std::uint8_t> bytes, const std::uint32_t offset, const std::uint32_t maxLength)
{
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

std::string previewU32List(const std::vector<std::uint32_t>& values, const std::size_t limit)
{
    std::ostringstream out;
    const auto count = std::min(values.size(), limit);
    for (std::size_t i = 0U; i < count; ++i) {
        if (i != 0U) {
            out << " ";
        }
        out << values[i];
    }
    if (values.size() > count) {
        out << " ...";
    }
    return out.str();
}

std::uint32_t probeScore(const BinIndexedTableProbe& probe) {
    if (!probe.present || !probe.offsetsInBounds) return 0U;
    return 1U + (probe.offsetsMonotonic ? 2U : 0U) + probe.sampledRecordCount;
}

} // namespace

bool BinFile::ok() const
{
    for (const BinDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            return false;
        }
    }

    return true;
}

const char* toString(DiagnosticSeverity severity)
{
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

BinIndexedTableProbe probeIndexedTable(std::span<const std::uint8_t> bytes,
    spice::root::Endian endian)
{
    BinIndexedTableProbe probe{};
    probe.endian = endian;
    const spice::root::EndianReader reader(bytes, endian);
    probe.headerInBounds = spice::root::bounds_contains(bytes.size(), 0U, sizeof(std::uint32_t));
    if (!probe.headerInBounds) {
        return probe;
    }

    probe.count = reader.read_u32(0U);
    if (probe.count == 0U) {
        return probe;
    }

    const auto offsetTableByteSize64 = static_cast<std::uint64_t>(probe.count) * kU32Size;
    const auto offsetTableEnd64 = static_cast<std::uint64_t>(probe.offsetTableOffset) + offsetTableByteSize64;
    if (offsetTableEnd64 > std::numeric_limits<std::uint32_t>::max()) {
        return probe;
    }

    probe.offsetTableEndOffset = static_cast<std::uint32_t>(offsetTableEnd64);
    probe.dataBaseOffset = probe.offsetTableEndOffset;
    probe.offsetTableInBounds = spice::root::bounds_contains(
        bytes.size(),
        probe.offsetTableOffset,
        probe.offsetTableEndOffset - probe.offsetTableOffset);
    if (!probe.offsetTableInBounds) {
        return probe;
    }

    std::vector<std::uint32_t> offsets{};
    offsets.reserve(probe.count);
    probe.offsetsInBounds = true;
    probe.offsetsMonotonic = true;
    std::uint32_t previousOffset = 0U;
    for (std::uint32_t i = 0U; i < probe.count; ++i) {
        const auto offsetEntryOffset = probe.offsetTableOffset + i * kU32Size;
        const auto relativeRecordOffset = reader.read_u32(offsetEntryOffset);
        offsets.push_back(relativeRecordOffset);
        if (i != 0U && relativeRecordOffset < previousOffset) {
            probe.offsetsMonotonic = false;
        }
        previousOffset = relativeRecordOffset;

        const auto recordOffset64 =
            static_cast<std::uint64_t>(probe.dataBaseOffset) + relativeRecordOffset;
        if (recordOffset64 > bytes.size()) {
            probe.offsetsInBounds = false;
        }
    }

    probe.present = probe.offsetTableInBounds;
    probe.offsetsPreview = previewU32List(offsets, 16U);
    if (!offsets.empty()) {
        probe.firstRecordOffset = probe.dataBaseOffset + offsets.front();
        probe.lastRecordOffset = probe.dataBaseOffset + offsets.back();
    }

    const auto sampleCount = std::min<std::uint32_t>(probe.count, kIndexedRecordSampleLimit);
    probe.sampledRecordCount = sampleCount;
    probe.samples.reserve(sampleCount);
    for (std::uint32_t i = 0U; i < sampleCount; ++i) {
        BinIndexedRecordSample sample{};
        sample.sampleIndex = i;
        sample.tableOffset = probe.offsetTableOffset + i * kU32Size;
        sample.recordOffset = probe.dataBaseOffset + offsets[i];
        sample.recordInBounds = spice::root::bounds_contains(bytes.size(), sample.recordOffset, 8U);
        if (sample.recordInBounds) {
            sample.word0 = reader.read_u32(sample.recordOffset);
            sample.word0EqualsDataBaseOffset = sample.word0 == probe.dataBaseOffset;
            sample.word4 = reader.read_u32(sample.recordOffset + 4U);
            sample.word4TargetInBounds = sample.word4 < bytes.size();
            sample.bytes16Hex = makeHexBytes(bytes, sample.recordOffset, 16U);
            sample.bytes32Hex = makeHexBytes(bytes, sample.recordOffset, 32U);
            if (spice::root::bounds_contains(bytes.size(), sample.recordOffset, 0x1cU)) {
                sample.word8 = reader.read_u32(sample.recordOffset + 0x08U);
                sample.word12 = reader.read_u32(sample.recordOffset + 0x0cU);
                sample.word16 = reader.read_u32(sample.recordOffset + 0x10U);
                sample.word20 = reader.read_u32(sample.recordOffset + 0x14U);
                sample.word24 = reader.read_u32(sample.recordOffset + 0x18U);
            }
        } else if (sample.recordOffset < bytes.size()) {
            sample.bytes16Hex = makeHexBytes(bytes, sample.recordOffset, 16U);
            sample.bytes32Hex = makeHexBytes(bytes, sample.recordOffset, 32U);
        }
        probe.samples.push_back(std::move(sample));
    }

    return probe;
}

BinIndexedTableProbe probeIndexedTable(std::span<const std::uint8_t> bytes)
{
    const auto big = probeIndexedTable(bytes, spice::root::Endian::Big);
    const auto little = probeIndexedTable(bytes, spice::root::Endian::Little);
    const auto bigScore = probeScore(big);
    const auto littleScore = probeScore(little);
    if (bigScore == littleScore) return {};
    return littleScore > bigScore ? little : big;
}

BinFile parseBytes(std::vector<std::uint8_t> bytes,
    std::string sourcePath,
    const BinParseOptions& options)
{
    BinFile result{};
    result.sourcePath = std::move(sourcePath);
    result.bytes = std::move(bytes);

    if (result.bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.rawSize = std::numeric_limits<std::uint32_t>::max();
        addDiagnostic(result, DiagnosticSeverity::Error, "BIN payload is too large to represent with a 32-bit size");
        return result;
    }

    result.rawSize = static_cast<std::uint32_t>(result.bytes.size());
    if (result.bytes.empty()) {
        addDiagnostic(result, DiagnosticSeverity::Warning, "BIN payload is empty");
        return result;
    }

    if (spice::compression::aklz::isAklz(result.bytes)) {
        result.sourceWasCompressedAklz = true;
        const auto decoded = spice::compression::aklz::decompress(result.bytes);
        if (!decoded.ok()) {
            addDiagnostic(result, DiagnosticSeverity::Error,
                "AKLZ decompression failed: " + std::string(spice::compression::aklz::errorToString(decoded.error)));
            return result;
        }
        result.decodedBytes = decoded.bytes;
    } else {
        result.decodedBytes = result.bytes;
    }
    result.decodedSize = static_cast<std::uint32_t>(result.decodedBytes.size());

    if (options.forcedEndian.has_value()) {
        result.indexedTableProbe = probeIndexedTable(result.decodedBytes, *options.forcedEndian);
    } else {
        const auto big = probeIndexedTable(result.decodedBytes, spice::root::Endian::Big);
        const auto little = probeIndexedTable(result.decodedBytes, spice::root::Endian::Little);
        const auto bigScore = probeScore(big);
        const auto littleScore = probeScore(little);
        if (bigScore != 0U && bigScore == littleScore) {
            addDiagnostic(result, DiagnosticSeverity::Warning,
                "Indexed BIN byte order is ambiguous; supply a forced endian for HRSBin interpretation");
        } else {
            result.indexedTableProbe = littleScore > bigScore ? little : big;
        }
    }
    if (result.indexedTableProbe.present && result.indexedTableProbe.offsetsInBounds) {
        result.sourceEndian = result.indexedTableProbe.endian;
        result.endianWasForced = options.forcedEndian.has_value();
    }
    return result;
}

BinFile parseFile(const std::filesystem::path& path, const BinParseOptions& options)
{
    BinFile result{};
    result.sourcePath = path.string();

    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        addDiagnostic(result, DiagnosticSeverity::Error, "Unable to open BIN file");
        return result;
    }

    const std::ifstream::pos_type end = stream.tellg();
    if (end < 0) {
        addDiagnostic(result, DiagnosticSeverity::Error, "Unable to determine BIN file size");
        return result;
    }

    const auto size = static_cast<std::uintmax_t>(end);
    if (size > std::numeric_limits<std::size_t>::max()) {
        addDiagnostic(result, DiagnosticSeverity::Error, "BIN file is too large to load on this platform");
        return result;
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);

    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            addDiagnostic(result, DiagnosticSeverity::Error, "Unable to read complete BIN file");
            return result;
        }
    }

    return parseBytes(std::move(bytes), result.sourcePath, options);
}

} // namespace spice::bin
