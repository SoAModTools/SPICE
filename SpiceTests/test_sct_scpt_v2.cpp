#include "../SpiceSCT/SpiceSCT.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace {
using namespace spice::sct;

void appendWord(std::vector<std::uint8_t>& bytes, std::uint32_t value, bool little) {
    if (little) {
        bytes.push_back(static_cast<std::uint8_t>(value));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8u));
        bytes.push_back(static_cast<std::uint8_t>(value >> 16u));
        bytes.push_back(static_cast<std::uint8_t>(value >> 24u));
    } else {
        bytes.push_back(static_cast<std::uint8_t>(value >> 24u));
        bytes.push_back(static_cast<std::uint8_t>(value >> 16u));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8u));
        bytes.push_back(static_cast<std::uint8_t>(value));
    }
}

void writeWord(std::vector<std::uint8_t>& bytes, std::size_t offset,
    std::uint32_t value, bool little) {
    std::vector<std::uint8_t> encoded;
    appendWord(encoded, value, little);
    std::copy(encoded.begin(), encoded.end(), bytes.begin() + offset);
}

std::vector<std::uint8_t> makeExpressionFile(
    const std::vector<std::uint32_t>& expressionWords, bool little = false) {
    std::vector<std::uint8_t> bytes(32u, 0u);
    writeWord(bytes, 8u, 1u, little);
    bytes[16u] = 'S'; bytes[17u] = 'C'; bytes[18u] = 'P'; bytes[19u] = 'T';
    appendWord(bytes, 16u, little);
    for (const auto word : expressionWords) appendWord(bytes, word, little);
    appendWord(bytes, 12u, little);
    return bytes;
}

const SctParameter& firstExpression(const SctParseResult& parsed) {
    return parsed.file.sections.front().instructions.front().parameters.front();
}

SctDocument makeExpressionDocument(SctCanonicalExpression expression) {
    SctDocument document;
    SctDocumentInstruction instruction;
    instruction.id = document.allocateInstructionId();
    instruction.opcode = 16u;
    instruction.fixedParameters.push_back({0u, std::move(expression)});
    document.sections.push_back({document.allocateSectionId(), "SCPT",
        SctScriptSectionContent{{std::move(instruction)}}});
    return document;
}

TEST(SctScptV2, FourthInlineValuePreservesBoundaryInBothByteOrders) {
    for (const bool little : {false, true}) {
        const auto parsed = SctParser{}.parse(makeExpressionFile({0x7ffffffeu}, little));
        ASSERT_TRUE(parsed.parseOk);
        ASSERT_GE(parsed.file.sections.front().instructions.size(), 2u);
        const auto& parameter = firstExpression(parsed);
        ASSERT_TRUE(parameter.expression && parameter.expression->ast);
        EXPECT_EQ(parameter.expression->ast->kind, SctScptAstNodeKind::NoLoopValue);
        EXPECT_EQ(parameter.rawWords, std::vector<std::uint32_t>{0x7ffffffeu});
        EXPECT_EQ(parsed.file.sections.front().instructions[1].opcode, 12u);

        const auto imported = SctDocumentImporter::import(parsed,
            {SctPlatform::GameCube, kSctShiftJisByte7FEncoding});
        ASSERT_TRUE(imported.document);
        const auto evidence = imported.context.bind(imported.context.revisionProvenance);
        ASSERT_TRUE(evidence);
        const SctDocumentExportOptions options(SctPlatform::GameCube,
            kSctShiftJisByte7FEncoding, little ? SctDocumentOutputByteOrder::LittleEndian
                                               : SctDocumentOutputByteOrder::BigEndian);
        const auto exported = SctDocumentExporter::exportDocument(
            *imported.document, options, &*evidence);
        ASSERT_TRUE(exported.success);
        const auto reparsed = SctParser{}.parse(exported.bytes);
        ASSERT_TRUE(reparsed.parseOk);
        EXPECT_EQ(firstExpression(reparsed).rawWords,
            std::vector<std::uint32_t>{0x7ffffffeu});
    }
}

TEST(SctScptV2, ScannerKeepsFloatPayloadAtomicAndRejectsMalformedBoundaries) {
    const std::array complete{0x04000000u, 0x0000001du, 0x0000001du};
    const auto scan = scanSctScptWords(complete);
    EXPECT_TRUE(scan.complete);
    EXPECT_EQ(scan.wordCount, 3u);

    const std::array truncated{0x04000000u};
    const auto truncatedScan = scanSctScptWords(truncated);
    EXPECT_FALSE(truncatedScan.complete);
    EXPECT_EQ(truncatedScan.error, SctScptScanError::TruncatedFloatPayload);

    const std::array unterminated{0x08000100u};
    EXPECT_EQ(scanSctScptWords(unterminated).error, SctScptScanError::MissingTerminator);
}

TEST(SctScptV2, NonTreeRuntimeSequencesBecomeWholeOpaqueExpressions) {
    const std::vector<std::vector<std::uint32_t>> cases{
        {0x08000100u, 0x00000017u, 0x0000001du},
        {0x08000100u, 0x08000200u, 0x0000001du},
        {0x0000000eu, 0x0000001du},
        {0x08000100u, 0x08000200u, 0x0000000au, 0x08000300u, 0x0000000eu, 0x0000001du},
    };
    for (const auto& words : cases) {
        const auto parsed = SctParser{}.parse(makeExpressionFile(words));
        ASSERT_TRUE(parsed.parseOk);
        const auto& parameter = firstExpression(parsed);
        ASSERT_TRUE(parameter.expression);
        EXPECT_FALSE(parameter.expression->ast) << words.front();
        const auto imported = SctDocumentImporter::import(parsed);
        ASSERT_TRUE(imported.document);
        const auto& instruction = std::get<SctScriptSectionContent>(
            imported.document->sections.front().content).instructions.front();
        const auto& expression = std::get<SctCanonicalExpression>(
            instruction.fixedParameters.front().value);
        ASSERT_TRUE(std::holds_alternative<SctOpaqueExpression>(expression.root));
        EXPECT_EQ(std::get<SctOpaqueExpression>(expression.root).words, words);
    }
}

TEST(SctScptV2, SignedDecimalAndNotEqualHaveDistinctTypedMeaning) {
    const auto parsed = SctParser{}.parse(makeExpressionFile(
        {0x08ffff80u, 0x08000000u, 0x00000005u, 0x0000001du}));
    ASSERT_TRUE(parsed.parseOk);
    const auto& expression = firstExpression(parsed).expression;
    ASSERT_TRUE(expression && expression->ast);
    EXPECT_EQ(expression->ast->kind, SctScptAstNodeKind::CompareOp);
    EXPECT_EQ(expression->ast->op, "!=");
    ASSERT_EQ(expression->ast->children.size(), 2u);
    const auto literal = expression->ast->children.front().numericLiteral();
    ASSERT_TRUE(literal);
    EXPECT_DOUBLE_EQ(literal->value, -0.5);

    const auto factoryLiteral = SctExpressionFactory::encodedDecimalLiteral(-1, 128u);
    EXPECT_EQ(encodeSctCanonicalExpressionWords(factoryLiteral),
        (std::vector<std::uint32_t>{0x08ffff80u, 0x0000001du}));
    const auto notEqual = SctExpressionFactory::binaryOperator(
        SctExpressionBinaryOperator::NotEqual,
        SctExpressionFactory::encodedDecimalLiteral(1),
        SctExpressionFactory::encodedDecimalLiteral(2));
    ASSERT_TRUE(notEqual.expression);
    EXPECT_EQ(std::get<SctCanonicalExpressionNode>(notEqual.expression->root).encodingCode, 0x05u);
}

TEST(SctScptV2, IntegerInputClassificationCoversConfirmedBoundaries) {
    struct Case { std::uint32_t index; SctScptWordKind kind; };
    const Case cases[]{
        {0u, SctScptWordKind::SecondaryValue}, {7u, SctScptWordKind::SecondaryValue},
        {8u, SctScptWordKind::NegatedIntVariable}, {14u, SctScptWordKind::NegatedIntVariable},
        {15u, SctScptWordKind::NegatedIntVariableLow16Comparison},
        {16u, SctScptWordKind::NegatedIntVariable}, {23u, SctScptWordKind::NegatedIntVariable},
        {24u, SctScptWordKind::DirectIntVariable}, {32u, SctScptWordKind::DirectIntVariable},
        {33u, SctScptWordKind::NegatedIntVariable}, {0x4au, SctScptWordKind::SecondaryValue},
        {0x4bu, SctScptWordKind::NegatedIntVariable},
        {0x00ffffffu, SctScptWordKind::NegatedIntVariable},
    };
    for (const auto& entry : cases) {
        EXPECT_EQ(classifySctScptWord(0x50000000u | entry.index).kind, entry.kind) << entry.index;
        const auto built = SctExpressionFactory::integerInput(entry.index);
        ASSERT_TRUE(built.expression) << entry.index;
        ASSERT_TRUE(built.selectedNodeKind) << entry.index;
        const auto& node = std::get<SctCanonicalExpressionNode>(built.expression->root);
        EXPECT_EQ(*built.selectedNodeKind, node.kind) << entry.index;
        EXPECT_EQ(classifySctScptWord(node.encodingCode).kind, entry.kind) << entry.index;
    }
}

TEST(SctScptV2, ExplicitVariableAndScaledDecimalFactoriesRejectWrongDomains) {
    const auto direct = SctExpressionFactory::directIntegerVariable(24u);
    ASSERT_TRUE(direct.expression);
    EXPECT_EQ(direct.selectedNodeKind, SctCanonicalExpressionNodeKind::IntVariable);
    EXPECT_FALSE(SctExpressionFactory::directIntegerVariable(23u).expression);

    const auto negated = SctExpressionFactory::negatedIntegerVariable(8u);
    ASSERT_TRUE(negated.expression);
    EXPECT_EQ(negated.selectedNodeKind, SctCanonicalExpressionNodeKind::NegatedIntVariable);
    EXPECT_FALSE(SctExpressionFactory::negatedIntegerVariable(24u).expression);

    const auto low16 = SctExpressionFactory::low16ComparisonIntegerVariable(15u);
    ASSERT_TRUE(low16.expression);
    EXPECT_EQ(low16.selectedNodeKind,
        SctCanonicalExpressionNodeKind::NegatedIntVariableLow16Comparison);
    EXPECT_FALSE(SctExpressionFactory::low16ComparisonIntegerVariable(14u).expression);

    EXPECT_TRUE(SctExpressionFactory::floatVariable(0x0fffffffu).expression);
    EXPECT_FALSE(SctExpressionFactory::floatVariable(0x10000000u).expression);
    EXPECT_TRUE(SctExpressionFactory::bitVariable(0x1fffffffu).expression);
    EXPECT_FALSE(SctExpressionFactory::bitVariable(0x20000000u).expression);
    EXPECT_TRUE(SctExpressionFactory::byteVariable(0x0fffffffu).expression);
    EXPECT_FALSE(SctExpressionFactory::byteVariable(0x10000000u).expression);

    const auto minimum = SctExpressionFactory::scaledDecimalLiteral(-0x800000);
    const auto maximum = SctExpressionFactory::scaledDecimalLiteral(0x7fffff);
    ASSERT_TRUE(minimum.expression && maximum.expression);
    EXPECT_EQ(encodeSctCanonicalExpressionWords(*minimum.expression).front(), 0x08800000u);
    EXPECT_EQ(encodeSctCanonicalExpressionWords(*maximum.expression).front(), 0x087fffffu);
    EXPECT_FALSE(SctExpressionFactory::scaledDecimalLiteral(-0x800001).expression);
    EXPECT_FALSE(SctExpressionFactory::scaledDecimalLiteral(0x800000).expression);
}

TEST(SctScptV2, AlternateOperatorAndInputPrefixesRemainByteExact) {
    const std::vector<std::uint32_t> words{
        0x05000001u, 0x3f800000u, 0x09000180u, 0x00000015u, 0x0000001du};
    const auto parsed = SctParser{}.parse(makeExpressionFile(words));
    ASSERT_TRUE(parsed.parseOk);
    const auto imported = SctDocumentImporter::import(parsed);
    ASSERT_TRUE(imported.document);
    const auto& instruction = std::get<SctScriptSectionContent>(
        imported.document->sections.front().content).instructions.front();
    const auto& expression = std::get<SctCanonicalExpression>(instruction.fixedParameters.front().value);
    ASSERT_TRUE(std::holds_alternative<SctCanonicalExpressionNode>(expression.root));
    EXPECT_EQ(encodeSctCanonicalExpressionWords(expression), words);
    EXPECT_EQ(std::get<SctCanonicalExpressionNode>(expression.root).encodingCode, 0x15u);

    const auto equal = SctExpressionFactory::binaryOperator(
        SctExpressionBinaryOperator::Equal,
        SctExpressionFactory::encodedDecimalLiteral(1),
        SctExpressionFactory::encodedDecimalLiteral(2));
    ASSERT_TRUE(equal.expression);
    EXPECT_EQ(std::get<SctCanonicalExpressionNode>(equal.expression->root).encodingCode, 0x04u);
    EXPECT_EQ(sctScptOperatorSymbol(0x04u), "==");
    EXPECT_EQ(sctScptOperatorSymbol(0x05u), "!=");
}

TEST(SctScptV2, EveryInputRangeAcceptsAndPreservesRuntimeRecognizedPrefixes) {
    const std::vector<std::vector<std::uint32_t>> importedForms{
        {0x07ffffffu, 0x3f800000u, 0x0000001du},
        {0x0fffff80u, 0x0000001du},
        {0x1fffffffu, 0x0000001du},
        {0x3fffffffu, 0x0000001du},
        {0x4fffffffu, 0x0000001du},
        {0x60000018u, 0x0000001du},
    };
    for (const auto& words : importedForms) {
        const auto parsed = SctParser{}.parse(makeExpressionFile(words));
        ASSERT_TRUE(parsed.parseOk);
        ASSERT_TRUE(firstExpression(parsed).expression->ast) << words.front();
        const auto imported = SctDocumentImporter::import(parsed);
        ASSERT_TRUE(imported.document);
        const auto& instruction = std::get<SctScriptSectionContent>(
            imported.document->sections.front().content).instructions.front();
        const auto& expression = std::get<SctCanonicalExpression>(instruction.fixedParameters.front().value);
        EXPECT_EQ(encodeSctCanonicalExpressionWords(expression), words) << words.front();
    }

    EXPECT_EQ(std::get<SctCanonicalExpressionNode>(SctExpressionFactory::floatLiteral(1.0f).root).encodingCode,
        0x04000000u);
    EXPECT_EQ(std::get<SctCanonicalExpressionNode>(SctExpressionFactory::encodedDecimalLiteral(1).root).encodingCode,
        0x08000100u);
    EXPECT_EQ(std::get<SctCanonicalExpressionNode>(
        SctExpressionFactory::byteVariable(1).expression->root).encodingCode,
        0x10000001u);
    EXPECT_EQ(std::get<SctCanonicalExpressionNode>(
        SctExpressionFactory::bitVariable(1).expression->root).encodingCode,
        0x20000001u);
    EXPECT_EQ(std::get<SctCanonicalExpressionNode>(
        SctExpressionFactory::floatVariable(1).expression->root).encodingCode,
        0x40000001u);
    EXPECT_EQ(std::get<SctCanonicalExpressionNode>(
        SctExpressionFactory::directIntegerVariable(24).expression->root).encodingCode,
        0x50000018u);
}

TEST(SctScptV2, DynamicStackDepthIsAdvisoryOnly) {
    std::vector<std::uint32_t> words;
    for (std::uint32_t i = 0; i < 31u; ++i) words.push_back(0x08000000u | (i << 8u));
    for (std::uint32_t i = 1; i < 31u; ++i) words.push_back(0x0000000eu);
    words.push_back(0x0000001du);
    const auto parsed = SctParser{}.parse(makeExpressionFile(words));
    ASSERT_TRUE(parsed.parseOk);
    ASSERT_TRUE(firstExpression(parsed).expression->ast);
    EXPECT_TRUE(std::any_of(parsed.diagnostics.begin(), parsed.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.message.find("stack-depth warning threshold") != std::string::npos;
    }));
    const auto imported = SctDocumentImporter::import(parsed);
    ASSERT_TRUE(imported.document);
    const auto validation = SctDocumentValidator::validateDocument(*imported.document);
    EXPECT_TRUE(validation.validDocument);
    EXPECT_TRUE(std::any_of(validation.diagnostics.begin(), validation.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.code == SctDiagnosticCode::ExpressionRuntimeStackDepth;
    }));
}

TEST(SctScptV2, OpaqueExpressionStillRequiresACompleteLexicalBoundary) {
    auto document = makeExpressionDocument(
        SctCanonicalExpression{SctOpaqueExpression{{0x04000000u, 0x0000001du}},
            SctExpressionTermination::StopCode});
    const auto validation = SctDocumentValidator::validateDocument(document);
    EXPECT_FALSE(validation.validDocument);
    EXPECT_TRUE(std::any_of(validation.diagnostics.begin(), validation.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.code == SctDiagnosticCode::ExpressionInvalid;
    }));
}

} // namespace
