#pragma once

#include "SctDocument.h"
#include "SctModel.h"
#include "SctOpcodeMetadata.h"

#include <optional>
#include <vector>

namespace spice::sct {

enum class SctSourceByteOrder { BigEndian, LittleEndian, Unknown };
enum class SctSourceWrapper { None, Aklz };
enum class SctSourceCoverageKind { SemanticEntity, DerivedLayout, OpaqueAttachment };

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
};

struct SctDocumentImportOptions {
    std::optional<SctPlatform> declaredSourcePlatform;
};

struct SctDocumentImportReceipt {
    SctSourceObservations source;
    std::optional<SctPlatform> declaredSourcePlatform;
    std::vector<SctEntityProvenance> provenance;
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
