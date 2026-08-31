#pragma once

#include "SctDocument.h"
#include "SctModel.h"

#include <optional>
#include <vector>

namespace spice::sct {

enum class SctSourceByteOrder { BigEndian, LittleEndian, Unknown };
enum class SctSourceWrapper { None, Aklz };

struct SctEntityProvenance {
    SctDocumentEntityId entity;
    std::uint32_t decodedPayloadOffset = 0;
    std::uint32_t byteSize = 0;
    std::optional<std::uint32_t> physicalSectionIndex;
};

struct SctSourceObservations {
    SctSourceByteOrder byteOrder = SctSourceByteOrder::Unknown;
    SctSourceWrapper wrapper = SctSourceWrapper::None;
};

struct SctDocumentImportResult {
    std::optional<SctDocument> document;
    std::vector<SctDocumentDiagnostic> diagnostics;
    std::vector<SctEntityProvenance> provenance;
    SctSourceObservations source;
};

class SctDocumentImporter {
public:
    [[nodiscard]] static SctDocumentImportResult import(const SctParseResult& parsed);
};

} // namespace spice::sct
