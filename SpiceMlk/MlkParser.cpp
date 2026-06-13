#include "MlkParser.h"

#include "MlkScanner.h"

#include <algorithm>

namespace spice::mlk {
namespace {

MlkRecord makeRecord(const MlkRecordProbe& probe) {
    MlkRecord record{};
    record.index = probe.index;
    record.recordOffset = probe.recordOffset;
    record.key = probe.key;
    record.payloadOffset = probe.payloadOffset;
    record.payloadSize = probe.payloadSize;
    record.rawWord12 = probe.rawWord12;
    record.payloadInBounds = probe.payloadInBounds;
    record.payloadOverlapsRecordTable = probe.payloadOverlapsRecordTable;
    record.duplicateKey = probe.duplicateKey;
    record.payloadKind = probe.payloadKind;
    record.payloadSignature = probe.payloadSignature;
    record.embeddedMldHeader = probe.embeddedMldHeader;
    return record;
}

MlkFile makeFile(const MlkScanResult& scan) {
    MlkFile file{};
    file.sourcePath = scan.sourcePath;
    file.sourceWasCompressedAklz = scan.sourceWasCompressedAklz;
    file.rawSize = scan.rawSize;
    file.decodedSize = scan.decodedSize;
    file.headerWords = scan.headerWords;
    file.runtimeRecordCount = scan.signedRecordCountCandidate;
    file.rawRecordCountCandidate = scan.recordCountCandidate;
    file.selectedRecordCount = scan.selectedRecordCount;
    file.recordCountSource = scan.recordCountSource;
    file.recordsOffset = scan.recordsOffset;
    file.recordStride = scan.recordStride;
    file.recordTableEndOffset = scan.recordTableEndOffset;
    file.firstPayloadOffset = scan.firstPayloadOffset;
    file.recordCountInferredFromFirstPayloadOffset = scan.recordCountInferredFromFirstPayloadOffset;
    file.recordCountMatchesFirstPayloadOffset = scan.recordCountMatchesFirstPayloadOffset;
    file.recordTableInBounds = scan.recordTableInBounds;
    file.diagnostics = scan.diagnostics;
    file.tableShape = classifyMlkTableShape(scan);
    file.supported = file.tableShape == MlkTableShape::Normal && file.ok();

    file.records.reserve(scan.records.size());
    for (const auto& record : scan.records) {
        file.records.push_back(makeRecord(record));
    }
    return file;
}

} // namespace

bool MlkFile::ok() const {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const MlkDiagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

MlkTableShape classifyMlkTableShape(const MlkScanResult& scan) {
    const auto hasError = std::any_of(scan.diagnostics.begin(), scan.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
    const auto hasOutOfBoundsPayload = std::any_of(scan.records.begin(), scan.records.end(), [](const auto& record) {
        return !record.payloadInBounds;
    });
    if (scan.recordCountInferredFromFirstPayloadOffset > 0U &&
        scan.recordCountInferredFromFirstPayloadOffset != scan.selectedRecordCount &&
        scan.firstPayloadOffset >= scan.recordsOffset) {
        return MlkTableShape::FirstPayloadCountCandidate;
    }
    if (hasError || hasOutOfBoundsPayload) {
        return MlkTableShape::MalformedRecordSpans;
    }
    return MlkTableShape::Normal;
}

MlkFile MlkParser::parse(std::span<const std::uint8_t> bytes, std::string sourcePath) {
    const auto scan = MlkScanner::scan(bytes, std::move(sourcePath));
    return makeFile(scan);
}

MlkFile MlkParser::parseFile(const std::filesystem::path& path) {
    return makeFile(MlkScanner::scanFile(path));
}

} // namespace spice::mlk
