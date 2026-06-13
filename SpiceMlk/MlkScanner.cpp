#include "MlkScanner.h"

#include "../Compression/Aklz.h"
#include "../SpiceCore/Binary/EndianReader.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

namespace spice::mlk {
namespace {

using spice::core::Endian;
using spice::core::EndianReader;

constexpr std::uint32_t kMlkRecordsOffset = 0x08U;
constexpr std::uint32_t kMlkRecordStride = 0x10U;

void addDiagnostic(std::vector<MlkDiagnostic>& diagnostics,
    DiagnosticSeverity severity,
    std::string message,
    std::uint32_t offset = 0U) {
    diagnostics.push_back(MlkDiagnostic{ severity, std::move(message), offset });
}

bool canReadRange(std::size_t size, std::uint32_t offset, std::uint32_t length) {
    return offset <= size && length <= size - offset;
}

bool rangesOverlap(std::uint32_t leftOffset,
    std::uint32_t leftLength,
    std::uint32_t rightOffset,
    std::uint32_t rightLength) {
    const std::uint64_t leftEnd =
        static_cast<std::uint64_t>(leftOffset) + static_cast<std::uint64_t>(leftLength);
    const std::uint64_t rightEnd =
        static_cast<std::uint64_t>(rightOffset) + static_cast<std::uint64_t>(rightLength);
    return static_cast<std::uint64_t>(leftOffset) < rightEnd &&
        static_cast<std::uint64_t>(rightOffset) < leftEnd;
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

MlkEmbeddedMldHeaderProbe probeEmbeddedMldHeader(std::span<const std::uint8_t> payload) {
    MlkEmbeddedMldHeaderProbe probe{};
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
        static_cast<std::uint64_t>(probe.entryCount) * 0x68U;
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
    return probe;
}

MlkPayloadKind classifyPayload(std::span<const std::uint8_t> bytes,
    std::uint32_t offset,
    std::uint32_t length,
    const std::string& signature,
    const MlkEmbeddedMldHeaderProbe& embeddedMldHeader) {
    if (length == 0U) {
        return MlkPayloadKind::Empty;
    }
    if (!canReadRange(bytes.size(), offset, length)) {
        return MlkPayloadKind::Unknown;
    }

    const auto payload = bytes.subspan(offset, length);
    if (spice::compression::aklz::isAklz(payload)) {
        return MlkPayloadKind::AklzCompressed;
    }
    if (embeddedMldHeader.plausible) {
        return MlkPayloadKind::MldFile;
    }
    if (signature == "POF0") {
        return MlkPayloadKind::Pof0;
    }
    if (signature.size() == 4U && signature[0] == 'N' && signature[1] == 'J') {
        return MlkPayloadKind::NinjaChunk;
    }
    return MlkPayloadKind::Unknown;
}

std::span<const std::uint8_t> decodeIfNeeded(std::span<const std::uint8_t> input,
    MlkScanResult& result,
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

std::uint16_t inferCountFromFirstPayloadOffset(std::uint32_t firstPayloadOffset) {
    if (firstPayloadOffset < kMlkRecordsOffset) {
        return 0U;
    }
    const auto tableSize = firstPayloadOffset - kMlkRecordsOffset;
    if (tableSize % kMlkRecordStride != 0U) {
        return 0U;
    }
    const auto inferred = tableSize / kMlkRecordStride;
    if (inferred > std::numeric_limits<std::uint16_t>::max()) {
        return 0U;
    }
    return static_cast<std::uint16_t>(inferred);
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

bool MlkScanResult::ok() const {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const MlkDiagnostic& diagnostic) {
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

const char* toString(MlkPayloadKind kind) {
    switch (kind) {
    case MlkPayloadKind::Empty:
        return "empty";
    case MlkPayloadKind::Unknown:
        return "unknown";
    case MlkPayloadKind::AklzCompressed:
        return "aklz";
    case MlkPayloadKind::MldFile:
        return "mld";
    case MlkPayloadKind::NinjaChunk:
        return "ninja-chunk";
    case MlkPayloadKind::Pof0:
        return "pof0";
    }
    return "unknown";
}

const char* toString(MlkRecordCountSource source) {
    switch (source) {
    case MlkRecordCountSource::HeaderU16At04:
        return "header-u16-at-0x04";
    case MlkRecordCountSource::FirstPayloadOffset:
        return "first-payload-offset";
    case MlkRecordCountSource::Unresolved:
        return "unresolved";
    }
    return "unknown";
}

const char* toString(MlkTableShape shape) {
    switch (shape) {
    case MlkTableShape::Normal:
        return "normal";
    case MlkTableShape::FirstPayloadCountCandidate:
        return "first-payload-count-candidate";
    case MlkTableShape::MalformedRecordSpans:
        return "malformed-record-spans";
    }
    return "unknown";
}

MlkScanResult MlkScanner::scan(std::span<const std::uint8_t> bytes, std::string sourcePath) {
    MlkScanResult result{};
    result.sourcePath = std::move(sourcePath);
    result.rawSize = static_cast<std::uint32_t>(
        std::min<std::size_t>(bytes.size(), std::numeric_limits<std::uint32_t>::max()));

    std::vector<std::uint8_t> decodedStorage;
    const auto decodedBytes = decodeIfNeeded(bytes, result, decodedStorage);
    result.decodedSize = static_cast<std::uint32_t>(
        std::min<std::size_t>(decodedBytes.size(), std::numeric_limits<std::uint32_t>::max()));

    EndianReader reader(decodedBytes, Endian::Big);
    for (std::size_t i = 0; i < result.headerWords.size(); ++i) {
        const auto word = reader.try_read_u32(i * sizeof(std::uint32_t));
        result.headerWords[i] = word.value_or(0U);
    }

    const auto count = reader.try_read_i16(0x04U);
    const auto rawCount = reader.try_read_u16(0x04U);
    if (!count.has_value() || !rawCount.has_value()) {
        addDiagnostic(result.diagnostics, DiagnosticSeverity::Error, "MLK is too small for header");
        return result;
    }

    result.signedRecordCountCandidate = *count;
    result.recordCountCandidate = *rawCount;
    if (*count < 0) {
        addDiagnostic(result.diagnostics,
            DiagnosticSeverity::Error,
            "MLK record count candidate is negative",
            0x04U);
        return result;
    }

    const auto firstPayloadOffset = reader.try_read_u32(kMlkRecordsOffset + 0x04U);
    if (firstPayloadOffset.has_value()) {
        result.firstPayloadOffset = *firstPayloadOffset;
        result.recordCountInferredFromFirstPayloadOffset =
            inferCountFromFirstPayloadOffset(*firstPayloadOffset);
        result.recordCountMatchesFirstPayloadOffset =
            result.recordCountInferredFromFirstPayloadOffset == result.recordCountCandidate;
    }

    const std::uint64_t tableEnd =
        static_cast<std::uint64_t>(kMlkRecordsOffset) +
        static_cast<std::uint64_t>(result.recordCountCandidate) * kMlkRecordStride;
    result.selectedRecordCount = result.recordCountCandidate;
    result.recordCountSource = MlkRecordCountSource::HeaderU16At04;

    std::uint64_t selectedTableEnd = tableEnd;
    if (selectedTableEnd > decodedBytes.size()) {
        const std::uint64_t inferredTableEnd =
            static_cast<std::uint64_t>(kMlkRecordsOffset) +
            static_cast<std::uint64_t>(result.recordCountInferredFromFirstPayloadOffset) *
                kMlkRecordStride;
        if (result.recordCountInferredFromFirstPayloadOffset > 0U &&
            inferredTableEnd <= decodedBytes.size()) {
            result.selectedRecordCount = result.recordCountInferredFromFirstPayloadOffset;
            result.recordCountSource = MlkRecordCountSource::FirstPayloadOffset;
            selectedTableEnd = inferredTableEnd;
            addDiagnostic(result.diagnostics,
                DiagnosticSeverity::Warning,
                "MLK header record count table is out of bounds; using first payload offset inference",
                0x04U);
        } else {
            result.recordCountSource = MlkRecordCountSource::Unresolved;
            result.recordTableEndOffset = selectedTableEnd <= std::numeric_limits<std::uint32_t>::max()
                ? static_cast<std::uint32_t>(selectedTableEnd)
                : std::numeric_limits<std::uint32_t>::max();
            addDiagnostic(result.diagnostics,
                DiagnosticSeverity::Error,
                "MLK record table extends beyond decoded file",
                kMlkRecordsOffset);
            return result;
        }
    }

    result.recordTableEndOffset = selectedTableEnd <= std::numeric_limits<std::uint32_t>::max()
        ? static_cast<std::uint32_t>(selectedTableEnd)
        : std::numeric_limits<std::uint32_t>::max();
    result.recordTableInBounds = selectedTableEnd <= decodedBytes.size();

    result.records.reserve(result.selectedRecordCount);
    std::set<std::uint32_t> seenKeys;
    for (std::uint32_t i = 0U; i < result.selectedRecordCount; ++i) {
        const std::uint32_t recordOffset = kMlkRecordsOffset + (i * kMlkRecordStride);
        MlkRecordProbe record{};
        record.index = i;
        record.recordOffset = recordOffset;
        record.key = reader.read_u32(recordOffset + 0x00U);
        record.payloadOffset = reader.read_u32(recordOffset + 0x04U);
        record.payloadSize = reader.read_u32(recordOffset + 0x08U);
        record.rawWord12 = reader.read_u32(recordOffset + 0x0CU);
        record.duplicateKey = !seenKeys.insert(record.key).second;
        record.payloadInBounds = canReadRange(decodedBytes.size(), record.payloadOffset, record.payloadSize);
        record.payloadOverlapsRecordTable = rangesOverlap(record.payloadOffset,
            record.payloadSize,
            kMlkRecordsOffset,
            result.recordTableEndOffset - kMlkRecordsOffset);

        if (record.payloadInBounds) {
            if (i == 0U) {
                if (!result.recordCountMatchesFirstPayloadOffset) {
                    addDiagnostic(result.diagnostics,
                        DiagnosticSeverity::Warning,
                        "MLK first payload offset does not match the record count candidate",
                        record.recordOffset + 0x04U);
                }
            }
            record.payloadSignature = makeSignature(decodedBytes, record.payloadOffset);
            const auto payload =
                decodedBytes.subspan(record.payloadOffset, record.payloadSize);
            record.embeddedMldHeader = probeEmbeddedMldHeader(payload);
            record.payloadKind =
                classifyPayload(decodedBytes,
                    record.payloadOffset,
                    record.payloadSize,
                    record.payloadSignature,
                    record.embeddedMldHeader);
        } else {
            addDiagnostic(result.diagnostics,
                DiagnosticSeverity::Error,
                "MLK record payload span is out of bounds",
                record.recordOffset + 0x04U);
        }

        if (record.duplicateKey) {
            addDiagnostic(result.diagnostics,
                DiagnosticSeverity::Warning,
                "MLK record key duplicates an earlier record key",
                record.recordOffset);
        }
        if (record.payloadOverlapsRecordTable && record.payloadSize != 0U) {
            addDiagnostic(result.diagnostics,
                DiagnosticSeverity::Warning,
                "MLK record payload overlaps the record table",
                record.recordOffset + 0x04U);
        }

        result.records.push_back(std::move(record));
    }

    return result;
}

MlkScanResult MlkScanner::scanFile(const std::filesystem::path& path) {
    const auto bytes = readFileBytes(path);
    return scan(bytes, path.string());
}

} // namespace spice::mlk
