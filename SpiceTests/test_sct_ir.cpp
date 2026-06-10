#include "../SpiceSCT/SpiceSCT.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
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
