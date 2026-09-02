#include "SctScptEncoding.h"

#include "SctDocument.h"

#include <algorithm>

namespace spice::sct {
namespace {

SctScptWordKind integerInputKind(std::uint32_t index) noexcept {
    if (index <= 7u || index == 0x4au) return SctScptWordKind::SecondaryValue;
    if (index == 0x0fu) return SctScptWordKind::NegatedIntVariableLow16Comparison;
    if (index >= 24u && index <= 32u) return SctScptWordKind::DirectIntVariable;
    return SctScptWordKind::NegatedIntVariable;
}

std::uint32_t nodeMaximumStackDepth(const SctCanonicalExpressionNode& node) noexcept {
    if (node.children.empty()) return 1u;
    std::uint32_t depth = 0u;
    std::uint32_t retainedRoots = 0u;
    for (const auto& child : node.children) {
        depth = std::max(depth, retainedRoots + nodeMaximumStackDepth(child));
        ++retainedRoots;
    }
    return depth;
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
    if (word == 0x0au) return {SctScptWordKind::AssignmentOperator, 0};
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
    for (const auto word : words) {
        if (!scanner.consume(word)) break;
    }
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
    case 0x0au: return "=";
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
    if (const auto* opaque = std::get_if<SctOpaqueExpression>(&expression.root)) {
        return opaque->words;
    }
    std::vector<std::uint32_t> words;
    const auto appendNode = [&](const auto& self, const SctCanonicalExpressionNode& node) -> void {
        for (const auto& child : node.children) self(self, child);
        words.push_back(node.encodingCode);
        words.insert(words.end(), node.payloadWords.begin(), node.payloadWords.end());
    };
    const auto& root = std::get<SctCanonicalExpressionNode>(expression.root);
    appendNode(appendNode, root);
    if (expression.termination == SctExpressionTermination::StopCode
        && root.kind != SctCanonicalExpressionNodeKind::Stop) {
        words.push_back(kSctScptStopCode);
    }
    return words;
}

std::uint32_t sctCanonicalExpressionMaximumStackDepth(
    const SctCanonicalExpression& expression) noexcept {
    const auto* root = std::get_if<SctCanonicalExpressionNode>(&expression.root);
    return root == nullptr ? 0u : nodeMaximumStackDepth(*root);
}

} // namespace spice::sct
