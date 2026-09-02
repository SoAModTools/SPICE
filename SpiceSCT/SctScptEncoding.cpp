#include "SctScptEncoding.h"

#include <algorithm>
#include <type_traits>

namespace spice::sct {
namespace {

SctScptWordKind integerInputKind(std::uint32_t index) noexcept {
    if (index <= 7u || index == 0x4au) return SctScptWordKind::SecondaryValue;
    if (index == 0x0fu) return SctScptWordKind::NegatedIntVariableLow16Comparison;
    if (index >= 24u && index <= 32u) return SctScptWordKind::DirectIntVariable;
    return SctScptWordKind::NegatedIntVariable;
}

SctScptComparisonFlag pushedFlag(SctScptValueKind kind) noexcept {
    return kind == SctScptValueKind::NegatedIntVariableLow16Comparison
        ? SctScptComparisonFlag::Set : SctScptComparisonFlag::Clear;
}

SctScptSymbolicStackValue undefinedValue() {
    return {std::nullopt, SctScptComparisonFlag::Unknown};
}

} // namespace

bool isSctScptInlineValue(std::uint32_t word) noexcept {
    return word == 0x7f7fffffu || word == 0x00800000u
        || word == 0x7fffffffu || word == 0x7ffffffeu;
}

SctScptWordClassification classifySctScptWord(std::uint32_t word) noexcept {
    if (word == kSctScptStopCode) return {SctScptWordKind::Stop, 0};
    if (word <= 0x09u || word == 0x10u || word == 0x11u) {
        return {SctScptWordKind::CompareOperator, 0};
    }
    if (word == 0x0au) return {SctScptWordKind::StackOverwritePreviousWithTop, 0};
    if ((word >= 0x0bu && word <= 0x0fu) || (word >= 0x12u && word <= 0x16u)) {
        return {SctScptWordKind::ArithmeticOperator, 0};
    }
    if (word < 0x04000000u) return {SctScptWordKind::Inert, 0};
    if (word < 0x08000000u) return {SctScptWordKind::FloatLiteral, 1};
    if (word < 0x10000000u) return {SctScptWordKind::DecimalLiteral, 0};
    if (word < 0x20000000u) return {SctScptWordKind::ByteVariable, 0};
    if (word < 0x40000000u) return {SctScptWordKind::BitVariable, 0};
    if (word < 0x50000000u) return {SctScptWordKind::FloatVariable, 0};
    return {integerInputKind(word & 0x00ffffffu), 0};
}

bool isSctScptOperationEncodingValid(const SctScptOperation& operation) noexcept {
    return std::visit([](const auto& value) {
        const auto classification = classifySctScptWord(value.encodingWord);
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, SctScptValueOperation>) {
            bool kindMatches = false;
            switch (value.kind) {
            case SctScptValueKind::InlineValue:
                kindMatches = isSctScptInlineValue(value.encodingWord);
                break;
            case SctScptValueKind::FloatLiteral:
                kindMatches = classification.kind == SctScptWordKind::FloatLiteral;
                break;
            case SctScptValueKind::DecimalLiteral:
                kindMatches = classification.kind == SctScptWordKind::DecimalLiteral;
                break;
            case SctScptValueKind::ByteVariable:
                kindMatches = classification.kind == SctScptWordKind::ByteVariable;
                break;
            case SctScptValueKind::BitVariable:
                kindMatches = classification.kind == SctScptWordKind::BitVariable;
                break;
            case SctScptValueKind::FloatVariable:
                kindMatches = classification.kind == SctScptWordKind::FloatVariable;
                break;
            case SctScptValueKind::DirectIntVariable:
                kindMatches = classification.kind == SctScptWordKind::DirectIntVariable;
                break;
            case SctScptValueKind::NegatedIntVariable:
                kindMatches = classification.kind == SctScptWordKind::NegatedIntVariable;
                break;
            case SctScptValueKind::NegatedIntVariableLow16Comparison:
                kindMatches = classification.kind
                    == SctScptWordKind::NegatedIntVariableLow16Comparison;
                break;
            case SctScptValueKind::SecondaryValue:
                kindMatches = classification.kind == SctScptWordKind::SecondaryValue;
                break;
            }
            return kindMatches
                && value.payloadWords.size() == classification.payloadWordCount;
        } else if constexpr (std::is_same_v<T, SctScptBinaryOperation>) {
            return value.kind == SctScptBinaryOperationKind::Comparison
                ? classification.kind == SctScptWordKind::CompareOperator
                : classification.kind == SctScptWordKind::ArithmeticOperator;
        } else if constexpr (
            std::is_same_v<T, SctScptStackOverwritePreviousWithTopOperation>) {
            return classification.kind == SctScptWordKind::StackOverwritePreviousWithTop;
        } else {
            return classification.kind == SctScptWordKind::Inert;
        }
    }, operation);
}

bool SctScptScanner::consume(std::uint32_t word) noexcept {
    if (complete_) return false;
    ++wordCount_;
    if (expectingPayload_) {
        expectingPayload_ = false;
        return true;
    }
    if (wordCount_ == 1u && isSctScptInlineValue(word)) {
        complete_ = true;
        inlineValue_ = true;
        return false;
    }
    const auto classification = classifySctScptWord(word);
    if (classification.kind == SctScptWordKind::Stop) {
        complete_ = true;
        return false;
    }
    expectingPayload_ = classification.payloadWordCount != 0u;
    return true;
}

SctScptScanResult SctScptScanner::result() const noexcept {
    if (complete_) return {true, inlineValue_, wordCount_, SctScptScanError::None};
    if (wordCount_ == 0u) return {false, false, 0u, SctScptScanError::Empty};
    return {false, false, wordCount_, expectingPayload_
        ? SctScptScanError::TruncatedFloatPayload : SctScptScanError::MissingTerminator};
}

SctScptScanResult scanSctScptWords(std::span<const std::uint32_t> words) noexcept {
    SctScptScanner scanner;
    for (const auto word : words) if (!scanner.consume(word)) break;
    return scanner.result();
}

std::string_view sctScptOperatorSymbol(std::uint32_t word) noexcept {
    switch (word) {
    case 0x00u: return "<";
    case 0x01u: return "<=";
    case 0x02u: return ">";
    case 0x03u: return ">=";
    case 0x04u: return "==";
    case 0x05u: return "!=";
    case 0x06u: case 0x10u: return "&";
    case 0x07u: case 0x11u: return "|";
    case 0x08u: return "&&";
    case 0x09u: return "||";
    case 0x0au: return "overwrite-previous-with-top";
    case 0x0bu: case 0x12u: return "*";
    case 0x0cu: case 0x13u: return "/";
    case 0x0du: case 0x14u: return "%";
    case 0x0eu: case 0x15u: return "+";
    case 0x0fu: case 0x16u: return "-";
    default: return "?";
    }
}

std::string_view sctScptSecondaryValueName(std::uint32_t index) noexcept {
    switch (index) {
    case 0x00u: return "Gold";
    case 0x01u: return "Reputation";
    case 0x02u: return "Vyse.curHP";
    case 0x03u: return "Aika.curHP";
    case 0x04u: return "Fina.curHP";
    case 0x05u: return "Drachma.curHP";
    case 0x06u: return "Enrique.curHP";
    case 0x07u: return "Gilder.curHP";
    case 0x4au: return "Vyse.lvl";
    default: return {};
    }
}

std::vector<std::uint32_t> encodeSctCanonicalExpressionWords(
    const SctCanonicalExpression& expression) {
    if (const auto* opaque = std::get_if<SctOpaqueExpression>(&expression.body)) {
        return opaque->words;
    }
    std::vector<std::uint32_t> words;
    for (const auto& operation : std::get<SctTypedScptProgram>(expression.body).operations) {
        std::visit([&](const auto& value) {
            words.push_back(value.encodingWord);
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, SctScptValueOperation>) {
                words.insert(words.end(), value.payloadWords.begin(), value.payloadWords.end());
            }
        }, operation);
    }
    if (expression.termination == SctExpressionTermination::StopCode) {
        words.push_back(kSctScptStopCode);
    }
    return words;
}

SctScptProgramAnalysis analyzeSctScptProgram(const SctTypedScptProgram& program) {
    SctScptProgramAnalysis result;
    auto& stack = result.finalStack;
    bool underflow = false;
    for (std::uint32_t ordinal = 0; ordinal < program.operations.size(); ++ordinal) {
        const auto& operation = program.operations[ordinal];
        if (const auto* value = std::get_if<SctScptValueOperation>(&operation)) {
            stack.push_back({SctScptDerivedExpressionNode{ordinal}, pushedFlag(value->kind)});
            result.maximumLogicalStackDepth = std::max(result.maximumLogicalStackDepth,
                static_cast<std::uint32_t>(stack.size()));
            continue;
        }
        if (std::holds_alternative<SctScptInertOperation>(operation)) continue;
        if (std::holds_alternative<SctScptStackOverwritePreviousWithTopOperation>(operation)) {
            if (stack.size() < 2u) {
                underflow = true;
                result.issues.push_back({SctScptProgramIssueKind::LogicalStackUnderflow, ordinal});
                continue;
            }
            stack[stack.size() - 2u].expression = stack.back().expression;
            stack[stack.size() - 2u].comparisonFlag = SctScptComparisonFlag::Clear;
            continue;
        }
        const auto* binary = std::get_if<SctScptBinaryOperation>(&operation);
        if (binary == nullptr) continue;
        if (stack.size() < 2u) {
            underflow = true;
            result.issues.push_back({SctScptProgramIssueKind::LogicalStackUnderflow, ordinal});
            if (stack.empty()) stack.push_back(undefinedValue());
            else stack.front() = undefinedValue();
            continue;
        }
        auto right = std::move(stack.back()); stack.pop_back();
        auto left = std::move(stack.back()); stack.pop_back();
        SctScptDerivedExpressionNode node{ordinal};
        if (left.expression) node.children.push_back(*left.expression);
        if (right.expression) node.children.push_back(*right.expression);
        if (binary->kind == SctScptBinaryOperationKind::Comparison) {
            if (left.comparisonFlag == SctScptComparisonFlag::Set
                || right.comparisonFlag == SctScptComparisonFlag::Set) {
                node.comparisonMode = SctScptComparisonMode::Low16Integer;
            } else if (left.comparisonFlag == SctScptComparisonFlag::Clear
                && right.comparisonFlag == SctScptComparisonFlag::Clear) {
                node.comparisonMode = SctScptComparisonMode::Floating;
            } else {
                node.comparisonMode = SctScptComparisonMode::Unknown;
            }
        }
        stack.push_back({std::move(node), SctScptComparisonFlag::Clear});
    }
    if (result.maximumLogicalStackDepth > kSctScptRuntimeStackWarningThreshold) {
        result.issues.push_back({SctScptProgramIssueKind::RuntimeStackDepth, std::nullopt});
    }
    if (stack.empty() || !stack.front().expression) {
        result.issues.push_back({SctScptProgramIssueKind::UndefinedReturnValue, std::nullopt});
    } else {
        result.returnedExpression = stack.front().expression;
    }
    if (stack.size() > 1u) {
        result.issues.push_back({SctScptProgramIssueKind::ResidualStackValues, std::nullopt});
    }
    if (!underflow && stack.size() == 1u && result.returnedExpression) {
        result.conventionalTree = result.returnedExpression;
    }
    return result;
}

std::uint32_t sctCanonicalExpressionMaximumStackDepth(
    const SctCanonicalExpression& expression) {
    const auto* program = std::get_if<SctTypedScptProgram>(&expression.body);
    return program == nullptr ? 0u : analyzeSctScptProgram(*program).maximumLogicalStackDepth;
}

} // namespace spice::sct
