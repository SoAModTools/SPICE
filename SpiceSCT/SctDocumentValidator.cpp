#include "SctDocumentValidator.h"

#include <algorithm>
#include <unordered_set>

namespace spice::sct {
namespace {

void error(SctValidationResult& result, SctDiagnosticCode code, std::string message,
    std::optional<SctDocumentEntityId> entity = std::nullopt) {
    result.diagnostics.push_back({SctDiagnosticSeverity::Error, code, std::move(entity), std::move(message)});
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
    SctDocumentEntityId entity) {
    bool valid = true;
    const bool binary = node.kind == SctCanonicalExpressionNodeKind::CompareOperator
        || node.kind == SctCanonicalExpressionNodeKind::ArithmeticOperator
        || node.kind == SctCanonicalExpressionNodeKind::AssignmentOperator;
    if (binary && node.children.size() != 2) {
        error(result, SctDiagnosticCode::ExpressionInvalid,
            "A typed SCPT operator must have exactly two children.", entity);
        valid = false;
    }
    if (!binary && node.kind == SctCanonicalExpressionNodeKind::Stop && !node.children.empty()) {
        error(result, SctDiagnosticCode::ExpressionInvalid, "A stop node cannot have children.", entity);
        valid = false;
    }
    if (node.kind == SctCanonicalExpressionNodeKind::FloatLiteral && node.payloadWords.size() != 1) {
        error(result, SctDiagnosticCode::ExpressionInvalid,
            "A float literal must preserve exactly one payload word.", entity);
        valid = false;
    }
    for (const auto& child : node.children) validExpressionNode(child, result, entity);
    return valid;
}

void validateExpression(const SctCanonicalExpression& expression, SctValidationResult& result,
    SctDocumentEntityId entity) {
    if (const auto* opaque = std::get_if<SctOpaqueExpression>(&expression.root)) {
        if (opaque->words.empty()) {
            error(result, SctDiagnosticCode::ExpressionInvalid,
                "An opaque SCPT fallback must contain the preserved encoded words.", entity);
        }
    } else {
        validExpressionNode(std::get<SctCanonicalExpressionNode>(expression.root), result, entity);
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

SctValidationResult SctDocumentValidator::validate(const SctDocument& document, SctPlatform platform) {
    SctValidationResult result;
    std::unordered_set<std::uint64_t> sectionIds, instructionIds, stringIds, footerIds, attachmentIds;
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
        }
        if (const auto* script = std::get_if<SctScriptSectionContent>(&section.content)) {
            for (const auto& instruction : script->instructions) recordId(result, instruction.id, instructionIds,
                document.nextInstructionIdValue(), "Instruction");
        }
    }
    for (const auto& attachment : document.opaqueAttachments) recordId(result, attachment.id, attachmentIds,
        document.nextOpaqueAttachmentIdValue(), "Opaque attachment");

    for (const auto& section : document.sections) {
        for (const auto id : section.opaqueAttachments) {
            if (!id || !attachmentIds.contains(id.value())) {
                error(result, SctDiagnosticCode::UnresolvedReference,
                    "Section references a missing opaque attachment.", SctDocumentEntityId{section.id});
            }
        }
        const auto* script = std::get_if<SctScriptSectionContent>(&section.content);
        if (!script) continue;
        for (const auto& instruction : script->instructions) {
            const auto entity = SctDocumentEntityId{instruction.id};
            const auto* schema = findSctOpcodeSchema(instruction.opcode);
            if (!schema) {
                error(result, SctDiagnosticCode::ParameterMismatch, "Instruction opcode has no schema row.", entity);
                continue;
            }
            if (sctOpcodeAvailability(*schema, platform) != SctOpcodeAvailability::Available) {
                error(result, SctDiagnosticCode::OpcodeUnavailable,
                    "Instruction opcode is unavailable on the requested target platform.", entity);
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
            const auto validateValue = [&](const SctDocumentParameter& parameter) {
                const auto expectedEncoding = sctOpcodeParameterEncoding(*schema, parameter.schemaIndex);
                if (const auto* expression = std::get_if<SctCanonicalExpression>(&parameter.value)) {
                    if (expectedEncoding != SctOpcodeParameterEncoding::ScptExpression) {
                        error(result, SctDiagnosticCode::ParameterMismatch,
                            "Typed SCPT expression is assigned to a raw-word schema parameter.", entity);
                    }
                    validateExpression(*expression, result, entity);
                } else if (std::holds_alternative<SctEncodedWordValue>(parameter.value)) {
                    if (expectedEncoding != SctOpcodeParameterEncoding::RawWord) {
                        error(result, SctDiagnosticCode::ParameterMismatch,
                            "Encoded scalar word is assigned to an SCPT-expression schema parameter.", entity);
                    }
                } else if (const auto* reference = std::get_if<SctInstructionReference>(&parameter.value)) {
                    if (!reference->target || !instructionIds.contains(reference->target.value())) {
                        error(result, SctDiagnosticCode::UnresolvedReference,
                            "Instruction parameter references a missing instruction.", entity);
                    }
                    const bool isControlTarget = static_cast<int>(parameter.schemaIndex) == schema->parameters.jumpParam
                        || static_cast<int>(parameter.schemaIndex) == schema->parameters.switchJumpParam
                        || (schema->semantic.controlRole == SctOpcodeControlRole::CallSubscript && parameter.schemaIndex == 0);
                    if (!isControlTarget) {
                        error(result, SctDiagnosticCode::ParameterMismatch,
                            "Instruction reference is assigned outside the opcode's control-target parameter.", entity);
                    }
                } else if (const auto* reference = std::get_if<SctFooterEntryReference>(&parameter.value)) {
                    if (!reference->target || !footerIds.contains(reference->target.value())) {
                        error(result, SctDiagnosticCode::UnresolvedReference,
                            "Instruction parameter references a missing footer entry.", entity);
                    }
                    const auto rule = sctOpcodeFooterReference(*schema, parameter.schemaIndex);
                    if (rule.kind == SctFooterParamKind::None) {
                        error(result, SctDiagnosticCode::ParameterMismatch,
                            "Footer reference is assigned to a parameter without a footer-reference rule.", entity);
                    } else {
                        const auto found = std::find_if(document.footerEntries.begin(), document.footerEntries.end(),
                            [&](const auto& entry) { return entry.id == reference->target; });
                        if (found != document.footerEntries.end()
                            && ((rule.kind == SctFooterParamKind::String && found->kind != SctFooterEntryKind::String)
                                || (rule.kind == SctFooterParamKind::SctString && found->kind != SctFooterEntryKind::SctString))) {
                            error(result, SctDiagnosticCode::ParameterMismatch,
                                "Footer reference kind is incompatible with the opcode schema.", entity);
                        }
                    }
                } else if (const auto* opaque = std::get_if<SctOpaqueParameterValue>(&parameter.value)) {
                    if (opaque->words.empty()) error(result, SctDiagnosticCode::ParameterMismatch,
                        "Opaque parameter fallback must preserve at least one word.", entity);
                }
            };
            for (const auto& parameter : instruction.fixedParameters) validateValue(parameter);
            for (const auto& group : instruction.repeatedParameterGroups)
                for (const auto& parameter : group.parameters) validateValue(parameter);
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

    result.validForLayout = std::none_of(result.diagnostics.begin(), result.diagnostics.end(),
        [](const auto& diagnostic) { return diagnostic.severity == SctDiagnosticSeverity::Error; });
    return result;
}

} // namespace spice::sct
