#include "SctInstructionFactory.h"

#include "SctDocumentIndex.h"
#include "SctDocumentValidator.h"

#include <algorithm>
#include <bit>

namespace spice::sct {
namespace {

template <typename Result>
void addError(Result& result, SctDiagnosticCode code, std::string message,
    std::optional<SctParameterAddress> parameter = std::nullopt) {
    SctDocumentDiagnostic diagnostic{SctDiagnosticSeverity::Error, code, std::nullopt, std::move(message)};
    diagnostic.parameter = parameter;
    result.diagnostics.push_back(std::move(diagnostic));
}

template <typename Result>
bool hasErrors(const Result& result) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity == SctDiagnosticSeverity::Error;
    });
}

SctOpcodeAvailabilityMatrix availabilityMatrix(const SctOpcodeSchema& schema) {
    return {sctOpcodeAvailability(schema, SctPlatform::GameCube),
        sctOpcodeAvailability(schema, SctPlatform::Dreamcast)};
}

const SctInstructionParameterOverride* findOverride(const SctInstructionFactoryRequest& request,
    SctParameterAddress address) {
    const auto found = std::find_if(request.parameterOverrides.begin(), request.parameterOverrides.end(),
        [&](const auto& overrideValue) { return overrideValue.address == address; });
    return found == request.parameterOverrides.end() ? nullptr : &*found;
}

bool valueMatches(const SctOpcodeParameterSchema& schema, const SctDocumentParameterValue& value) {
    switch (schema.referenceKind) {
    case SctOpcodeReferenceKind::Instruction:
        return std::holds_alternative<SctInstructionReference>(value);
    case SctOpcodeReferenceKind::Text:
        if (!schema.textReference.has_value()) return false;
        return schema.textReference->storage == SctTextStorage::IndexedSection
            ? std::holds_alternative<SctStringReference>(value)
            : std::holds_alternative<SctFooterEntryReference>(value);
    case SctOpcodeReferenceKind::None:
        break;
    }
    if (schema.encoding == SctOpcodeParameterEncoding::ScptExpression) {
        return std::holds_alternative<SctCanonicalExpression>(value);
    }
    if (schema.encoding == SctOpcodeParameterEncoding::RawWordsUntilSentinel) {
        return std::holds_alternative<SctTerminatedWordSequenceValue>(value);
    }
    return std::holds_alternative<SctEncodedWordValue>(value);
}

SctDocumentParameterValue defaultValue(const SctOpcodeParameterSchema& schema) {
    if (schema.encoding == SctOpcodeParameterEncoding::ScptExpression) {
        return SctExpressionFactory::decimalLiteral(0);
    }
    if (schema.encoding == SctOpcodeParameterEncoding::RawWordsUntilSentinel) {
        return SctTerminatedWordSequenceValue{{schema.defaultEncodedWord}};
    }
    return SctEncodedWordValue{schema.defaultEncodedWord};
}

bool validGroupCount(const SctOpcodeSchema& schema, std::uint32_t count) {
    const auto repeated = sctOpcodeRepeatedGroup(schema);
    if (!repeated) return count == 0u;
    const auto minimum = repeated->firstParameter < schema.parameters.paramCount ? 1u : 0u;
    return count >= minimum;
}

std::uint32_t requestedGroupCount(const SctOpcodeSchema& schema,
    const std::optional<std::uint32_t>& requested) {
    const auto repeated = sctOpcodeRepeatedGroup(schema);
    if (!repeated) return requested.value_or(0u);
    const auto minimum = repeated->firstParameter < schema.parameters.paramCount ? 1u : 0u;
    return requested.value_or(minimum);
}

} // namespace

SctCanonicalExpression SctExpressionFactory::decimalLiteral(
    std::uint16_t whole, std::uint8_t fraction256) {
    SctCanonicalExpressionNode node;
    node.kind = SctCanonicalExpressionNodeKind::DecimalLiteral;
    node.encodingCode = 0x08000000u | (static_cast<std::uint32_t>(whole) << 8u) | fraction256;
    return {std::move(node), SctExpressionTermination::StopCode};
}

SctCanonicalExpression SctExpressionFactory::floatLiteral(float value) {
    SctCanonicalExpressionNode node;
    node.kind = SctCanonicalExpressionNodeKind::FloatLiteral;
    node.encodingCode = 0x04000000u;
    node.payloadWords.push_back(std::bit_cast<std::uint32_t>(value));
    return {std::move(node), SctExpressionTermination::StopCode};
}

SctExpressionBuildResult SctExpressionFactory::variable(
    SctExpressionVariableKind kind, std::uint32_t index) {
    SctExpressionBuildResult result;
    if (index > 0x00ffffffu) {
        addError(result, SctDiagnosticCode::ExpressionInvalid,
            "SCPT variable index exceeds the confirmed 24-bit encoded domain.");
        return result;
    }

    std::uint32_t prefix = 0;
    SctCanonicalExpressionNodeKind nodeKind = SctCanonicalExpressionNodeKind::IntVariable;
    switch (kind) {
    case SctExpressionVariableKind::Integer: prefix = 0x50000000u; break;
    case SctExpressionVariableKind::Float:
        prefix = 0x40000000u;
        nodeKind = SctCanonicalExpressionNodeKind::FloatVariable;
        break;
    case SctExpressionVariableKind::Bit:
        prefix = 0x20000000u;
        nodeKind = SctCanonicalExpressionNodeKind::BitVariable;
        break;
    case SctExpressionVariableKind::Byte:
        prefix = 0x10000000u;
        nodeKind = SctCanonicalExpressionNodeKind::ByteVariable;
        break;
    }
    SctCanonicalExpressionNode node;
    node.kind = nodeKind;
    node.encodingCode = prefix | index;
    result.expression = SctCanonicalExpression{std::move(node), SctExpressionTermination::StopCode};
    return result;
}

SctExpressionBuildResult SctExpressionFactory::binaryOperator(
    SctExpressionBinaryOperator operation,
    SctCanonicalExpression left, SctCanonicalExpression right) {
    SctExpressionBuildResult result;
    const auto* leftNode = std::get_if<SctCanonicalExpressionNode>(&left.root);
    const auto* rightNode = std::get_if<SctCanonicalExpressionNode>(&right.root);
    if (leftNode == nullptr || rightNode == nullptr) {
        addError(result, SctDiagnosticCode::ExpressionInvalid,
            "A typed SCPT operator cannot contain an opaque expression child.");
        return result;
    }

    SctCanonicalExpressionNode node;
    switch (operation) {
    case SctExpressionBinaryOperator::Less: node.kind = SctCanonicalExpressionNodeKind::CompareOperator; node.encodingCode = 0x00u; break;
    case SctExpressionBinaryOperator::LessOrEqual: node.kind = SctCanonicalExpressionNodeKind::CompareOperator; node.encodingCode = 0x01u; break;
    case SctExpressionBinaryOperator::Greater: node.kind = SctCanonicalExpressionNodeKind::CompareOperator; node.encodingCode = 0x02u; break;
    case SctExpressionBinaryOperator::GreaterOrEqual: node.kind = SctCanonicalExpressionNodeKind::CompareOperator; node.encodingCode = 0x03u; break;
    case SctExpressionBinaryOperator::Equal: node.kind = SctCanonicalExpressionNodeKind::CompareOperator; node.encodingCode = 0x04u; break;
    case SctExpressionBinaryOperator::BitAnd: node.kind = SctCanonicalExpressionNodeKind::CompareOperator; node.encodingCode = 0x06u; break;
    case SctExpressionBinaryOperator::BitOr: node.kind = SctCanonicalExpressionNodeKind::CompareOperator; node.encodingCode = 0x07u; break;
    case SctExpressionBinaryOperator::LogicalAnd: node.kind = SctCanonicalExpressionNodeKind::CompareOperator; node.encodingCode = 0x08u; break;
    case SctExpressionBinaryOperator::LogicalOr: node.kind = SctCanonicalExpressionNodeKind::CompareOperator; node.encodingCode = 0x09u; break;
    case SctExpressionBinaryOperator::Assign: node.kind = SctCanonicalExpressionNodeKind::AssignmentOperator; node.encodingCode = 0x0au; break;
    case SctExpressionBinaryOperator::Multiply: node.kind = SctCanonicalExpressionNodeKind::ArithmeticOperator; node.encodingCode = 0x0bu; break;
    case SctExpressionBinaryOperator::Divide: node.kind = SctCanonicalExpressionNodeKind::ArithmeticOperator; node.encodingCode = 0x0cu; break;
    case SctExpressionBinaryOperator::Modulo: node.kind = SctCanonicalExpressionNodeKind::ArithmeticOperator; node.encodingCode = 0x0du; break;
    case SctExpressionBinaryOperator::Add: node.kind = SctCanonicalExpressionNodeKind::ArithmeticOperator; node.encodingCode = 0x0eu; break;
    case SctExpressionBinaryOperator::Subtract: node.kind = SctCanonicalExpressionNodeKind::ArithmeticOperator; node.encodingCode = 0x0fu; break;
    }
    node.children.push_back(*leftNode);
    node.children.push_back(*rightNode);
    result.expression = SctCanonicalExpression{std::move(node), SctExpressionTermination::StopCode};
    return result;
}

SctInstructionDraftResult SctInstructionFactory::createDraft(
    const SctInstructionFactoryRequest& request) {
    SctInstructionDraftResult result;
    const auto* schema = findSctOpcodeSchema(request.opcode);
    if (schema == nullptr) {
        addError(result, SctDiagnosticCode::EncodingUnsupported, "Opcode has no SpiceSCT schema.");
        return result;
    }
    const auto availability = availabilityMatrix(*schema);
    if (!availability.availableAnywhere()) {
        addError(result, SctDiagnosticCode::OpcodeUnavailable,
            "Opcode is unavailable on every supported platform.");
        return result;
    }
    if (schema->documentRole == SctOpcodeDocumentRole::FoldedModifier) {
        addError(result, SctDiagnosticCode::EncodingUnsupported,
            "Opcode is represented by modifier fields on another canonical instruction.");
        return result;
    }

    const auto groupCount = requestedGroupCount(*schema, request.repeatedGroupCount);
    if (!validGroupCount(*schema, groupCount)) {
        addError(result, SctDiagnosticCode::ParameterMismatch,
            sctOpcodeRepeatedGroup(*schema)
                ? "Repeated opcode shape requires at least one parameter group."
                : "Repeated group count was supplied for an opcode without a repeated group.");
    }
    for (std::size_t i = 0; i < request.parameterOverrides.size(); ++i) {
        for (std::size_t j = i + 1; j < request.parameterOverrides.size(); ++j) {
            if (request.parameterOverrides[i].address == request.parameterOverrides[j].address) {
                addError(result, SctDiagnosticCode::ParameterMismatch,
                    "The same parameter address was overridden more than once.",
                    request.parameterOverrides[i].address);
            }
        }
    }
    if (hasErrors(result)) return result;

    SctInstructionDraft draft;
    draft.opcode = request.opcode;
    draft.availability = availability;
    draft.repeatedGroupCount = groupCount;
    draft.skipRefresh = request.skipRefresh;
    draft.scheduledExpression = request.scheduledExpression;

    for (std::uint32_t parameterIndex = 0; parameterIndex < schema->parameterCatalogCount; ++parameterIndex) {
        const auto& parameterSchema = schema->parameterCatalog[parameterIndex];
        if (parameterSchema.defaultKind == SctOpcodeDefaultKind::DerivedRepeatedGroupCount
            || parameterSchema.defaultKind == SctOpcodeDefaultKind::DerivedInstructionByteLength) continue;
        const auto instances = parameterSchema.belongsToRepeatedGroup ? groupCount : 1u;
        for (std::uint32_t ordinal = 0; ordinal < instances; ++ordinal) {
            const SctParameterAddress address{parameterIndex,
                parameterSchema.belongsToRepeatedGroup
                    ? std::optional<std::uint32_t>{ordinal} : std::nullopt};
            SctInstructionDraftParameter parameter;
            parameter.address = address;
            if (const auto* supplied = findOverride(request, address)) {
                if (!valueMatches(parameterSchema, supplied->value)) {
                    addError(result, SctDiagnosticCode::ParameterMismatch,
                        "Parameter override does not match the opcode contract.", address);
                } else {
                    parameter.value = supplied->value;
                }
            } else if (parameterSchema.defaultKind == SctOpcodeDefaultKind::ProvisionalZero) {
                parameter.suggestedValue = defaultValue(parameterSchema);
                SctDocumentDiagnostic diagnostic{SctDiagnosticSeverity::Info,
                    SctDiagnosticCode::ProvisionalAuthoringDefault, std::nullopt,
                    "Instruction parameter has a provisional zero suggestion that must be resolved explicitly."};
                diagnostic.parameter = address;
                result.diagnostics.push_back(std::move(diagnostic));
            } else if (parameterSchema.defaultKind != SctOpcodeDefaultKind::Required) {
                parameter.value = defaultValue(parameterSchema);
            }
            draft.parameters.push_back(std::move(parameter));
        }
    }

    for (const auto& supplied : request.parameterOverrides) {
        const auto* parameterSchema = sctOpcodeParameterSchema(*schema, supplied.address.schemaIndex);
        const bool validAddress = parameterSchema != nullptr
            && parameterSchema->defaultKind != SctOpcodeDefaultKind::DerivedRepeatedGroupCount
            && parameterSchema->defaultKind != SctOpcodeDefaultKind::DerivedInstructionByteLength
            && (parameterSchema->belongsToRepeatedGroup
                ? supplied.address.repeatedGroupOrdinal.has_value()
                    && *supplied.address.repeatedGroupOrdinal < groupCount
                : !supplied.address.repeatedGroupOrdinal.has_value());
        if (!validAddress) {
            addError(result, SctDiagnosticCode::ParameterMismatch,
                "Parameter override address is outside the opcode shape.", supplied.address);
        }
    }

    if (!hasErrors(result)) result.draft = std::move(draft);
    return result;
}

SctInstructionMaterializationResult SctInstructionFactory::materialize(
    SctDocument& document, const SctInstructionDraft& draft) {
    SctInstructionMaterializationResult result;
    const auto* schema = findSctOpcodeSchema(draft.opcode);
    if (schema == nullptr) {
        addError(result, SctDiagnosticCode::EncodingUnsupported, "Draft opcode has no SpiceSCT schema.");
        return result;
    }
    const auto availability = availabilityMatrix(*schema);
    if (!availability.availableAnywhere()) {
        addError(result, SctDiagnosticCode::OpcodeUnavailable,
            "Draft opcode is unavailable on every supported platform.");
    }
    if (draft.availability != availability) {
        addError(result, SctDiagnosticCode::InvalidContent,
            "Draft platform-availability matrix disagrees with the opcode schema.");
    }
    if (schema->documentRole == SctOpcodeDocumentRole::FoldedModifier) {
        addError(result, SctDiagnosticCode::EncodingUnsupported,
            "Folded modifier opcode cannot materialize as a canonical instruction.");
    }
    if (!validGroupCount(*schema, draft.repeatedGroupCount)) {
        addError(result, SctDiagnosticCode::ParameterMismatch,
            "Draft repeated-group count disagrees with the opcode schema.");
    }
    if (hasErrors(result)) return result;

    SctDocumentInstruction instruction;
    instruction.opcode = draft.opcode;
    instruction.skipRefresh = draft.skipRefresh;
    instruction.scheduledExpression = draft.scheduledExpression;
    instruction.repeatedParameterGroups.resize(draft.repeatedGroupCount);
    std::vector<bool> consumed(draft.parameters.size(), false);

    for (std::uint32_t parameterIndex = 0; parameterIndex < schema->parameterCatalogCount; ++parameterIndex) {
        const auto& parameterSchema = schema->parameterCatalog[parameterIndex];
        if (parameterSchema.defaultKind == SctOpcodeDefaultKind::DerivedRepeatedGroupCount
            || parameterSchema.defaultKind == SctOpcodeDefaultKind::DerivedInstructionByteLength) continue;
        const auto instances = parameterSchema.belongsToRepeatedGroup ? draft.repeatedGroupCount : 1u;
        for (std::uint32_t ordinal = 0; ordinal < instances; ++ordinal) {
            const SctParameterAddress address{parameterIndex,
                parameterSchema.belongsToRepeatedGroup
                    ? std::optional<std::uint32_t>{ordinal} : std::nullopt};
            std::vector<std::size_t> matches;
            for (std::size_t i = 0; i < draft.parameters.size(); ++i) {
                if (draft.parameters[i].address == address) matches.push_back(i);
            }
            if (matches.size() != 1u) {
                addError(result, SctDiagnosticCode::ParameterMismatch,
                    matches.empty() ? "Draft is missing a parameter slot."
                                    : "Draft contains a duplicated parameter slot.", address);
                continue;
            }
            const auto draftIndex = matches.front();
            consumed[draftIndex] = true;
            const auto& source = draft.parameters[draftIndex];
            if (!source.value.has_value()) {
                addError(result,
                    source.suggestedValue.has_value() ? SctDiagnosticCode::ProvisionalAuthoringDefault
                                                      : SctDiagnosticCode::ParameterMismatch,
                    source.suggestedValue.has_value()
                        ? "Provisional parameter suggestion must be accepted or replaced explicitly."
                        : "Required draft parameter has not been resolved.", address);
                continue;
            }
            if (!valueMatches(parameterSchema, *source.value)) {
                addError(result, SctDiagnosticCode::ParameterMismatch,
                    "Resolved draft parameter does not match the opcode contract.", address);
                continue;
            }
            SctDocumentParameter parameter{parameterIndex, *source.value};
            if (parameterSchema.belongsToRepeatedGroup) {
                instruction.repeatedParameterGroups[ordinal].parameters.push_back(std::move(parameter));
            } else {
                instruction.fixedParameters.push_back(std::move(parameter));
            }
        }
    }
    for (std::size_t i = 0; i < consumed.size(); ++i) {
        if (!consumed[i]) {
            addError(result, SctDiagnosticCode::ParameterMismatch,
                "Draft contains a parameter slot outside the opcode shape.", draft.parameters[i].address);
        }
    }
    if (hasErrors(result)) return result;

    SctDocument validationDocument;
    const auto validationSectionId = validationDocument.allocateSectionId();
    const auto validationInstructionId = validationDocument.allocateInstructionId();
    instruction.id = validationInstructionId;
    validationDocument.sections.push_back({validationSectionId, "DRAFT",
        SctScriptSectionContent{{instruction}}});
    const auto validation = SctDocumentValidator::validateDocument(validationDocument);
    for (const auto& diagnostic : validation.diagnostics) {
        if (diagnostic.code == SctDiagnosticCode::UnresolvedReference) continue;
        auto draftDiagnostic = diagnostic;
        draftDiagnostic.entity.reset();
        result.diagnostics.push_back(std::move(draftDiagnostic));
    }

    const auto index = SctDocumentIndex::build(document);
    for (const auto& parameter : draft.parameters) {
        if (!parameter.value.has_value()) continue;
        if (const auto* reference = std::get_if<SctInstructionReference>(&*parameter.value)) {
            if (index.find(reference->target) == nullptr) {
                addError(result, SctDiagnosticCode::UnresolvedReference,
                    "Resolved draft parameter references a missing instruction.", parameter.address);
            }
        } else if (const auto* reference = std::get_if<SctStringReference>(&*parameter.value)) {
            const auto* string = index.find(reference->target);
            const auto location = index.stringLocation(reference->target);
            const auto rule = sctOpcodeTextReference(*schema, parameter.address.schemaIndex);
            if (string == nullptr || !location.has_value()) {
                addError(result, SctDiagnosticCode::UnresolvedReference,
                    "Resolved draft parameter references a missing or unplaced indexed string.", parameter.address);
            } else if (!rule.has_value() || rule->storage != SctTextStorage::IndexedSection
                || string->kind != rule->kind) {
                addError(result, SctDiagnosticCode::ParameterMismatch,
                    "Resolved indexed string reference disagrees with the opcode schema.", parameter.address);
            }
        } else if (const auto* reference = std::get_if<SctFooterEntryReference>(&*parameter.value)) {
            const auto* footer = index.find(reference->target);
            if (footer == nullptr) {
                addError(result, SctDiagnosticCode::UnresolvedReference,
                    "Resolved draft parameter references a missing footer entry.", parameter.address);
            } else {
                const auto rule = sctOpcodeTextReference(*schema, parameter.address.schemaIndex);
                if (!rule.has_value() || rule->storage != SctTextStorage::Footer
                    || footer->kind != rule->kind) {
                    addError(result, SctDiagnosticCode::ParameterMismatch,
                        "Resolved footer reference kind disagrees with the opcode schema.", parameter.address);
                }
            }
        }
    }
    if (hasErrors(result)) return result;

    instruction.id = document.allocateInstructionId();
    result.instruction = std::move(instruction);
    return result;
}

SctRepeatedParameterGroupDraftResult SctInstructionFactory::createRepeatedGroupDraft(
    std::uint16_t opcode, const std::vector<SctRepeatedParameterOverride>& overrides) {
    SctRepeatedParameterGroupDraftResult result;
    const auto* schema = findSctOpcodeSchema(opcode);
    const auto repeated = schema == nullptr ? std::nullopt : sctOpcodeRepeatedGroup(*schema);
    if (schema == nullptr || !repeated) {
        addError(result, SctDiagnosticCode::ParameterMismatch,
            "Opcode does not define a repeated parameter group.");
        return result;
    }
    for (std::size_t i = 0; i < overrides.size(); ++i) {
        for (std::size_t j = i + 1; j < overrides.size(); ++j) {
            if (overrides[i].schemaIndex == overrides[j].schemaIndex) {
                addError(result, SctDiagnosticCode::ParameterMismatch,
                    "The same repeated parameter was overridden more than once.",
                    SctParameterAddress{overrides[i].schemaIndex, 0u});
            }
        }
    }
    SctRepeatedParameterGroupDraft draft;
    draft.opcode = opcode;
    for (std::uint32_t index = repeated->firstParameter; index <= repeated->lastParameter; ++index) {
        const auto* parameterSchema = sctOpcodeParameterSchema(*schema, index);
        if (parameterSchema == nullptr || !parameterSchema->belongsToRepeatedGroup) {
            addError(result, SctDiagnosticCode::ParameterMismatch,
                "Repeated parameter schema is incomplete.", SctParameterAddress{index, 0u});
            continue;
        }
        SctRepeatedParameterDraft parameter;
        parameter.schemaIndex = index;
        const auto supplied = std::find_if(overrides.begin(), overrides.end(), [&](const auto& value) {
            return value.schemaIndex == index;
        });
        if (supplied != overrides.end()) {
            if (!valueMatches(*parameterSchema, supplied->value)) {
                addError(result, SctDiagnosticCode::ParameterMismatch,
                    "Repeated parameter override does not match the opcode contract.",
                    SctParameterAddress{index, 0u});
            } else parameter.value = supplied->value;
        } else if (parameterSchema->defaultKind == SctOpcodeDefaultKind::ProvisionalZero) {
            parameter.suggestedValue = defaultValue(*parameterSchema);
            SctDocumentDiagnostic diagnostic{SctDiagnosticSeverity::Info,
                SctDiagnosticCode::ProvisionalAuthoringDefault, std::nullopt,
                "Repeated parameter has a provisional suggestion that must be resolved explicitly."};
            diagnostic.parameter = SctParameterAddress{index, 0u};
            result.diagnostics.push_back(std::move(diagnostic));
        } else if (parameterSchema->defaultKind != SctOpcodeDefaultKind::Required) {
            parameter.value = defaultValue(*parameterSchema);
        }
        draft.parameters.push_back(std::move(parameter));
    }
    for (const auto& supplied : overrides) {
        if (supplied.schemaIndex < repeated->firstParameter || supplied.schemaIndex > repeated->lastParameter) {
            addError(result, SctDiagnosticCode::ParameterMismatch,
                "Repeated parameter override is outside the repeated group shape.",
                SctParameterAddress{supplied.schemaIndex, 0u});
        }
    }
    if (!hasErrors(result)) result.draft = std::move(draft);
    return result;
}

SctRepeatedParameterGroupMaterializationResult SctInstructionFactory::materializeRepeatedGroup(
    const SctRepeatedParameterGroupDraft& draft) {
    SctRepeatedParameterGroupMaterializationResult result;
    const auto* schema = findSctOpcodeSchema(draft.opcode);
    const auto repeated = schema == nullptr ? std::nullopt : sctOpcodeRepeatedGroup(*schema);
    if (schema == nullptr || !repeated) {
        addError(result, SctDiagnosticCode::ParameterMismatch,
            "Draft opcode does not define a repeated parameter group.");
        return result;
    }
    SctDocumentRepeatedParameterGroup group;
    std::vector<bool> consumed(draft.parameters.size(), false);
    for (std::uint32_t index = repeated->firstParameter; index <= repeated->lastParameter; ++index) {
        std::vector<std::size_t> matches;
        for (std::size_t i = 0; i < draft.parameters.size(); ++i) {
            if (draft.parameters[i].schemaIndex == index) matches.push_back(i);
        }
        if (matches.size() != 1u) {
            addError(result, SctDiagnosticCode::ParameterMismatch,
                matches.empty() ? "Repeated group draft is missing a parameter."
                                : "Repeated group draft duplicates a parameter.",
                SctParameterAddress{index, 0u});
            continue;
        }
        const auto draftIndex = matches.front();
        consumed[draftIndex] = true;
        const auto& parameter = draft.parameters[draftIndex];
        const auto* parameterSchema = sctOpcodeParameterSchema(*schema, index);
        if (!parameter.value) {
            addError(result,
                parameter.suggestedValue ? SctDiagnosticCode::ProvisionalAuthoringDefault
                                         : SctDiagnosticCode::ParameterMismatch,
                parameter.suggestedValue
                    ? "Provisional repeated parameter suggestion must be accepted or replaced explicitly."
                    : "Required repeated parameter has not been resolved.",
                SctParameterAddress{index, 0u});
        } else if (parameterSchema == nullptr || !valueMatches(*parameterSchema, *parameter.value)) {
            addError(result, SctDiagnosticCode::ParameterMismatch,
                "Repeated parameter value does not match the opcode contract.",
                SctParameterAddress{index, 0u});
        } else group.parameters.push_back({index, *parameter.value});
    }
    for (std::size_t i = 0; i < consumed.size(); ++i) {
        if (!consumed[i]) addError(result, SctDiagnosticCode::ParameterMismatch,
            "Repeated group draft contains a parameter outside the group shape.",
            SctParameterAddress{draft.parameters[i].schemaIndex, 0u});
    }
    if (!hasErrors(result)) result.group = std::move(group);
    return result;
}

} // namespace spice::sct
