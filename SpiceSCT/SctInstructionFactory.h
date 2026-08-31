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
    struct BuildResult {
        std::optional<SctCanonicalExpression> expression;
        std::vector<SctDocumentDiagnostic> diagnostics;
    };

    [[nodiscard]] static BuildResult variable(
        SctExpressionVariableKind kind, std::uint32_t index);
    [[nodiscard]] static BuildResult binaryOperator(
        SctExpressionBinaryOperator operation,
        SctCanonicalExpression left, SctCanonicalExpression right);
};

using SctExpressionBuildResult = SctExpressionFactory::BuildResult;

struct SctInstructionParameterOverride {
    SctParameterAddress address;
    SctDocumentParameterValue value;
};

struct SctInstructionFactoryRequest {
    std::uint16_t opcode = 0;
    std::optional<std::uint32_t> repeatedGroupCount;
    std::vector<SctInstructionParameterOverride> parameterOverrides;
    bool skipRefresh = false;
    std::optional<SctCanonicalExpression> scheduledExpression;
};

struct SctOpcodeAvailabilityMatrix {
    SctOpcodeAvailability gameCube = SctOpcodeAvailability::Unknown;
    SctOpcodeAvailability dreamcast = SctOpcodeAvailability::Unknown;

    [[nodiscard]] constexpr bool availableAnywhere() const noexcept {
        return gameCube == SctOpcodeAvailability::Available
            || dreamcast == SctOpcodeAvailability::Available;
    }
    auto operator<=>(const SctOpcodeAvailabilityMatrix&) const = default;
};

struct SctInstructionDraftParameter {
    SctParameterAddress address;
    std::optional<SctDocumentParameterValue> value;
    std::optional<SctDocumentParameterValue> suggestedValue;
};

struct SctInstructionDraft {
    std::uint16_t opcode = 0;
    SctOpcodeAvailabilityMatrix availability;
    std::uint32_t repeatedGroupCount = 0;
    std::vector<SctInstructionDraftParameter> parameters;
    bool skipRefresh = false;
    std::optional<SctCanonicalExpression> scheduledExpression;
};

struct SctInstructionDraftResult {
    std::optional<SctInstructionDraft> draft;
    std::vector<SctDocumentDiagnostic> diagnostics;
};

struct SctInstructionMaterializationResult {
    std::optional<SctDocumentInstruction> instruction;
    std::vector<SctDocumentDiagnostic> diagnostics;
};

class SctInstructionFactory {
public:
    [[nodiscard]] static SctInstructionDraftResult createDraft(
        const SctInstructionFactoryRequest& request);
    [[nodiscard]] static SctInstructionMaterializationResult materialize(
        SctDocument& document, const SctInstructionDraft& draft);
};

} // namespace spice::sct
