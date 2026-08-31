#include "SctDocumentValidator.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace spice::sct {
namespace {

void error(SctValidationResult& result, SctDiagnosticCode code, std::string message,
    std::optional<SctDocumentEntityId> entity = std::nullopt,
    std::optional<SctParameterAddress> parameter = std::nullopt,
    std::vector<std::uint32_t> expressionPath = {}) {
    SctDocumentDiagnostic diagnostic{SctDiagnosticSeverity::Error, code, std::move(entity), std::move(message)};
    diagnostic.parameter = parameter;
    diagnostic.expressionChildPath = std::move(expressionPath);
    result.diagnostics.push_back(std::move(diagnostic));
}

template <typename Id>
bool recordId(SctValidationResult& result, Id id, std::unordered_set<std::uint64_t>& seen,
    std::uint64_t nextValue, const char* label) {
    if (!id) {
        error(result, SctDiagnosticCode::InvalidId, std::string(label) + " ID is zero.", SctDocumentEntityId{id});
        return false;
    }
    if (!seen.insert(id.value()).second) {
        error(result, SctDiagnosticCode::DuplicateId, std::string(label) + " ID is duplicated.", SctDocumentEntityId{id});
        return false;
    }
    if (id.value() >= nextValue) {
        error(result, SctDiagnosticCode::AllocatorDiscontinuity,
            std::string(label) + " ID is outside the document allocator state.", SctDocumentEntityId{id});
        return false;
    }
    return true;
}

bool validExpressionNode(const SctCanonicalExpressionNode& node, SctValidationResult& result,
    SctDocumentEntityId entity, std::optional<SctParameterAddress> parameter,
    const std::vector<std::uint32_t>& path) {
    bool valid = true;
    const bool binary = node.kind == SctCanonicalExpressionNodeKind::CompareOperator
        || node.kind == SctCanonicalExpressionNodeKind::ArithmeticOperator
        || node.kind == SctCanonicalExpressionNodeKind::AssignmentOperator;
    if (binary && node.children.size() != 2) {
        error(result, SctDiagnosticCode::ExpressionInvalid,
            "A typed SCPT operator must have exactly two children.", entity, parameter, path);
        valid = false;
    }
    if (!binary && !node.children.empty()) {
        error(result, SctDiagnosticCode::ExpressionInvalid, "A typed SCPT leaf cannot have children.", entity, parameter, path);
        valid = false;
    }
    if (node.kind == SctCanonicalExpressionNodeKind::FloatLiteral && node.payloadWords.size() != 1) {
        error(result, SctDiagnosticCode::ExpressionInvalid,
            "A float literal must preserve exactly one payload word.", entity, parameter, path);
        valid = false;
    }
    if (node.kind != SctCanonicalExpressionNodeKind::FloatLiteral && !node.payloadWords.empty()) {
        error(result, SctDiagnosticCode::ExpressionInvalid,
            "Only a typed SCPT float literal may contain a payload word.", entity, parameter, path);
        valid = false;
    }
    const auto prefix = node.encodingCode & 0xff000000u;
    const auto compare = node.encodingCode <= 0x0au || node.encodingCode == 0x10u || node.encodingCode == 0x11u;
    const auto arithmetic = (node.encodingCode >= 0x0bu && node.encodingCode <= 0x0fu)
        || (node.encodingCode >= 0x12u && node.encodingCode <= 0x16u);
    bool encodingMatches = true;
    switch (node.kind) {
    case SctCanonicalExpressionNodeKind::NoLoopValue:
        encodingMatches = node.encodingCode == 0x7f7fffffu || node.encodingCode == 0x00800000u
            || node.encodingCode == 0x7fffffffu;
        break;
    case SctCanonicalExpressionNodeKind::FloatLiteral: encodingMatches = prefix == 0x04000000u; break;
    case SctCanonicalExpressionNodeKind::DecimalLiteral: encodingMatches = prefix == 0x08000000u; break;
    case SctCanonicalExpressionNodeKind::IntVariable:
    case SctCanonicalExpressionNodeKind::SecondaryValue: encodingMatches = prefix == 0x50000000u; break;
    case SctCanonicalExpressionNodeKind::FloatVariable: encodingMatches = prefix == 0x40000000u; break;
    case SctCanonicalExpressionNodeKind::BitVariable: encodingMatches = prefix == 0x20000000u; break;
    case SctCanonicalExpressionNodeKind::ByteVariable: encodingMatches = prefix == 0x10000000u; break;
    case SctCanonicalExpressionNodeKind::CompareOperator: encodingMatches = compare && node.encodingCode != 0x0au; break;
    case SctCanonicalExpressionNodeKind::AssignmentOperator: encodingMatches = node.encodingCode == 0x0au; break;
    case SctCanonicalExpressionNodeKind::ArithmeticOperator: encodingMatches = arithmetic; break;
    case SctCanonicalExpressionNodeKind::Stop: encodingMatches = node.encodingCode == 0x1du; break;
    case SctCanonicalExpressionNodeKind::RawValue: break;
    }
    if (!encodingMatches) {
        error(result, SctDiagnosticCode::ExpressionInvalid,
            "Typed SCPT node kind does not match its exact encoding code.", entity, parameter, path);
        valid = false;
    }
    for (std::size_t childIndex = 0; childIndex < node.children.size(); ++childIndex) {
        auto childPath = path;
        childPath.push_back(static_cast<std::uint32_t>(childIndex));
        validExpressionNode(node.children[childIndex], result, entity, parameter, childPath);
    }
    return valid;
}

void validateExpression(const SctCanonicalExpression& expression, SctValidationResult& result,
    SctDocumentEntityId entity, std::optional<SctParameterAddress> parameter = std::nullopt) {
    if (const auto* opaque = std::get_if<SctOpaqueExpression>(&expression.root)) {
        if (opaque->words.empty()) {
            error(result, SctDiagnosticCode::ExpressionInvalid,
                "An opaque SCPT fallback must contain the preserved encoded words.", entity, parameter);
        }
    } else {
        const auto& root = std::get<SctCanonicalExpressionNode>(expression.root);
        validExpressionNode(root, result, entity, parameter, {});
        const bool inlineValue = root.kind == SctCanonicalExpressionNodeKind::NoLoopValue;
        const bool explicitStop = root.kind == SctCanonicalExpressionNodeKind::Stop;
        if ((inlineValue && expression.termination != SctExpressionTermination::InlineValue)
            || (!inlineValue && expression.termination != SctExpressionTermination::StopCode)
            || (explicitStop && expression.termination != SctExpressionTermination::StopCode)) {
            error(result, SctDiagnosticCode::ExpressionInvalid,
                "Typed SCPT expression termination is incompatible with its root encoding.", entity, parameter);
        }
    }
}

bool powerOfTwo(std::uint32_t value) { return value && (value & (value - 1)) == 0; }

void validateText(const SctTextValue& value, SctValidationResult& result, SctDocumentEntityId entity) {
    if (const auto* text = std::get_if<SctEditableText>(&value)) {
        if (text->bytes.find('\0') != std::string::npos) {
            error(result, SctDiagnosticCode::InvalidContent,
                "Editable single-byte text cannot contain an embedded terminator.", entity);
        }
    } else if (std::get<SctOpaqueText>(value).bytes.empty()) {
        error(result, SctDiagnosticCode::InvalidContent,
            "Opaque text must preserve at least one source byte.", entity);
    }
}

} // namespace

SctValidationResult SctDocumentValidator::validate(
    const SctDocument& document,
    const SctDocumentValidationOptions& options,
    const SctDocumentImportReceipt* receipt) {
    SctValidationResult result;
    std::unordered_set<std::uint64_t> sectionIds, instructionIds, stringIds, footerIds, attachmentIds;
    std::unordered_map<std::uint64_t, std::size_t> stringSectionUses;
    for (const auto& string : document.strings) {
        recordId(result, string.id, stringIds, document.nextStringIdValue(), "String");
        validateText(string.value, result, SctDocumentEntityId{string.id});
    }
    for (const auto& footer : document.footerEntries) {
        recordId(result, footer.id, footerIds, document.nextFooterEntryIdValue(), "Footer entry");
        validateText(footer.value, result, SctDocumentEntityId{footer.id});
    }

    for (const auto& section : document.sections) {
        recordId(result, section.id, sectionIds, document.nextSectionIdValue(), "Section");
        if (section.nameBytes.size() > 16 || section.nameBytes.find('\0') != std::string::npos) {
            error(result, SctDiagnosticCode::InvalidName,
                "Section names must be zero-free byte strings no longer than 16 bytes.", SctDocumentEntityId{section.id});
        }
        if (const auto* strings = std::get_if<SctStringSectionContent>(&section.content)) {
            if (!strings->stringId || !stringIds.contains(strings->stringId.value())) {
                error(result, SctDiagnosticCode::UnresolvedReference,
                    "String section references a missing string entity.", SctDocumentEntityId{section.id});
            }
            ++stringSectionUses[strings->stringId.value()];
        }
        if (const auto* script = std::get_if<SctScriptSectionContent>(&section.content)) {
            for (const auto& instruction : script->instructions) recordId(result, instruction.id, instructionIds,
                document.nextInstructionIdValue(), "Instruction");
        }
    }
    for (const auto& string : document.strings) {
        if (stringSectionUses[string.id.value()] != 1u) {
            error(result, SctDiagnosticCode::InvalidContent,
                "Each document string must belong to exactly one physical string section.",
                SctDocumentEntityId{string.id});
        }
    }
    for (const auto& attachment : document.opaqueAttachments) recordId(result, attachment.id, attachmentIds,
        document.nextOpaqueAttachmentIdValue(), "Opaque attachment");

    for (const auto& section : document.sections) {
        const auto* script = std::get_if<SctScriptSectionContent>(&section.content);
        if (!script) continue;
        for (const auto& instruction : script->instructions) {
            const auto entity = SctDocumentEntityId{instruction.id};
            const auto* schema = findSctOpcodeSchema(instruction.opcode);
            if (!schema) {
                error(result, SctDiagnosticCode::ParameterMismatch, "Instruction opcode has no schema row.", entity);
                continue;
            }
            if (sctOpcodeAvailability(*schema, options.targetPlatform) != SctOpcodeAvailability::Available) {
                error(result, SctDiagnosticCode::OpcodeUnavailable,
                    "Instruction opcode is unavailable on the requested target platform.", entity);
            }
            if (schema->documentRole == SctOpcodeDocumentRole::FoldedModifier) {
                error(result, SctDiagnosticCode::InvalidContent,
                    "Folded modifier opcode cannot be stored as a canonical instruction entity.", entity);
            }
            if (instruction.scheduledExpression) validateExpression(*instruction.scheduledExpression, result, entity);
            const auto repeated = sctOpcodeRepeatedGroup(*schema);
            std::unordered_set<std::uint32_t> fixedIndexes;
            for (const auto& parameter : instruction.fixedParameters) {
                if (!fixedIndexes.insert(parameter.schemaIndex).second
                    || parameter.schemaIndex >= schema->parameters.paramCount
                    || (repeated && (parameter.schemaIndex == repeated->iterationCountParameter
                        || (parameter.schemaIndex >= repeated->firstParameter && parameter.schemaIndex <= repeated->lastParameter)))) {
                    error(result, SctDiagnosticCode::ParameterMismatch,
                        "Fixed parameter classification disagrees with the opcode schema.", entity);
                }
            }
            std::size_t expectedFixed = 0;
            for (std::uint32_t index = 0; index < schema->parameters.paramCount; ++index) {
                const bool derivedCount = repeated && index == repeated->iterationCountParameter;
                const bool repeatedMember = repeated && index >= repeated->firstParameter && index <= repeated->lastParameter;
                if (!derivedCount && !repeatedMember) ++expectedFixed;
            }
            if (instruction.fixedParameters.size() != expectedFixed) {
                error(result, SctDiagnosticCode::ParameterMismatch,
                    "Instruction has the wrong number of fixed parameters for its schema.", entity);
            }
            if (!repeated && !instruction.repeatedParameterGroups.empty()) {
                error(result, SctDiagnosticCode::ParameterMismatch,
                    "Instruction has repeated groups but its schema has none.", entity);
            }
            if (repeated) {
                const auto width = repeated->lastParameter - repeated->firstParameter + 1;
                for (const auto& group : instruction.repeatedParameterGroups) {
                    if (group.parameters.size() != width) {
                        error(result, SctDiagnosticCode::ParameterMismatch,
                            "Repeated parameter group has the wrong width.", entity);
                        continue;
                    }
                    for (std::size_t i = 0; i < group.parameters.size(); ++i) {
                        if (group.parameters[i].schemaIndex != repeated->firstParameter + i) {
                            error(result, SctDiagnosticCode::ParameterMismatch,
                                "Repeated parameter group indexes disagree with the schema.", entity);
                        }
                    }
                }
            }
            const auto validateValue = [&](const SctDocumentParameter& parameter,
                std::optional<std::uint32_t> repeatedGroupOrdinal) {
                const SctParameterAddress address{parameter.schemaIndex, repeatedGroupOrdinal};
                const auto expectedEncoding = sctOpcodeParameterEncoding(*schema, parameter.schemaIndex);
                const auto* parameterSchema = sctOpcodeParameterSchema(*schema, parameter.schemaIndex);
                if (const auto* expression = std::get_if<SctCanonicalExpression>(&parameter.value)) {
                    if (expectedEncoding != SctOpcodeParameterEncoding::ScptExpression) {
                        error(result, SctDiagnosticCode::ParameterMismatch,
                            "Typed SCPT expression is assigned to a raw-word schema parameter.", entity, address);
                    }
                    validateExpression(*expression, result, entity, address);
                } else if (const auto* scalar = std::get_if<SctEncodedWordValue>(&parameter.value)) {
                    if (expectedEncoding != SctOpcodeParameterEncoding::RawWord) {
                        error(result, SctDiagnosticCode::ParameterMismatch,
                            "Encoded scalar word is assigned to an SCPT-expression schema parameter.", entity, address);
                    }
                    if (parameterSchema != nullptr && parameterSchema->referenceKind != SctOpcodeReferenceKind::None) {
                        error(result, SctDiagnosticCode::ParameterMismatch,
                            "Encoded scalar word is assigned where the schema requires a typed reference.", entity, address);
                    }
                    if (parameterSchema != nullptr
                        && ((scalar->value & ~parameterSchema->allowedBitMask) != 0u
                            || (scalar->value & parameterSchema->requiredBitValue)
                                != parameterSchema->requiredBitValue)) {
                        error(result, SctDiagnosticCode::ParameterMismatch,
                            "Encoded scalar word violates the parameter bit contract.", entity, address);
                    }
                } else if (const auto* reference = std::get_if<SctInstructionReference>(&parameter.value)) {
                    if (!reference->target || !instructionIds.contains(reference->target.value())) {
                        error(result, SctDiagnosticCode::UnresolvedReference,
                            "Instruction parameter references a missing instruction.", entity, address);
                    }
                    const bool isControlTarget = static_cast<int>(parameter.schemaIndex) == schema->parameters.jumpParam
                        || static_cast<int>(parameter.schemaIndex) == schema->parameters.switchJumpParam
                        || (schema->semantic.controlRole == SctOpcodeControlRole::CallSubscript && parameter.schemaIndex == 0);
                    if (!isControlTarget) {
                        error(result, SctDiagnosticCode::ParameterMismatch,
                            "Instruction reference is assigned outside the opcode's control-target parameter.", entity, address);
                    }
                } else if (const auto* reference = std::get_if<SctFooterEntryReference>(&parameter.value)) {
                    if (!reference->target || !footerIds.contains(reference->target.value())) {
                        error(result, SctDiagnosticCode::UnresolvedReference,
                            "Instruction parameter references a missing footer entry.", entity, address);
                    }
                    const auto rule = sctOpcodeFooterReference(*schema, parameter.schemaIndex);
                    if (rule.kind == SctFooterParamKind::None) {
                        error(result, SctDiagnosticCode::ParameterMismatch,
                            "Footer reference is assigned to a parameter without a footer-reference rule.", entity, address);
                    } else {
                        const auto found = std::find_if(document.footerEntries.begin(), document.footerEntries.end(),
                            [&](const auto& entry) { return entry.id == reference->target; });
                        if (found != document.footerEntries.end()
                            && ((rule.kind == SctFooterParamKind::String && found->kind != SctDocumentFooterEntryKind::String)
                                || (rule.kind == SctFooterParamKind::SctString && found->kind != SctDocumentFooterEntryKind::SctString))) {
                            error(result, SctDiagnosticCode::ParameterMismatch,
                                "Footer reference kind is incompatible with the opcode schema.", entity, address);
                        }
                    }
                } else if (const auto* opaque = std::get_if<SctOpaqueParameterValue>(&parameter.value)) {
                    if (opaque->words.empty()) error(result, SctDiagnosticCode::ParameterMismatch,
                        "Opaque parameter fallback must preserve at least one word.", entity, address);
                }
            };
            for (const auto& parameter : instruction.fixedParameters) validateValue(parameter, std::nullopt);
            for (std::size_t groupOrdinal = 0; groupOrdinal < instruction.repeatedParameterGroups.size(); ++groupOrdinal)
                for (const auto& parameter : instruction.repeatedParameterGroups[groupOrdinal].parameters)
                    validateValue(parameter, static_cast<std::uint32_t>(groupOrdinal));
        }
    }

    for (const auto& attachment : document.opaqueAttachments) {
        const auto entity = SctDocumentEntityId{attachment.id};
        if (attachment.bytes.empty()) error(result, SctDiagnosticCode::AttachmentInvalid,
            "Opaque attachment must contain preserved bytes.", entity);
        if (!powerOfTwo(attachment.alignment)) error(result, SctDiagnosticCode::AttachmentInvalid,
            "Opaque attachment alignment must be a nonzero power of two.", entity);
        if (attachment.placement == SctOpaquePlacement::FixedOffset && !attachment.fixedOffset) {
            error(result, SctDiagnosticCode::AttachmentInvalid,
                "A fixed opaque attachment must record its decoded-payload offset.", entity);
        }
        if (attachment.placement != SctOpaquePlacement::FixedOffset && attachment.fixedOffset) {
            error(result, SctDiagnosticCode::AttachmentInvalid,
                "A relocatable before/after attachment cannot also claim a fixed offset.", entity);
        }
        if (attachment.relocation == SctOpaqueRelocationSupport::FixedOnly && !attachment.fixedOffset) {
            error(result, SctDiagnosticCode::AttachmentInvalid,
                "A fixed-only opaque attachment must retain a fixed placement constraint.", entity);
        }
        const bool anchorExists = std::visit([&](const auto& anchor) {
            using T = std::decay_t<decltype(anchor)>;
            if constexpr (std::is_same_v<T, SctDocumentAnchor>) return true;
            else if constexpr (std::is_same_v<T, SctSectionId>) return anchor && sectionIds.contains(anchor.value());
            else if constexpr (std::is_same_v<T, SctInstructionId>) return anchor && instructionIds.contains(anchor.value());
            else if constexpr (std::is_same_v<T, SctStringId>) return anchor && stringIds.contains(anchor.value());
            else return anchor && footerIds.contains(anchor.value());
        }, attachment.anchor);
        if (!anchorExists) error(result, SctDiagnosticCode::AttachmentInvalid,
            "Opaque attachment anchor does not resolve to a document entity.", entity);
        if (attachment.relocation == SctOpaqueRelocationSupport::FixedOnly) {
            result.unresolvedOpaqueAttachments.push_back(attachment.id);
        }
    }

    if (!document.opaqueAttachments.empty()
        && (receipt == nullptr
            || !receipt->declaredSourcePlatform.has_value()
            || *receipt->declaredSourcePlatform != options.targetPlatform)) {
        error(result, SctDiagnosticCode::OpaquePlatformUnverified,
            "Strict use of opaque attachments requires a matching caller-declared source platform.");
    }

    result.validForLayout = std::none_of(result.diagnostics.begin(), result.diagnostics.end(),
        [](const auto& diagnostic) { return diagnostic.severity == SctDiagnosticSeverity::Error; });
    return result;
}

} // namespace spice::sct
