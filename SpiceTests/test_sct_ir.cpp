#include "../SpiceSCT/SpiceSCT.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kHeaderSize = 12;
constexpr std::size_t kIndexEntrySize = 0x14;
constexpr std::size_t kIndexNameOffset = 4;

spice::sct::SctInstruction instruction(
    std::uint32_t offset,
    std::uint16_t opcode,
    std::initializer_list<std::uint32_t> operands,
    std::uint32_t sizeBytes)
{
    spice::sct::SctInstruction result{};
    result.offset = offset;
    result.opcode = opcode;
    result.operands = operands;
    result.sizeBytes = sizeBytes;
    result.decodeOk = true;
    return result;
}

void writeU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
{
    bytes[offset + 0u] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[offset + 3u] = static_cast<std::uint8_t>(value & 0xffu);
}

void appendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    const auto offset = bytes.size();
    bytes.resize(offset + 4u);
    writeU32(bytes, offset, value);
}

void writeName(std::vector<std::uint8_t>& bytes, std::size_t offset, std::string name)
{
    for (std::size_t i = 0; i < 16u; ++i) {
        bytes[offset + i] = 0u;
    }
    for (std::size_t i = 0; i < name.size() && i < 16u; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>(name[i]);
    }
}

std::vector<std::uint8_t> makeSingleSectionSct(const std::vector<std::uint8_t>& section)
{
    std::vector<std::uint8_t> out(kHeaderSize + kIndexEntrySize, 0u);
    writeU32(out, 8u, 1u);
    writeName(out, kHeaderSize + kIndexNameOffset, "M00001");
    out.insert(out.end(), section.begin(), section.end());
    return out;
}

std::vector<std::uint8_t> makeScptAstFixture()
{
    std::vector<std::uint8_t> section{};

    appendU32(section, 16u);
    appendU32(section, 0x04000000u);
    appendU32(section, 0x3f800000u);
    appendU32(section, 0x04000000u);
    appendU32(section, 0x40000000u);
    appendU32(section, 0x00000015u);
    appendU32(section, 0x0000001du);

    appendU32(section, 16u);
    appendU32(section, 0x50000000u);
    appendU32(section, 0x0000001du);

    appendU32(section, 16u);
    appendU32(section, 0x5000000fu);
    appendU32(section, 0x0000001du);

    appendU32(section, 16u);
    appendU32(section, 0x40000002u);
    appendU32(section, 0x0000001du);

    appendU32(section, 16u);
    appendU32(section, 0x20000affu);
    appendU32(section, 0x0000001du);

    appendU32(section, 16u);
    appendU32(section, 0x10000003u);
    appendU32(section, 0x0000001du);

    appendU32(section, 16u);
    appendU32(section, 0x08000180u);
    appendU32(section, 0x0000001du);

    appendU32(section, 16u);
    appendU32(section, 0x7fffffffu);

    appendU32(section, 12u);
    return makeSingleSectionSct(section);
}

std::vector<std::uint8_t> makeCallSubscriptFixture()
{
    std::vector<std::uint8_t> section{};

    // The signed relative target is calculated from the word immediately before
    // the next instruction: payloadOffset + sizeBytes - 4 + operand.
    appendU32(section, 11u);
    appendU32(section, 12u);          // 0 + 8 - 4 + 12 = 16
    appendU32(section, 2u);
    appendU32(section, 2u);
    appendU32(section, 11u);
    appendU32(section, 0xffffffecu);  // 16 + 8 - 4 - 20 = 0
    appendU32(section, 12u);

    return makeSingleSectionSct(section);
}

spice::sct::SctParseResult makeLegacyStyleParseResult()
{
    spice::sct::SctSection section{};
    section.id.index = 0;
    section.id.name = "M00001";
    section.startOffset = 0;
    section.endOffset = 48;
    section.instructions = {
        instruction(0, 10, { 32 }, 8),
        instruction(32, 23, { 17 }, 8),
    };
    section.blocks = {
        spice::sct::SctBasicBlock{ .startOffset = 0, .endOffset = 8, .instructionOffsets = { 0 }, .successorOffsets = { 32 } },
        spice::sct::SctBasicBlock{ .startOffset = 32, .endOffset = 40, .instructionOffsets = { 32 }, .successorOffsets = {} },
    };
    section.unknownRegions = {
        spice::sct::SctUnknownRegion{ .startOffset = 8, .endOffset = 32, .rawBytes = { 0xde, 0xad }, .reason = "not reached by control flow" },
    };

    spice::sct::SctParseResult result{};
    result.file.sourcePath = "C:/fixtures/example.sct";
    result.file.sections.push_back(std::move(section));
    result.parseOk = true;
    return result;
}

bool hasEdge(const spice::sct::SctSection& section, spice::sct::SctEdgeType type)
{
    return std::any_of(section.edges.begin(), section.edges.end(),
        [type](const auto& edge) { return edge.type == type; });
}

bool isControlTargetParam(const spice::sct::SctOpcodeParamPattern& pattern, std::size_t parameterIndex)
{
    if (pattern.jumpParam >= 0 && parameterIndex == static_cast<std::size_t>(pattern.jumpParam)) {
        return true;
    }
    if (pattern.switchJumpParam < 0 || pattern.loopStartParam < 0 || pattern.loopEndParam < pattern.loopStartParam) {
        return false;
    }
    const auto loopWidth = static_cast<std::size_t>(pattern.loopEndParam - pattern.loopStartParam + 1);
    return parameterIndex >= static_cast<std::size_t>(pattern.switchJumpParam)
        && ((parameterIndex - static_cast<std::size_t>(pattern.switchJumpParam)) % loopWidth) == 0u;
}

std::vector<spice::sct::SctParameter> parametersForPattern(
    std::uint16_t opcode,
    const spice::sct::SctOpcodeParamPattern& pattern)
{
    std::uint32_t totalSlots = pattern.paramCount;
    if (pattern.loopStartParam >= 0 && pattern.loopEndParam >= pattern.loopStartParam
        && pattern.iterationCountParam >= 0) {
        const auto loopWidth = static_cast<std::uint32_t>(pattern.loopEndParam - pattern.loopStartParam + 1);
        totalSlots += loopWidth;
    }

    std::vector<spice::sct::SctParameter> result{};
    result.reserve(totalSlots);
    for (std::uint32_t i = 0; i < totalSlots; ++i) {
        spice::sct::SctParameter parameter{};
        parameter.index = i;
        parameter.valueKind = spice::sct::SctParameterValueKind::Integer;
        parameter.rawWords.push_back(100000u + static_cast<std::uint32_t>(opcode) * 100u + i);
        if (pattern.iterationCountParam >= 0 && i == static_cast<std::uint32_t>(pattern.iterationCountParam)) {
            parameter.rawWords.front() = 2u;
        }
        result.push_back(std::move(parameter));
    }
    return result;
}

spice::sct::SctParseResult makeSingleInstructionParseResult(
    std::uint16_t opcode,
    std::vector<spice::sct::SctParameter> parameters)
{
    spice::sct::SctInstruction inst{};
    inst.offset = 0;
    inst.opcode = opcode;
    inst.sizeBytes = static_cast<std::uint32_t>(4u + (parameters.size() * 4u));
    inst.decodeOk = true;
    inst.parameters = std::move(parameters);
    inst.operands.reserve(inst.parameters.size());
    inst.rawWords.push_back(opcode);
    for (const auto& parameter : inst.parameters) {
        const auto word = parameter.rawWords.empty() ? 0u : parameter.rawWords.front();
        inst.operands.push_back(word);
        inst.rawWords.push_back(word);
    }

    spice::sct::SctSection section{};
    section.id.index = 0;
    section.id.name = "M00001";
    section.kind = spice::sct::SctSectionKind::Script;
    section.startOffset = 0;
    section.endOffset = inst.sizeBytes;
    section.instructions.push_back(std::move(inst));

    spice::sct::SctParseResult result{};
    result.file.sourcePath = "C:/fixtures/opcode_table.sct";
    result.file.sections.push_back(std::move(section));
    result.parseOk = true;
    return result;
}

} // namespace

TEST(SctIr, MetadataNamesKnownControlAndResourceOpcodes)
{
    const auto jump = spice::sct::sctOpcodeMetadata(10);
    EXPECT_EQ("Jump", jump.mnemonic);
    EXPECT_EQ(spice::sct::SctSemanticConfidence::Known, jump.confidence);
    EXPECT_EQ(spice::sct::SctOpcodeControlRole::Jump, jump.controlRole);

    const auto loadMld = spice::sct::sctOpcodeMetadata(23);
    EXPECT_EQ("LoadMld", loadMld.mnemonic);
    EXPECT_EQ(spice::sct::SctSemanticConfidence::Partial, loadMld.confidence);
    EXPECT_EQ(spice::sct::SctOpcodeResourceRole::LoadsMld, loadMld.resourceRole);

    const auto unknown = spice::sct::sctOpcodeMetadata(999);
    EXPECT_TRUE(unknown.mnemonic.empty());
    EXPECT_EQ(spice::sct::SctSemanticConfidence::Unknown, unknown.confidence);
}

TEST(SctIr, BuilderEnrichesLegacyParseResults)
{
    const auto ir = spice::sct::SctIrBuilder{}.build(makeLegacyStyleParseResult());
    ASSERT_EQ(1u, ir.file.sections.size());

    const auto& section = ir.file.sections.front();
    EXPECT_EQ(spice::sct::SctSectionKind::Script, section.kind);
    ASSERT_EQ(1u, section.rawSpans.size());
    EXPECT_EQ(spice::sct::SctRawSpanReason::Unreached, section.rawSpans.front().reason);
    EXPECT_EQ(8u, section.rawSpans.front().startOffset);
    EXPECT_EQ(32u, section.rawSpans.front().endOffset);

    ASSERT_EQ(2u, section.instructions.size());
    EXPECT_EQ("Jump", section.instructions[0].mnemonic);
    ASSERT_EQ(1u, section.instructions[0].parameters.size());
    EXPECT_EQ("offset", section.instructions[0].parameters.front().role);
    EXPECT_EQ(spice::sct::SctParameterValueKind::Link, section.instructions[0].parameters.front().valueKind);

    EXPECT_EQ("LoadMld", section.instructions[1].mnemonic);
    ASSERT_EQ(1u, section.instructions[1].parameters.size());
    EXPECT_EQ("mldRef", section.instructions[1].parameters.front().role);
    EXPECT_EQ(spice::sct::SctParameterValueKind::ResourceRef, section.instructions[1].parameters.front().valueKind);

    EXPECT_TRUE(hasEdge(section, spice::sct::SctEdgeType::Jump));
    EXPECT_TRUE(hasEdge(section, spice::sct::SctEdgeType::LoadsMld));
}

TEST(SctIr, JsonExporterEmitsSharedSchemaAndSemanticFields)
{
    const auto ir = spice::sct::SctIrBuilder{}.build(makeLegacyStyleParseResult());
    const std::string json = spice::sct::SctJsonExporter{}.toJson(ir);

    EXPECT_NE(std::string::npos, json.find("\"schema\": \"spice_sct_ir_v1\""));
    EXPECT_NE(std::string::npos, json.find("\"kind\": \"script\""));
    EXPECT_NE(std::string::npos, json.find("\"mnemonic\":\"Jump\""));
    EXPECT_NE(std::string::npos, json.find("\"valueKind\":\"link\""));
    EXPECT_NE(std::string::npos, json.find("\"type\":\"loads_mld\""));
    EXPECT_NE(std::string::npos, json.find("\"reason\":\"unreached\""));
}

TEST(SctIr, SemanticComparerUsesSalsaParamPatternsForKnownOpcodeParameters)
{
    std::size_t checkedOpcodes = 0;
    for (std::uint16_t opcode = 0; opcode < spice::sct::kSalsaOpcodeParamPatterns.size(); ++opcode) {
        const auto& pattern = spice::sct::kSalsaOpcodeParamPatterns[opcode];
        auto lhsParameters = parametersForPattern(opcode, pattern);
        if (lhsParameters.empty()) {
            continue;
        }

        std::optional<std::size_t> changedParameterIndex{};
        for (std::size_t i = 0; i < lhsParameters.size(); ++i) {
            if (!isControlTargetParam(pattern, lhsParameters[i].index)) {
                changedParameterIndex = i;
                break;
            }
        }
        if (!changedParameterIndex.has_value()) {
            continue;
        }

        auto rhsParameters = lhsParameters;
        rhsParameters[*changedParameterIndex].rawWords.front() += 1u;

        const auto lhs = makeSingleInstructionParseResult(opcode, std::move(lhsParameters));
        const auto rhs = makeSingleInstructionParseResult(opcode, std::move(rhsParameters));
        const auto comparison = spice::sct::SctSemanticComparer{}.compare(lhs, rhs);
        EXPECT_FALSE(comparison.equivalent) << "opcode " << opcode;
        ++checkedOpcodes;
    }

    EXPECT_GT(checkedOpcodes, 200u);
}

TEST(SctIr, ParserBuildsSalsaScptAstFamilies)
{
    const auto parsed = spice::sct::SctParser{}.parse(makeScptAstFixture(), "scpt_ast.sct");
    ASSERT_TRUE(parsed.parseOk);
    ASSERT_EQ(1u, parsed.file.sections.size());

    const auto& instructions = parsed.file.sections.front().instructions;
    ASSERT_GE(instructions.size(), 9u);

    const auto& arithmetic = instructions[0].parameters.front().expression;
    ASSERT_TRUE(arithmetic.has_value());
    ASSERT_TRUE(arithmetic->ast.has_value());
    EXPECT_TRUE(arithmetic->hitStopCode);
    EXPECT_EQ(spice::sct::SctScptAstNodeKind::ArithmeticOp, arithmetic->ast->kind);
    EXPECT_EQ("+", arithmetic->ast->op);
    ASSERT_EQ(2u, arithmetic->ast->children.size());
    EXPECT_EQ(spice::sct::SctScptAstNodeKind::FloatLiteral, arithmetic->ast->children[0].kind);
    EXPECT_EQ(spice::sct::SctScptAstNodeKind::FloatLiteral, arithmetic->ast->children[1].kind);
    const auto lhsLiteral = arithmetic->ast->children[0].numericLiteral();
    const auto rhsLiteral = arithmetic->ast->children[1].numericLiteral();
    ASSERT_TRUE(lhsLiteral.has_value());
    ASSERT_TRUE(rhsLiteral.has_value());
    EXPECT_EQ(spice::sct::SctNumericLiteralEncoding::Float32, lhsLiteral->encoding);
    EXPECT_EQ(spice::sct::SctNumericLiteralEncoding::Float32, rhsLiteral->encoding);
    EXPECT_DOUBLE_EQ(1.0, lhsLiteral->value);
    EXPECT_DOUBLE_EQ(2.0, rhsLiteral->value);
    EXPECT_FALSE(arithmetic->ast->numericLiteral().has_value());

    const auto& secondary = instructions[1].parameters.front().expression;
    ASSERT_TRUE(secondary.has_value());
    ASSERT_TRUE(secondary->ast.has_value());
    EXPECT_EQ(spice::sct::SctScptAstNodeKind::SecondaryValue, secondary->ast->kind);
    EXPECT_EQ("Gold", secondary->ast->display);

    const auto& intVariable = instructions[2].parameters.front().expression;
    ASSERT_TRUE(intVariable.has_value());
    ASSERT_TRUE(intVariable->ast.has_value());
    EXPECT_EQ(spice::sct::SctScptAstNodeKind::IntVariable, intVariable->ast->kind);

    const auto& floatVariable = instructions[3].parameters.front().expression;
    ASSERT_TRUE(floatVariable.has_value());
    ASSERT_TRUE(floatVariable->ast.has_value());
    EXPECT_EQ(spice::sct::SctScptAstNodeKind::FloatVariable, floatVariable->ast->kind);

    const auto& bitVariable = instructions[4].parameters.front().expression;
    ASSERT_TRUE(bitVariable.has_value());
    ASSERT_TRUE(bitVariable->ast.has_value());
    EXPECT_EQ(spice::sct::SctScptAstNodeKind::BitVariable, bitVariable->ast->kind);

    const auto& byteVariable = instructions[5].parameters.front().expression;
    ASSERT_TRUE(byteVariable.has_value());
    ASSERT_TRUE(byteVariable->ast.has_value());
    EXPECT_EQ(spice::sct::SctScptAstNodeKind::ByteVariable, byteVariable->ast->kind);

    const auto& decimal = instructions[6].parameters.front().expression;
    ASSERT_TRUE(decimal.has_value());
    ASSERT_TRUE(decimal->ast.has_value());
    EXPECT_EQ(spice::sct::SctScptAstNodeKind::DecimalLiteral, decimal->ast->kind);
    EXPECT_EQ("decimal: 1+128/256", decimal->ast->display);
    const auto decimalLiteral = decimal->ast->numericLiteral();
    ASSERT_TRUE(decimalLiteral.has_value());
    EXPECT_EQ(spice::sct::SctNumericLiteralEncoding::Decimal16_8, decimalLiteral->encoding);
    EXPECT_DOUBLE_EQ(1.5, decimalLiteral->value);

    const auto& noLoop = instructions[7].parameters.front().expression;
    ASSERT_TRUE(noLoop.has_value());
    ASSERT_TRUE(noLoop->ast.has_value());
    EXPECT_EQ(spice::sct::SctScptAstNodeKind::NoLoopValue, noLoop->ast->kind);
}

TEST(SctIr, ParserResolvesCallSubscriptOperandsAsSignedRelativePayloadTargets)
{
    const auto parsed = spice::sct::SctParser{}.parse(makeCallSubscriptFixture(), "call_subscript.sct");
    ASSERT_TRUE(parsed.parseOk);
    ASSERT_EQ(1u, parsed.file.sections.size());

    std::vector<const spice::sct::SctEdge*> callEdges{};
    for (const auto& edge : parsed.file.sections.front().edges) {
        if (edge.type == spice::sct::SctEdgeType::CallSubscript) {
            callEdges.push_back(&edge);
        }
    }
    ASSERT_EQ(2u, callEdges.size());

    ASSERT_TRUE(callEdges[0]->fromPayloadOffset.has_value());
    ASSERT_TRUE(callEdges[0]->toPayloadOffset.has_value());
    ASSERT_TRUE(callEdges[0]->toOffset.has_value());
    EXPECT_EQ(0u, *callEdges[0]->fromPayloadOffset);
    EXPECT_EQ(16u, *callEdges[0]->toPayloadOffset);
    EXPECT_EQ(16u, *callEdges[0]->toOffset);
    EXPECT_EQ("12", callEdges[0]->attributes.at("signed_offset_operand"));

    ASSERT_TRUE(callEdges[1]->fromPayloadOffset.has_value());
    ASSERT_TRUE(callEdges[1]->toPayloadOffset.has_value());
    ASSERT_TRUE(callEdges[1]->toOffset.has_value());
    EXPECT_EQ(16u, *callEdges[1]->fromPayloadOffset);
    EXPECT_EQ(0u, *callEdges[1]->toPayloadOffset);
    EXPECT_EQ(0u, *callEdges[1]->toOffset);
    EXPECT_EQ("-20", callEdges[1]->attributes.at("signed_offset_operand"));
}

TEST(SctIr, JsonExporterEmitsScptAst)
{
    const auto parsed = spice::sct::SctParser{}.parse(makeScptAstFixture(), "scpt_ast.sct");
    const auto json = spice::sct::SctJsonExporter{}.toJson(parsed);

    EXPECT_NE(std::string::npos, json.find("\"hitStopCode\":true"));
    EXPECT_NE(std::string::npos, json.find("\"kind\":\"arithmetic_op\""));
    EXPECT_NE(std::string::npos, json.find("\"kind\":\"secondary_value\""));
    EXPECT_NE(std::string::npos, json.find("\"kind\":\"decimal_literal\""));
}
