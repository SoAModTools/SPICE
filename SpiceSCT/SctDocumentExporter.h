#pragma once

#include "SctDocumentValidator.h"
#include "SctHeaderContract.h"

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace spice::sct {

enum class SctDocumentOutputByteOrder { BigEndian, LittleEndian };
enum class SctDocumentOutputWrapper { Raw, Aklz };
enum class SctOpaquePreservationPolicy { RequirePreservation };

struct SctDocumentExportOptions {
    explicit SctDocumentExportOptions(
        SctPlatform target,
        SctTextEncoding targetTextEncoding,
        SctDocumentOutputByteOrder outputByteOrder = SctDocumentOutputByteOrder::BigEndian,
        SctDocumentOutputWrapper outputWrapper = SctDocumentOutputWrapper::Raw,
        SctOpaquePreservationPolicy preservationPolicy = SctOpaquePreservationPolicy::RequirePreservation,
        SctHeaderExportOptions headerOptions = {}) noexcept
        : targetPlatform(target), textEncoding(targetTextEncoding), byteOrder(outputByteOrder), wrapper(outputWrapper),
          opaquePolicy(preservationPolicy), header(headerOptions) {}

    SctPlatform targetPlatform;
    SctTextEncoding textEncoding;
    SctDocumentOutputByteOrder byteOrder = SctDocumentOutputByteOrder::BigEndian;
    SctDocumentOutputWrapper wrapper = SctDocumentOutputWrapper::Raw;
    SctOpaquePreservationPolicy opaquePolicy = SctOpaquePreservationPolicy::RequirePreservation;
    SctHeaderExportOptions header;
};

struct SctDocumentByteSpan {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    auto operator<=>(const SctDocumentByteSpan&) const = default;
};

struct SctSectionLayoutRecord {
    SctSectionId id;
    SctDocumentByteSpan indexRowSpan;
    SctDocumentByteSpan payloadSpan;
    std::uint32_t dataRelativeOffset = 0;
};

template <typename Id>
struct SctEntityLayoutRecord {
    Id id;
    SctDocumentByteSpan span;
};

using SctInstructionLayoutRecord = SctEntityLayoutRecord<SctInstructionId>;
using SctStringLayoutRecord = SctEntityLayoutRecord<SctStringId>;
using SctFooterEntryLayoutRecord = SctEntityLayoutRecord<SctFooterEntryId>;

using SctParameterLocation = SctParameterAddress;

enum class SctRelocationFormula { InstructionEndMinusWord, OperandWordRelative };
using SctRelocationTarget = std::variant<SctInstructionId, SctStringId, SctFooterEntryId>;

struct SctRelocationRecord {
    SctInstructionId sourceInstruction;
    SctParameterLocation parameter;
    SctRelocationTarget target;
    SctRelocationFormula formula = SctRelocationFormula::OperandWordRelative;
    SctDocumentByteSpan operandSpan;
    std::uint32_t encodedValue = 0;
};

enum class SctOpaquePreservationStatus { PreservedByteIdentically, RelocatedUnderRule, Rejected };

enum class SctTextMaterializationStatus {
    EncodedSemantically,
    PreservedOpaqueBytes,
    EmptyIndexedText,
};

struct SctTextMaterializationRecord {
    SctDocumentEntityId entity;
    SctDocumentByteSpan span;
    SctTextMaterializationStatus status = SctTextMaterializationStatus::EncodedSemantically;
};

struct SctOpaquePlacementRecord {
    SctOpaqueAttachmentId id;
    SctDocumentByteSpan span;
    SctOpaquePreservationStatus status = SctOpaquePreservationStatus::Rejected;
};

struct SctPreservationReport {
    std::vector<SctOpaquePlacementRecord> attachments;
    std::vector<SctTextMaterializationRecord> text;
    std::optional<SctHeaderMaterializationRecord> header;
};

struct SctDocumentLayout {
    std::uint32_t decodedPayloadSize = 0;
    std::vector<SctSectionLayoutRecord> sections;
    std::vector<SctInstructionLayoutRecord> instructions;
    std::vector<SctStringLayoutRecord> strings;
    std::vector<SctFooterEntryLayoutRecord> footerEntries;
    std::vector<SctRelocationRecord> relocations;
    std::vector<SctOpaquePlacementRecord> opaquePlacements;
};

struct SctDocumentLayoutResult {
    bool success = false;
    std::optional<SctDocumentLayout> layout;
    std::vector<SctDocumentDiagnostic> diagnostics;
    SctPreservationReport preservation;
};

struct SctDocumentExportResult {
    bool success = false;
    std::vector<std::uint8_t> bytes;
    std::optional<SctDocumentLayout> layout;
    std::vector<SctDocumentDiagnostic> diagnostics;
    SctPreservationReport preservation;
    std::uint32_t decodedPayloadSize = 0;
    std::uint64_t outputSize = 0;
};

class SctDocumentLayoutEngine {
public:
    [[nodiscard]] static SctDocumentLayoutResult layout(
        const SctDocument& document,
        const SctDocumentExportOptions& options,
        const SctBoundImportEvidence* evidence = nullptr);
};

class SctDocumentExporter {
public:
    [[nodiscard]] static SctDocumentExportResult exportDocument(
        const SctDocument& document,
        const SctDocumentExportOptions& options,
        const SctBoundImportEvidence* evidence = nullptr);
};

} // namespace spice::sct
