#include "SctInstructionFactory.h"

#include <algorithm>
#include <bit>

namespace spice::sct {
namespace {

void addError(SctInstructionFactoryResult& result, SctDiagnosticCode code, std::string message,
    std::optional<SctParameterAddress> parameter = std::nullopt) {
    SctDocumentDiagnostic diagnostic{SctDiagnosticSeverity::Error, code, std::nullopt, std::move(message)};
    diagnostic.parameter = parameter;
    result.diagnostics.push_back(std::move(diagnostic));
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
    case SctOpcodeReferenceKind::FooterString:
    case SctOpcodeReferenceKind::FooterSctString:
        return std::holds_alternative<SctFooterEntryReference>(value);
    case SctOpcodeReferenceKind::None:
        break;
    }
    if (schema.encoding == SctOpcodeParameterEncoding::ScptExpression) {
        return std::holds_alternative<SctCanonicalExpression>(value);
    }
    return std::holds_alternative<SctEncodedWordValue>(value);
}

SctDocumentParameter makeDefaultParameter(const SctOpcodeParameterSchema& schema,
    SctParameterAddress address, SctInstructionFactoryResult& result) {
    SctDocumentParameter parameter;
    parameter.schemaIndex = schema.schemaIndex;
    if (schema.encoding == SctOpcodeParameterEncoding::ScptExpression) {
        parameter.value = SctExpressionFactory::decimalLiteral(0);
    } else {
        parameter.value = SctEncodedWordValue{schema.defaultEncodedWord};
    }
    if (schema.defaultConfidence == SctOpcodeContractConfidence::Provisional) {
        result.provisionalDefaults.push_back(address);
        SctDocumentDiagnostic diagnostic{SctDiagnosticSeverity::Info,
            SctDiagnosticCode::ProvisionalAuthoringDefault, std::nullopt,
            "Instruction parameter was seeded with a provisional zero default."};
        diagnostic.parameter = address;
        result.diagnostics.push_back(std::move(diagnostic));
    }
    return parameter;
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

SctCanonicalExpression SctExpressionFactory::variable(
    SctExpressionVariableKind kind, std::uint32_t index) {
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
    node.encodingCode = prefix | (index & 0x00ffffffu);
    return {std::move(node), SctExpressionTermination::StopCode};
}

SctCanonicalExpression SctExpressionFactory::binaryOperator(
    SctExpressionBinaryOperator operation,
    SctCanonicalExpression left, SctCanonicalExpression right) {
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
    if (const auto* leftNode = std::get_if<SctCanonicalExpressionNode>(&left.root)) node.children.push_back(*leftNode);
    if (const auto* rightNode = std::get_if<SctCanonicalExpressionNode>(&right.root)) node.children.push_back(*rightNode);
    return {std::move(node), SctExpressionTermination::StopCode};
}

SctInstructionFactoryResult SctInstructionFactory::create(
    SctDocument& document, const SctInstructionFactoryRequest& request) {
    SctInstructionFactoryResult result;
    const auto* schema = findSctOpcodeSchema(request.opcode);
    if (schema == nullptr) {
        addError(result, SctDiagnosticCode::EncodingUnsupported, "Opcode has no SpiceSCT schema.");
        return result;
    }
    if (sctOpcodeAvailability(*schema, request.targetPlatform) != SctOpcodeAvailability::Available) {
        addError(result, SctDiagnosticCode::OpcodeUnavailable,
            "Opcode is unavailable for the requested target platform.");
        return result;
    }
    if (schema->documentRole == SctOpcodeDocumentRole::FoldedModifier) {
        addError(result, SctDiagnosticCode::EncodingUnsupported,
            "Opcode is represented by modifier fields on another canonical instruction.");
        return result;
    }

    const auto repeated = sctOpcodeRepeatedGroup(*schema);
    std::uint32_t groupCount = 0;
    if (repeated) {
        const std::uint32_t minimum = repeated->firstParameter < schema->parameters.paramCount ? 1u : 0u;
        groupCount = request.repeatedGroupCount.value_or(minimum);
        if (groupCount < minimum) {
            addError(result, SctDiagnosticCode::ParameterMismatch,
                "Repeated opcode shape requires at least one parameter group.");
        }
    } else if (request.repeatedGroupCount.value_or(0u) != 0u) {
        addError(result, SctDiagnosticCode::ParameterMismatch,
            "Repeated group count was supplied for an opcode without a repeated group.");
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
    if (std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& diagnostic) {
            return diagnostic.severity == SctDiagnosticSeverity::Error;
        })) return result;

    SctDocumentInstruction instruction;
    instruction.opcode = request.opcode;
    instruction.skipRefresh = request.skipRefresh;
    instruction.scheduledExpression = request.scheduledExpression;
    instruction.repeatedParameterGroups.resize(groupCount);

    for (std::uint32_t parameterIndex = 0; parameterIndex < schema->parameterCatalogCount; ++parameterIndex) {
        const auto& parameterSchema = schema->parameterCatalog[parameterIndex];
        if (parameterSchema.defaultKind == SctOpcodeDefaultKind::DerivedRepeatedGroupCount
            || parameterSchema.defaultKind == SctOpcodeDefaultKind::DerivedInstructionByteLength) continue;
        const bool groupParameter = parameterSchema.belongsToRepeatedGroup;
        const std::uint32_t instances = groupParameter ? groupCount : 1u;
        for (std::uint32_t ordinal = 0; ordinal < instances; ++ordinal) {
            const SctParameterAddress address{parameterIndex,
                groupParameter ? std::optional<std::uint32_t>{ordinal} : std::nullopt};
            SctDocumentParameter parameter;
            if (const auto* supplied = findOverride(request, address)) {
                if (!valueMatches(parameterSchema, supplied->value)) {
                    addError(result, SctDiagnosticCode::ParameterMismatch,
                        "Parameter override does not match the opcode contract.", address);
                    continue;
                }
                parameter = {parameterIndex, supplied->value};
            } else if (parameterSchema.defaultKind == SctOpcodeDefaultKind::Required) {
                addError(result, SctDiagnosticCode::ParameterMismatch,
                    "A typed reference or other required parameter must be supplied.", address);
                continue;
            } else {
                parameter = makeDefaultParameter(parameterSchema, address, result);
            }
            if (groupParameter) instruction.repeatedParameterGroups[ordinal].parameters.push_back(std::move(parameter));
            else instruction.fixedParameters.push_back(std::move(parameter));
        }
    }

    for (const auto& supplied : request.parameterOverrides) {
        const auto* parameterSchema = sctOpcodeParameterSchema(*schema, supplied.address.schemaIndex);
        const bool validGroup = parameterSchema != nullptr
            && (parameterSchema->belongsToRepeatedGroup
                ? supplied.address.repeatedGroupOrdinal.has_value()
                    && *supplied.address.repeatedGroupOrdinal < groupCount
                : !supplied.address.repeatedGroupOrdinal.has_value());
        if (!validGroup) {
            addError(result, SctDiagnosticCode::ParameterMismatch,
                "Parameter override address is outside the opcode shape.", supplied.address);
        }
    }

    if (std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& diagnostic) {
            return diagnostic.severity == SctDiagnosticSeverity::Error;
        })) return result;

    instruction.id = document.allocateInstructionId();
    result.instruction = std::move(instruction);
    return result;
}

} // namespace spice::sct
