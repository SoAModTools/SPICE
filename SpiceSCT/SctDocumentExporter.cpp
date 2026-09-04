#include "SctDocumentExporter.h"

#include "SctOpcodeMetadata.h"
#include "SctDocumentFingerprint.h"
#include "SctScptEncoding.h"
#include "SctTextCodec.h"

#include "../Compression/Aklz.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace spice::sct {
namespace {

constexpr std::uint32_t kHeaderSize = 12;
constexpr std::uint32_t kIndexEntrySize = 20;
constexpr std::uint32_t kIndexNameOffset = 4;
constexpr std::uint32_t kIndexNameSize = 16;
constexpr std::uint32_t kMaximumPayloadSize = spice::compression::aklz::kDefaultMaxDecompressedSize;

void addDiagnostic(std::vector<SctDocumentDiagnostic>& diagnostics, SctDiagnosticCode code,
    std::string message, std::optional<SctDocumentEntityId> entity = std::nullopt) {
    SctDocumentDiagnostic diagnostic{SctDiagnosticSeverity::Error, code, std::move(message)};
    if (entity) diagnostic.primaryLocation = SctDiagnosticLocation{*entity};
    diagnostics.push_back(std::move(diagnostic));
}

bool hasErrors(const std::vector<SctDocumentDiagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity == SctDiagnosticSeverity::Error;
    });
}

SctTextMaterializationStatus textMaterializationStatus(const SctTextValue& value) {
    if (std::holds_alternative<SctOpaqueText>(value)) {
        return SctTextMaterializationStatus::PreservedOpaqueBytes;
    }
    if (std::holds_alternative<SctEmptyIndexedText>(value)) {
        return SctTextMaterializationStatus::EmptyIndexedText;
    }
    return SctTextMaterializationStatus::EncodedSemantically;
}

std::vector<std::uint8_t> encodeWords(
    const std::vector<std::uint32_t>& words,
    SctDocumentOutputByteOrder byteOrder) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(words.size() * 4u);
    for (const auto word : words) {
        if (byteOrder == SctDocumentOutputByteOrder::BigEndian) {
            bytes.push_back(static_cast<std::uint8_t>((word >> 24) & 0xff));
            bytes.push_back(static_cast<std::uint8_t>((word >> 16) & 0xff));
            bytes.push_back(static_cast<std::uint8_t>((word >> 8) & 0xff));
            bytes.push_back(static_cast<std::uint8_t>(word & 0xff));
        } else {
            bytes.push_back(static_cast<std::uint8_t>(word & 0xff));
            bytes.push_back(static_cast<std::uint8_t>((word >> 8) & 0xff));
            bytes.push_back(static_cast<std::uint8_t>((word >> 16) & 0xff));
            bytes.push_back(static_cast<std::uint8_t>((word >> 24) & 0xff));
        }
    }
    return bytes;
}

void patchWord(std::vector<std::uint8_t>& bytes, std::uint32_t offset, std::uint32_t value,
    SctDocumentOutputByteOrder byteOrder) {
    if (byteOrder == SctDocumentOutputByteOrder::BigEndian) {
        bytes[offset] = static_cast<std::uint8_t>((value >> 24) & 0xff);
        bytes[offset + 1] = static_cast<std::uint8_t>((value >> 16) & 0xff);
        bytes[offset + 2] = static_cast<std::uint8_t>((value >> 8) & 0xff);
        bytes[offset + 3] = static_cast<std::uint8_t>(value & 0xff);
    } else {
        bytes[offset] = static_cast<std::uint8_t>(value & 0xff);
        bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
        bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xff);
        bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xff);
    }
}

std::vector<std::uint8_t> encodeHeaderValues(const SctHeaderValues& values,
    SctDocumentOutputByteOrder byteOrder) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(8u);
    const std::uint16_t fields[]{values.sourceYear, values.sourceMonth, values.sourceDay, values.fourthValue};
    for (const auto field : fields) {
        if (byteOrder == SctDocumentOutputByteOrder::BigEndian) {
            bytes.push_back(static_cast<std::uint8_t>(field >> 8));
            bytes.push_back(static_cast<std::uint8_t>(field & 0xffu));
        } else {
            bytes.push_back(static_cast<std::uint8_t>(field & 0xffu));
            bytes.push_back(static_cast<std::uint8_t>(field >> 8));
        }
    }
    return bytes;
}

class PayloadCanvas {
public:
    explicit PayloadCanvas(std::vector<SctDocumentDiagnostic>& diagnostics)
        : diagnostics_(diagnostics) {}

    bool ensure(std::uint64_t size) {
        if (size > kMaximumPayloadSize) {
            addDiagnostic(diagnostics_, SctDiagnosticCode::LayoutOverflow,
                "Decoded SCT payload exceeds the supported 64 MiB layout limit.");
            return false;
        }
        if (size > bytes_.size()) {
            bytes_.resize(static_cast<std::size_t>(size), 0);
            claimed_.resize(static_cast<std::size_t>(size), false);
        }
        return true;
    }

    bool claim(std::uint32_t offset, const std::vector<std::uint8_t>& bytes,
        SctDiagnosticCode conflictCode, std::string conflictMessage,
        std::optional<SctDocumentEntityId> entity = std::nullopt) {
        const auto end = static_cast<std::uint64_t>(offset) + bytes.size();
        if (!ensure(end)) return false;
        for (std::size_t i = offset; i < end; ++i) {
            if (claimed_[i]) {
                addDiagnostic(diagnostics_, conflictCode, std::move(conflictMessage), std::move(entity));
                return false;
            }
        }
        std::copy(bytes.begin(), bytes.end(), bytes_.begin() + offset);
        std::fill(claimed_.begin() + offset, claimed_.begin() + static_cast<std::size_t>(end), true);
        return true;
    }

    bool claimZeros(std::uint32_t offset, std::uint32_t size, SctDiagnosticCode code,
        std::string message, std::optional<SctDocumentEntityId> entity = std::nullopt) {
        const auto end = static_cast<std::uint64_t>(offset) + size;
        if (!ensure(end)) return false;
        if (std::any_of(claimed_.begin() + offset,
                claimed_.begin() + static_cast<std::size_t>(end),
                [](const bool claimed) { return claimed; })) {
            addDiagnostic(diagnostics_, code, std::move(message), std::move(entity));
            return false;
        }
        std::fill(bytes_.begin() + offset,
            bytes_.begin() + static_cast<std::size_t>(end), 0u);
        std::fill(claimed_.begin() + offset,
            claimed_.begin() + static_cast<std::size_t>(end), true);
        return true;
    }

    bool claimUnclaimedZeros(std::uint32_t offset, std::uint32_t size) {
        const auto end = static_cast<std::uint64_t>(offset) + size;
        if (!ensure(end)) return false;
        for (std::size_t i = offset; i < end; ++i) claimed_[i] = true;
        return true;
    }

    std::optional<std::uint32_t> placeAtFirstFit(std::uint32_t cursor, const std::vector<std::uint8_t>& bytes) {
        if (bytes.empty()) return cursor;
        std::uint64_t candidate = cursor;
        while (candidate + bytes.size() <= kMaximumPayloadSize) {
            if (!ensure(candidate + bytes.size())) return std::nullopt;
            const auto conflict = std::find(
                claimed_.begin() + static_cast<std::ptrdiff_t>(candidate),
                claimed_.begin() + static_cast<std::ptrdiff_t>(candidate + bytes.size()), true);
            if (conflict == claimed_.begin() + static_cast<std::ptrdiff_t>(candidate + bytes.size())) {
                const auto offset = static_cast<std::uint32_t>(candidate);
                std::copy(bytes.begin(), bytes.end(), bytes_.begin() + offset);
                std::fill(claimed_.begin() + offset,
                    claimed_.begin() + offset + bytes.size(), true);
                return offset;
            }
            candidate = static_cast<std::uint64_t>(std::distance(claimed_.begin(), conflict)) + 1u;
        }
        addDiagnostic(diagnostics_, SctDiagnosticCode::LayoutOverflow,
            "No encodable payload span remains for a semantic entity.");
        return std::nullopt;
    }

    bool rangeIsFree(std::uint32_t offset, std::uint32_t size) {
        const auto end = static_cast<std::uint64_t>(offset) + size;
        if (!ensure(end)) return false;
        return std::none_of(claimed_.begin() + offset,
            claimed_.begin() + static_cast<std::size_t>(end), [](bool claimed) { return claimed; });
    }

    std::vector<std::uint8_t>& bytes() { return bytes_; }
    const std::vector<std::uint8_t>& bytes() const { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
    std::vector<bool> claimed_;
    std::vector<SctDocumentDiagnostic>& diagnostics_;
};

struct PendingRelocation {
    SctInstructionId source;
    SctParameterLocation parameter;
    SctRelocationTarget target;
    SctRelocationFormula formula = SctRelocationFormula::OperandWordRelative;
    bool signedValue = true;
    std::uint32_t targetAlignment = 1;
    std::uint32_t encodedValueMask = 0xffffffffu;
    std::uint32_t wordOffsetWithinInstruction = 0;
};

struct EncodedInstruction {
    std::vector<std::uint8_t> bytes;
    std::vector<PendingRelocation> relocations;
};

const SctDocumentParameter* findFixedParameter(const SctDocumentInstruction& instruction, std::uint32_t index) {
    const auto found = std::find_if(instruction.fixedParameters.begin(), instruction.fixedParameters.end(),
        [&](const auto& parameter) { return parameter.schemaIndex == index; });
    return found == instruction.fixedParameters.end() ? nullptr : &*found;
}

EncodedInstruction encodeInstruction(const SctDocumentInstruction& instruction,
    const SctOpcodeSchema& schema, SctDocumentOutputByteOrder byteOrder,
    std::vector<SctDocumentDiagnostic>& diagnostics) {
    EncodedInstruction result;
    std::vector<std::uint32_t> words;
    if (instruction.skipRefresh) words.push_back(13);
    std::optional<std::size_t> scheduledLengthIndex;
    if (instruction.scheduledExpression) {
        words.push_back(129);
        const auto expressionWords = encodeSctCanonicalExpressionWords(*instruction.scheduledExpression);
        words.insert(words.end(), expressionWords.begin(), expressionWords.end());
        scheduledLengthIndex = words.size();
        words.push_back(0);
    }
    const auto opcodeWordIndex = words.size();
    words.push_back(instruction.opcode);

    const auto emitParameter = [&](const SctDocumentParameter& parameter,
        std::optional<std::uint32_t> groupOrdinal) {
        const auto wordIndex = static_cast<std::uint32_t>(words.size());
        if (const auto* scalar = std::get_if<SctEncodedWordValue>(&parameter.value)) {
            words.push_back(scalar->value);
        } else if (const auto* expression = std::get_if<SctCanonicalExpression>(&parameter.value)) {
            const auto expressionWords = encodeSctCanonicalExpressionWords(*expression);
            words.insert(words.end(), expressionWords.begin(), expressionWords.end());
        } else if (const auto* sequence = std::get_if<SctTerminatedWordSequenceValue>(&parameter.value)) {
            words.insert(words.end(), sequence->words.begin(), sequence->words.end());
        } else if (const auto* reference = std::get_if<SctInstructionReference>(&parameter.value)) {
            words.push_back(0);
            const auto* parameterSchema = sctOpcodeParameterSchema(schema, parameter.schemaIndex);
            const auto formula = parameterSchema != nullptr
                && parameterSchema->relativeReferenceBase == SctRelativeReferenceBase::InstructionEndMinusWord
                ? SctRelocationFormula::InstructionEndMinusWord
                : SctRelocationFormula::OperandWordRelative;
            result.relocations.push_back({instruction.id, {parameter.schemaIndex, groupOrdinal},
                reference->target, formula,
                parameterSchema == nullptr || parameterSchema->relativeReferenceSigned,
                parameterSchema == nullptr ? 4u : parameterSchema->referenceTargetAlignment,
                parameterSchema == nullptr ? 0xfffffffcu : parameterSchema->referenceEncodedValueMask,
                wordIndex * 4u});
        } else if (const auto* reference = std::get_if<SctStringReference>(&parameter.value)) {
            words.push_back(0);
            const auto rule = sctOpcodeTextReference(schema, parameter.schemaIndex);
            if (!rule.has_value() || rule->storage != SctTextStorage::IndexedSection) {
                addDiagnostic(diagnostics, SctDiagnosticCode::EncodingUnsupported,
                    "Indexed string reference has no matching opcode schema rule.", SctDocumentEntityId{instruction.id});
                return;
            }
            const auto formula = rule->relativeBase == SctRelativeReferenceBase::InstructionEndMinusWord
                ? SctRelocationFormula::InstructionEndMinusWord
                : SctRelocationFormula::OperandWordRelative;
            result.relocations.push_back({instruction.id, {parameter.schemaIndex, groupOrdinal},
                reference->target, formula, rule->signedRelative, rule->targetAlignment,
                rule->encodedValueMask, wordIndex * 4u});
        } else if (const auto* reference = std::get_if<SctFooterEntryReference>(&parameter.value)) {
            words.push_back(0);
            const auto rule = sctOpcodeTextReference(schema, parameter.schemaIndex);
            if (!rule.has_value() || rule->storage != SctTextStorage::Footer) {
                addDiagnostic(diagnostics, SctDiagnosticCode::EncodingUnsupported,
                    "Footer reference has no matching opcode schema rule.", SctDocumentEntityId{instruction.id});
                return;
            }
            const auto formula = rule->relativeBase == SctRelativeReferenceBase::InstructionEndMinusWord
                ? SctRelocationFormula::InstructionEndMinusWord
                : SctRelocationFormula::OperandWordRelative;
            result.relocations.push_back({instruction.id, {parameter.schemaIndex, groupOrdinal},
                reference->target, formula, rule->signedRelative, rule->targetAlignment,
                rule->encodedValueMask, wordIndex * 4u});
        } else if (const auto* unresolved = std::get_if<SctUnresolvedReferenceValue>(&parameter.value)) {
            (void)unresolved;
            addDiagnostic(diagnostics, SctDiagnosticCode::UnresolvedReference,
                "Unresolved relative references cannot be encoded safely.",
                SctDocumentEntityId{instruction.id});
        } else {
            const auto& opaque = std::get<SctOpaqueParameterValue>(parameter.value);
            words.insert(words.end(), opaque.words.begin(), opaque.words.end());
        }
    };

    const auto repeated = sctOpcodeRepeatedGroup(schema);
    for (std::uint32_t index = 0; index < schema.parameters.paramCount; ++index) {
        if (repeated && index == repeated->iterationCountParameter) {
            if (instruction.repeatedParameterGroups.size() > std::numeric_limits<std::uint32_t>::max()) {
                addDiagnostic(diagnostics, SctDiagnosticCode::LayoutOverflow,
                    "Repeated parameter group count exceeds the SCT word domain.", SctDocumentEntityId{instruction.id});
                return result;
            }
            words.push_back(static_cast<std::uint32_t>(instruction.repeatedParameterGroups.size()));
        } else if (repeated && index >= repeated->firstParameter && index <= repeated->lastParameter) {
            if (instruction.repeatedParameterGroups.empty()) {
                addDiagnostic(diagnostics, SctDiagnosticCode::EncodingUnsupported,
                    "The base opcode shape requires a repeated parameter group.", SctDocumentEntityId{instruction.id});
                return result;
            }
            const auto& group = instruction.repeatedParameterGroups.front();
            emitParameter(group.parameters[index - repeated->firstParameter], 0);
        } else if (const auto* parameter = findFixedParameter(instruction, index)) {
            emitParameter(*parameter, std::nullopt);
        }
    }
    if (repeated) {
        const auto firstExtraGroup = repeated->firstParameter < schema.parameters.paramCount ? 1u : 0u;
        for (std::size_t groupIndex = firstExtraGroup; groupIndex < instruction.repeatedParameterGroups.size(); ++groupIndex) {
            for (const auto& parameter : instruction.repeatedParameterGroups[groupIndex].parameters) {
                emitParameter(parameter, static_cast<std::uint32_t>(groupIndex));
            }
        }
    }

    if (scheduledLengthIndex) {
        const auto byteLength = (words.size() - opcodeWordIndex) * 4u;
        if (byteLength > std::numeric_limits<std::uint32_t>::max()) {
            addDiagnostic(diagnostics, SctDiagnosticCode::LayoutOverflow,
                "Scheduled instruction length exceeds the SCT word domain.", SctDocumentEntityId{instruction.id});
            return result;
        }
        words[*scheduledLengthIndex] = static_cast<std::uint32_t>(byteLength);
    }
    result.bytes = encodeWords(words, byteOrder);
    return result;
}

struct InternalBuildResult {
    bool success = false;
    std::vector<std::uint8_t> payload;
    std::optional<SctDocumentLayout> layout;
    std::vector<SctDocumentDiagnostic> diagnostics;
    SctPreservationReport preservation;
    SctValidationReceiptUse validationReceiptUse = SctValidationReceiptUse::NotProvided;
};

InternalBuildResult buildPayload(const SctDocument& document, const SctDocumentExportOptions& options,
    const SctBoundImportEvidence* evidence,
    const SctTargetValidationReceipt* validationReceipt) {
    InternalBuildResult result;
    const auto* receipt = evidence == nullptr ? nullptr : &evidence->receipt();
    switch (options.opaquePolicy) {
    case SctOpaquePreservationPolicy::RequirePreservation:
        break;
    }
    bool reusedValidation = false;
    if (validationReceipt != nullptr) {
        const auto documentFingerprint = detail::fingerprintDocument(document);
        const auto targetFingerprint = detail::fingerprintTargetValidation(
            documentFingerprint, options.targetPlatform, options.textEncoding, evidence);
        if (std::ranges::equal(validationReceipt->validationFingerprint(), targetFingerprint)) {
            result.validationReceiptUse = SctValidationReceiptUse::Reused;
            result.diagnostics.assign(validationReceipt->diagnostics().begin(),
                validationReceipt->diagnostics().end());
            reusedValidation = true;
        } else {
            result.validationReceiptUse = SctValidationReceiptUse::MismatchRevalidated;
        }
    }
    if (!reusedValidation) {
        const auto validation = SctDocumentValidator::validateForTarget(
            document, options.targetPlatform, options.textEncoding, evidence);
        result.diagnostics = validation.diagnostics;
    }
    if (hasErrors(result.diagnostics)) return result;

    const auto dataStart64 = static_cast<std::uint64_t>(kHeaderSize)
        + static_cast<std::uint64_t>(document.sections.size()) * kIndexEntrySize;
    if (dataStart64 > kMaximumPayloadSize || document.sections.size() > std::numeric_limits<std::uint32_t>::max()) {
        addDiagnostic(result.diagnostics, SctDiagnosticCode::LayoutOverflow,
            "SCT section index exceeds the supported payload domain.");
        return result;
    }
    const auto dataStart = static_cast<std::uint32_t>(dataStart64);
    PayloadCanvas canvas(result.diagnostics);
    if (!canvas.ensure(dataStart)) return result;

    std::vector<std::uint8_t> headerBytes;
    SctHeaderMaterializationRecord headerRecord;
    if (options.header.mode == SctHeaderExportMode::ExplicitValues) {
        headerRecord = {SctHeaderMaterializationStatus::ExplicitValues, options.header.explicitValues};
        headerBytes = encodeHeaderValues(options.header.explicitValues, options.byteOrder);
    } else if (receipt != nullptr && receipt->source.header.available) {
        headerRecord.values = receipt->source.header.values;
        const bool sameByteOrder = (options.byteOrder == SctDocumentOutputByteOrder::BigEndian
                && receipt->source.byteOrder == SctSourceByteOrder::BigEndian)
            || (options.byteOrder == SctDocumentOutputByteOrder::LittleEndian
                && receipt->source.byteOrder == SctSourceByteOrder::LittleEndian);
        if (sameByteOrder) {
            headerRecord.status = SctHeaderMaterializationStatus::PreservedSourceBytes;
            headerBytes.assign(receipt->source.header.rawBytes.begin(), receipt->source.header.rawBytes.end());
        } else {
            headerRecord.status = SctHeaderMaterializationStatus::ReencodedSourceValues;
            headerBytes = encodeHeaderValues(receipt->source.header.values, options.byteOrder);
        }
    } else {
        headerRecord = {SctHeaderMaterializationStatus::CanonicalDefault, kCanonicalSctHeaderValues};
        headerBytes = encodeHeaderValues(kCanonicalSctHeaderValues, options.byteOrder);
    }
    if (!canvas.claim(0u, headerBytes, SctDiagnosticCode::OpaquePlacementUnsatisfied,
        "The materialized SCT header overlaps another fixed span.")) return result;
    result.preservation.header = headerRecord;

    std::unordered_map<std::uint64_t, SctOpaquePlacementRecord> opaquePlacements;
    using AttachmentLists = std::array<std::vector<const SctOpaqueAttachment*>, 3u>;
    std::map<SctOpaqueAnchor, AttachmentLists> attachmentsByAnchor;
    for (const auto& attachment : document.opaqueAttachments) {
        attachmentsByAnchor[attachment.anchor][static_cast<std::size_t>(attachment.placement)]
            .push_back(&attachment);
        if (attachment.placement != SctOpaquePlacement::FixedOffset || !attachment.fixedOffset) continue;
        const auto offset = *attachment.fixedOffset;
        const bool aligned = offset % attachment.alignment == 0;
        const bool claimed = aligned && canvas.claim(offset, attachment.bytes,
            SctDiagnosticCode::OpaquePlacementUnsatisfied,
            "Fixed opaque attachments overlap or conflict with another fixed span.",
            SctDocumentEntityId{attachment.id});
        opaquePlacements[attachment.id.value()] = {attachment.id,
            {offset, static_cast<std::uint32_t>(attachment.bytes.size())},
            claimed ? SctOpaquePreservationStatus::PreservedByteIdentically
                    : SctOpaquePreservationStatus::Rejected};
        if (!aligned) {
            addDiagnostic(result.diagnostics, SctDiagnosticCode::OpaquePlacementUnsatisfied,
                "Fixed opaque attachment does not satisfy its declared alignment.",
                SctDocumentEntityId{attachment.id});
        }
    }
    if (hasErrors(result.diagnostics)) return result;

    if (!canvas.claimZeros(8, 4, SctDiagnosticCode::OpaquePlacementUnsatisfied,
            "Opaque bytes overlap the derived SCT section count.")) return result;
    patchWord(canvas.bytes(), 8, static_cast<std::uint32_t>(document.sections.size()), options.byteOrder);
    for (std::size_t i = 0; i < document.sections.size(); ++i) {
        const auto rowOffset = kHeaderSize + static_cast<std::uint32_t>(i * kIndexEntrySize);
        if (!canvas.claimZeros(rowOffset, 4, SctDiagnosticCode::OpaquePlacementUnsatisfied,
                "Opaque bytes overlap a derived SCT section offset.", SctDocumentEntityId{document.sections[i].id})) {
            return result;
        }
        std::vector<std::uint8_t> name(document.sections[i].nameBytes.begin(), document.sections[i].nameBytes.end());
        if (!canvas.claim(rowOffset + kIndexNameOffset, name, SctDiagnosticCode::OpaquePlacementUnsatisfied,
                "Opaque bytes overlap canonical SCT section-name bytes.", SctDocumentEntityId{document.sections[i].id})
            || !canvas.claimUnclaimedZeros(rowOffset + kIndexNameOffset, kIndexNameSize)) return result;
    }

    const auto alignUp = [&](std::uint32_t value, std::uint32_t alignment) -> std::optional<std::uint32_t> {
        const auto aligned = (static_cast<std::uint64_t>(value) + alignment - 1u) & ~(static_cast<std::uint64_t>(alignment) - 1u);
        if (aligned > kMaximumPayloadSize) {
            addDiagnostic(result.diagnostics, SctDiagnosticCode::LayoutOverflow,
                "Opaque attachment alignment exceeds the payload domain.");
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(aligned);
    };

    const auto placeRelativeAttachment = [&](const SctOpaqueAttachment& attachment,
        std::uint32_t& cursor) -> bool {
        const auto offset = alignUp(cursor, attachment.alignment);
        if (!offset) return false;
        const auto claimed = canvas.claim(*offset, attachment.bytes,
            SctDiagnosticCode::OpaquePlacementUnsatisfied,
            "Relocatable opaque attachment cannot remain adjacent to its anchor.",
            SctDocumentEntityId{attachment.id});
        opaquePlacements[attachment.id.value()] = {attachment.id,
            {*offset, static_cast<std::uint32_t>(attachment.bytes.size())},
            claimed ? SctOpaquePreservationStatus::RelocatedUnderRule
                    : SctOpaquePreservationStatus::Rejected};
        if (!claimed) return false;
        cursor = *offset + static_cast<std::uint32_t>(attachment.bytes.size());
        return true;
    };

    const auto placeAttachments = [&](const SctOpaqueAnchor& anchor, SctOpaquePlacement placement,
        std::uint32_t& cursor) -> bool {
        const auto found = attachmentsByAnchor.find(anchor);
        if (found == attachmentsByAnchor.end()) return true;
        for (const auto* attachment : found->second[static_cast<std::size_t>(placement)]) {
            if (!placeRelativeAttachment(*attachment, cursor)) return false;
        }
        return true;
    };

    const auto fixedEndFor = [&](const SctOpaqueAnchor& anchor, std::uint32_t floor) {
        std::uint32_t end = floor;
        const auto found = attachmentsByAnchor.find(anchor);
        if (found == attachmentsByAnchor.end()) return end;
        for (const auto* attachment : found->second[
                 static_cast<std::size_t>(SctOpaquePlacement::FixedOffset)]) {
            if (attachment->fixedOffset && *attachment->fixedOffset >= dataStart) {
                const auto attachmentEnd = static_cast<std::uint64_t>(*attachment->fixedOffset)
                    + attachment->bytes.size();
                if (attachmentEnd <= kMaximumPayloadSize) end = std::max(end, static_cast<std::uint32_t>(attachmentEnd));
            }
        }
        return end;
    };

    SctDocumentLayout layout;
    std::unordered_map<std::uint64_t, SctDocumentByteSpan> instructionSpans;
    std::unordered_map<std::uint64_t, SctDocumentByteSpan> indexedStringTargetSpans;
    std::unordered_map<std::uint64_t, SctDocumentByteSpan> footerSpans;
    std::unordered_set<std::uint64_t> placedStrings;
    std::vector<PendingRelocation> pendingRelocations;
    std::uint32_t cursor = dataStart;
    const SctOpaqueAnchor documentAnchor{SctDocumentAnchor{}};
    if (!placeAttachments(documentAnchor, SctOpaquePlacement::Before, cursor)) return result;

    for (std::size_t sectionIndex = 0; sectionIndex < document.sections.size(); ++sectionIndex) {
        const auto& section = document.sections[sectionIndex];
        const SctOpaqueAnchor sectionAnchor{section.id};
        if (!placeAttachments(sectionAnchor, SctOpaquePlacement::Before, cursor)) return result;
        const auto alignedSectionStart = alignUp(cursor, 4u);
        if (!alignedSectionStart) return result;
        if (!canvas.ensure(*alignedSectionStart)) return result;
        cursor = *alignedSectionStart;
        const auto sectionStart = cursor;

        if (const auto* script = std::get_if<SctScriptSectionContent>(&section.content)) {
            for (const auto& instruction : script->instructions) {
                const SctOpaqueAnchor instructionAnchor{instruction.id};
                if (!placeAttachments(instructionAnchor, SctOpaquePlacement::Before, cursor)) return result;
                const auto* schema = findSctOpcodeSchema(instruction.opcode);
                if (!schema) {
                    addDiagnostic(result.diagnostics, SctDiagnosticCode::EncodingUnsupported,
                        "Instruction has no opcode schema for encoding.", SctDocumentEntityId{instruction.id});
                    return result;
                }
                auto encoded = encodeInstruction(instruction, *schema, options.byteOrder, result.diagnostics);
                if (hasErrors(result.diagnostics)) return result;
                const auto offset = canvas.placeAtFirstFit(cursor, encoded.bytes);
                if (!offset) return result;
                const SctDocumentByteSpan span{*offset, static_cast<std::uint32_t>(encoded.bytes.size())};
                layout.instructions.push_back({instruction.id, span});
                instructionSpans.emplace(instruction.id.value(), span);
                for (auto& relocation : encoded.relocations) {
                    relocation.wordOffsetWithinInstruction += *offset;
                    pendingRelocations.push_back(std::move(relocation));
                }
                cursor = span.offset + span.size;
                cursor = fixedEndFor(instructionAnchor, cursor);
                if (!placeAttachments(instructionAnchor, SctOpaquePlacement::After, cursor)) return result;
            }
        } else if (const auto* marker = std::get_if<SctStringGroupMarkerSectionContent>(&section.content)) {
            const auto preambleBytes = encodeWords(marker->preambleWords, options.byteOrder);
            const auto preambleOffset = canvas.placeAtFirstFit(cursor, preambleBytes);
            if (!preambleOffset) return result;
            if (*preambleOffset != sectionStart) {
                addDiagnostic(result.diagnostics, SctDiagnosticCode::OpaquePlacementUnsatisfied,
                    "Indexed-string group marker could not begin at its physical section start.",
                    SctDocumentEntityId{section.id});
                return result;
            }
            cursor = *preambleOffset + static_cast<std::uint32_t>(preambleBytes.size());
        } else if (const auto* stringContent = std::get_if<SctStringSectionContent>(&section.content)) {
            const auto& string = stringContent->string;
            if (!placedStrings.insert(string.id.value()).second) {
                addDiagnostic(result.diagnostics, SctDiagnosticCode::EncodingUnsupported,
                    "An indexed string ID occurs more than once.", SctDocumentEntityId{string.id});
                return result;
            }
            const auto preambleBytes = encodeWords(stringContent->preambleWords, options.byteOrder);
            const auto preambleOffset = canvas.placeAtFirstFit(cursor, preambleBytes);
            if (!preambleOffset) return result;
            if (*preambleOffset != sectionStart) {
                addDiagnostic(result.diagnostics, SctDiagnosticCode::OpaquePlacementUnsatisfied,
                    "Indexed string preamble could not begin at its physical section start.",
                    SctDocumentEntityId{section.id});
                return result;
            }
            cursor = *preambleOffset + static_cast<std::uint32_t>(preambleBytes.size());
            indexedStringTargetSpans.emplace(string.id.value(),
                SctDocumentByteSpan{sectionStart, static_cast<std::uint32_t>(preambleBytes.size())});
            const SctOpaqueAnchor stringAnchor{string.id};
            if (!placeAttachments(stringAnchor, SctOpaquePlacement::Before, cursor)) return result;
            const auto encodedText = encodeSctTextRecord(string.value, string.kind,
                SctTextStorage::IndexedSection, options.textEncoding);
            if (!encodedText.bytes) {
                addDiagnostic(result.diagnostics, SctDiagnosticCode::EncodingUnsupported,
                    encodedText.error, SctDocumentEntityId{string.id});
                return result;
            }
            const auto& bytes = *encodedText.bytes;
            const auto offset = canvas.placeAtFirstFit(cursor, bytes);
            if (!offset) return result;
            const SctDocumentByteSpan span{*offset, static_cast<std::uint32_t>(bytes.size())};
            layout.strings.push_back({string.id, span});
            result.preservation.text.push_back(
                {SctDocumentEntityId{string.id}, span, textMaterializationStatus(string.value)});
            cursor = span.offset + span.size;
            cursor = fixedEndFor(stringAnchor, cursor);
            if (!placeAttachments(stringAnchor, SctOpaquePlacement::After, cursor)) return result;
        }

        cursor = fixedEndFor(sectionAnchor, cursor);
        if (!placeAttachments(sectionAnchor, SctOpaquePlacement::After, cursor)) return result;
        const SctDocumentByteSpan sectionSpan{sectionStart, cursor - sectionStart};
        const auto rowOffset = kHeaderSize + static_cast<std::uint32_t>(sectionIndex * kIndexEntrySize);
        layout.sections.push_back({section.id, {rowOffset, kIndexEntrySize}, sectionSpan, sectionStart - dataStart});
        patchWord(canvas.bytes(), rowOffset, sectionStart - dataStart, options.byteOrder);
    }

    for (const auto& footer : document.footerEntries) {
        const SctOpaqueAnchor footerAnchor{footer.id};
        if (!placeAttachments(footerAnchor, SctOpaquePlacement::Before, cursor)) return result;
        const auto encodedText = encodeSctTextRecord(footer.value, footer.kind,
            SctTextStorage::Footer, options.textEncoding);
        if (!encodedText.bytes) {
            addDiagnostic(result.diagnostics, SctDiagnosticCode::EncodingUnsupported,
                encodedText.error, SctDocumentEntityId{footer.id});
            return result;
        }
        const auto& bytes = *encodedText.bytes;
        const auto offset = canvas.placeAtFirstFit(cursor, bytes);
        if (!offset) return result;
        const SctDocumentByteSpan span{*offset, static_cast<std::uint32_t>(bytes.size())};
        layout.footerEntries.push_back({footer.id, span});
        result.preservation.text.push_back(
            {SctDocumentEntityId{footer.id}, span, textMaterializationStatus(footer.value)});
        footerSpans.emplace(footer.id.value(), span);
        cursor = span.offset + span.size;
        cursor = fixedEndFor(footerAnchor, cursor);
        if (!placeAttachments(footerAnchor, SctOpaquePlacement::After, cursor)) return result;
    }
    cursor = fixedEndFor(documentAnchor, cursor);
    if (!placeAttachments(documentAnchor, SctOpaquePlacement::After, cursor)) return result;
    if (!canvas.ensure(cursor)) return result;

    for (const auto& pending : pendingRelocations) {
        const auto sourceIt = instructionSpans.find(pending.source.value());
        if (sourceIt == instructionSpans.end()) {
            addDiagnostic(result.diagnostics, SctDiagnosticCode::UnresolvedReference,
                "Relocation source instruction has no layout span.", SctDocumentEntityId{pending.source});
            continue;
        }
        std::optional<SctDocumentByteSpan> targetSpan;
        std::visit([&](const auto& target) {
            using T = std::decay_t<decltype(target)>;
            if constexpr (std::is_same_v<T, SctInstructionId>) {
                if (const auto found = instructionSpans.find(target.value()); found != instructionSpans.end()) targetSpan = found->second;
            } else if constexpr (std::is_same_v<T, SctStringId>) {
                if (const auto found = indexedStringTargetSpans.find(target.value()); found != indexedStringTargetSpans.end()) targetSpan = found->second;
            } else {
                if (const auto found = footerSpans.find(target.value()); found != footerSpans.end()) targetSpan = found->second;
            }
        }, pending.target);
        if (!targetSpan || pending.wordOffsetWithinInstruction < dataStart) {
            addDiagnostic(result.diagnostics, SctDiagnosticCode::UnresolvedReference,
                "Relocation target has no layout span.", SctDocumentEntityId{pending.source});
            continue;
        }
        const auto targetDataOffset = static_cast<std::int64_t>(targetSpan->offset - dataStart);
        if (pending.targetAlignment == 0u
            || (targetDataOffset % static_cast<std::int64_t>(pending.targetAlignment)) != 0) {
            addDiagnostic(result.diagnostics, SctDiagnosticCode::RelocationOutOfRange,
                "Document reference target does not satisfy the opcode alignment contract.",
                SctDocumentEntityId{pending.source});
            continue;
        }
        std::int64_t value = 0;
        if (pending.formula == SctRelocationFormula::InstructionEndMinusWord) {
            value = targetDataOffset - static_cast<std::int64_t>(sourceIt->second.offset - dataStart)
                - static_cast<std::int64_t>(sourceIt->second.size) + 4;
        } else {
            value = targetDataOffset - static_cast<std::int64_t>(pending.wordOffsetWithinInstruction - dataStart);
        }
        const bool encodable = pending.signedValue
            ? value >= std::numeric_limits<std::int32_t>::min() && value <= std::numeric_limits<std::int32_t>::max()
            : value >= 0 && value <= std::numeric_limits<std::uint32_t>::max();
        if (!encodable) {
            addDiagnostic(result.diagnostics, SctDiagnosticCode::RelocationOutOfRange,
                "Document reference cannot be represented by the opcode's relative word.",
                SctDocumentEntityId{pending.source});
            continue;
        }
        const auto encodedValue = static_cast<std::uint32_t>(value);
        if ((encodedValue & ~pending.encodedValueMask) != 0u) {
            addDiagnostic(result.diagnostics, SctDiagnosticCode::RelocationOutOfRange,
                "Document reference encoding would use reserved low target bits.",
                SctDocumentEntityId{pending.source});
            continue;
        }
        patchWord(canvas.bytes(), pending.wordOffsetWithinInstruction, encodedValue, options.byteOrder);
        layout.relocations.push_back({pending.source, pending.parameter, pending.target, pending.formula,
            {pending.wordOffsetWithinInstruction, 4}, encodedValue});
    }
    if (hasErrors(result.diagnostics)) return result;

    layout.decodedPayloadSize = static_cast<std::uint32_t>(canvas.bytes().size());
    for (const auto& attachment : document.opaqueAttachments) {
        const auto found = opaquePlacements.find(attachment.id.value());
        const auto record = found == opaquePlacements.end()
            ? SctOpaquePlacementRecord{attachment.id, {}, SctOpaquePreservationStatus::Rejected}
            : found->second;
        layout.opaquePlacements.push_back(record);
        result.preservation.attachments.push_back(record);
        if (record.status == SctOpaquePreservationStatus::Rejected) {
            addDiagnostic(result.diagnostics, SctDiagnosticCode::OpaquePlacementUnsatisfied,
                "An opaque attachment was not emitted under strict preservation.", SctDocumentEntityId{attachment.id});
        }
    }
    if (hasErrors(result.diagnostics)) return result;

    result.success = true;
    result.payload = std::move(canvas.bytes());
    result.layout = std::move(layout);
    return result;
}

void markFailedPreservation(const SctDocument& document, InternalBuildResult& result) {
    if (result.success) return;
    result.preservation.text.clear();
    result.preservation.attachments.clear();
    result.preservation.attachments.reserve(document.opaqueAttachments.size());
    for (const auto& attachment : document.opaqueAttachments) {
        result.preservation.attachments.push_back(
            {attachment.id, {}, SctOpaquePreservationStatus::Rejected});
    }
}

} // namespace

SctDocumentLayoutResult SctDocumentLayoutEngine::layout(
    const SctDocument& document,
    const SctDocumentExportOptions& options,
    const SctBoundImportEvidence* evidence,
    const SctTargetValidationReceipt* validationReceipt) {
    auto built = buildPayload(document, options, evidence, validationReceipt);
    markFailedPreservation(document, built);
    return {built.success, std::move(built.layout), std::move(built.diagnostics),
        std::move(built.preservation), built.validationReceiptUse};
}

SctDocumentExportResult SctDocumentExporter::exportDocument(
    const SctDocument& document,
    const SctDocumentExportOptions& options,
    const SctBoundImportEvidence* evidence,
    const SctTargetValidationReceipt* validationReceipt) {
    auto built = buildPayload(document, options, evidence, validationReceipt);
    markFailedPreservation(document, built);
    SctDocumentExportResult result;
    result.success = built.success;
    result.layout = std::move(built.layout);
    result.diagnostics = std::move(built.diagnostics);
    result.preservation = std::move(built.preservation);
    result.validationReceiptUse = built.validationReceiptUse;
    if (!built.success) return result;
    result.decodedPayloadSize = static_cast<std::uint32_t>(built.payload.size());
    if (options.wrapper == SctDocumentOutputWrapper::Aklz) {
        auto compressed = spice::compression::aklz::compress(built.payload);
        if (!compressed.ok()) {
            addDiagnostic(result.diagnostics, SctDiagnosticCode::CompressionFailed,
                "AKLZ compression failed while exporting the SCT document.");
            result.success = false;
            result.layout.reset();
            result.preservation.text.clear();
            for (auto& attachment : result.preservation.attachments) {
                attachment.span = {};
                attachment.status = SctOpaquePreservationStatus::Rejected;
            }
            return result;
        }
        result.bytes = std::move(compressed.bytes);
    } else {
        result.bytes = std::move(built.payload);
    }
    result.outputSize = result.bytes.size();
    return result;
}

} // namespace spice::sct
