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

const SctTypedScptProgram& typedProgram(const SctCanonicalExpression& expression) {
    return std::get<SctTypedScptProgram>(expression.body);
}

const SctScptValueOperation& firstValue(const SctCanonicalExpression& expression) {
    return std::get<SctScptValueOperation>(typedProgram(expression).operations.front());
}

TEST(SctScptV3, FourthInlineValuePreservesBoundaryInBothByteOrders) {
    for (const bool little : {false, true}) {
        const auto parsed = SctParser{}.parse(makeExpressionFile({0x7ffffffeu}, little));
        ASSERT_TRUE(parsed.parseOk);
        ASSERT_GE(parsed.file.sections.front().instructions.size(), 2u);
        const auto& parameter = firstExpression(parsed);
        ASSERT_TRUE(parameter.expression && parameter.expression->program);
        ASSERT_EQ(parameter.expression->program->operations.size(), 1u);
        EXPECT_EQ(std::get<SctScptValueOperation>(
            parameter.expression->program->operations.front()).kind, SctScptValueKind::InlineValue);
        EXPECT_EQ(parameter.rawWords, std::vector<std::uint32_t>{0x7ffffffeu});
        EXPECT_EQ(parsed.file.sections.front().instructions[1].opcode, 12u);

        const auto imported = SctDocumentImporter::import(parsed,
            {SctPlatform::GameCube, kSctShiftJisByte7FEncoding});
        ASSERT_TRUE(imported.document);
        const auto evidence = imported.context.bind(imported.context.revisionProvenance());
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

TEST(SctScptV3, ScannerKeepsFloatPayloadAtomicAndRejectsMalformedBoundaries) {
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

TEST(SctScptV3, NonTreeRuntimeSequencesRemainTypedStackPrograms) {
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
        ASSERT_TRUE(parameter.expression->program) << words.front();
        const auto imported = SctDocumentImporter::import(parsed);
        ASSERT_TRUE(imported.document);
        const auto& instruction = std::get<SctScriptSectionContent>(
            imported.document->sections.front().content).instructions.front();
        const auto& expression = std::get<SctCanonicalExpression>(
            instruction.fixedParameters.front().value);
        ASSERT_TRUE(std::holds_alternative<SctTypedScptProgram>(expression.body));
        EXPECT_EQ(encodeSctCanonicalExpressionWords(expression), words);
    }
}

TEST(SctScptV3, StackOverwriteIsTypedNonterminalAndPreservesInstructionBoundary) {
    const std::vector<std::uint32_t> words{
        0x08000200u, 0x08000300u, 0x0000000au, 0x0000000eu, 0x0000001du};
    for (const bool little : {false, true}) {
        const auto parsed = SctParser{}.parse(makeExpressionFile(words, little));
        ASSERT_TRUE(parsed.parseOk);
        ASSERT_GE(parsed.file.sections.front().instructions.size(), 2u);
        EXPECT_EQ(parsed.file.sections.front().instructions[1].opcode, 12u);
        const auto& parsedExpression = firstExpression(parsed).expression;
        ASSERT_TRUE(parsedExpression && parsedExpression->program);
        ASSERT_EQ(parsedExpression->program->operations.size(), 4u);
        EXPECT_TRUE(std::holds_alternative<SctScptStackOverwritePreviousWithTopOperation>(
            parsedExpression->program->operations[2]));

        const auto analysis = analyzeSctScptProgram(*parsedExpression->program);
        ASSERT_TRUE(analysis.returnedExpression);
        ASSERT_TRUE(analysis.conventionalTree);
        EXPECT_EQ(analysis.returnedExpression->operationOrdinal, 3u);
        ASSERT_EQ(analysis.returnedExpression->children.size(), 2u);
        EXPECT_EQ(analysis.returnedExpression->children[0].operationOrdinal, 1u);
        EXPECT_EQ(analysis.returnedExpression->children[1].operationOrdinal, 1u);

        const auto imported = SctDocumentImporter::import(parsed);
        ASSERT_TRUE(imported.document);
        const auto& instruction = std::get<SctScriptSectionContent>(
            imported.document->sections.front().content).instructions.front();
        const auto& expression = std::get<SctCanonicalExpression>(
            instruction.fixedParameters.front().value);
        EXPECT_EQ(encodeSctCanonicalExpressionWords(expression), words);
    }
}

TEST(SctScptV3, StackOverwriteUnderflowAndResidualValuesAreAdvisoryOnly) {
    const std::vector<std::vector<std::uint32_t>> cases{
        {0x0000000au, 0x0000001du},
        {0x08000100u, 0x0000000au, 0x0000001du},
        {0x08000100u, 0x08000200u, 0x0000000au, 0x08000300u,
            0x0000000eu, 0x0000001du},
    };
    for (const auto& words : cases) {
        const auto parsed = SctParser{}.parse(makeExpressionFile(words));
        ASSERT_TRUE(parsed.parseOk);
        ASSERT_TRUE(firstExpression(parsed).expression->program);
        const auto imported = SctDocumentImporter::import(parsed);
        ASSERT_TRUE(imported.document);
        const auto validation = SctDocumentValidator::validateDocument(*imported.document);
        EXPECT_TRUE(validation.validDocument);
        const auto& expression = std::get<SctCanonicalExpression>(
            std::get<SctScriptSectionContent>(imported.document->sections.front().content)
                .instructions.front().fixedParameters.front().value);
        EXPECT_EQ(encodeSctCanonicalExpressionWords(expression), words);
    }

    const auto residualParsed = SctParser{}.parse(makeExpressionFile(cases.back()));
    const auto analysis = analyzeSctScptProgram(*firstExpression(residualParsed).expression->program);
    ASSERT_TRUE(analysis.returnedExpression);
    EXPECT_EQ(analysis.returnedExpression->operationOrdinal, 1u);
    EXPECT_FALSE(analysis.conventionalTree);
    EXPECT_TRUE(std::any_of(analysis.issues.begin(), analysis.issues.end(), [](const auto& issue) {
        return issue.kind == SctScptProgramIssueKind::ResidualStackValues;
    }));
}

TEST(SctScptV3, StackOverwriteClearsOnlyDestinationComparisonFlag) {
    const auto analyze = [](std::vector<SctScptOperation> operations) {
        operations.push_back(SctScptBinaryOperation{SctScptBinaryOperationKind::Comparison, 0x04u});
        return analyzeSctScptProgram(SctTypedScptProgram{std::move(operations)});
    };
    const auto flaggedThenPlain = analyze({
        SctScptValueOperation{SctScptValueKind::IntegerVariableLow16Comparison, 0x5000000fu, {}},
        SctScptValueOperation{SctScptValueKind::DecimalLiteral, 0x08000100u, {}},
        SctScptStackOverwritePreviousWithTopOperation{},
    });
    ASSERT_TRUE(flaggedThenPlain.returnedExpression);
    EXPECT_EQ(flaggedThenPlain.returnedExpression->comparisonMode, SctScptComparisonMode::Floating);

    const auto plainThenFlagged = analyze({
        SctScptValueOperation{SctScptValueKind::DecimalLiteral, 0x08000100u, {}},
        SctScptValueOperation{SctScptValueKind::IntegerVariableLow16Comparison, 0x5000000fu, {}},
        SctScptStackOverwritePreviousWithTopOperation{},
    });
    ASSERT_TRUE(plainThenFlagged.returnedExpression);
    EXPECT_EQ(plainThenFlagged.returnedExpression->comparisonMode,
        SctScptComparisonMode::Low16Integer);
}

TEST(SctScptV3, SignedDecimalAndNotEqualHaveDistinctTypedMeaning) {
    const auto parsed = SctParser{}.parse(makeExpressionFile(
        {0x08ffff80u, 0x08000000u, 0x00000005u, 0x0000001du}));
    ASSERT_TRUE(parsed.parseOk);
    const auto& expression = firstExpression(parsed).expression;
    ASSERT_TRUE(expression && expression->program);
    ASSERT_EQ(expression->program->operations.size(), 3u);
    const auto& literal = std::get<SctScptValueOperation>(expression->program->operations[0]);
    EXPECT_EQ(literal.kind, SctScptValueKind::DecimalLiteral);
    EXPECT_EQ(literal.encodingWord, 0x08ffff80u);
    const auto& comparison = std::get<SctScptBinaryOperation>(expression->program->operations[2]);
    EXPECT_EQ(comparison.kind, SctScptBinaryOperationKind::Comparison);
    EXPECT_EQ(comparison.encodingWord, 0x05u);

    const auto factoryLiteral = SctExpressionFactory::encodedDecimalLiteral(-1, 128u);
    EXPECT_EQ(encodeSctCanonicalExpressionWords(factoryLiteral),
        (std::vector<std::uint32_t>{0x08ffff80u, 0x0000001du}));
    const auto notEqual = SctExpressionFactory::binaryOperator(
        SctExpressionBinaryOperator::NotEqual,
        SctExpressionFactory::encodedDecimalLiteral(1),
        SctExpressionFactory::encodedDecimalLiteral(2));
    ASSERT_TRUE(notEqual.expression);
    EXPECT_EQ(std::get<SctScptBinaryOperation>(
        typedProgram(*notEqual.expression).operations.back()).encodingWord, 0x05u);
}

TEST(SctScptV3, IntegerInputClassificationCoversConfirmedBoundaries) {
    struct Case { std::uint32_t index; SctScptWordKind kind; };
    const Case cases[]{
        {0u, SctScptWordKind::SecondaryValue}, {7u, SctScptWordKind::SecondaryValue},
        {8u, SctScptWordKind::IntegerVariable}, {14u, SctScptWordKind::IntegerVariable},
        {15u, SctScptWordKind::IntegerVariableLow16Comparison},
        {16u, SctScptWordKind::IntegerVariable}, {23u, SctScptWordKind::IntegerVariable},
        {24u, SctScptWordKind::FloatBackedIntegerVariable},
        {32u, SctScptWordKind::FloatBackedIntegerVariable},
        {33u, SctScptWordKind::IntegerVariable}, {0x4au, SctScptWordKind::SecondaryValue},
        {0x4bu, SctScptWordKind::IntegerVariable},
        {0x00ffffffu, SctScptWordKind::IntegerVariable},
    };
    for (const auto& entry : cases) {
        EXPECT_EQ(classifySctScptWord(0x50000000u | entry.index).kind, entry.kind) << entry.index;
        const auto built = SctExpressionFactory::integerInput(entry.index);
        ASSERT_TRUE(built.expression) << entry.index;
        ASSERT_TRUE(built.selectedValueKind) << entry.index;
        const auto& value = firstValue(*built.expression);
        EXPECT_EQ(*built.selectedValueKind, value.kind) << entry.index;
        EXPECT_EQ(classifySctScptWord(value.encodingWord).kind, entry.kind) << entry.index;
    }
}

TEST(SctScptV3, IntegerInputPresentationDoesNotClaimArithmeticNegation) {
    const auto parsed = SctParser{}.parse(makeExpressionFile(
        {0x50000008u, 0x5000000fu, 0x50000018u, 0x0000001du}));
    ASSERT_TRUE(parsed.parseOk);
    const auto& parameter = firstExpression(parsed);
    ASSERT_TRUE(parameter.expression);
    ASSERT_EQ(parameter.expression->trace.size(), 4u);
    EXPECT_EQ(parameter.expression->trace[0].interpretedValue, "IntVar: 8");
    EXPECT_EQ(parameter.expression->trace[1].interpretedValue,
        "IntVarLow16Comparison: 15");
    EXPECT_EQ(parameter.expression->trace[2].interpretedValue,
        "FloatBackedIntVar: 24");

    const auto json = SctJsonExporter{}.toJson(parsed);
    EXPECT_NE(json.find("\"valueKind\":\"int_variable\""), std::string::npos);
    EXPECT_NE(json.find("\"valueKind\":\"int_variable_low16_comparison\""),
        std::string::npos);
    EXPECT_NE(json.find("\"valueKind\":\"float_backed_int_variable\""),
        std::string::npos);
    EXPECT_EQ(json.find("negated_int_variable"), std::string::npos);
}

TEST(SctScptV3, ExplicitVariableAndScaledDecimalFactoriesRejectWrongDomains) {
    const auto floatBacked = SctExpressionFactory::floatBackedIntegerVariable(24u);
    ASSERT_TRUE(floatBacked.expression);
    EXPECT_EQ(floatBacked.selectedValueKind, SctScptValueKind::FloatBackedIntegerVariable);
    EXPECT_FALSE(SctExpressionFactory::floatBackedIntegerVariable(23u).expression);

    const auto integer = SctExpressionFactory::integerVariable(8u);
    ASSERT_TRUE(integer.expression);
    EXPECT_EQ(integer.selectedValueKind, SctScptValueKind::IntegerVariable);
    EXPECT_FALSE(SctExpressionFactory::integerVariable(24u).expression);

    const auto low16 = SctExpressionFactory::low16ComparisonIntegerVariable(15u);
    ASSERT_TRUE(low16.expression);
    EXPECT_EQ(low16.selectedValueKind,
        SctScptValueKind::IntegerVariableLow16Comparison);
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

TEST(SctScptV3, AlternateOperatorAndInputPrefixesRemainByteExact) {
    const std::vector<std::uint32_t> words{
        0x05000001u, 0x3f800000u, 0x09000180u, 0x00000015u, 0x0000001du};
    const auto parsed = SctParser{}.parse(makeExpressionFile(words));
    ASSERT_TRUE(parsed.parseOk);
    const auto imported = SctDocumentImporter::import(parsed);
    ASSERT_TRUE(imported.document);
    const auto& instruction = std::get<SctScriptSectionContent>(
        imported.document->sections.front().content).instructions.front();
    const auto& expression = std::get<SctCanonicalExpression>(instruction.fixedParameters.front().value);
    ASSERT_TRUE(std::holds_alternative<SctTypedScptProgram>(expression.body));
    EXPECT_EQ(encodeSctCanonicalExpressionWords(expression), words);
    EXPECT_EQ(std::get<SctScptBinaryOperation>(typedProgram(expression).operations.back()).encodingWord,
        0x15u);

    const auto equal = SctExpressionFactory::binaryOperator(
        SctExpressionBinaryOperator::Equal,
        SctExpressionFactory::encodedDecimalLiteral(1),
        SctExpressionFactory::encodedDecimalLiteral(2));
    ASSERT_TRUE(equal.expression);
    EXPECT_EQ(std::get<SctScptBinaryOperation>(typedProgram(*equal.expression).operations.back()).encodingWord,
        0x04u);
    EXPECT_EQ(sctScptOperatorSymbol(0x04u), "==");
    EXPECT_EQ(sctScptOperatorSymbol(0x05u), "!=");
}

TEST(SctScptV3, ProgramFactoryAcceptsArbitraryValidOperationOrderAndRejectsMismatches) {
    const std::vector<SctScptOperation> unusual{
        SctScptBinaryOperation{SctScptBinaryOperationKind::Arithmetic, 0x0eu},
        SctScptInertOperation{0x17u},
        SctExpressionFactory::stackOverwritePreviousWithTop(),
    };
    const auto built = SctExpressionFactory::program(unusual);
    ASSERT_TRUE(built.expression);
    EXPECT_EQ(encodeSctCanonicalExpressionWords(*built.expression),
        (std::vector<std::uint32_t>{0x0eu, 0x17u, 0x0au, 0x1du}));

    const auto mismatched = SctExpressionFactory::program({
        SctScptBinaryOperation{SctScptBinaryOperationKind::Arithmetic, 0x04u},
    });
    EXPECT_FALSE(mismatched.expression);
    ASSERT_EQ(mismatched.diagnostics.size(), 1u);
    ASSERT_TRUE(mismatched.diagnostics.front().primaryLocation);
    EXPECT_TRUE(std::holds_alternative<SctDraftExpressionOperationSite>(
        *mismatched.diagnostics.front().primaryLocation));
    EXPECT_EQ(std::get<SctDraftExpressionOperationSite>(
        *mismatched.diagnostics.front().primaryLocation).operationOrdinal, 0u);
}

TEST(SctScptV3, BinaryOperatorRequiresSingleDefinedOperandResults) {
    const auto residual = SctExpressionFactory::program({
        SctScptValueOperation{SctScptValueKind::DecimalLiteral, 0x08000100u, {}},
        SctScptValueOperation{SctScptValueKind::DecimalLiteral, 0x08000200u, {}},
    });
    ASSERT_TRUE(residual.expression);
    const auto residualRejected = SctExpressionFactory::binaryOperator(
        SctExpressionBinaryOperator::Add, *residual.expression,
        SctExpressionFactory::encodedDecimalLiteral(3));
    EXPECT_FALSE(residualRejected.expression);
    ASSERT_EQ(residualRejected.diagnostics.size(), 1u);
    EXPECT_NE(residualRejected.diagnostics.front().message.find("Left SCPT operand"),
        std::string::npos);

    const auto underflow = SctExpressionFactory::program({
        SctScptBinaryOperation{SctScptBinaryOperationKind::Arithmetic, 0x0eu},
    });
    ASSERT_TRUE(underflow.expression);
    const auto underflowRejected = SctExpressionFactory::binaryOperator(
        SctExpressionBinaryOperator::Add,
        SctExpressionFactory::encodedDecimalLiteral(1), *underflow.expression);
    EXPECT_FALSE(underflowRejected.expression);
    ASSERT_EQ(underflowRejected.diagnostics.size(), 1u);
    EXPECT_NE(underflowRejected.diagnostics.front().message.find("Right SCPT operand"),
        std::string::npos);

    const auto overwritten = SctExpressionFactory::program({
        SctScptValueOperation{SctScptValueKind::DecimalLiteral, 0x08000200u, {}},
        SctScptValueOperation{SctScptValueKind::DecimalLiteral, 0x08000300u, {}},
        SctExpressionFactory::stackOverwritePreviousWithTop(),
        SctScptBinaryOperation{SctScptBinaryOperationKind::Arithmetic, 0x0eu},
    });
    ASSERT_TRUE(overwritten.expression);
    ASSERT_TRUE(analyzeSctScptProgram(typedProgram(*overwritten.expression)).conventionalTree);
    const auto accepted = SctExpressionFactory::binaryOperator(
        SctExpressionBinaryOperator::Multiply, *overwritten.expression,
        SctExpressionFactory::encodedDecimalLiteral(4));
    ASSERT_TRUE(accepted.expression);
    const auto analysis = analyzeSctScptProgram(typedProgram(*accepted.expression));
    EXPECT_TRUE(analysis.conventionalTree);
}

TEST(SctScptV3, EveryInputRangeAcceptsAndPreservesRuntimeRecognizedPrefixes) {
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
        ASSERT_TRUE(firstExpression(parsed).expression->program) << words.front();
        const auto imported = SctDocumentImporter::import(parsed);
        ASSERT_TRUE(imported.document);
        const auto& instruction = std::get<SctScriptSectionContent>(
            imported.document->sections.front().content).instructions.front();
        const auto& expression = std::get<SctCanonicalExpression>(instruction.fixedParameters.front().value);
        EXPECT_EQ(encodeSctCanonicalExpressionWords(expression), words) << words.front();
    }

    EXPECT_EQ(firstValue(SctExpressionFactory::floatLiteral(1.0f)).encodingWord, 0x04000000u);
    EXPECT_EQ(firstValue(SctExpressionFactory::encodedDecimalLiteral(1)).encodingWord, 0x08000100u);
    EXPECT_EQ(firstValue(*SctExpressionFactory::byteVariable(1).expression).encodingWord, 0x10000001u);
    EXPECT_EQ(firstValue(*SctExpressionFactory::bitVariable(1).expression).encodingWord, 0x20000001u);
    EXPECT_EQ(firstValue(*SctExpressionFactory::floatVariable(1).expression).encodingWord, 0x40000001u);
    EXPECT_EQ(firstValue(*SctExpressionFactory::floatBackedIntegerVariable(24).expression).encodingWord,
        0x50000018u);
}

TEST(SctScptV3, DynamicStackDepthIsAdvisoryOnly) {
    std::vector<std::uint32_t> words;
    for (std::uint32_t i = 0; i < 31u; ++i) words.push_back(0x08000000u | (i << 8u));
    for (std::uint32_t i = 1; i < 31u; ++i) words.push_back(0x0000000eu);
    words.push_back(0x0000001du);
    const auto parsed = SctParser{}.parse(makeExpressionFile(words));
    ASSERT_TRUE(parsed.parseOk);
    ASSERT_TRUE(firstExpression(parsed).expression->program);
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

TEST(SctScptV3, OpaqueExpressionStillRequiresACompleteLexicalBoundary) {
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
