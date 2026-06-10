#include "../SpiceSCT/SpiceSCT.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>

namespace {

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
