#pragma once

#include "SctScptProgram.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace spice::sct {

inline constexpr std::uint32_t kSctScptStopCode = 0x0000001du;
inline constexpr std::uint32_t kSctScptRuntimeStackWarningThreshold = 30u;

enum class SctScptWordKind {
    Inert,
    CompareOperator,
    ArithmeticOperator,
    StackOverwritePreviousWithTop,
    FloatLiteral,
    DecimalLiteral,
    ByteVariable,
    BitVariable,
    FloatVariable,
    // The 0x50000000 family selects one of three confirmed IntVars access
    // paths. IntegerVariable performs signed-int32-to-float conversion,
    // IntegerVariableLow16Comparison additionally sets the comparison flag,
    // and FloatBackedIntegerVariable reads the IntVars slot directly as float.
    FloatBackedIntegerVariable,
    IntegerVariable,
    IntegerVariableLow16Comparison,
    SecondaryValue,
    Stop,

    // Frozen-v3 source compatibility. These names reflected a decompiler
    // artifact and must not be used to describe the runtime semantics.
    DirectIntVariable = FloatBackedIntegerVariable,
    NegatedIntVariable = IntegerVariable,
    NegatedIntVariableLow16Comparison = IntegerVariableLow16Comparison,
};

struct SctScptWordClassification {
    SctScptWordKind kind = SctScptWordKind::Inert;
    std::uint8_t payloadWordCount = 0;
};

enum class SctScptScanError {
    None,
    Empty,
    MissingTerminator,
    TruncatedFloatPayload,
};

struct SctScptScanResult {
    bool complete = false;
    bool inlineValue = false;
    std::size_t wordCount = 0;
    SctScptScanError error = SctScptScanError::None;
};

class SctScptScanner {
public:
    [[nodiscard]] bool consume(std::uint32_t word) noexcept;
    [[nodiscard]] SctScptScanResult result() const noexcept;
private:
    bool complete_ = false;
    bool inlineValue_ = false;
    bool expectingPayload_ = false;
    std::size_t wordCount_ = 0;
};

[[nodiscard]] bool isSctScptInlineValue(std::uint32_t word) noexcept;
[[nodiscard]] SctScptWordClassification classifySctScptWord(std::uint32_t word) noexcept;
[[nodiscard]] bool isSctScptOperationEncodingValid(
    const SctScptOperation& operation) noexcept;
[[nodiscard]] SctScptScanResult scanSctScptWords(std::span<const std::uint32_t> words) noexcept;
[[nodiscard]] std::string_view sctScptOperatorSymbol(std::uint32_t word) noexcept;
[[nodiscard]] std::string_view sctScptSecondaryValueName(std::uint32_t index) noexcept;
[[nodiscard]] std::vector<std::uint32_t> encodeSctCanonicalExpressionWords(
    const SctCanonicalExpression& expression);
[[nodiscard]] std::uint32_t sctCanonicalExpressionMaximumStackDepth(
    const SctCanonicalExpression& expression);
[[nodiscard]] SctScptProgramAnalysis analyzeSctScptProgram(
    const SctTypedScptProgram& program);

} // namespace spice::sct
