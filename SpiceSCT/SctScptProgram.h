#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace spice::sct {

enum class SctExpressionTermination { InlineValue, StopCode };

enum class SctScptValueKind {
    InlineValue,
    FloatLiteral,
    DecimalLiteral,
    ByteVariable,
    BitVariable,
    FloatVariable,
    DirectIntVariable,
    NegatedIntVariable,
    NegatedIntVariableLow16Comparison,
    SecondaryValue,
};

struct SctScptValueOperation {
    SctScptValueKind kind = SctScptValueKind::InlineValue;
    std::uint32_t encodingWord = 0x7f7fffffu;
    std::vector<std::uint32_t> payloadWords;
    auto operator<=>(const SctScptValueOperation&) const = default;
};

enum class SctScptBinaryOperationKind { Comparison, Arithmetic };

struct SctScptBinaryOperation {
    SctScptBinaryOperationKind kind = SctScptBinaryOperationKind::Comparison;
    std::uint32_t encodingWord = 0;
    auto operator<=>(const SctScptBinaryOperation&) const = default;
};

// SCPT opcode 0x0a copies the top value into the preceding slot, clears the
// destination comparison flag, preserves the source flag, and leaves depth unchanged.
struct SctScptStackOverwritePreviousWithTopOperation {
    std::uint32_t encodingWord = 0x0au;
    auto operator<=>(const SctScptStackOverwritePreviousWithTopOperation&) const = default;
};

struct SctScptInertOperation {
    std::uint32_t encodingWord = 0x17u;
    auto operator<=>(const SctScptInertOperation&) const = default;
};

using SctScptOperation = std::variant<SctScptValueOperation, SctScptBinaryOperation,
    SctScptStackOverwritePreviousWithTopOperation, SctScptInertOperation>;

struct SctTypedScptProgram { std::vector<SctScptOperation> operations; };
struct SctOpaqueExpression { std::vector<std::uint32_t> words; };

struct SctCanonicalExpression {
    std::variant<SctTypedScptProgram, SctOpaqueExpression> body;
    SctExpressionTermination termination = SctExpressionTermination::InlineValue;
};

enum class SctScptComparisonMode { NotApplicable, Floating, Low16Integer, Unknown };
enum class SctScptComparisonFlag { Clear, Set, Unknown };

// A node in a symbolic presentation projection of an ordered SCPT program.
// The ordinal identifies a contributing operation in the analyzed revision; it
// is not independently owned syntax. The same operation ordinal may therefore
// appear in multiple derived positions (notably after opcode 0x0a), and all
// ordinals may shift when earlier operations are inserted, removed, or moved.
// This projection must not be used as source-mutation authority without a
// separate transformation analysis that proves a safe operation edit.
struct SctScptDerivedExpressionNode {
    std::uint32_t operationOrdinal = 0;
    SctScptComparisonMode comparisonMode = SctScptComparisonMode::NotApplicable;
    std::vector<SctScptDerivedExpressionNode> children;
};

struct SctScptSymbolicStackValue {
    std::optional<SctScptDerivedExpressionNode> expression;
    SctScptComparisonFlag comparisonFlag = SctScptComparisonFlag::Unknown;
};

enum class SctScptProgramIssueKind {
    LogicalStackUnderflow,
    UndefinedReturnValue,
    ResidualStackValues,
    RuntimeStackDepth,
};

struct SctScptProgramIssue {
    SctScptProgramIssueKind kind = SctScptProgramIssueKind::LogicalStackUnderflow;
    std::optional<std::uint32_t> operationOrdinal;
};

struct SctScptProgramAnalysis {
    std::vector<SctScptSymbolicStackValue> finalStack;
    std::optional<SctScptDerivedExpressionNode> returnedExpression;
    // Present only for a conventional single-result program. This is a
    // presentation projection over contributing operations, not an editable
    // ownership tree; duplicate operation ordinals are valid.
    std::optional<SctScptDerivedExpressionNode> conventionalTree;
    std::uint32_t maximumLogicalStackDepth = 0;
    std::vector<SctScptProgramIssue> issues;
};

} // namespace spice::sct
