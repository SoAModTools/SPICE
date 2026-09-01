#include "SctDocumentImporter.h"

#include "SctTextCodec.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace spice::sct {
namespace {

void addDiagnostic(SctDocumentImportResult& result, SctDiagnosticSeverity severity,
    SctDiagnosticCode code, std::string message,
    std::optional<SctDocumentEntityId> entity = std::nullopt) {
    result.diagnostics.push_back({severity, code, std::move(entity), std::move(message)});
}

bool hasUnknownNode(const SctScptAstNode& node) {
    if (node.kind == SctScptAstNodeKind::Unknown) return true;
    return std::any_of(node.children.begin(), node.children.end(), hasUnknownNode);
}

SctCanonicalExpressionNodeKind canonicalKind(SctScptAstNodeKind kind) {
    switch (kind) {
    case SctScptAstNodeKind::NoLoopValue: return SctCanonicalExpressionNodeKind::NoLoopValue;
    case SctScptAstNodeKind::RawValue: return SctCanonicalExpressionNodeKind::RawValue;
    case SctScptAstNodeKind::FloatLiteral: return SctCanonicalExpressionNodeKind::FloatLiteral;
    case SctScptAstNodeKind::DecimalLiteral: return SctCanonicalExpressionNodeKind::DecimalLiteral;
    case SctScptAstNodeKind::IntVariable: return SctCanonicalExpressionNodeKind::IntVariable;
    case SctScptAstNodeKind::FloatVariable: return SctCanonicalExpressionNodeKind::FloatVariable;
    case SctScptAstNodeKind::BitVariable: return SctCanonicalExpressionNodeKind::BitVariable;
    case SctScptAstNodeKind::ByteVariable: return SctCanonicalExpressionNodeKind::ByteVariable;
    case SctScptAstNodeKind::SecondaryValue: return SctCanonicalExpressionNodeKind::SecondaryValue;
    case SctScptAstNodeKind::CompareOp: return SctCanonicalExpressionNodeKind::CompareOperator;
    case SctScptAstNodeKind::ArithmeticOp: return SctCanonicalExpressionNodeKind::ArithmeticOperator;
    case SctScptAstNodeKind::AssignmentOp: return SctCanonicalExpressionNodeKind::AssignmentOperator;
    case SctScptAstNodeKind::Stop: return SctCanonicalExpressionNodeKind::Stop;
    case SctScptAstNodeKind::Unknown: break;
    }
    return SctCanonicalExpressionNodeKind::RawValue;
}

SctCanonicalExpressionNode convertNode(const SctScptAstNode& node) {
    SctCanonicalExpressionNode converted;
    converted.kind = canonicalKind(node.kind);
    if (!node.rawWords.empty()) {
        converted.encodingCode = node.rawWords.front();
        converted.payloadWords.assign(node.rawWords.begin() + 1, node.rawWords.end());
    }
    converted.children.reserve(node.children.size());
    for (const auto& child : node.children) converted.children.push_back(convertNode(child));
    return converted;
}

SctCanonicalExpression convertExpression(const SctExpression& expression,
    const std::vector<std::uint32_t>& rawWords, SctDocumentImportResult& result,
    SctDocumentEntityId entity) {
    SctCanonicalExpression converted;
    converted.termination = expression.hitStopCode ? SctExpressionTermination::StopCode
                                                    : SctExpressionTermination::InlineValue;
    if (!expression.ast || hasUnknownNode(*expression.ast)) {
        converted.root = SctOpaqueExpression{rawWords};
        addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::AmbiguousExpression,
            "SCPT expression retained as opaque words because its exact typed structure is incomplete.", entity);
    } else {
        converted.root = convertNode(*expression.ast);
    }
    return converted;
}

std::uint16_t readHeaderU16(const std::vector<std::uint8_t>& bytes, std::size_t offset,
    SctSourceByteOrder order) {
    if (order == SctSourceByteOrder::LittleEndian) {
        return static_cast<std::uint16_t>(bytes[offset]
            | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
    }
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8)
        | bytes[offset + 1]);
}

struct ClaimLedger {
    std::vector<std::uint8_t> claims;
    bool claim(std::size_t begin, std::size_t size) {
        if (begin > claims.size() || size > claims.size() - begin) return false;
        for (std::size_t i = begin; i < begin + size; ++i) if (claims[i]) return false;
        std::fill(claims.begin() + begin, claims.begin() + begin + size, 1);
        return true;
    }
};

using InstructionMap = std::unordered_map<std::uint32_t, SctInstructionId>;
using StringMap = std::unordered_map<std::uint32_t, SctStringId>;
using FooterMap = std::unordered_map<std::uint32_t, SctFooterEntryId>;

std::vector<std::size_t> physicalInstructionOrder(const SctSection& section) {
    std::vector<std::size_t> order(section.instructions.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        return section.instructions[left].offset < section.instructions[right].offset;
    });
    return order;
}

struct ControlTarget {
    std::optional<SctInstructionId> id;
    std::optional<std::uint32_t> payloadOffset;
};

ControlTarget edgeTarget(const SctSection& section, const SctInstruction& instruction,
    std::uint32_t parameterIndex, const SctOpcodeSchema& schema, const InstructionMap& ids) {
    SctEdgeType wanted = SctEdgeType::Jump;
    switch (schema.semantic.controlRole) {
    case SctOpcodeControlRole::Branch: wanted = SctEdgeType::BranchFalse; break;
    case SctOpcodeControlRole::Switch: wanted = SctEdgeType::SwitchCase; break;
    case SctOpcodeControlRole::Jump: wanted = SctEdgeType::Jump; break;
    case SctOpcodeControlRole::CallSubscript: wanted = SctEdgeType::CallSubscript; break;
    default: return {};
    }
    std::vector<const SctEdge*> matching;
    for (const auto& edge : section.edges) {
        if (edge.type == wanted && edge.fromPayloadOffset == instruction.payloadOffset && edge.toPayloadOffset) {
            matching.push_back(&edge);
        }
    }
    const SctEdge* selected = nullptr;
    if (wanted == SctEdgeType::SwitchCase) {
        const auto repeated = sctOpcodeRepeatedGroup(schema);
        if (!repeated || parameterIndex < repeated->firstParameter) return {};
        const auto width = repeated->lastParameter - repeated->firstParameter + 1;
        const auto ordinal = (parameterIndex - repeated->firstParameter) / width;
        if (ordinal < matching.size()) selected = matching[ordinal];
    } else if (!matching.empty()) {
        selected = matching.front();
    }
    if (!selected) return {};
    const auto found = ids.find(*selected->toPayloadOffset);
    return {found == ids.end() ? std::nullopt : std::optional(found->second), selected->toPayloadOffset};
}

std::optional<SctFooterEntryId> footerTarget(const SctInstruction& instruction,
    std::uint32_t parameterIndex, const SctFooter& footer, const FooterMap& ids) {
    for (const auto& entry : footer.entries) {
        for (const auto& reference : entry.references) {
            if (reference.instructionPayloadOffset == instruction.payloadOffset
                && reference.parameterIndex == parameterIndex) {
                const auto found = ids.find(entry.payloadOffset);
                if (found != ids.end()) return found->second;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t> operandPayloadOffset(
    const SctInstruction& instruction, const SctParameter& parameter) {
    std::uint32_t operandWordIndex = instruction.opcodeWordIndex + 1u;
    bool foundParameter = false;
    for (const auto& candidate : instruction.parameters) {
        if (candidate.index == parameter.index) {
            foundParameter = true;
            break;
        }
        operandWordIndex += static_cast<std::uint32_t>(candidate.rawWords.size());
    }
    if (!foundParameter) return std::nullopt;
    return instruction.payloadOffset + operandWordIndex * 4u;
}

std::optional<std::int64_t> calculatedTextTarget(const SctInstruction& instruction,
    const SctParameter& parameter, const SctOpcodeTextReferenceRule& rule,
    std::optional<std::uint32_t> operandOffset) {
    if (parameter.rawWords.empty() || !operandOffset) return std::nullopt;
    const auto relativeBase = rule.relativeBase == SctRelativeReferenceBase::OperandWord
        ? static_cast<std::int64_t>(*operandOffset)
        : static_cast<std::int64_t>(instruction.payloadOffset + instruction.sizeBytes - 4u);
    const auto masked = parameter.rawWords.front() & rule.encodedValueMask;
    const auto relative = rule.signedRelative
        ? static_cast<std::int64_t>(static_cast<std::int32_t>(masked))
        : static_cast<std::int64_t>(masked);
    return relativeBase + relative;
}

std::optional<std::int64_t> calculatedControlTarget(const SctInstruction& instruction,
    const SctParameter& parameter, const SctOpcodeParameterSchema& rule,
    std::optional<std::uint32_t> operandOffset) {
    if (parameter.rawWords.empty() || !operandOffset) return std::nullopt;
    const auto relativeBase = rule.relativeReferenceBase == SctRelativeReferenceBase::OperandWord
        ? static_cast<std::int64_t>(*operandOffset)
        : static_cast<std::int64_t>(instruction.payloadOffset + instruction.sizeBytes - 4u);
    const auto masked = parameter.rawWords.front() & rule.referenceEncodedValueMask;
    const auto relative = rule.relativeReferenceSigned
        ? static_cast<std::int64_t>(static_cast<std::int32_t>(masked))
        : static_cast<std::int64_t>(masked);
    return relativeBase + relative;
}

std::optional<SctStringId> indexedStringTarget(std::optional<std::int64_t> target,
    const SctOpcodeTextReferenceRule& rule, const StringMap& ids) {
    if (!target || *target < 0 || *target > std::numeric_limits<std::uint32_t>::max()
        || static_cast<std::uint32_t>(*target) % rule.targetAlignment != 0u) return std::nullopt;
    const auto found = ids.find(static_cast<std::uint32_t>(*target));
    return found == ids.end() ? std::nullopt : std::optional(found->second);
}

SctParameterAddress parameterAddress(const SctOpcodeSchema& schema, std::uint32_t parameterIndex) {
    SctParameterAddress address{sctOpcodeBaseParameterIndex(schema, parameterIndex), std::nullopt};
    if (const auto repeated = sctOpcodeRepeatedGroup(schema);
        repeated && parameterIndex >= repeated->firstParameter) {
        const auto width = repeated->lastParameter - repeated->firstParameter + 1u;
        address.repeatedGroupOrdinal = (parameterIndex - repeated->firstParameter) / width;
    }
    return address;
}

SctExpectedReferenceTarget expectedInstructionTarget() {
    return {SctReferenceTargetStorage::Instruction, std::nullopt};
}

SctExpectedReferenceTarget expectedTextTarget(const SctOpcodeTextReferenceRule& rule) {
    return {rule.storage == SctTextStorage::IndexedSection
            ? SctReferenceTargetStorage::IndexedString : SctReferenceTargetStorage::FooterEntry,
        rule.kind};
}

SctDocumentParameter unresolvedReference(const SctParameter& parameter,
    const SctInstruction& instruction, const SctOpcodeSchema& schema,
    SctExpectedReferenceTarget expected, std::optional<std::uint32_t> operandOffset,
    std::optional<std::int64_t> targetOffset, SctDocumentImportResult& result,
    SctInstructionId entityId, std::uint32_t dataStart, std::string message) {
    const auto address = parameterAddress(schema, parameter.index);
    const auto absoluteOperand = operandOffset
        ? std::optional<std::uint32_t>{dataStart + *operandOffset} : std::nullopt;
    const auto absoluteTarget = targetOffset
        ? std::optional<std::int64_t>{static_cast<std::int64_t>(dataStart) + *targetOffset}
        : std::nullopt;
    result.receipt.unresolvedReferences.push_back({entityId, address,
        dataStart + instruction.payloadOffset, absoluteOperand, absoluteTarget});
    addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::UnresolvedReference,
        std::move(message), SctDocumentEntityId{entityId});
    return {address.schemaIndex, SctUnresolvedReferenceValue{std::move(expected), parameter.rawWords}};
}

SctDocumentParameter makeParameter(const SctParameter& parameter, const SctInstruction& instruction,
    const SctSection& section, const SctOpcodeSchema& schema, const InstructionMap& instructionIds,
    const StringMap& stringIds, const SctFooter* footer, const FooterMap& footerIds, SctDocumentImportResult& result,
    SctInstructionId entityId, std::uint32_t dataStart) {
    SctDocumentParameter converted;
    converted.schemaIndex = sctOpcodeBaseParameterIndex(schema, parameter.index);
    if (schema.semantic.controlRole != SctOpcodeControlRole::None) {
        const auto pattern = schema.parameters;
        const bool isTarget = static_cast<int>(converted.schemaIndex) == pattern.jumpParam
            || static_cast<int>(converted.schemaIndex) == pattern.switchJumpParam
            || (schema.semantic.controlRole == SctOpcodeControlRole::CallSubscript && converted.schemaIndex == 0);
        if (isTarget) {
            const auto target = edgeTarget(section, instruction, parameter.index, schema, instructionIds);
            if (target.id) {
                converted.value = SctInstructionReference{*target.id};
                return converted;
            }
            const auto operandOffset = operandPayloadOffset(instruction, parameter);
            const auto* parameterSchema = sctOpcodeParameterSchema(schema, parameter.index);
            const auto calculated = parameterSchema
                ? calculatedControlTarget(instruction, parameter, *parameterSchema, operandOffset)
                : std::optional<std::int64_t>{};
            return unresolvedReference(parameter, instruction, schema, expectedInstructionTarget(),
                operandOffset, target.payloadOffset
                    ? std::optional<std::int64_t>{static_cast<std::int64_t>(*target.payloadOffset)} : calculated,
                result, entityId, dataStart,
                "Control-flow target could not be resolved to an instruction ID.");
        }
    }
    if (const auto textReference = sctOpcodeTextReference(schema, parameter.index); textReference.has_value()) {
        const auto operandOffset = operandPayloadOffset(instruction, parameter);
        const auto calculatedTarget = calculatedTextTarget(instruction, parameter, *textReference, operandOffset);
        if (textReference->storage == SctTextStorage::IndexedSection) {
            if (const auto target = indexedStringTarget(calculatedTarget, *textReference, stringIds)) {
                converted.value = SctStringReference{*target};
                return converted;
            }
            return unresolvedReference(parameter, instruction, schema, expectedTextTarget(*textReference),
                operandOffset, calculatedTarget, result, entityId,
                dataStart,
                "Indexed SCT string target could not be resolved to a string ID.");
        }
        if (footer) {
            if (const auto target = footerTarget(instruction, parameter.index, *footer, footerIds)) {
                converted.value = SctFooterEntryReference{*target};
                return converted;
            }
        }
        return unresolvedReference(parameter, instruction, schema, expectedTextTarget(*textReference),
            operandOffset, calculatedTarget, result, entityId,
            dataStart,
            "Footer target could not be resolved to a footer-entry ID.");
    }
    const auto encoding = sctOpcodeParameterEncoding(schema, parameter.index);
    if (encoding == SctOpcodeParameterEncoding::ScptExpression) {
        if (parameter.expression) {
            converted.value = convertExpression(*parameter.expression, parameter.rawWords, result, SctDocumentEntityId{entityId});
        } else {
            converted.value = SctCanonicalExpression{SctOpaqueExpression{parameter.rawWords},
                SctExpressionTermination::InlineValue};
            addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::AmbiguousExpression,
                "SCPT parameter had no parser AST and was retained as opaque words.", SctDocumentEntityId{entityId});
        }
    } else if (encoding == SctOpcodeParameterEncoding::RawWordsUntilSentinel) {
        converted.value = SctTerminatedWordSequenceValue{parameter.rawWords};
    } else if (parameter.rawWords.size() == 1) {
        converted.value = SctEncodedWordValue{parameter.rawWords.front()};
    } else {
        converted.value = SctOpaqueParameterValue{parameter.rawWords};
    }
    return converted;
}

SctOpaqueAttachmentId addAttachment(SctDocument& document, SctDocumentImportResult& result, SctOpaqueAnchor anchor,
    std::uint32_t absoluteOffset, std::vector<std::uint8_t> bytes, SctOpaqueReason reason) {
    if (bytes.empty()) return {};
    const auto id = document.allocateOpaqueAttachmentId();
    document.opaqueAttachments.push_back({id, std::move(bytes), std::move(anchor),
        SctOpaquePlacement::FixedOffset, absoluteOffset, 1, SctOpaqueRelocationSupport::FixedOnly, reason});
    result.receipt.provenance.push_back({SctDocumentEntityId{id}, absoluteOffset,
        static_cast<std::uint32_t>(document.opaqueAttachments.back().bytes.size()), std::nullopt,
        SctSourceCoverageKind::OpaqueAttachment});
    return id;
}

} // namespace

SctDocumentImportResult SctDocumentImporter::import(
    const SctParseResult& parsed,
    const SctDocumentImportOptions& options) {
    SctDocumentImportResult result;
    result.receipt.source.byteOrder = parsed.file.detectedEndian == "big" ? SctSourceByteOrder::BigEndian
        : parsed.file.detectedEndian == "little" ? SctSourceByteOrder::LittleEndian : SctSourceByteOrder::Unknown;
    result.receipt.source.wrapper = parsed.file.originalCompressedAklz ? SctSourceWrapper::Aklz : SctSourceWrapper::None;
    result.receipt.declaredSourcePlatform = options.declaredSourcePlatform;
    result.receipt.sourceTextEncoding = options.sourceTextEncoding;
    if (!parsed.parseOk) {
        addDiagnostic(result, SctDiagnosticSeverity::Error, SctDiagnosticCode::ParseFailed,
            "A canonical document cannot be imported from a failed parse.");
        return result;
    }
    const auto& bytes = parsed.file.originalPayloadBytes;
    const std::uint64_t dataStart64 = 12ull + (20ull * parsed.file.sections.size());
    if (bytes.size() < dataStart64) {
        addDiagnostic(result, SctDiagnosticSeverity::Error, SctDiagnosticCode::UnsafePhysicalStructure,
            "Decoded SCT payload is shorter than its physical index table.");
        return result;
    }
    const auto dataStart = static_cast<std::uint32_t>(dataStart64);
    if (bytes.size() >= 8u) {
        auto& header = result.receipt.source.header;
        std::copy_n(bytes.begin(), 8u, header.rawBytes.begin());
        if (result.receipt.source.byteOrder != SctSourceByteOrder::Unknown) {
            header.values = {
                readHeaderU16(bytes, 0u, result.receipt.source.byteOrder),
                readHeaderU16(bytes, 2u, result.receipt.source.byteOrder),
                readHeaderU16(bytes, 4u, result.receipt.source.byteOrder),
                readHeaderU16(bytes, 6u, result.receipt.source.byteOrder),
            };
            header.available = true;
        }
    }
    for (const auto& section : parsed.file.sections) {
        if (section.startOffset > section.endOffset || section.endOffset > bytes.size()) {
            addDiagnostic(result, SctDiagnosticSeverity::Error, SctDiagnosticCode::UnsafePhysicalStructure,
                "A physical section has contradictory decoded-payload bounds.");
            return result;
        }
    }

    SctDocument document;
    std::vector<SctSectionId> sectionIds;
    sectionIds.reserve(parsed.file.sections.size());
    for (std::size_t i = 0; i < parsed.file.sections.size(); ++i) sectionIds.push_back(document.allocateSectionId());

    StringMap stringIds;
    for (const auto& section : parsed.file.sections) {
        if (section.stringEntry.has_value()) {
            stringIds.emplace(section.startOffset - dataStart, document.allocateStringId());
        }
    }

    InstructionMap instructionIds;
    for (const auto& section : parsed.file.sections) {
        std::vector<int> owners(section.endOffset - section.startOffset, -1);
        std::vector<bool> unsafe(section.instructions.size(), false);
        const auto order = physicalInstructionOrder(section);
        for (const auto i : order) {
            const auto& instruction = section.instructions[i];
            if (!instruction.decodeOk || !findSctOpcodeSchema(instruction.opcode)
                || instruction.offset > owners.size() || instruction.sizeBytes > owners.size() - instruction.offset) {
                unsafe[i] = true;
                continue;
            }
            for (std::size_t pos = instruction.offset; pos < instruction.offset + instruction.sizeBytes; ++pos) {
                if (owners[pos] >= 0) {
                    unsafe[i] = true;
                    unsafe[static_cast<std::size_t>(owners[pos])] = true;
                } else {
                    owners[pos] = static_cast<int>(i);
                }
            }
        }
        for (const auto i : order) {
            if (!unsafe[i]) instructionIds.emplace(section.instructions[i].payloadOffset, document.allocateInstructionId());
        }
    }
    FooterMap footerIds;
    std::vector<std::size_t> footerEntryOrder;
    if (parsed.file.footer) {
        const auto& sourceFooter = *parsed.file.footer;
        footerEntryOrder.resize(sourceFooter.entries.size());
        std::iota(footerEntryOrder.begin(), footerEntryOrder.end(), 0);
        std::stable_sort(footerEntryOrder.begin(), footerEntryOrder.end(), [&](std::size_t left, std::size_t right) {
            return sourceFooter.entries[left].payloadOffset < sourceFooter.entries[right].payloadOffset;
        });
        const auto footerSize = sourceFooter.payloadEndOffset >= sourceFooter.payloadStartOffset
            ? sourceFooter.payloadEndOffset - sourceFooter.payloadStartOffset : 0;
        std::vector<int> owners(footerSize, -1);
        std::vector<bool> unsafe(sourceFooter.entries.size(), false);
        for (const auto i : footerEntryOrder) {
            const auto& entry = sourceFooter.entries[i];
            if (entry.payloadOffset < sourceFooter.payloadStartOffset) { unsafe[i] = true; continue; }
            const auto local = entry.payloadOffset - sourceFooter.payloadStartOffset;
            if (local > owners.size() || entry.rawBytes.size() > owners.size() - local) { unsafe[i] = true; continue; }
            for (std::size_t pos = local; pos < local + entry.rawBytes.size(); ++pos) {
                if (owners[pos] >= 0) {
                    unsafe[i] = true;
                    unsafe[static_cast<std::size_t>(owners[pos])] = true;
                } else owners[pos] = static_cast<int>(i);
            }
        }
        for (const auto i : footerEntryOrder) {
            if (!unsafe[i]) footerIds.emplace(sourceFooter.entries[i].payloadOffset, document.allocateFooterEntryId());
        }
    }

    result.receipt.provenance.push_back({{}, 0, 8, std::nullopt,
        SctSourceCoverageKind::SourceObservation});
    result.receipt.provenance.push_back({{}, 8, 4, std::nullopt});
    std::vector<std::string> sectionNames;
    sectionNames.reserve(parsed.file.sections.size());
    for (std::size_t sectionIndex = 0; sectionIndex < parsed.file.sections.size(); ++sectionIndex) {
        const auto rowOffset = static_cast<std::uint32_t>(12 + sectionIndex * 20);
        result.receipt.provenance.push_back({SctDocumentEntityId{sectionIds[sectionIndex]}, rowOffset,
            4, static_cast<std::uint32_t>(sectionIndex)});
        const auto nameBegin = bytes.begin() + rowOffset + 4;
        const auto zero = std::find(nameBegin, nameBegin + 16, 0);
        sectionNames.emplace_back(nameBegin, zero);
        if (!sectionNames.back().empty()) {
            result.receipt.provenance.push_back({SctDocumentEntityId{sectionIds[sectionIndex]}, rowOffset + 4,
                static_cast<std::uint32_t>(sectionNames.back().size()), static_cast<std::uint32_t>(sectionIndex)});
        }
        const auto fieldEnd = nameBegin + 16;
        const auto unusual = std::find_if(zero, fieldEnd, [](std::uint8_t value) { return value != 0; });
        if (unusual == fieldEnd) {
            result.receipt.provenance.push_back({SctDocumentEntityId{sectionIds[sectionIndex]},
                rowOffset + 4 + static_cast<std::uint32_t>(sectionNames.back().size()),
                static_cast<std::uint32_t>(fieldEnd - zero), static_cast<std::uint32_t>(sectionIndex),
                SctSourceCoverageKind::DerivedLayout});
        } else {
            if (unusual != zero) {
                result.receipt.provenance.push_back({SctDocumentEntityId{sectionIds[sectionIndex]},
                    rowOffset + 4 + static_cast<std::uint32_t>(sectionNames.back().size()),
                    static_cast<std::uint32_t>(unusual - zero), static_cast<std::uint32_t>(sectionIndex),
                    SctSourceCoverageKind::DerivedLayout});
            }
            addAttachment(document, result, sectionIds[sectionIndex],
                rowOffset + 4 + static_cast<std::uint32_t>(unusual - nameBegin),
                std::vector<std::uint8_t>(unusual, fieldEnd), SctOpaqueReason::Padding);
        }
    }
    const SctFooter* footer = parsed.file.footer ? &*parsed.file.footer : nullptr;
    if (footer) {
        const auto dataSize = bytes.size() - static_cast<std::size_t>(dataStart64);
        if (footer->payloadStartOffset > footer->payloadEndOffset || footer->payloadEndOffset > dataSize) {
            addDiagnostic(result, SctDiagnosticSeverity::Error, SctDiagnosticCode::UnsafePhysicalStructure,
                "The parsed footer has contradictory decoded-payload bounds.");
            return result;
        }
    }
    for (std::size_t sectionIndex = 0; sectionIndex < parsed.file.sections.size(); ++sectionIndex) {
        const auto& sourceSection = parsed.file.sections[sectionIndex];
        SctDocumentSection targetSection;
        targetSection.id = sectionIds[sectionIndex];
        targetSection.nameBytes = sectionNames[sectionIndex];
        ClaimLedger ledger{std::vector<std::uint8_t>(sourceSection.endOffset - sourceSection.startOffset)};

        if (sourceSection.stringEntry) {
            const auto stringIdIt = stringIds.find(sourceSection.startOffset - dataStart);
            if (stringIdIt == stringIds.end()) {
                addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::UnsafePhysicalStructure,
                    "Indexed string section had no deterministic string identity.", SctDocumentEntityId{targetSection.id});
                targetSection.content = SctOpaqueSectionContent{};
                document.sections.push_back(std::move(targetSection));
                continue;
            }
            const auto stringId = stringIdIt->second;
            const auto& entry = *sourceSection.stringEntry;
            const auto local = static_cast<std::size_t>(entry.textStartOffset);
            const auto preambleSize = entry.preambleWords.size() * sizeof(std::uint32_t);
            const auto sectionSize = static_cast<std::size_t>(sourceSection.endOffset - sourceSection.startOffset);
            const auto physicalTextSize = local <= sectionSize ? sectionSize - local : 0u;
            std::vector<std::uint8_t> physicalText;
            if (local <= sectionSize) {
                physicalText.assign(bytes.begin() + sourceSection.startOffset + local,
                    bytes.begin() + sourceSection.endOffset);
            }
            std::size_t recordSize = physicalText.size();
            if (!entry.rawTextBytes.empty() && entry.rawTextBytes.size() <= physicalText.size()) {
                const auto suffix = physicalText.size() - entry.rawTextBytes.size();
                const bool derivedAlignment = suffix <= 3u
                    && std::all_of(physicalText.begin() + entry.rawTextBytes.size(), physicalText.end(),
                        [](std::uint8_t value) { return value == 0u; });
                if (derivedAlignment) recordSize = entry.rawTextBytes.size();
            } else if (physicalText.empty()) {
                recordSize = 0u;
            }
            std::vector<std::uint8_t> recordBytes(physicalText.begin(), physicalText.begin() + recordSize);
            SctDocumentString string{stringId, SctOpaqueText{recordBytes}, SctTextKind::SctString};
            std::string textReason = "No source text encoding was supplied.";
            bool semanticText = false;
            if (options.sourceTextEncoding.has_value()) {
                const auto decoded = decodeSctTextRecord(recordBytes, SctTextKind::SctString,
                    SctTextStorage::IndexedSection, *options.sourceTextEncoding);
                if (decoded.value) {
                    string.value = *decoded.value;
                    semanticText = !std::holds_alternative<SctOpaqueText>(string.value);
                    textReason.clear();
                } else textReason = decoded.error;
            }
            if (!semanticText) {
                addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::AmbiguousString,
                    "Indexed SCT string remained opaque: " + textReason, SctDocumentEntityId{stringId});
            }
            result.receipt.text.push_back({SctDocumentEntityId{stringId}, options.sourceTextEncoding,
                semanticText, textReason});
            const bool validPreamble = entry.preambleWords.size() >= 2u
                && entry.preambleWords.front() == 9u
                && entry.preambleWords.back() == 0x0000001du
                && std::find(entry.preambleWords.begin(), entry.preambleWords.end() - 1u, 0x0000001du)
                    == entry.preambleWords.end() - 1u;
            if (!validPreamble || local > ledger.claims.size() || physicalTextSize > ledger.claims.size() - local
                || !ledger.claim(0u, preambleSize) || !ledger.claim(local, recordSize)) {
                addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::OverlappingSourceClaims,
                    "String evidence overlapped or exceeded its physical section and was preserved opaquely.", SctDocumentEntityId{stringId});
                targetSection.content = SctOpaqueSectionContent{};
            } else {
                targetSection.content = SctStringSectionContent{std::move(string), entry.preambleWords};
                result.receipt.provenance.push_back({SctDocumentEntityId{targetSection.id},
                    sourceSection.startOffset, static_cast<std::uint32_t>(preambleSize),
                    static_cast<std::uint32_t>(sectionIndex)});
                result.receipt.provenance.push_back({SctDocumentEntityId{stringId},
                    sourceSection.startOffset + entry.textStartOffset,
                    static_cast<std::uint32_t>(recordSize), static_cast<std::uint32_t>(sectionIndex)});
            }
        } else if (sourceSection.kind == SctSectionKind::Label) {
            targetSection.content = SctLabelSectionContent{};
        } else if (!sourceSection.instructions.empty() || sourceSection.kind == SctSectionKind::Script) {
            SctScriptSectionContent script;
            const auto instructionOrder = physicalInstructionOrder(sourceSection);
            for (const auto instructionIndex : instructionOrder) {
                const auto& instruction = sourceSection.instructions[instructionIndex];
                const auto idIt = instructionIds.find(instruction.payloadOffset);
                const auto* schema = findSctOpcodeSchema(instruction.opcode);
                const auto localOffset = static_cast<std::size_t>(instruction.offset);
                if (idIt == instructionIds.end() || !schema || !ledger.claim(localOffset, instruction.sizeBytes)) {
                    addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::OverlappingSourceClaims,
                        "Instruction evidence could not be promoted safely; its bytes remain opaque.", SctDocumentEntityId{targetSection.id});
                    continue;
                }
                SctDocumentInstruction converted;
                converted.id = idIt->second;
                converted.opcode = instruction.opcode;
                converted.skipRefresh = instruction.skipRefresh;
                if (instruction.scheduled.present) {
                    const auto& scheduled = instruction.scheduled.frameDelay;
                    if (scheduled.expression) converted.scheduledExpression = convertExpression(
                        *scheduled.expression, scheduled.rawWords, result, SctDocumentEntityId{converted.id});
                    else converted.scheduledExpression = SctCanonicalExpression{SctOpaqueExpression{scheduled.rawWords}, SctExpressionTermination::InlineValue};
                }
                const auto repeated = sctOpcodeRepeatedGroup(*schema);
                std::uint32_t countValue = 0;
                bool countKnown = false;
                for (const auto& parameter : instruction.parameters) {
                    const auto base = sctOpcodeBaseParameterIndex(*schema, parameter.index);
                    if (repeated && base == repeated->iterationCountParameter && parameter.index < schema->parameters.paramCount) {
                        if (parameter.rawWords.size() == 1) { countValue = parameter.rawWords.front(); countKnown = true; }
                        continue;
                    }
                    auto canonical = makeParameter(parameter, instruction, sourceSection, *schema,
                        instructionIds, stringIds, footer, footerIds, result, converted.id, dataStart);
                    if (repeated && parameter.index >= repeated->firstParameter) {
                        const auto width = repeated->lastParameter - repeated->firstParameter + 1;
                        const auto ordinal = (parameter.index - repeated->firstParameter) / width;
                        if (converted.repeatedParameterGroups.size() <= ordinal) converted.repeatedParameterGroups.resize(ordinal + 1);
                        converted.repeatedParameterGroups[ordinal].parameters.push_back(std::move(canonical));
                    } else {
                        converted.fixedParameters.push_back(std::move(canonical));
                    }
                }
                if (repeated && countKnown && countValue != converted.repeatedParameterGroups.size()) {
                    addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::RepeatedCountMismatch,
                        "Encoded repetition count disagrees with the imported group count.", SctDocumentEntityId{converted.id});
                }
                script.instructions.push_back(std::move(converted));
                result.receipt.provenance.push_back({SctDocumentEntityId{idIt->second},
                    sourceSection.startOffset + instruction.offset, instruction.sizeBytes, static_cast<std::uint32_t>(sectionIndex)});
            }
            targetSection.content = std::move(script);
        } else {
            targetSection.content = SctOpaqueSectionContent{};
        }

        for (std::size_t pos = 0; pos < ledger.claims.size();) {
            if (ledger.claims[pos]) { ++pos; continue; }
            const auto begin = pos;
            while (pos < ledger.claims.size() && !ledger.claims[pos]) ++pos;
            const bool derivedStringPadding = std::holds_alternative<SctStringSectionContent>(targetSection.content)
                && std::all_of(bytes.begin() + sourceSection.startOffset + begin,
                    bytes.begin() + sourceSection.startOffset + pos,
                    [](std::uint8_t value) { return value == 0; });
            if (derivedStringPadding) {
                result.receipt.provenance.push_back({SctDocumentEntityId{targetSection.id},
                    sourceSection.startOffset + static_cast<std::uint32_t>(begin),
                    static_cast<std::uint32_t>(pos - begin), static_cast<std::uint32_t>(sectionIndex),
                    SctSourceCoverageKind::DerivedLayout});
                continue;
            }
            addAttachment(document, result, targetSection.id,
                sourceSection.startOffset + static_cast<std::uint32_t>(begin),
                std::vector<std::uint8_t>(bytes.begin() + sourceSection.startOffset + begin,
                    bytes.begin() + sourceSection.startOffset + pos),
                SctOpaqueReason::Gap);
        }
        document.sections.push_back(std::move(targetSection));
    }

    if (footer) {
        ClaimLedger ledger{std::vector<std::uint8_t>(footer->payloadEndOffset - footer->payloadStartOffset)};
        for (const auto entryIndex : footerEntryOrder) {
            const auto& entry = footer->entries[entryIndex];
            const auto idIt = footerIds.find(entry.payloadOffset);
            if (idIt == footerIds.end()) {
                addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::OverlappingSourceClaims,
                    "Footer entry evidence could not be promoted safely and remains opaque.");
                continue;
            }
            const auto id = idIt->second;
            const auto kind = entry.kind == SctFooterEntryKind::SctString
                ? SctTextKind::SctString
                : SctTextKind::PlainString;
            SctDocumentFooterEntry converted{id, kind, SctOpaqueText{entry.rawBytes}};
            std::string textReason = "No source text encoding was supplied.";
            bool semanticText = false;
            if (options.sourceTextEncoding.has_value()) {
                const auto decoded = decodeSctTextRecord(entry.rawBytes, kind,
                    SctTextStorage::Footer, *options.sourceTextEncoding);
                if (decoded.value) {
                    converted.value = *decoded.value;
                    semanticText = !std::holds_alternative<SctOpaqueText>(converted.value);
                    textReason.clear();
                } else textReason = decoded.error;
            }
            if (!semanticText) {
                addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::AmbiguousString,
                    "Footer text remained opaque: " + textReason, SctDocumentEntityId{id});
            }
            result.receipt.text.push_back({SctDocumentEntityId{id}, options.sourceTextEncoding,
                semanticText, textReason});
            const auto local = entry.payloadOffset - footer->payloadStartOffset;
            if (ledger.claim(local, entry.rawBytes.size())) {
                document.footerEntries.push_back(std::move(converted));
                result.receipt.provenance.push_back({SctDocumentEntityId{id}, dataStart + entry.payloadOffset,
                    static_cast<std::uint32_t>(entry.rawBytes.size()), std::nullopt});
            } else {
                addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::OverlappingSourceClaims,
                    "Footer entry evidence overlapped or exceeded the footer and remains opaque.", SctDocumentEntityId{id});
            }
        }
        const auto footerAbsolute = dataStart + footer->payloadStartOffset;
        for (std::size_t pos = 0; pos < ledger.claims.size();) {
            if (ledger.claims[pos]) { ++pos; continue; }
            const auto begin = pos;
            while (pos < ledger.claims.size() && !ledger.claims[pos]) ++pos;
            const bool derivedFooterPadding = std::all_of(
                bytes.begin() + footerAbsolute + begin, bytes.begin() + footerAbsolute + pos,
                [](std::uint8_t value) { return value == 0; });
            if (derivedFooterPadding) {
                result.receipt.provenance.push_back({{},
                    footerAbsolute + static_cast<std::uint32_t>(begin),
                    static_cast<std::uint32_t>(pos - begin), std::nullopt,
                    SctSourceCoverageKind::DerivedLayout});
                continue;
            }
            addAttachment(document, result, SctDocumentAnchor{}, footerAbsolute + static_cast<std::uint32_t>(begin),
                std::vector<std::uint8_t>(bytes.begin() + footerAbsolute + begin, bytes.begin() + footerAbsolute + pos),
                SctOpaqueReason::Gap);
        }
    }

    std::vector<std::uint8_t> totalCoverage(bytes.size(), 0);
    for (const auto& provenance : result.receipt.provenance) {
        const auto begin = static_cast<std::size_t>(provenance.decodedPayloadOffset);
        const auto size = static_cast<std::size_t>(provenance.byteSize);
        if (begin > totalCoverage.size() || size > totalCoverage.size() - begin) {
            addDiagnostic(result, SctDiagnosticSeverity::Error, SctDiagnosticCode::UnsafePhysicalStructure,
                "An imported source claim exceeds the decoded SCT payload.");
            return result;
        }
        for (std::size_t pos = begin; pos < begin + size; ++pos) {
            if (totalCoverage[pos] != 0) {
                addDiagnostic(result, SctDiagnosticSeverity::Error, SctDiagnosticCode::UnsafePhysicalStructure,
                    "Contradictory physical bounds prevent lossless decoded-payload coverage.");
                return result;
            }
            totalCoverage[pos] = 1;
        }
    }
    for (std::size_t pos = 0; pos < totalCoverage.size();) {
        if (totalCoverage[pos]) { ++pos; continue; }
        const auto begin = pos;
        while (pos < totalCoverage.size() && !totalCoverage[pos]) ++pos;
        addAttachment(document, result, SctDocumentAnchor{}, static_cast<std::uint32_t>(begin),
            std::vector<std::uint8_t>(bytes.begin() + begin, bytes.begin() + pos), SctOpaqueReason::Gap);
    }

    result.document = std::move(document);
    return result;
}

} // namespace spice::sct
