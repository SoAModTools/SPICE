#include "BinParser.h"

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

bool canReadRange(const std::size_t size, const std::uint32_t offset, const std::uint32_t length)
{
    return offset <= size && length <= size - offset;
}

std::uint32_t readU32BeUnchecked(std::span<const std::uint8_t> bytes, const std::uint32_t offset)
{
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
        static_cast<std::uint32_t>(bytes[offset + 3U]);
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

BinIndexedTableProbe probeIndexedTable(std::span<const std::uint8_t> bytes)
{
    BinIndexedTableProbe probe{};
    probe.headerInBounds = canReadRange(bytes.size(), 0U, sizeof(std::uint32_t));
    if (!probe.headerInBounds) {
        return probe;
    }

    probe.count = readU32BeUnchecked(bytes, 0U);
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
    probe.offsetTableInBounds = canReadRange(
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
        const auto relativeRecordOffset = readU32BeUnchecked(bytes, offsetEntryOffset);
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
        sample.recordInBounds = canReadRange(bytes.size(), sample.recordOffset, 8U);
        if (sample.recordInBounds) {
            sample.word0 = readU32BeUnchecked(bytes, sample.recordOffset);
            sample.word0EqualsDataBaseOffset = sample.word0 == probe.dataBaseOffset;
            sample.word4 = readU32BeUnchecked(bytes, sample.recordOffset + 4U);
            sample.word4TargetInBounds = sample.word4 < bytes.size();
            sample.bytes16Hex = makeHexBytes(bytes, sample.recordOffset, 16U);
            sample.bytes32Hex = makeHexBytes(bytes, sample.recordOffset, 32U);
            if (canReadRange(bytes.size(), sample.recordOffset, 0x1cU)) {
                sample.word8 = readU32BeUnchecked(bytes, sample.recordOffset + 0x08U);
                sample.word12 = readU32BeUnchecked(bytes, sample.recordOffset + 0x0cU);
                sample.word16 = readU32BeUnchecked(bytes, sample.recordOffset + 0x10U);
                sample.word20 = readU32BeUnchecked(bytes, sample.recordOffset + 0x14U);
                sample.word24 = readU32BeUnchecked(bytes, sample.recordOffset + 0x18U);
            }
        } else if (sample.recordOffset < bytes.size()) {
            sample.bytes16Hex = makeHexBytes(bytes, sample.recordOffset, 16U);
            sample.bytes32Hex = makeHexBytes(bytes, sample.recordOffset, 32U);
        }
        probe.samples.push_back(std::move(sample));
    }

    return probe;
}

BinFile parseBytes(std::vector<std::uint8_t> bytes, std::string sourcePath)
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

    result.indexedTableProbe = probeIndexedTable(result.bytes);
    return result;
}

BinFile parseFile(const std::filesystem::path& path)
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

    return parseBytes(std::move(bytes), result.sourcePath);
}

} // namespace spice::bin
