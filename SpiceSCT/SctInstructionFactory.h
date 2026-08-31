#pragma once

#include "SctDocument.h"
#include "SctOpcodeMetadata.h"

#include <optional>
#include <vector>

namespace spice::sct {

enum class SctExpressionVariableKind { Integer, Float, Bit, Byte };
enum class SctExpressionBinaryOperator {
    Less,
    LessOrEqual,
    Greater,
    GreaterOrEqual,
    Equal,
    BitAnd,
    BitOr,
    LogicalAnd,
    LogicalOr,
    Assign,
    Multiply,
    Divide,
    Modulo,
    Add,
    Subtract,
};

class SctExpressionFactory {
public:
    [[nodiscard]] static SctCanonicalExpression decimalLiteral(
        std::uint16_t whole, std::uint8_t fraction256 = 0);
    [[nodiscard]] static SctCanonicalExpression floatLiteral(float value);
    [[nodiscard]] static SctCanonicalExpression variable(
        SctExpressionVariableKind kind, std::uint32_t index);
    [[nodiscard]] static SctCanonicalExpression binaryOperator(
        SctExpressionBinaryOperator operation,
        SctCanonicalExpression left, SctCanonicalExpression right);
};

struct SctInstructionParameterOverride {
    SctParameterAddress address;
    SctDocumentParameterValue value;
};

struct SctInstructionFactoryRequest {
    std::uint16_t opcode = 0;
    SctPlatform targetPlatform = SctPlatform::GameCube;
    std::optional<std::uint32_t> repeatedGroupCount;
    std::vector<SctInstructionParameterOverride> parameterOverrides;
    bool skipRefresh = false;
    std::optional<SctCanonicalExpression> scheduledExpression;
};

struct SctInstructionFactoryResult {
    std::optional<SctDocumentInstruction> instruction;
    std::vector<SctDocumentDiagnostic> diagnostics;
    std::vector<SctParameterAddress> provisionalDefaults;
};

class SctInstructionFactory {
public:
    [[nodiscard]] static SctInstructionFactoryResult create(
        SctDocument& document, const SctInstructionFactoryRequest& request);
};

} // namespace spice::sct
