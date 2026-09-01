#pragma once

#include "SctDocument.h"
#include "SctHeaderContract.h"
#include "SctModel.h"
#include "SctOpcodeMetadata.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace spice::sct {

enum class SctSourceByteOrder { BigEndian, LittleEndian, Unknown };
enum class SctSourceWrapper { None, Aklz };
enum class SctSourceCoverageKind { SemanticEntity, DerivedLayout, OpaqueAttachment, SourceObservation };

struct SctEntityProvenance {
    SctDocumentEntityId entity;
    std::uint32_t decodedPayloadOffset = 0;
    std::uint32_t byteSize = 0;
    std::optional<std::uint32_t> physicalSectionIndex;
    SctSourceCoverageKind coverageKind = SctSourceCoverageKind::SemanticEntity;
};

struct SctSourceObservations {
    SctSourceByteOrder byteOrder = SctSourceByteOrder::Unknown;
    SctSourceWrapper wrapper = SctSourceWrapper::None;
    struct Header {
        std::array<std::uint8_t, 8> rawBytes{};
        SctHeaderValues values;
        bool available = false;
    } header;
};

struct SctDocumentImportOptions {
    std::optional<SctPlatform> declaredSourcePlatform;
    std::optional<SctTextEncoding> sourceTextEncoding;
};

struct SctTextImportObservation {
    SctDocumentEntityId entity;
    std::optional<SctTextEncoding> encoding;
    bool semantic = false;
    std::string reason;
};

struct SctDocumentImportReceipt {
    SctSourceObservations source;
    std::optional<SctPlatform> declaredSourcePlatform;
    std::optional<SctTextEncoding> sourceTextEncoding;
    std::vector<SctEntityProvenance> provenance;
    std::vector<SctTextImportObservation> text;
};

struct SctDocumentImportResult {
    std::optional<SctDocument> document;
    std::vector<SctDocumentDiagnostic> diagnostics;
    SctDocumentImportReceipt receipt;
};

class SctDocumentImporter {
public:
    [[nodiscard]] static SctDocumentImportResult import(
        const SctParseResult& parsed,
        const SctDocumentImportOptions& options = {});
};

} // namespace spice::sct
