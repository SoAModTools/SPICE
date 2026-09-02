#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace spice::sct {

struct SctCanonicalExpression;

inline constexpr std::uint32_t kSctScptStopCode = 0x0000001du;
inline constexpr std::uint32_t kSctScptRuntimeStackWarningThreshold = 30u;

enum class SctScptWordKind {
    Inert,
    CompareOperator,
    ArithmeticOperator,
    AssignmentOperator,
    FloatLiteral,
    DecimalLiteral,
    ByteVariable,
    BitVariable,
    FloatVariable,
    DirectIntVariable,
    NegatedIntVariable,
    NegatedIntVariableLow16Comparison,
    SecondaryValue,
    Stop,
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
[[nodiscard]] SctScptScanResult scanSctScptWords(std::span<const std::uint32_t> words) noexcept;
[[nodiscard]] std::string_view sctScptOperatorSymbol(std::uint32_t word) noexcept;
[[nodiscard]] std::string_view sctScptSecondaryValueName(std::uint32_t index) noexcept;
[[nodiscard]] std::vector<std::uint32_t> encodeSctCanonicalExpressionWords(
    const SctCanonicalExpression& expression);
[[nodiscard]] std::uint32_t sctCanonicalExpressionMaximumStackDepth(
    const SctCanonicalExpression& expression) noexcept;

} // namespace spice::sct
