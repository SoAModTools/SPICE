#include "SctDocumentImporter.h"
#include "SctSha256.h"

#include "SctScptEncoding.h"
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
    SctDocumentDiagnostic diagnostic{severity, code, std::move(message)};
    if (entity) diagnostic.primaryLocation = SctDiagnosticLocation{*entity};
    result.diagnostics.push_back(std::move(diagnostic));
}

bool hasUnknownNode(const SctScptAstNode& node) {
    if (node.kind == SctScptAstNodeKind::Unknown) return true;
    return std::any_of(node.children.begin(), node.children.end(), hasUnknownNode);
}

SctCanonicalExpressionNodeKind canonicalKind(SctScptAstNodeKind kind) {
    switch (kind) {
    case SctScptAstNodeKind::NoLoopValue: return SctCanonicalExpressionNodeKind::NoLoopValue;
    case SctScptAstNodeKind::FloatLiteral: return SctCanonicalExpressionNodeKind::FloatLiteral;
    case SctScptAstNodeKind::DecimalLiteral: return SctCanonicalExpressionNodeKind::DecimalLiteral;
    case SctScptAstNodeKind::IntVariable: return SctCanonicalExpressionNodeKind::IntVariable;
    case SctScptAstNodeKind::NegatedIntVariable: return SctCanonicalExpressionNodeKind::NegatedIntVariable;
    case SctScptAstNodeKind::NegatedIntVariableLow16Comparison:
        return SctCanonicalExpressionNodeKind::NegatedIntVariableLow16Comparison;
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
    return SctCanonicalExpressionNodeKind::NoLoopValue;
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
        if (encodeSctCanonicalExpressionWords(converted) != rawWords) {
            converted.root = SctOpaqueExpression{rawWords};
            addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::AmbiguousExpression,
                "SCPT expression retained as opaque words because typed re-encoding was not byte-exact.", entity);
        }
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

bool sameCommandArgument(const SctMessageCommandArgument& left,
    const SctMessageCommandArgument& right) {
    if (left.index() != right.index()) return false;
    if (const auto* value = std::get_if<SctNoCommandArgument>(&left)) {
        return *value == std::get<SctNoCommandArgument>(right);
    }
    if (const auto* value = std::get_if<SctDecimalCommandArgument>(&left)) {
        return *value == std::get<SctDecimalCommandArgument>(right);
    }
    return std::get<SctByteListCommandArgument>(left)
        == std::get<SctByteListCommandArgument>(right);
}

bool sameFormattedText(const SctFormattedText& left, const SctFormattedText& right) {
    if (left.elements.size() != right.elements.size()) return false;
    for (std::size_t index = 0; index < left.elements.size(); ++index) {
        const auto& leftElement = left.elements[index];
        const auto& rightElement = right.elements[index];
        if (leftElement.index() != rightElement.index()) return false;
        if (const auto* chunk = std::get_if<SctTextChunk>(&leftElement)) {
            if (chunk->utf8 != std::get<SctTextChunk>(rightElement).utf8) return false;
            continue;
        }
        const auto& leftCommand = std::get<SctInlineCommand>(leftElement);
        const auto& rightCommand = std::get<SctInlineCommand>(rightElement);
        if (leftCommand.code != rightCommand.code
            || !sameCommandArgument(leftCommand.argument, rightCommand.argument)) return false;
    }
    return true;
}

bool sameSemanticText(const SctTextValue& left, const SctTextValue& right) {
    if (left.index() != right.index()) return false;
    if (const auto* value = std::get_if<SctPlainText>(&left)) {
        return value->utf8 == std::get<SctPlainText>(right).utf8;
    }
    if (const auto* value = std::get_if<SctMessage>(&left)) {
        const auto& other = std::get<SctMessage>(right);
        return value->headerUtf8 == other.headerUtf8
            && sameFormattedText(value->body, other.body);
    }
    if (const auto* value = std::get_if<SctOpaqueText>(&left)) {
        return value->bytes == std::get<SctOpaqueText>(right).bytes;
    }
    return true;
}

struct TextImportDecision {
    std::optional<SctTextValue> semanticValue;
    SctTextImportDisposition disposition = SctTextImportDisposition::OpaqueNoEncoding;
    std::vector<SctKnownTextConvention> viableAlternativeConventions;
    std::string reason = "No source text encoding was supplied.";
};

TextImportDecision decideTextImport(std::span<const std::uint8_t> bytes,
    SctTextKind kind, SctTextStorage storage,
    const SctDocumentImportOptions& options) {
    TextImportDecision decision;
    const auto alternatives = SctTextInspectionService::inspectKnownConventions(
        bytes, kind, storage);
    for (const auto& interpretation : alternatives.interpretations) {
        if (interpretation.complete && interpretation.knownConvention) {
            decision.viableAlternativeConventions.push_back(*interpretation.knownConvention);
        }
    }
    if (!options.sourceTextEncoding) return decision;

    const auto selected = decodeSctTextRecord(
        bytes, kind, storage, *options.sourceTextEncoding);
    if (!selected.value || std::holds_alternative<SctOpaqueText>(*selected.value)) {
        decision.disposition = SctTextImportDisposition::OpaqueDecodeFailed;
        decision.reason = selected.error.empty()
            ? "The selected source text encoding did not produce semantic text."
            : selected.error;
        return decision;
    }

    if (storage == SctTextStorage::Footer
        && options.footerTextPromotion == SctFooterTextPromotionPolicy::PreserveAmbiguous) {
        const bool conflicting = std::any_of(alternatives.interpretations.begin(),
            alternatives.interpretations.end(), [&](const SctTextInterpretation& interpretation) {
                return interpretation.complete && interpretation.semanticValue
                    && !sameSemanticText(*selected.value, *interpretation.semanticValue);
            });
        if (conflicting) {
            decision.disposition =
                SctTextImportDisposition::OpaqueConflictingInterpretations;
            decision.reason = "The footer record has multiple complete but semantically different text interpretations.";
            return decision;
        }
    }

    decision.semanticValue = *selected.value;
    decision.disposition = SctTextImportDisposition::Semantic;
    decision.reason.clear();
    return decision;
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

std::optional<SctControlFlowKind> controlFlowKind(SctEdgeType type) {
    switch (type) {
    case SctEdgeType::Fallthrough: return SctControlFlowKind::Fallthrough;
    case SctEdgeType::BranchTrue: return SctControlFlowKind::BranchTrue;
    case SctEdgeType::BranchFalse: return SctControlFlowKind::BranchFalse;
    case SctEdgeType::SwitchCase: return SctControlFlowKind::SwitchCase;
    case SctEdgeType::Jump: return SctControlFlowKind::Jump;
    case SctEdgeType::CallSubscript: return SctControlFlowKind::Call;
    case SctEdgeType::Return: return SctControlFlowKind::Return;
    case SctEdgeType::LoadsScript:
    case SctEdgeType::LoadsMld:
    case SctEdgeType::ReferencesString:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<SctParameterSite> controlFlowOrigin(const SctInstruction& instruction,
    SctControlFlowKind kind, std::uint32_t switchOrdinal, SctInstructionId id) {
    const auto* schema = findSctOpcodeSchema(instruction.opcode);
    if (schema == nullptr) return std::nullopt;
    switch (kind) {
    case SctControlFlowKind::BranchFalse:
    case SctControlFlowKind::Jump:
        if (schema->parameters.jumpParam >= 0) {
            return SctParameterSite{id, {static_cast<std::uint32_t>(schema->parameters.jumpParam), std::nullopt}};
        }
        break;
    case SctControlFlowKind::SwitchCase:
        if (schema->parameters.switchJumpParam >= 0) {
            return SctParameterSite{id, {static_cast<std::uint32_t>(schema->parameters.switchJumpParam), switchOrdinal}};
        }
        break;
    case SctControlFlowKind::Call:
        return SctParameterSite{id, {0u, std::nullopt}};
    default:
        break;
    }
    return std::nullopt;
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
    result.context.receipt.unresolvedReferences.push_back({entityId, address,
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

void addSourceRecord(std::vector<SctSourceSpanRecord>& records, std::uint32_t offset,
    std::uint32_t size, SctSourceSpanRole role, SctSourceCoverageKind coverage,
    std::optional<SctDocumentEntityId> entity = std::nullopt,
    std::optional<SctSectionId> section = std::nullopt,
    std::optional<std::uint32_t> sectionRelativeOffset = std::nullopt,
    SctSourceRegion region = SctSourceRegion::SectionPayload,
    SctSourceSpanLayer layer = SctSourceSpanLayer::Leaf,
    bool primary = false) {
    records.push_back({{offset, size}, role, layer, coverage, std::move(entity), section,
        sectionRelativeOffset, region, primary});
}

void addSourceSiteRecord(std::vector<SctSourceSpanRecord>& records, std::uint32_t offset,
    std::uint32_t size, SctSourceSpanRole role, SctSourceCoverageKind coverage,
    SctImportedSourceTarget target, std::optional<SctSectionId> section,
    std::optional<std::uint32_t> sectionRelativeOffset, SctSourceRegion region,
    SctSourceSpanLayer layer = SctSourceSpanLayer::Envelope) {
    records.push_back({{offset, size}, role, layer, coverage, std::move(target), section,
        sectionRelativeOffset, region, false});
}

std::uint32_t astWordCount(const SctScptAstNode& node) {
    std::uint32_t count = static_cast<std::uint32_t>(node.rawWords.size());
    for (const auto& child : node.children) count += astWordCount(child);
    return count;
}

std::uint32_t addExpressionChildEnvelopes(std::vector<SctSourceSpanRecord>& records,
    const SctScptAstNode& node, std::uint32_t absoluteWordOffset,
    const SctExpressionSite& rootSite, std::vector<std::uint32_t> path,
    std::optional<SctSectionId> section, std::uint32_t sectionStart) {
    auto cursor = absoluteWordOffset;
    for (std::uint32_t child = 0; child < node.children.size(); ++child) {
        auto childPath = path;
        childPath.push_back(child);
        cursor = addExpressionChildEnvelopes(records, node.children[child], cursor,
            rootSite, std::move(childPath), section, sectionStart);
    }
    const auto end = cursor + static_cast<std::uint32_t>(node.rawWords.size() * 4u);
    if (!path.empty()) {
        auto site = rootSite;
        site.childPath = std::move(path);
        addSourceSiteRecord(records, absoluteWordOffset, end - absoluteWordOffset,
            SctSourceSpanRole::Expression, SctSourceCoverageKind::SemanticEntity,
            SctImportedSourceTarget{std::move(site)}, section,
            absoluteWordOffset - sectionStart, SctSourceRegion::SectionPayload);
    }
    return end;
}

void addExpressionProvenance(std::vector<SctSourceSpanRecord>& records,
    const SctExpression& expression, std::span<const std::uint32_t> rawWords,
    std::uint32_t absoluteOffset, SctExpressionSite site,
    std::optional<SctSectionId> section, std::uint32_t sectionStart) {
    addSourceSiteRecord(records, absoluteOffset,
        static_cast<std::uint32_t>(rawWords.size() * 4u), SctSourceSpanRole::Expression,
        SctSourceCoverageKind::SemanticEntity, SctImportedSourceTarget{site}, section,
        absoluteOffset - sectionStart, SctSourceRegion::SectionPayload);
    if (!expression.ast || hasUnknownNode(*expression.ast)) return;
    const SctCanonicalExpression candidate{convertNode(*expression.ast),
        expression.hitStopCode ? SctExpressionTermination::StopCode
                               : SctExpressionTermination::InlineValue};
    const auto encoded = encodeSctCanonicalExpressionWords(candidate);
    if (encoded.size() != rawWords.size()
        || !std::equal(encoded.begin(), encoded.end(), rawWords.begin())) return;
    addExpressionChildEnvelopes(records, *expression.ast, absoluteOffset, site, {},
        section, sectionStart);
}

void addInstructionSourceRecords(std::vector<SctSourceSpanRecord>& records,
    const SctInstruction& instruction, const SctOpcodeSchema& schema,
    SctInstructionId id, SctSectionId section, std::uint32_t sectionStart) {
    const auto absoluteStart = sectionStart + instruction.offset;
    addSourceRecord(records, absoluteStart, instruction.sizeBytes,
        SctSourceSpanRole::Instruction, SctSourceCoverageKind::SemanticEntity,
        SctDocumentEntityId{id}, section, instruction.offset, SctSourceRegion::SectionPayload,
        SctSourceSpanLayer::Envelope, true);

    const auto prefixSize = instruction.opcodeWordIndex * 4u;
    if (prefixSize != 0u) {
        addSourceRecord(records, absoluteStart, prefixSize, SctSourceSpanRole::InstructionModifier,
            SctSourceCoverageKind::SemanticEntity, SctDocumentEntityId{id}, section,
            instruction.offset, SctSourceRegion::SectionPayload);
    }
    const auto opcodeOffset = absoluteStart + prefixSize;
    addSourceRecord(records, opcodeOffset, 4u, SctSourceSpanRole::InstructionOpcode,
        SctSourceCoverageKind::SemanticEntity, SctDocumentEntityId{id}, section,
        instruction.offset + prefixSize, SctSourceRegion::SectionPayload);

    std::uint32_t cursor = opcodeOffset + 4u;
    for (const auto& parameter : instruction.parameters) {
        const auto size = static_cast<std::uint32_t>(parameter.rawWords.size() * 4u);
        const auto address = parameterAddress(schema, parameter.index);
        const auto repeated = sctOpcodeRepeatedGroup(schema);
        const bool derivedCount = repeated && address.schemaIndex == repeated->iterationCountParameter
            && parameter.index < schema.parameters.paramCount;
        if (size != 0u) {
            if (derivedCount) {
                addSourceRecord(records, cursor, size, SctSourceSpanRole::InstructionParameter,
                    SctSourceCoverageKind::SemanticEntity, SctDocumentEntityId{id}, section,
                    cursor - sectionStart, SctSourceRegion::SectionPayload);
            } else {
                addSourceSiteRecord(records, cursor, size, SctSourceSpanRole::InstructionParameter,
                    SctSourceCoverageKind::SemanticEntity,
                    SctImportedSourceTarget{SctParameterSite{id, address}}, section,
                    cursor - sectionStart, SctSourceRegion::SectionPayload, SctSourceSpanLayer::Leaf);
                if (parameter.expression) {
                    addExpressionProvenance(records, *parameter.expression, parameter.rawWords,
                        cursor, SctExpressionSite{id, SctExpressionOwner{address}, {}},
                        section, sectionStart);
                }
            }
            cursor += size;
        }
    }
    const auto end = absoluteStart + instruction.sizeBytes;
    if (cursor < end) {
        addSourceRecord(records, cursor, end - cursor, SctSourceSpanRole::InstructionParameter,
            SctSourceCoverageKind::SemanticEntity, SctDocumentEntityId{id}, section,
            cursor - sectionStart, SctSourceRegion::SectionPayload);
    }

    if (instruction.scheduled.present && instruction.scheduled.frameDelay.expression) {
        const auto& delay = instruction.scheduled.frameDelay;
        const auto found = std::search(instruction.rawWords.begin(),
            instruction.rawWords.begin() + instruction.opcodeWordIndex,
            delay.rawWords.begin(), delay.rawWords.end());
        if (found != instruction.rawWords.begin() + instruction.opcodeWordIndex
            && std::search(found + 1, instruction.rawWords.begin() + instruction.opcodeWordIndex,
                delay.rawWords.begin(), delay.rawWords.end())
                == instruction.rawWords.begin() + instruction.opcodeWordIndex) {
            const auto word = static_cast<std::uint32_t>(found - instruction.rawWords.begin());
            addExpressionProvenance(records, *delay.expression, delay.rawWords,
                absoluteStart + word * 4u,
                SctExpressionSite{id, SctExpressionOwner{SctScheduledExpressionSite{}}, {}},
                section, sectionStart);
        }
    }
}

void addTextProvenance(std::vector<SctSourceSpanRecord>& records,
    std::span<const std::uint8_t> bytes, SctTextKind kind, SctTextStorage storage,
    SctTextEncoding encoding, SctTextEntityId id, std::uint32_t absoluteOffset,
    std::optional<SctSectionId> section, std::optional<std::uint32_t> sectionRelative,
    SctSourceRegion region) {
    const auto interpreted = SctTextInspectionService::interpret(bytes, kind, storage, encoding);
    if (!interpreted.complete) return;
    std::uint32_t bodyOrdinal = 0;
    std::uint32_t bodyElementUtf8Offset = 0;
    std::uint32_t headerUtf8Offset = 0;
    const auto* message = interpreted.semanticValue
        ? std::get_if<SctMessage>(&*interpreted.semanticValue) : nullptr;
    for (const auto& span : interpreted.spans) {
        if (span.kind == SctTextInspectionSpanKind::Terminator) {
            addSourceSiteRecord(records, absoluteOffset + span.source.offset, span.source.size,
                SctSourceSpanRole::TextTerminator, SctSourceCoverageKind::SemanticEntity,
                SctImportedSourceTarget{SctTextSite{id, SctTextRegion::Body, std::nullopt, {}}},
                section, sectionRelative ? std::optional<std::uint32_t>{*sectionRelative + span.source.offset}
                                         : std::nullopt, region);
            continue;
        }
        const bool header = span.kind == SctTextInspectionSpanKind::Header;
        const bool textLike = span.kind != SctTextInspectionSpanKind::Command
            || !span.utf8.empty();
        if (!header && message != nullptr) {
            while (bodyOrdinal < message->body.elements.size()) {
                const auto* chunk = std::get_if<SctTextChunk>(&message->body.elements[bodyOrdinal]);
                if (textLike && chunk != nullptr && bodyElementUtf8Offset < chunk->utf8.size()) break;
                if (!textLike && chunk == nullptr) break;
                ++bodyOrdinal;
                bodyElementUtf8Offset = 0;
            }
        }
        const auto utf8Size = static_cast<std::uint32_t>(span.utf8.size());
        const SctTextSite site{id, header ? SctTextRegion::Header : SctTextRegion::Body,
            header ? std::nullopt : std::optional<std::uint32_t>{bodyOrdinal},
            {header ? headerUtf8Offset : bodyElementUtf8Offset, utf8Size}};
        addSourceSiteRecord(records, absoluteOffset + span.source.offset, span.source.size,
            SctSourceSpanRole::TextElement, SctSourceCoverageKind::SemanticEntity,
            SctImportedSourceTarget{site}, section,
            sectionRelative ? std::optional<std::uint32_t>{*sectionRelative + span.source.offset}
                            : std::nullopt, region);
        if (header) headerUtf8Offset += utf8Size;
        else if (textLike) bodyElementUtf8Offset += utf8Size;
        else { ++bodyOrdinal; bodyElementUtf8Offset = 0; }
    }
}

void addSemanticEntityEnvelopeAndLeaf(std::vector<SctSourceSpanRecord>& records,
    std::uint32_t offset, std::uint32_t size, SctSourceSpanRole role,
    SctDocumentEntityId entity, std::optional<SctSectionId> section,
    std::optional<std::uint32_t> sectionRelativeOffset, SctSourceRegion region) {
    addSourceRecord(records, offset, size, role, SctSourceCoverageKind::SemanticEntity,
        entity, section, sectionRelativeOffset, region, SctSourceSpanLayer::Envelope, true);
    if (size != 0u) {
        addSourceRecord(records, offset, size, role, SctSourceCoverageKind::SemanticEntity,
            std::move(entity), section, sectionRelativeOffset, region, SctSourceSpanLayer::Leaf);
    }
}

SctOpaqueAttachmentId addAttachment(SctDocument& document,
    std::vector<SctSourceSpanRecord>& sourceRecords, SctOpaqueAnchor anchor,
    std::uint32_t absoluteOffset, std::vector<std::uint8_t> bytes, SctOpaqueReason reason,
    std::optional<SctSectionId> section = std::nullopt,
    std::optional<std::uint32_t> sectionRelativeOffset = std::nullopt,
    SctSourceRegion region = SctSourceRegion::SectionPayload) {
    if (bytes.empty()) return {};
    const auto id = document.allocateOpaqueAttachmentId();
    document.opaqueAttachments.push_back({id, std::move(bytes), std::move(anchor),
        SctOpaquePlacement::FixedOffset, absoluteOffset, 1, SctOpaqueRelocationSupport::FixedOnly, reason});
    addSourceRecord(sourceRecords, absoluteOffset,
        static_cast<std::uint32_t>(document.opaqueAttachments.back().bytes.size()),
        SctSourceSpanRole::OpaqueAttachment, SctSourceCoverageKind::OpaqueAttachment,
        SctDocumentEntityId{id}, section, sectionRelativeOffset, region,
        SctSourceSpanLayer::Leaf, true);
    return id;
}

} // namespace

SctDocumentImportResult SctDocumentImporter::import(
    const SctParseResult& parsed,
    const SctDocumentImportOptions& options) {
    SctDocumentImportResult result;
    std::vector<SctSourceSpanRecord> sourceRecords;
    result.context.receipt.source.byteOrder = parsed.file.detectedEndian == "big" ? SctSourceByteOrder::BigEndian
        : parsed.file.detectedEndian == "little" ? SctSourceByteOrder::LittleEndian : SctSourceByteOrder::Unknown;
    result.context.receipt.source.wrapper = parsed.file.originalCompressedAklz ? SctSourceWrapper::Aklz : SctSourceWrapper::None;
    result.context.receipt.declaredSourcePlatform = options.declaredSourcePlatform;
    result.context.receipt.sourceTextEncoding = options.sourceTextEncoding;
    result.context.receipt.footerTextPromotion = options.footerTextPromotion;
    if (!parsed.parseOk) {
        addDiagnostic(result, SctDiagnosticSeverity::Error, SctDiagnosticCode::ParseFailed,
            "A canonical document cannot be imported from a failed parse.");
        return result;
    }
    const auto& bytes = parsed.file.originalPayloadBytes;
    std::vector<std::uint8_t> lineageBytes(bytes.begin(), bytes.end());
    lineageBytes.push_back(0x53u);
    lineageBytes.push_back(static_cast<std::uint8_t>(result.context.receipt.source.wrapper));
    lineageBytes.push_back(options.declaredSourcePlatform
        ? static_cast<std::uint8_t>(*options.declaredSourcePlatform) : 0xffu);
    lineageBytes.push_back(options.sourceTextEncoding
        ? static_cast<std::uint8_t>(options.sourceTextEncoding->characters) : 0xffu);
    lineageBytes.push_back(options.sourceTextEncoding
        ? static_cast<std::uint8_t>(options.sourceTextEncoding->messageSpace) : 0xffu);
    lineageBytes.push_back(static_cast<std::uint8_t>(options.footerTextPromotion));
    result.context.receipt.lineage.sha256 = detail::sha256(lineageBytes);
    result.context.revisionProvenance.importLineage = result.context.receipt.lineage;
    const std::uint64_t dataStart64 = 12ull + (20ull * parsed.file.sections.size());
    if (bytes.size() < dataStart64) {
        addDiagnostic(result, SctDiagnosticSeverity::Error, SctDiagnosticCode::UnsafePhysicalStructure,
            "Decoded SCT payload is shorter than its physical index table.");
        return result;
    }
    const auto dataStart = static_cast<std::uint32_t>(dataStart64);
    if (bytes.size() >= 8u) {
        auto& header = result.context.receipt.source.header;
        std::copy_n(bytes.begin(), 8u, header.rawBytes.begin());
        if (result.context.receipt.source.byteOrder != SctSourceByteOrder::Unknown) {
            header.values = {
                readHeaderU16(bytes, 0u, result.context.receipt.source.byteOrder),
                readHeaderU16(bytes, 2u, result.context.receipt.source.byteOrder),
                readHeaderU16(bytes, 4u, result.context.receipt.source.byteOrder),
                readHeaderU16(bytes, 6u, result.context.receipt.source.byteOrder),
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
    for (const auto& section : parsed.file.sections) {
        std::unordered_map<std::uint32_t, std::uint32_t> switchOrdinals;
        for (const auto& edge : section.edges) {
            if (!edge.fromPayloadOffset) continue;
            const auto kind = controlFlowKind(edge.type);
            const auto sourceId = instructionIds.find(*edge.fromPayloadOffset);
            if (!kind || sourceId == instructionIds.end()) continue;
            const auto sourceInstruction = std::find_if(section.instructions.begin(),
                section.instructions.end(), [&](const auto& instruction) {
                    return instruction.payloadOffset == *edge.fromPayloadOffset;
                });
            if (sourceInstruction == section.instructions.end()) continue;
            const auto switchOrdinal = *kind == SctControlFlowKind::SwitchCase
                ? switchOrdinals[*edge.fromPayloadOffset]++ : 0u;
            std::optional<SctInstructionId> targetId;
            if (edge.toPayloadOffset) {
                if (const auto found = instructionIds.find(*edge.toPayloadOffset);
                    found != instructionIds.end()) targetId = found->second;
            }
            result.context.receipt.controlFlow.push_back({sourceId->second, *kind, edge.confidence,
                controlFlowOrigin(*sourceInstruction, *kind, switchOrdinal, sourceId->second),
                targetId, edge.toPayloadOffset
                    ? std::optional<std::uint32_t>{dataStart + *edge.toPayloadOffset}
                    : std::nullopt});
        }
        const auto order = physicalInstructionOrder(section);
        for (std::size_t orderIndex = 0; orderIndex + 1u < order.size(); ++orderIndex) {
            const auto& source = section.instructions[order[orderIndex]];
            const auto& target = section.instructions[order[orderIndex + 1u]];
            const auto sourceId = instructionIds.find(source.payloadOffset);
            const auto targetId = instructionIds.find(target.payloadOffset);
            const auto* schema = findSctOpcodeSchema(source.opcode);
            if (sourceId == instructionIds.end() || schema == nullptr) continue;
            if (schema->semantic.controlRole != SctOpcodeControlRole::None
                && schema->semantic.controlRole != SctOpcodeControlRole::CallSubscript) continue;
            const bool alreadyObserved = std::any_of(result.context.receipt.controlFlow.begin(),
                result.context.receipt.controlFlow.end(), [&](const auto& observation) {
                    return observation.sourceInstruction == sourceId->second
                        && observation.kind == SctControlFlowKind::Fallthrough;
                });
            if (alreadyObserved) continue;
            result.context.receipt.controlFlow.push_back({sourceId->second,
                SctControlFlowKind::Fallthrough, SctSemanticConfidence::Known, std::nullopt,
                targetId == instructionIds.end() ? std::nullopt
                    : std::optional<SctInstructionId>{targetId->second},
                dataStart + target.payloadOffset});
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

    addSourceRecord(sourceRecords, 0u, 8u, SctSourceSpanRole::Header,
        SctSourceCoverageKind::SourceObservation, std::nullopt, std::nullopt,
        std::nullopt, SctSourceRegion::Header);
    addSourceRecord(sourceRecords, 8u, 4u, SctSourceSpanRole::SectionCount,
        SctSourceCoverageKind::SemanticEntity, std::nullopt, std::nullopt,
        std::nullopt, SctSourceRegion::SectionIndex);
    std::vector<std::string> sectionNames;
    sectionNames.reserve(parsed.file.sections.size());
    for (std::size_t sectionIndex = 0; sectionIndex < parsed.file.sections.size(); ++sectionIndex) {
        const auto rowOffset = static_cast<std::uint32_t>(12 + sectionIndex * 20);
        addSourceRecord(sourceRecords, rowOffset, 20u, SctSourceSpanRole::SectionIndexRow,
            SctSourceCoverageKind::SemanticEntity, SctDocumentEntityId{sectionIds[sectionIndex]},
            sectionIds[sectionIndex], std::nullopt, SctSourceRegion::SectionIndex,
            SctSourceSpanLayer::Envelope);
        addSourceRecord(sourceRecords, rowOffset, 4u, SctSourceSpanRole::SectionOffsetField,
            SctSourceCoverageKind::SemanticEntity, SctDocumentEntityId{sectionIds[sectionIndex]},
            sectionIds[sectionIndex], std::nullopt, SctSourceRegion::SectionIndex);
        const auto nameBegin = bytes.begin() + rowOffset + 4;
        const auto zero = std::find(nameBegin, nameBegin + 16, 0);
        sectionNames.emplace_back(nameBegin, zero);
        if (!sectionNames.back().empty()) {
            addSourceRecord(sourceRecords, rowOffset + 4u,
                static_cast<std::uint32_t>(sectionNames.back().size()), SctSourceSpanRole::SectionName,
                SctSourceCoverageKind::SemanticEntity, SctDocumentEntityId{sectionIds[sectionIndex]},
                sectionIds[sectionIndex], std::nullopt, SctSourceRegion::SectionIndex);
        }
        const auto fieldEnd = nameBegin + 16;
        const auto unusual = std::find_if(zero, fieldEnd, [](std::uint8_t value) { return value != 0; });
        if (unusual == fieldEnd) {
            if (zero != fieldEnd) {
                addSourceRecord(sourceRecords,
                    rowOffset + 4u + static_cast<std::uint32_t>(sectionNames.back().size()),
                    static_cast<std::uint32_t>(fieldEnd - zero), SctSourceSpanRole::SectionNamePadding,
                    SctSourceCoverageKind::DerivedLayout, SctDocumentEntityId{sectionIds[sectionIndex]},
                    sectionIds[sectionIndex], std::nullopt, SctSourceRegion::SectionIndex);
            }
        } else {
            if (unusual != zero) {
                addSourceRecord(sourceRecords,
                    rowOffset + 4u + static_cast<std::uint32_t>(sectionNames.back().size()),
                    static_cast<std::uint32_t>(unusual - zero), SctSourceSpanRole::SectionNamePadding,
                    SctSourceCoverageKind::DerivedLayout, SctDocumentEntityId{sectionIds[sectionIndex]},
                    sectionIds[sectionIndex], std::nullopt, SctSourceRegion::SectionIndex);
            }
            addAttachment(document, sourceRecords, sectionIds[sectionIndex],
                rowOffset + 4 + static_cast<std::uint32_t>(unusual - nameBegin),
                std::vector<std::uint8_t>(unusual, fieldEnd), SctOpaqueReason::Padding,
                sectionIds[sectionIndex], std::nullopt, SctSourceRegion::SectionIndex);
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
        addSourceRecord(sourceRecords, sourceSection.startOffset,
            sourceSection.endOffset - sourceSection.startOffset, SctSourceSpanRole::SectionPayload,
            SctSourceCoverageKind::SemanticEntity, SctDocumentEntityId{targetSection.id},
            targetSection.id, 0u, SctSourceRegion::SectionPayload,
            SctSourceSpanLayer::Envelope, true);
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
            auto textDecision = decideTextImport(recordBytes, SctTextKind::SctString,
                SctTextStorage::IndexedSection, options);
            const bool semanticText = textDecision.semanticValue.has_value();
            if (textDecision.semanticValue) string.value = *textDecision.semanticValue;
            if (!semanticText) {
                addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::AmbiguousString,
                    "Indexed SCT string remained opaque: " + textDecision.reason,
                    SctDocumentEntityId{stringId});
            }
            result.context.receipt.text.push_back({SctDocumentEntityId{stringId}, options.sourceTextEncoding,
                textDecision.disposition, std::move(textDecision.viableAlternativeConventions),
                std::move(textDecision.reason)});
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
                addSourceRecord(sourceRecords, sourceSection.startOffset,
                    static_cast<std::uint32_t>(preambleSize), SctSourceSpanRole::IndexedStringPreamble,
                    SctSourceCoverageKind::SemanticEntity, SctDocumentEntityId{targetSection.id},
                    targetSection.id, 0u, SctSourceRegion::SectionPayload);
                addSemanticEntityEnvelopeAndLeaf(sourceRecords,
                    sourceSection.startOffset + entry.textStartOffset,
                    static_cast<std::uint32_t>(recordSize), SctSourceSpanRole::IndexedStringRecord,
                    SctDocumentEntityId{stringId}, targetSection.id, entry.textStartOffset,
                    SctSourceRegion::SectionPayload);
                if (semanticText && options.sourceTextEncoding) {
                    addTextProvenance(sourceRecords, recordBytes, SctTextKind::SctString,
                        SctTextStorage::IndexedSection, *options.sourceTextEncoding,
                        SctTextEntityId{stringId}, sourceSection.startOffset + entry.textStartOffset,
                        targetSection.id, entry.textStartOffset, SctSourceRegion::SectionPayload);
                }
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
                addInstructionSourceRecords(sourceRecords, instruction, *schema, idIt->second,
                    targetSection.id, sourceSection.startOffset);
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
                addSourceRecord(sourceRecords,
                    sourceSection.startOffset + static_cast<std::uint32_t>(begin),
                    static_cast<std::uint32_t>(pos - begin), SctSourceSpanRole::DerivedPadding,
                    SctSourceCoverageKind::DerivedLayout, SctDocumentEntityId{targetSection.id},
                    targetSection.id, static_cast<std::uint32_t>(begin), SctSourceRegion::SectionPayload);
                continue;
            }
            addAttachment(document, sourceRecords, targetSection.id,
                sourceSection.startOffset + static_cast<std::uint32_t>(begin),
                std::vector<std::uint8_t>(bytes.begin() + sourceSection.startOffset + begin,
                    bytes.begin() + sourceSection.startOffset + pos),
                SctOpaqueReason::Gap, targetSection.id, static_cast<std::uint32_t>(begin),
                SctSourceRegion::SectionPayload);
        }
        document.sections.push_back(std::move(targetSection));
    }

    if (footer) {
        ClaimLedger ledger{std::vector<std::uint8_t>(footer->payloadEndOffset - footer->payloadStartOffset)};
        const auto footerAbsolute = dataStart + footer->payloadStartOffset;
        addSourceRecord(sourceRecords, footerAbsolute,
            footer->payloadEndOffset - footer->payloadStartOffset, SctSourceSpanRole::FooterRegion,
            SctSourceCoverageKind::SemanticEntity, std::nullopt, std::nullopt, std::nullopt,
            SctSourceRegion::Footer, SctSourceSpanLayer::Envelope);
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
            auto textDecision = decideTextImport(
                entry.rawBytes, kind, SctTextStorage::Footer, options);
            const bool semanticText = textDecision.semanticValue.has_value();
            if (textDecision.semanticValue) converted.value = *textDecision.semanticValue;
            if (!semanticText) {
                addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::AmbiguousString,
                    "Footer text remained opaque: " + textDecision.reason,
                    SctDocumentEntityId{id});
            }
            result.context.receipt.text.push_back({SctDocumentEntityId{id}, options.sourceTextEncoding,
                textDecision.disposition, std::move(textDecision.viableAlternativeConventions),
                std::move(textDecision.reason)});
            const auto local = entry.payloadOffset - footer->payloadStartOffset;
            if (ledger.claim(local, entry.rawBytes.size())) {
                document.footerEntries.push_back(std::move(converted));
                addSemanticEntityEnvelopeAndLeaf(sourceRecords, dataStart + entry.payloadOffset,
                    static_cast<std::uint32_t>(entry.rawBytes.size()), SctSourceSpanRole::FooterEntry,
                    SctDocumentEntityId{id}, std::nullopt, std::nullopt, SctSourceRegion::Footer);
                if (semanticText && options.sourceTextEncoding) {
                    addTextProvenance(sourceRecords, entry.rawBytes, kind, SctTextStorage::Footer,
                        *options.sourceTextEncoding, SctTextEntityId{id}, dataStart + entry.payloadOffset,
                        std::nullopt, std::nullopt,
                        SctSourceRegion::Footer);
                }
            } else {
                addDiagnostic(result, SctDiagnosticSeverity::Warning, SctDiagnosticCode::OverlappingSourceClaims,
                    "Footer entry evidence overlapped or exceeded the footer and remains opaque.", SctDocumentEntityId{id});
            }
        }
        for (std::size_t pos = 0; pos < ledger.claims.size();) {
            if (ledger.claims[pos]) { ++pos; continue; }
            const auto begin = pos;
            while (pos < ledger.claims.size() && !ledger.claims[pos]) ++pos;
            const bool derivedFooterPadding = std::all_of(
                bytes.begin() + footerAbsolute + begin, bytes.begin() + footerAbsolute + pos,
                [](std::uint8_t value) { return value == 0; });
            if (derivedFooterPadding) {
                addSourceRecord(sourceRecords, footerAbsolute + static_cast<std::uint32_t>(begin),
                    static_cast<std::uint32_t>(pos - begin), SctSourceSpanRole::DerivedPadding,
                    SctSourceCoverageKind::DerivedLayout, std::nullopt, std::nullopt,
                    std::nullopt, SctSourceRegion::Footer);
                continue;
            }
            addAttachment(document, sourceRecords, SctDocumentAnchor{},
                footerAbsolute + static_cast<std::uint32_t>(begin),
                std::vector<std::uint8_t>(bytes.begin() + footerAbsolute + begin, bytes.begin() + footerAbsolute + pos),
                SctOpaqueReason::Gap, std::nullopt, std::nullopt,
                SctSourceRegion::Footer);
        }
    }

    std::vector<std::uint8_t> totalCoverage(bytes.size(), 0);
    for (const auto& record : sourceRecords) {
        if (record.layer != SctSourceSpanLayer::Leaf) continue;
        const auto begin = static_cast<std::size_t>(record.span.offset);
        const auto size = static_cast<std::size_t>(record.span.size);
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
        SctSourceRegion region = SctSourceRegion::SectionPayload;
        if (begin < 8u) region = SctSourceRegion::Header;
        else if (begin < dataStart) region = SctSourceRegion::SectionIndex;
        else if (footer && begin >= dataStart + footer->payloadStartOffset) region = SctSourceRegion::Footer;
        addAttachment(document, sourceRecords, SctDocumentAnchor{}, static_cast<std::uint32_t>(begin),
            std::vector<std::uint8_t>(bytes.begin() + begin, bytes.begin() + pos),
            SctOpaqueReason::Gap, std::nullopt, std::nullopt, region);
    }

    auto sourceMap = SctImportedSourceMap::build(
        static_cast<std::uint32_t>(bytes.size()), std::move(sourceRecords));
    if (!sourceMap.map) {
        for (const auto& issue : sourceMap.issues) {
            addDiagnostic(result, SctDiagnosticSeverity::Error,
                SctDiagnosticCode::UnsafePhysicalStructure, issue.message);
        }
        return result;
    }
    result.context.receipt.sourceMap = std::move(*sourceMap.map);
    result.document = std::move(document);
    return result;
}

} // namespace spice::sct
