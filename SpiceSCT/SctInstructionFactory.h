#pragma once

#include "SctDocument.h"
#include "SctOpcodeMetadata.h"

#include <optional>
#include <vector>

namespace spice::sct {

enum class SctExpressionOneWordValue : std::uint32_t {
    Value7F7FFFFF = 0x7f7fffffu,
    Value00800000 = 0x00800000u,
    Value7FFFFFFF = 0x7fffffffu,
    Value7FFFFFFE = 0x7ffffffeu,
};
enum class SctExpressionSecondaryValue : std::uint32_t {
    Gold = 0u,
    Reputation = 1u,
    VyseCurrentHp = 2u,
    AikaCurrentHp = 3u,
    FinaCurrentHp = 4u,
    DrachmaCurrentHp = 5u,
    EnriqueCurrentHp = 6u,
    GilderCurrentHp = 7u,
    VyseLevel = 0x4au,
};
enum class SctExpressionBinaryOperator {
    Less,
    LessOrEqual,
    Greater,
    GreaterOrEqual,
    Equal,
    NotEqual,
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
    [[nodiscard]] static SctCanonicalExpression encodedDecimalLiteral(
        std::int16_t whole, std::uint8_t fraction256 = 0);
    [[nodiscard]] static SctCanonicalExpression floatLiteral(float value);
    [[nodiscard]] static SctCanonicalExpression oneWordValue(SctExpressionOneWordValue value);
    [[nodiscard]] static SctCanonicalExpression secondaryValue(SctExpressionSecondaryValue value);
    struct BuildResult {
        std::optional<SctCanonicalExpression> expression;
        std::optional<SctCanonicalExpressionNodeKind> selectedNodeKind;
        std::vector<SctDocumentDiagnostic> diagnostics;
    };

    [[nodiscard]] static BuildResult scaledDecimalLiteral(std::int32_t units256);
    [[nodiscard]] static BuildResult integerInput(std::uint32_t index);
    [[nodiscard]] static BuildResult directIntegerVariable(std::uint32_t index);
    [[nodiscard]] static BuildResult negatedIntegerVariable(std::uint32_t index);
    [[nodiscard]] static BuildResult low16ComparisonIntegerVariable(std::uint32_t index);
    [[nodiscard]] static BuildResult floatVariable(std::uint32_t index);
    [[nodiscard]] static BuildResult bitVariable(std::uint32_t index);
    [[nodiscard]] static BuildResult byteVariable(std::uint32_t index);
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

struct SctRepeatedParameterOverride {
    std::uint32_t schemaIndex = 0;
    SctDocumentParameterValue value;
};

struct SctRepeatedParameterDraft {
    std::uint32_t schemaIndex = 0;
    std::optional<SctDocumentParameterValue> value;
    std::optional<SctDocumentParameterValue> suggestedValue;
};

struct SctRepeatedParameterGroupDraft {
    std::uint16_t opcode = 0;
    std::vector<SctRepeatedParameterDraft> parameters;
};

struct SctRepeatedParameterGroupDraftResult {
    std::optional<SctRepeatedParameterGroupDraft> draft;
    std::vector<SctDocumentDiagnostic> diagnostics;
};

struct SctRepeatedParameterGroupMaterializationResult {
    std::optional<SctDocumentRepeatedParameterGroup> group;
    std::vector<SctDocumentDiagnostic> diagnostics;
};

class SctInstructionFactory {
public:
    [[nodiscard]] static SctInstructionDraftResult createDraft(
        const SctInstructionFactoryRequest& request);
    [[nodiscard]] static SctInstructionMaterializationResult materialize(
        SctDocument& document, const SctInstructionDraft& draft);
    [[nodiscard]] static SctRepeatedParameterGroupDraftResult createRepeatedGroupDraft(
        std::uint16_t opcode, const std::vector<SctRepeatedParameterOverride>& overrides = {});
    [[nodiscard]] static SctRepeatedParameterGroupMaterializationResult materializeRepeatedGroup(
        const SctRepeatedParameterGroupDraft& draft);
};

} // namespace spice::sct
