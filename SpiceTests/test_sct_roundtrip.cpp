#include "../SpiceSCT/SpiceSCT.h"
#include "../Compression/Aklz.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kHeaderSize = 12;
constexpr std::size_t kIndexEntrySize = 0x14;
constexpr std::size_t kIndexNameOffset = 4;

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

std::vector<std::uint8_t> makeScriptWithGarbage()
{
    std::vector<std::uint8_t> section{};
    appendU32(section, 10u);
    appendU32(section, 12u);
    appendU32(section, 0xdeadbeefu);
    appendU32(section, 0xcafebabeu);
    appendU32(section, 12u);
    return section;
}

std::vector<std::uint8_t> makeReturnOnlyScript()
{
    std::vector<std::uint8_t> section{};
    appendU32(section, 12u);
    return section;
}

std::vector<std::uint8_t> makeStringSection()
{
    std::vector<std::uint8_t> section{};
    appendU32(section, 9u);
    appendU32(section, 0x1du);
    appendU32(section, 999u);
    return section;
}

std::vector<std::uint8_t> makeBranchAndSwitchScript()
{
    std::vector<std::uint8_t> section{};

    appendU32(section, 0u);
    appendU32(section, 0x1du);
    appendU32(section, 8u);
    appendU32(section, 12u);
    appendU32(section, 3u);
    appendU32(section, 0x1du);
    appendU32(section, 2u);
    appendU32(section, 111u);
    appendU32(section, 12u);
    appendU32(section, 222u);
    appendU32(section, 4u);
    appendU32(section, 12u);
    return section;
}

void appendOpcode16Float(std::vector<std::uint8_t>& section, std::uint32_t floatWord)
{
    appendU32(section, 16u);
    appendU32(section, 0x04000000u);
    appendU32(section, floatWord);
    appendU32(section, 0x1du);
}

void appendLabel(std::vector<std::uint8_t>& section, std::uint32_t floatWord)
{
    appendU32(section, 9u);
    appendU32(section, 0x04000000u);
    appendU32(section, floatWord);
    appendU32(section, 0x1du);
}

void appendScheduledPrefix(std::vector<std::uint8_t>& section)
{
    appendU32(section, 129u);
    appendU32(section, 0x04000000u);
    appendU32(section, 0x41200000u);
    appendU32(section, 0x1du);
    appendU32(section, 16u);
}

void appendScheduledArithmeticPrefix(std::vector<std::uint8_t>& section)
{
    appendU32(section, 129u);
    appendU32(section, 0x04000000u);
    appendU32(section, 0x41200000u);
    appendU32(section, 0x04000000u);
    appendU32(section, 0x40000000u);
    appendU32(section, 21u);
    appendU32(section, 0x1du);
    appendU32(section, 16u);
}

std::vector<std::uint8_t> makeSkipRefreshScript()
{
    std::vector<std::uint8_t> section{};
    appendU32(section, 13u);
    appendOpcode16Float(section, 0x3f800000u);
    appendU32(section, 12u);
    return section;
}

std::vector<std::uint8_t> makeScheduledScript()
{
    std::vector<std::uint8_t> section{};
    appendScheduledPrefix(section);
    appendOpcode16Float(section, 0x3f800000u);
    appendU32(section, 12u);
    return section;
}

std::vector<std::uint8_t> makeComplexScheduledScript()
{
    std::vector<std::uint8_t> section{};
    appendScheduledArithmeticPrefix(section);
    appendOpcode16Float(section, 0x3f800000u);
    appendU32(section, 12u);
    return section;
}

std::vector<std::uint8_t> makeCombinedModifierScript()
{
    std::vector<std::uint8_t> section{};
    appendU32(section, 13u);
    appendScheduledPrefix(section);
    appendOpcode16Float(section, 0x3f800000u);
    appendU32(section, 12u);
    return section;
}

std::vector<std::uint8_t> makeSwitchWithMultiwordScptSelector()
{
    std::vector<std::uint8_t> section{};
    appendU32(section, 3u);
    appendU32(section, 0x5000000fu);
    appendU32(section, 0x1du);
    appendU32(section, 2u);
    appendU32(section, 111u);
    appendU32(section, 12u);
    appendU32(section, 222u);
    appendU32(section, 4u);
    appendU32(section, 12u);
    return section;
}

std::vector<std::uint8_t> makeOpcode119LoopScript(std::uint32_t iterations)
{
    std::vector<std::uint8_t> section{};
    appendU32(section, 119u);
    appendU32(section, 0x04000000u);
    appendU32(section, 0u);
    appendU32(section, 0x1du);
    appendU32(section, iterations);
    if (iterations != 0x00010000u) {
        for (std::uint32_t i = 0; i < iterations; ++i) {
            appendU32(section, 0x04000000u);
            appendU32(section, 0x44976000u + (i * 0x2000u));
            appendU32(section, 0x1du);
        }
    }
    appendU32(section, 12u);
    return section;
}

std::vector<std::uint8_t> makeCrossRowJumpFirstSection()
{
    std::vector<std::uint8_t> section{};
    appendLabel(section, 0x3f800000u);
    appendU32(section, 10u);
    appendU32(section, 4u);
    return section;
}

std::vector<std::uint8_t> makeLabelReturnSection(std::uint32_t labelFloatWord = 0x40000000u)
{
    std::vector<std::uint8_t> section{};
    appendLabel(section, labelFloatWord);
    appendU32(section, 12u);
    return section;
}

std::vector<std::uint8_t> makeCrossRowSwitchFirstSection()
{
    std::vector<std::uint8_t> section{};
    appendLabel(section, 0x3f800000u);
    appendU32(section, 3u);
    appendU32(section, 0x5000000fu);
    appendU32(section, 0x1du);
    appendU32(section, 1u);
    appendU32(section, 111u);
    appendU32(section, 4u);
    return section;
}

std::vector<std::uint8_t> makeOverlapTargetScript()
{
    std::vector<std::uint8_t> section{};
    appendLabel(section, 0x3f800000u);
    appendOpcode16Float(section, 0x434c0000u);
    appendU32(section, 10u);
    appendU32(section, 0xfffffff8u);
    return section;
}

std::vector<std::uint8_t> makeSct(std::span<const std::vector<std::uint8_t>> sections)
{
    std::vector<std::uint8_t> out(kHeaderSize + (sections.size() * kIndexEntrySize), 0u);
    writeU32(out, 8u, static_cast<std::uint32_t>(sections.size()));

    std::uint32_t sectionStart = 0;
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const auto rowOffset = kHeaderSize + (i * kIndexEntrySize);
        writeU32(out, rowOffset, sectionStart);
        writeName(out, rowOffset + kIndexNameOffset, "M0000" + std::to_string(i + 1u));
        sectionStart += static_cast<std::uint32_t>(sections[i].size());
    }

    for (const auto& section : sections) {
        out.insert(out.end(), section.begin(), section.end());
    }
    return out;
}

std::vector<std::uint8_t> makeRoundTripFixture()
{
    const std::vector<std::vector<std::uint8_t>> sections = {
        makeScriptWithGarbage(),
        makeReturnOnlyScript(),
        makeStringSection(),
    };
    return makeSct(sections);
}

} // namespace

TEST(SctRoundTrip, CanonicalExportDropsUnreachableGarbageButPreservesSemantics)
{
    const auto originalBytes = makeRoundTripFixture();
    const auto original = spice::sct::SctParser{}.parse(originalBytes, "fixture.sct");
    ASSERT_TRUE(original.parseOk);
    ASSERT_EQ(3u, original.file.sections.size());
    EXPECT_EQ(2u, original.file.sections[0].instructions.size());
    EXPECT_FALSE(original.file.sections[0].rawSpans.empty());

    const auto exported = spice::sct::SctBinaryExporter{}.exportFile(original);
    EXPECT_NE(originalBytes, exported);

    const auto reparsed = spice::sct::SctParser{}.parse(exported, "fixture.exported.sct");
    ASSERT_TRUE(reparsed.parseOk);
    ASSERT_EQ(3u, reparsed.file.sections.size());
    EXPECT_EQ(2u, reparsed.file.sections[0].instructions.size());
    EXPECT_TRUE(reparsed.file.sections[0].rawSpans.empty());

    const auto comparison = spice::sct::SctSemanticComparer{}.compare(original, reparsed);
    EXPECT_TRUE(comparison.equivalent);
}

TEST(SctRoundTrip, EveryScriptSectionIsExportedAsPotentialEntryPoint)
{
    const auto original = spice::sct::SctParser{}.parse(makeRoundTripFixture(), "fixture.sct");
    const auto exported = spice::sct::SctBinaryExporter{}.exportFile(original);
    const auto reparsed = spice::sct::SctParser{}.parse(exported, "fixture.exported.sct");

    ASSERT_TRUE(reparsed.parseOk);
    ASSERT_EQ(3u, reparsed.file.sections.size());
    EXPECT_EQ(spice::sct::SctSectionKind::Script, reparsed.file.sections[0].kind);
    EXPECT_EQ(spice::sct::SctSectionKind::Script, reparsed.file.sections[1].kind);
    EXPECT_EQ(1u, reparsed.file.sections[1].instructions.size());
    EXPECT_EQ(12u, reparsed.file.sections[1].instructions.front().opcode);
}

TEST(SctRoundTrip, PreserveModeReturnsOriginalBytesForDiagnostics)
{
    const auto originalBytes = makeRoundTripFixture();
    const auto original = spice::sct::SctParser{}.parse(originalBytes, "fixture.sct");

    spice::sct::SctExportOptions options{};
    options.mode = spice::sct::SctExportMode::PreserveBytesForTest;
    const auto preserved = spice::sct::SctBinaryExporter{}.exportFile(original, options);

    EXPECT_EQ(originalBytes, preserved);
}

TEST(SctRoundTrip, CanonicalExportRewritesBranchAndSwitchTargetsSemantically)
{
    const std::vector<std::vector<std::uint8_t>> sections = {
        makeBranchAndSwitchScript(),
    };
    const auto original = spice::sct::SctParser{}.parse(makeSct(sections), "branch_switch.sct");
    ASSERT_TRUE(original.parseOk);

    const auto exported = spice::sct::SctBinaryExporter{}.exportFile(original);
    const auto reparsed = spice::sct::SctParser{}.parse(exported, "branch_switch.exported.sct");
    ASSERT_TRUE(reparsed.parseOk);

    const auto comparison = spice::sct::SctSemanticComparer{}.compare(original, reparsed);
    EXPECT_TRUE(comparison.equivalent);
}

TEST(SctRoundTrip, ParserFoldsSkipRefreshIntoFollowingInstructionMetadata)
{
    const std::vector<std::vector<std::uint8_t>> sections = {
        makeSkipRefreshScript(),
    };
    const auto parsed = spice::sct::SctParser{}.parse(makeSct(sections), "skip_refresh.sct");
    ASSERT_TRUE(parsed.parseOk);

    const auto& instructions = parsed.file.sections.front().instructions;
    ASSERT_EQ(2u, instructions.size());
    EXPECT_EQ(16u, instructions[0].opcode);
    EXPECT_TRUE(instructions[0].skipRefresh);
    EXPECT_FALSE(instructions[0].scheduled.present);
    EXPECT_EQ(1u, instructions[0].opcodeWordIndex);
    EXPECT_EQ(20u, instructions[0].sizeBytes);
    ASSERT_FALSE(instructions[0].rawWords.empty());
    EXPECT_EQ(13u, instructions[0].rawWords.front());
    EXPECT_EQ(12u, instructions[1].opcode);
}

TEST(SctRoundTrip, ParserFoldsScheduledInstructionIntoFollowingInstructionMetadata)
{
    const std::vector<std::vector<std::uint8_t>> sections = {
        makeScheduledScript(),
    };
    const auto parsed = spice::sct::SctParser{}.parse(makeSct(sections), "scheduled.sct");
    ASSERT_TRUE(parsed.parseOk);

    const auto& instructions = parsed.file.sections.front().instructions;
    ASSERT_EQ(2u, instructions.size());
    EXPECT_EQ(16u, instructions[0].opcode);
    EXPECT_FALSE(instructions[0].skipRefresh);
    EXPECT_TRUE(instructions[0].scheduled.present);
    EXPECT_EQ(5u, instructions[0].opcodeWordIndex);
    EXPECT_EQ(36u, instructions[0].sizeBytes);
    EXPECT_EQ(16u, instructions[0].scheduled.instructionByteLength);
    EXPECT_EQ(3u, instructions[0].scheduled.frameDelay.rawWords.size());
    EXPECT_EQ(12u, instructions[1].opcode);
}

TEST(SctRoundTrip, ScheduledFrameDelayIsFirstClassScptAst)
{
    const std::vector<std::vector<std::uint8_t>> sections = {
        makeComplexScheduledScript(),
    };
    const auto parsed = spice::sct::SctParser{}.parse(makeSct(sections), "complex_scheduled.sct");
    ASSERT_TRUE(parsed.parseOk);

    const auto& instruction = parsed.file.sections.front().instructions.front();
    ASSERT_TRUE(instruction.scheduled.present);
    const auto& frameDelay = instruction.scheduled.frameDelay;
    EXPECT_EQ(spice::sct::SctParameterValueKind::Expression, frameDelay.valueKind);
    ASSERT_TRUE(frameDelay.expression.has_value());
    ASSERT_TRUE(frameDelay.expression->ast.has_value());
    EXPECT_TRUE(frameDelay.expression->hitStopCode);
    EXPECT_EQ(spice::sct::SctScptAstNodeKind::ArithmeticOp, frameDelay.expression->ast->kind);
    EXPECT_EQ("+", frameDelay.expression->ast->op);
    ASSERT_EQ(2u, frameDelay.expression->ast->children.size());

    const auto json = spice::sct::SctJsonExporter{}.toJson(parsed);
    EXPECT_NE(std::string::npos, json.find("\"frameDelay\":{\"index\":0"));
    EXPECT_NE(std::string::npos, json.find("\"kind\":\"arithmetic_op\""));
    EXPECT_NE(std::string::npos, json.find("\"op\":\"+\""));
}

TEST(SctRoundTrip, CanonicalExportPreservesFoldedInstructionModifierSemantics)
{
    const std::vector<std::vector<std::uint8_t>> sections = {
        makeCombinedModifierScript(),
    };
    const auto original = spice::sct::SctParser{}.parse(makeSct(sections), "combined_modifiers.sct");
    ASSERT_TRUE(original.parseOk);

    const auto& originalInstructions = original.file.sections.front().instructions;
    ASSERT_EQ(2u, originalInstructions.size());
    EXPECT_EQ(16u, originalInstructions[0].opcode);
    EXPECT_TRUE(originalInstructions[0].skipRefresh);
    EXPECT_TRUE(originalInstructions[0].scheduled.present);
    EXPECT_EQ(6u, originalInstructions[0].opcodeWordIndex);
    EXPECT_EQ(40u, originalInstructions[0].sizeBytes);

    const auto exported = spice::sct::SctBinaryExporter{}.exportFile(original);
    const auto reparsed = spice::sct::SctParser{}.parse(exported, "combined_modifiers.exported.sct");
    ASSERT_TRUE(reparsed.parseOk);

    const auto& reparsedInstructions = reparsed.file.sections.front().instructions;
    ASSERT_EQ(2u, reparsedInstructions.size());
    EXPECT_EQ(16u, reparsedInstructions[0].opcode);
    EXPECT_TRUE(reparsedInstructions[0].skipRefresh);
    EXPECT_TRUE(reparsedInstructions[0].scheduled.present);

    const auto comparison = spice::sct::SctSemanticComparer{}.compare(original, reparsed);
    EXPECT_TRUE(comparison.equivalent);
}

TEST(SctRoundTrip, SwitchTargetsUseLogicalParametersAfterMultiwordScptSelector)
{
    const std::vector<std::vector<std::uint8_t>> sections = {
        makeSwitchWithMultiwordScptSelector(),
    };
    const auto original = spice::sct::SctParser{}.parse(makeSct(sections), "switch_multiword_scpt.sct");
    ASSERT_TRUE(original.parseOk);

    const auto& section = original.file.sections.front();
    ASSERT_EQ(2u, section.instructions.size());
    EXPECT_EQ(3u, section.instructions[0].opcode);
    EXPECT_EQ(12u, section.instructions[1].opcode);
    std::vector<const spice::sct::SctEdge*> switchEdges{};
    for (const auto& edge : section.edges) {
        if (edge.type == spice::sct::SctEdgeType::SwitchCase) {
            switchEdges.push_back(&edge);
        }
    }
    ASSERT_EQ(2u, switchEdges.size());
    ASSERT_TRUE(switchEdges[0]->toOffset.has_value());
    ASSERT_TRUE(switchEdges[1]->toOffset.has_value());
    EXPECT_EQ(32u, *switchEdges[0]->toOffset);
    EXPECT_EQ(32u, *switchEdges[1]->toOffset);

    const auto exported = spice::sct::SctBinaryExporter{}.exportFile(original);
    const auto reparsed = spice::sct::SctParser{}.parse(exported, "switch_multiword_scpt.exported.sct");
    ASSERT_TRUE(reparsed.parseOk);

    const auto comparison = spice::sct::SctSemanticComparer{}.compare(original, reparsed);
    EXPECT_TRUE(comparison.equivalent);
}

TEST(SctRoundTrip, Opcode119RepeatsLoopParameterByIterationCount)
{
    const std::vector<std::vector<std::uint8_t>> sections = {
        makeOpcode119LoopScript(2u),
    };
    const auto original = spice::sct::SctParser{}.parse(makeSct(sections), "opcode119_loop.sct");
    ASSERT_TRUE(original.parseOk);

    const auto& instructions = original.file.sections.front().instructions;
    ASSERT_EQ(2u, instructions.size());
    EXPECT_EQ(119u, instructions[0].opcode);
    EXPECT_EQ(44u, instructions[0].sizeBytes);
    ASSERT_EQ(4u, instructions[0].parameters.size());
    EXPECT_EQ(spice::sct::SctParameterValueKind::Expression, instructions[0].parameters[2].valueKind);
    EXPECT_EQ(spice::sct::SctParameterValueKind::Expression, instructions[0].parameters[3].valueKind);
    EXPECT_EQ(12u, instructions[1].opcode);
    EXPECT_EQ(44u, instructions[1].offset);

    const auto exported = spice::sct::SctBinaryExporter{}.exportFile(original);
    const auto reparsed = spice::sct::SctParser{}.parse(exported, "opcode119_loop.exported.sct");
    ASSERT_TRUE(reparsed.parseOk);

    const auto comparison = spice::sct::SctSemanticComparer{}.compare(original, reparsed);
    EXPECT_TRUE(comparison.equivalent);
}

TEST(SctRoundTrip, Opcode119ExternalBreakSkipsLoopBody)
{
    const std::vector<std::vector<std::uint8_t>> sections = {
        makeOpcode119LoopScript(0x00010000u),
    };
    const auto parsed = spice::sct::SctParser{}.parse(makeSct(sections), "opcode119_external_break.sct");
    ASSERT_TRUE(parsed.parseOk);

    const auto& instructions = parsed.file.sections.front().instructions;
    ASSERT_EQ(2u, instructions.size());
    EXPECT_EQ(119u, instructions[0].opcode);
    EXPECT_EQ(20u, instructions[0].sizeBytes);
    ASSERT_EQ(2u, instructions[0].parameters.size());
    EXPECT_EQ(12u, instructions[1].opcode);
    EXPECT_EQ(20u, instructions[1].offset);
}

TEST(SctRoundTrip, CodeRegionFallsThroughAcrossIndexRows)
{
    std::vector<std::uint8_t> first{};
    appendLabel(first, 0x3f800000u);
    appendOpcode16Float(first, 0x40000000u);

    const std::vector<std::vector<std::uint8_t>> sections = {
        first,
        makeLabelReturnSection(),
    };
    const auto parsed = spice::sct::SctParser{}.parse(makeSct(sections), "cross_row_fallthrough.sct");
    ASSERT_TRUE(parsed.parseOk);
    ASSERT_EQ(2u, parsed.file.sections.size());

    ASSERT_EQ(2u, parsed.file.sections[0].instructions.size());
    EXPECT_EQ(9u, parsed.file.sections[0].instructions[0].opcode);
    EXPECT_EQ(0u, parsed.file.sections[0].instructions[0].payloadOffset);
    EXPECT_EQ(16u, parsed.file.sections[0].instructions[1].payloadOffset);

    ASSERT_EQ(2u, parsed.file.sections[1].instructions.size());
    EXPECT_EQ(9u, parsed.file.sections[1].instructions[0].opcode);
    EXPECT_EQ(32u, parsed.file.sections[1].instructions[0].payloadOffset);
    EXPECT_EQ(12u, parsed.file.sections[1].instructions[1].opcode);

    ASSERT_GE(parsed.file.codeRegions.size(), 2u);
    EXPECT_EQ(0u, parsed.file.codeRegions[0].entryPayloadOffset);
    EXPECT_EQ(std::vector<std::uint32_t>({0u, 16u, 32u, 48u}), parsed.file.codeRegions[0].instructionPayloadOffsets);
    EXPECT_EQ(std::vector<std::uint32_t>({0u, 1u}), parsed.file.codeRegions[0].coveredSectionIndexes);
    EXPECT_EQ(32u, parsed.file.codeRegions[1].entryPayloadOffset);
}

TEST(SctRoundTrip, CrossRowJumpUsesPayloadTargetWithoutClamping)
{
    const std::vector<std::vector<std::uint8_t>> sections = {
        makeCrossRowJumpFirstSection(),
        makeLabelReturnSection(),
    };
    const auto parsed = spice::sct::SctParser{}.parse(makeSct(sections), "cross_row_jump.sct");
    ASSERT_TRUE(parsed.parseOk);

    const auto& first = parsed.file.sections[0];
    ASSERT_EQ(2u, first.instructions.size());
    EXPECT_EQ(10u, first.instructions[1].opcode);
    ASSERT_FALSE(first.edges.empty());
    const auto jump = std::find_if(first.edges.begin(), first.edges.end(), [](const auto& edge) {
        return edge.type == spice::sct::SctEdgeType::Jump;
    });
    ASSERT_NE(first.edges.end(), jump);
    ASSERT_TRUE(jump->toPayloadOffset.has_value());
    EXPECT_EQ(24u, *jump->toPayloadOffset);
    ASSERT_TRUE(jump->toOffset.has_value());
    EXPECT_EQ(0u, *jump->toOffset);
    EXPECT_EQ("1", jump->attributes.at("target_section_index"));

    const auto exported = spice::sct::SctBinaryExporter{}.exportFile(parsed);
    const auto reparsed = spice::sct::SctParser{}.parse(exported, "cross_row_jump.exported.sct");
    ASSERT_TRUE(reparsed.parseOk);
    const auto comparison = spice::sct::SctSemanticComparer{}.compare(parsed, reparsed);
    EXPECT_TRUE(comparison.equivalent);
}

TEST(SctRoundTrip, CrossRowSwitchUsesPayloadTargetWithoutClamping)
{
    const std::vector<std::vector<std::uint8_t>> sections = {
        makeCrossRowSwitchFirstSection(),
        makeLabelReturnSection(),
    };
    const auto parsed = spice::sct::SctParser{}.parse(makeSct(sections), "cross_row_switch.sct");
    ASSERT_TRUE(parsed.parseOk);

    const auto& first = parsed.file.sections[0];
    const auto switchEdge = std::find_if(first.edges.begin(), first.edges.end(), [](const auto& edge) {
        return edge.type == spice::sct::SctEdgeType::SwitchCase;
    });
    ASSERT_NE(first.edges.end(), switchEdge);
    ASSERT_TRUE(switchEdge->toPayloadOffset.has_value());
    EXPECT_EQ(40u, *switchEdge->toPayloadOffset);
    ASSERT_TRUE(switchEdge->toOffset.has_value());
    EXPECT_EQ(0u, *switchEdge->toOffset);
    EXPECT_EQ("1", switchEdge->attributes.at("target_section_index"));

    const auto exported = spice::sct::SctBinaryExporter{}.exportFile(parsed);
    const auto reparsed = spice::sct::SctParser{}.parse(exported, "cross_row_switch.exported.sct");
    ASSERT_TRUE(reparsed.parseOk);
    const auto& reparsedFirst = reparsed.file.sections[0];
    const auto reparsedSwitchEdge = std::find_if(reparsedFirst.edges.begin(), reparsedFirst.edges.end(), [](const auto& edge) {
        return edge.type == spice::sct::SctEdgeType::SwitchCase;
    });
    ASSERT_NE(reparsedFirst.edges.end(), reparsedSwitchEdge);
    ASSERT_TRUE(reparsedSwitchEdge->toPayloadOffset.has_value());
    EXPECT_EQ(40u, *reparsedSwitchEdge->toPayloadOffset);
}

TEST(SctRoundTrip, TargetInsideExistingInstructionDoesNotDecodeOverlap)
{
    const std::vector<std::vector<std::uint8_t>> sections = {
        makeOverlapTargetScript(),
    };
    const auto parsed = spice::sct::SctParser{}.parse(makeSct(sections), "overlap_target.sct");
    ASSERT_TRUE(parsed.parseOk);

    const auto& instructions = parsed.file.sections.front().instructions;
    ASSERT_EQ(3u, instructions.size());
    EXPECT_EQ(9u, instructions[0].opcode);
    EXPECT_EQ(16u, instructions[1].opcode);
    EXPECT_EQ(10u, instructions[2].opcode);
    EXPECT_TRUE(std::none_of(instructions.begin(), instructions.end(), [](const auto& inst) {
        return inst.payloadOffset == 28u;
    }));
    EXPECT_TRUE(std::any_of(parsed.diagnostics.begin(), parsed.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.message.find("inside an already decoded instruction") != std::string::npos;
    }));
}

TEST(SctRoundTrip, MissingIndexLabelIsDiagnosticOnly)
{
    const std::vector<std::vector<std::uint8_t>> sections = {
        makeReturnOnlyScript(),
    };
    const auto parsed = spice::sct::SctParser{}.parse(makeSct(sections), "missing_label.sct");
    ASSERT_TRUE(parsed.parseOk);
    ASSERT_EQ(1u, parsed.file.sections.front().instructions.size());
    EXPECT_EQ(12u, parsed.file.sections.front().instructions.front().opcode);
    EXPECT_TRUE(std::any_of(parsed.diagnostics.begin(), parsed.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.message.find("does not start with label opcode 9") != std::string::npos;
    }));
}

TEST(SctRoundTrip, CompressedInputCanExportCanonicalUncompressedBytes)
{
    const auto originalBytes = makeRoundTripFixture();
    const auto compressed = spice::compression::aklz::compress(originalBytes);
    ASSERT_TRUE(compressed.ok()) << spice::compression::aklz::errorToString(compressed.error);

    const auto original = spice::sct::SctParser{}.parse(compressed.bytes, "fixture.sct");
    ASSERT_TRUE(original.parseOk);
    EXPECT_TRUE(original.file.originalCompressedAklz);

    const auto exported = spice::sct::SctBinaryExporter{}.exportFile(original);
    EXPECT_FALSE(spice::compression::aklz::isAklz(exported));

    const auto reparsed = spice::sct::SctParser{}.parse(exported, "fixture.exported.sct");
    const auto comparison = spice::sct::SctSemanticComparer{}.compare(original, reparsed);
    EXPECT_TRUE(comparison.equivalent);
}
