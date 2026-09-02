#include "../SpiceSCT/SpiceSCT.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <ranges>
#include <set>
#include <vector>

namespace {

using namespace spice::sct;

SctDocumentInstruction ordinary(SctDocument& document, const std::uint16_t opcode = 125u) {
    return {document.allocateInstructionId(), opcode};
}

SctDocumentInstruction branch(SctDocument& document, const SctInstructionId falseTarget) {
    auto result = ordinary(document, 0u);
    result.fixedParameters = {
        {0u, SctExpressionFactory::encodedDecimalLiteral(0)},
        {1u, SctInstructionReference{falseTarget}},
    };
    return result;
}

SctDocumentInstruction jump(SctDocument& document, const SctInstructionId target) {
    auto result = ordinary(document, 10u);
    result.fixedParameters = {{0u, SctInstructionReference{target}}};
    return result;
}

SctDocumentInstruction returnInstruction(SctDocument& document) {
    return ordinary(document, 12u);
}

SctDocumentInstruction switchInstruction(SctDocument& document,
    const std::vector<std::pair<std::int32_t, SctInstructionId>>& cases) {
    auto result = ordinary(document, 3u);
    result.fixedParameters = {
        {0u, SctExpressionFactory::encodedDecimalLiteral(0)},
    };
    for (const auto& [value, target] : cases) {
        result.repeatedParameterGroups.push_back({{{2u,
            SctEncodedWordValue{static_cast<std::uint32_t>(value)}},
            {3u, SctInstructionReference{target}}}});
    }
    return result;
}

SctDocument makeDocument(std::vector<SctDocumentInstruction> instructions) {
    SctDocument document;
    const auto section = document.allocateSectionId();
    document.sections.push_back({section, "test", SctScriptSectionContent{std::move(instructions)}});
    return document;
}

SctStructuredControlFlowAnalysis analyze(const SctDocument& document) {
    const auto validation = SctDocumentValidator::validateDocument(document);
    EXPECT_TRUE(validation.validDocument);
    return SctStructuredControlFlowAnalysis::build(document);
}

struct ImportedGapFixture final {
    SctDocument document;
    SctBoundImportEvidence evidence;
    SctInstructionId loopHeader;
    SctInstructionId historicalJump;
};

ImportedGapFixture importedBackwardGapFixture() {
    SctParseResult parsed;
    parsed.parseOk = true;
    parsed.file.detectedEndian = "big";
    parsed.file.originalPayloadBytes.resize(60u, 0u);
    parsed.file.originalPayloadBytes[11] = 1u;
    parsed.file.originalPayloadBytes[16] = 'M';
    parsed.file.originalPayloadBytes[17] = 'A';
    parsed.file.originalPayloadBytes[18] = 'I';
    parsed.file.originalPayloadBytes[19] = 'N';
    std::fill(parsed.file.originalPayloadBytes.begin() + 36,
        parsed.file.originalPayloadBytes.begin() + 52, 0xacu);

    SctInstruction header;
    header.offset = 0u;
    header.payloadOffset = 0u;
    header.opcode = 125u;
    header.rawWords = {125u};
    header.sizeBytes = 4u;
    header.decodeOk = true;

    SctInstruction backward;
    backward.offset = 20u;
    backward.payloadOffset = 20u;
    backward.opcode = 10u;
    backward.rawWords = {10u, 0xffffffe8u};
    backward.parameters = {{0u, "offset", SctParameterValueKind::Link,
        SctSemanticConfidence::Known, {0xffffffe8u}}};
    backward.sizeBytes = 8u;
    backward.decodeOk = true;

    SctInstruction middle;
    middle.offset = 12u;
    middle.payloadOffset = 12u;
    middle.opcode = 125u;
    middle.rawWords = {125u};
    middle.sizeBytes = 4u;
    middle.decodeOk = true;

    SctSection section;
    section.id = {0u, "MAIN"};
    section.startOffset = 32u;
    section.endOffset = 60u;
    section.kind = SctSectionKind::Script;
    section.instructions = {header, middle, backward};
    section.edges.push_back({SctEdgeType::Jump, SctSemanticConfidence::Known,
        20u, 0u, 20u, 0u, 10u, "jump"});
    parsed.file.sections.push_back(std::move(section));

    auto imported = SctDocumentImporter::import(parsed);
    EXPECT_TRUE(imported.document.has_value());
    auto evidence = imported.context.bind(imported.context.revisionProvenance());
    EXPECT_TRUE(evidence.has_value());
    auto document = std::move(*imported.document);
    const auto& instructions = std::get<SctScriptSectionContent>(
        document.sections.front().content).instructions;
    EXPECT_EQ(instructions.size(), 3u);
    const auto loopHeader = instructions[0].id;
    const auto historicalJump = instructions[2].id;
    return {std::move(document), std::move(*evidence),
        loopHeader, historicalJump};
}

const SctStructuredRegion* regionOf(
    const SctStructuredControlFlowAnalysis& analysis, const SctStructuredRegionKind kind) {
    for (const auto& section : analysis.sections()) {
        const auto found = std::ranges::find(section.regions, kind,
            [](const auto& region) { return region.id.kind; });
        if (found != section.regions.end()) return &*found;
    }
    return nullptr;
}

TEST(SctStructuredControlFlow, EmptyAndNonScriptDocumentsProduceNoSections) {
    SctDocument document;
    document.sections.push_back({document.allocateSectionId(), "label", SctLabelSectionContent{}});
    EXPECT_TRUE(analyze(document).sections().empty());
}

TEST(SctStructuredControlFlow, BuildsStableBasicBlocksAndRetainsUnreachableInstructions) {
    SctDocument document;
    const auto first = document.allocateInstructionId();
    const auto unreachable = document.allocateInstructionId();
    const auto target = document.allocateInstructionId();
    document.sections.push_back({document.allocateSectionId(), "test", SctScriptSectionContent{{
        {first, 10u, false, std::nullopt, {{0u, SctInstructionReference{target}}}},
        {unreachable, 125u},
        {target, 12u},
    }}});
    const auto analysis = analyze(document);
    ASSERT_EQ(analysis.sections().size(), 1u);
    ASSERT_EQ(analysis.sections()[0].blocks.size(), 3u);
    EXPECT_TRUE(analysis.sections()[0].blocks[0].reachable);
    EXPECT_FALSE(analysis.sections()[0].blocks[1].reachable);
    EXPECT_TRUE(analysis.sections()[0].blocks[2].reachable);
    EXPECT_EQ(analysis.blockContaining(unreachable)->id.entryInstruction, unreachable);
}

TEST(SctStructuredControlFlow, CallsRemainInsideMaximalBlocksAndRetainTransfers) {
    SctDocument document;
    const auto callId = document.allocateInstructionId();
    const auto nextId = document.allocateInstructionId();
    const auto targetId = document.allocateInstructionId();
    auto call = SctDocumentInstruction{callId, 11u};
    call.fixedParameters = {{0u, SctInstructionReference{targetId}}};
    document.sections.push_back({document.allocateSectionId(), "test",
        SctScriptSectionContent{{call, {nextId, 125u}, {targetId, 12u}}}});
    const auto analysis = analyze(document);
    ASSERT_EQ(analysis.sections().size(), 1u);
    ASSERT_EQ(analysis.sections()[0].blocks.size(), 2u);
    EXPECT_EQ(analysis.sections()[0].blocks[0].instructions,
        (std::vector<SctInstructionId>{callId, nextId}));
    const auto& transfers = analysis.sections()[0].blocks[0].transfers;
    EXPECT_TRUE(std::ranges::any_of(transfers, [callId, nextId](const auto& edge) {
        return edge.sourceInstruction == callId
            && edge.kind == SctControlFlowKind::Fallthrough
            && edge.targetInstruction == std::optional{nextId};
    }));
    EXPECT_TRUE(std::ranges::any_of(transfers, [targetId](const auto& edge) {
        return edge.kind == SctControlFlowKind::Call
            && edge.targetInstruction == std::optional{targetId};
    }));
    ASSERT_EQ(analysis.sections()[0].entryPoints.size(), 2u);
    EXPECT_EQ(analysis.sections()[0].entryPoints[1].kind,
        SctSectionEntryKind::SameSectionCallTarget);
}

TEST(SctStructuredControlFlow, SameSectionCallTargetsCreateIndependentReachableRoots) {
    SctDocument document;
    const auto callId = document.allocateInstructionId();
    const auto continuationId = document.allocateInstructionId();
    const auto targetId = document.allocateInstructionId();
    const auto targetReturnId = document.allocateInstructionId();
    auto call = SctDocumentInstruction{callId, 11u};
    call.fixedParameters = {{0u, SctInstructionReference{targetId}}};
    document.sections.push_back({document.allocateSectionId(), "test",
        SctScriptSectionContent{{call, {continuationId, 12u},
            {targetId, 125u}, {targetReturnId, 12u}}}});

    const auto analysis = analyze(document);
    ASSERT_EQ(analysis.sections().size(), 1u);
    const auto& section = analysis.sections().front();
    ASSERT_EQ(section.blocks.size(), 2u);
    EXPECT_TRUE(section.blocks[0].reachable);
    EXPECT_TRUE(section.blocks[1].reachable);
    ASSERT_EQ(section.entryPoints.size(), 2u);
    EXPECT_EQ(section.entryPoints[0].kind, SctSectionEntryKind::PhysicalSectionStart);
    EXPECT_EQ(section.entryPoints[1].instruction, targetId);
    EXPECT_EQ(section.entryPoints[1].kind, SctSectionEntryKind::SameSectionCallTarget);
    EXPECT_TRUE(section.regions.empty());
}

TEST(SctStructuredControlFlow, CrossSectionCallInsideIfDoesNotInvalidateLocalRegion) {
    SctDocument document;
    const auto ifId = document.allocateInstructionId();
    const auto callId = document.allocateInstructionId();
    const auto bodyId = document.allocateInstructionId();
    const auto jumpId = document.allocateInstructionId();
    const auto joinId = document.allocateInstructionId();
    const auto remoteId = document.allocateInstructionId();
    auto conditional = branch(document, joinId);
    conditional.id = ifId;
    auto call = SctDocumentInstruction{callId, 11u};
    call.fixedParameters = {{0u, SctInstructionReference{remoteId}}};
    auto exit = SctDocumentInstruction{jumpId, 10u};
    exit.fixedParameters = {{0u, SctInstructionReference{joinId}}};
    document.sections.push_back({document.allocateSectionId(), "main",
        SctScriptSectionContent{{conditional, call, {bodyId, 125u}, exit, {joinId, 12u}}}});
    document.sections.push_back({document.allocateSectionId(), "sub",
        SctScriptSectionContent{{{remoteId, 12u}}}});

    const auto analysis = analyze(document);
    const auto* region = regionOf(analysis, SctStructuredRegionKind::If);
    ASSERT_NE(region, nullptr);
    EXPECT_EQ(region->controllerSite,
        (SctParameterSite{ifId, {0u, std::nullopt}}));
    EXPECT_EQ(region->minimumEdgeConfidence, SctSemanticConfidence::Known);
    EXPECT_TRUE(std::ranges::none_of(region->evidence, [callId, remoteId](const auto& evidence) {
        return evidence.source == std::optional{callId}
            && evidence.target == std::optional{remoteId};
    }));
    ASSERT_EQ(analysis.sections().size(), 2u);
    EXPECT_TRUE(std::ranges::any_of(analysis.sections()[1].entryPoints,
        [remoteId](const auto& entry) {
            return entry.instruction == remoteId
                && entry.kind == SctSectionEntryKind::CrossSectionCallTarget;
        }));
    EXPECT_TRUE(std::ranges::none_of(analysis.sections()[0].issues, [](const auto& issue) {
        return issue.kind == SctStructureIssueKind::CrossSectionNonCallControlFlow;
    }));
    const auto* callBlock = analysis.blockContaining(callId);
    ASSERT_NE(callBlock, nullptr);
    EXPECT_TRUE(std::ranges::any_of(callBlock->transfers,
        [callId, remoteId](const auto& transfer) {
            return transfer.sourceInstruction == callId
                && transfer.kind == SctControlFlowKind::Call
                && transfer.targetInstruction == std::optional{remoteId}
                && !transfer.targetBlock.has_value();
        }));
}

TEST(SctStructuredControlFlow, ClosedBranchArmDoesNotProducePostdominatorJoin) {
    SctDocument document;
    const auto branchId = document.allocateInstructionId();
    const auto exitId = document.allocateInstructionId();
    const auto closedId = document.allocateInstructionId();
    auto conditional = branch(document, closedId);
    conditional.id = branchId;
    auto closed = jump(document, closedId);
    closed.id = closedId;
    document.sections.push_back({document.allocateSectionId(), "test",
        SctScriptSectionContent{{conditional, {exitId, 12u}, closed}}});

    const auto analysis = analyze(document);
    EXPECT_EQ(regionOf(analysis, SctStructuredRegionKind::If), nullptr);
    EXPECT_EQ(regionOf(analysis, SctStructuredRegionKind::IfElse), nullptr);
    EXPECT_TRUE(std::ranges::any_of(analysis.sections()[0].issues, [](const auto& issue) {
        return issue.kind == SctStructureIssueKind::AmbiguousJoin
            && issue.rejectionReason
                == SctStructuredRejectionReason::ClosedComponentWithoutExit;
    }));
}

TEST(SctStructuredControlFlow, CrossSectionCallInsideWhileDoesNotInvalidateLocalRegion) {
    SctDocument document;
    const auto headerId = document.allocateInstructionId();
    const auto callId = document.allocateInstructionId();
    const auto backId = document.allocateInstructionId();
    const auto exitId = document.allocateInstructionId();
    const auto remoteId = document.allocateInstructionId();
    auto conditional = branch(document, exitId);
    conditional.id = headerId;
    auto call = SctDocumentInstruction{callId, 11u};
    call.fixedParameters = {{0u, SctInstructionReference{remoteId}}};
    auto back = jump(document, headerId);
    back.id = backId;
    document.sections.push_back({document.allocateSectionId(), "main",
        SctScriptSectionContent{{conditional, call, back, {exitId, 12u}}}});
    document.sections.push_back({document.allocateSectionId(), "sub",
        SctScriptSectionContent{{{remoteId, 12u}}}});

    const auto analysis = analyze(document);
    const auto* region = regionOf(analysis, SctStructuredRegionKind::While);
    ASSERT_NE(region, nullptr);
    EXPECT_EQ(region->minimumEdgeConfidence, SctSemanticConfidence::Known);
}

TEST(SctStructuredControlFlow, CrossSectionCallInsideSwitchDoesNotInvalidateLocalRegion) {
    SctDocument document;
    const auto selectorId = document.allocateInstructionId();
    const auto firstCaseId = document.allocateInstructionId();
    const auto callId = document.allocateInstructionId();
    const auto firstExitId = document.allocateInstructionId();
    const auto secondCaseId = document.allocateInstructionId();
    const auto secondExitId = document.allocateInstructionId();
    const auto joinId = document.allocateInstructionId();
    const auto remoteId = document.allocateInstructionId();
    auto selector = switchInstruction(document, {{1, firstCaseId}, {2, secondCaseId}});
    selector.id = selectorId;
    auto call = SctDocumentInstruction{callId, 11u};
    call.fixedParameters = {{0u, SctInstructionReference{remoteId}}};
    auto firstExit = jump(document, joinId);
    firstExit.id = firstExitId;
    auto secondExit = jump(document, joinId);
    secondExit.id = secondExitId;
    document.sections.push_back({document.allocateSectionId(), "main", SctScriptSectionContent{{
        selector, {firstCaseId, 125u}, call, firstExit,
        {secondCaseId, 125u}, secondExit, {joinId, 12u},
    }}});
    document.sections.push_back({document.allocateSectionId(), "sub",
        SctScriptSectionContent{{{remoteId, 12u}}}});

    const auto analysis = analyze(document);
    const auto* region = regionOf(analysis, SctStructuredRegionKind::Switch);
    ASSERT_NE(region, nullptr);
    EXPECT_EQ(region->minimumEdgeConfidence, SctSemanticConfidence::Known);
}

TEST(SctStructuredControlFlow, RecognizesLegacyIfWithoutElsePattern) {
    SctDocument document;
    const auto ifId = document.allocateInstructionId();
    const auto bodyId = document.allocateInstructionId();
    const auto jumpId = document.allocateInstructionId();
    const auto joinId = document.allocateInstructionId();
    document.sections.push_back({document.allocateSectionId(), "test", SctScriptSectionContent{{
        [&] { auto value = SctDocumentInstruction{ifId, 0u}; value.fixedParameters = {
            {0u, SctExpressionFactory::encodedDecimalLiteral(0)},
            {1u, SctInstructionReference{joinId}}}; return value; }(),
        {bodyId, 125u},
        [&] { auto value = SctDocumentInstruction{jumpId, 10u}; value.fixedParameters = {
            {0u, SctInstructionReference{joinId}}}; return value; }(),
        {joinId, 12u},
    }}});
    const auto analysis = analyze(document);
    const auto* region = regionOf(analysis, SctStructuredRegionKind::If);
    ASSERT_NE(region, nullptr);
    EXPECT_EQ(region->id.headerInstruction, ifId);
    ASSERT_EQ(region->arms.size(), 1u);
    EXPECT_EQ(region->arms[0].kind, SctStructuredArmKind::Then);
    EXPECT_TRUE(std::ranges::any_of(region->evidence, [](const auto& evidence) {
        return evidence.kind == SctStructureEvidenceKind::PreTargetJump;
    }));
}

TEST(SctStructuredControlFlow, RecognizesLegacyIfElseAndCommonForwardExit) {
    SctDocument document;
    const auto ifId = document.allocateInstructionId();
    const auto thenId = document.allocateInstructionId();
    const auto jumpId = document.allocateInstructionId();
    const auto elseId = document.allocateInstructionId();
    const auto joinId = document.allocateInstructionId();
    auto conditional = SctDocumentInstruction{ifId, 0u};
    conditional.fixedParameters = {{0u, SctExpressionFactory::encodedDecimalLiteral(0)},
        {1u, SctInstructionReference{elseId}}};
    auto exit = SctDocumentInstruction{jumpId, 10u};
    exit.fixedParameters = {{0u, SctInstructionReference{joinId}}};
    document.sections.push_back({document.allocateSectionId(), "test", SctScriptSectionContent{{
        conditional, {thenId, 125u}, exit, {elseId, 125u}, {joinId, 12u},
    }}});
    const auto analysis = analyze(document);
    const auto* region = regionOf(analysis, SctStructuredRegionKind::IfElse);
    ASSERT_NE(region, nullptr);
    ASSERT_EQ(region->arms.size(), 2u);
    EXPECT_EQ(region->arms[0].kind, SctStructuredArmKind::Then);
    EXPECT_EQ(region->arms[1].kind, SctStructuredArmKind::Else);
    EXPECT_EQ(region->join->entryInstruction, joinId);
}

TEST(SctStructuredControlFlow, RecognizesBackwardJumpAsWhile) {
    SctDocument document;
    const auto ifId = document.allocateInstructionId();
    const auto bodyId = document.allocateInstructionId();
    const auto jumpId = document.allocateInstructionId();
    const auto exitId = document.allocateInstructionId();
    auto conditional = SctDocumentInstruction{ifId, 0u};
    conditional.fixedParameters = {{0u, SctExpressionFactory::encodedDecimalLiteral(0)},
        {1u, SctInstructionReference{exitId}}};
    auto back = SctDocumentInstruction{jumpId, 10u};
    back.fixedParameters = {{0u, SctInstructionReference{ifId}}};
    document.sections.push_back({document.allocateSectionId(), "test", SctScriptSectionContent{{
        conditional, {bodyId, 125u}, back, {exitId, 12u},
    }}});
    const auto analysis = analyze(document);
    const auto* region = regionOf(analysis, SctStructuredRegionKind::While);
    ASSERT_NE(region, nullptr);
    EXPECT_EQ(region->join->entryInstruction, exitId);
    EXPECT_TRUE(std::ranges::any_of(region->evidence, [](const auto& evidence) {
        return evidence.kind == SctStructureEvidenceKind::BackwardTerminatorJump;
    }));
}

TEST(SctStructuredControlFlow, RecognizesSwitchCasesSharedTargetsAndCommonExit) {
    SctDocument document;
    const auto switchId = document.allocateInstructionId();
    const auto caseOne = document.allocateInstructionId();
    const auto jumpOne = document.allocateInstructionId();
    const auto caseTwo = document.allocateInstructionId();
    const auto jumpTwo = document.allocateInstructionId();
    const auto join = document.allocateInstructionId();
    auto selector = SctDocumentInstruction{switchId, 3u};
    selector.fixedParameters = {{0u, SctExpressionFactory::encodedDecimalLiteral(0)}};
    selector.repeatedParameterGroups = {
        {{{2u, SctEncodedWordValue{1u}}, {3u, SctInstructionReference{caseOne}}}},
        {{{2u, SctEncodedWordValue{2u}}, {3u, SctInstructionReference{caseOne}}}},
        {{{2u, SctEncodedWordValue{3u}}, {3u, SctInstructionReference{caseTwo}}}},
    };
    auto firstExit = SctDocumentInstruction{jumpOne, 10u};
    firstExit.fixedParameters = {{0u, SctInstructionReference{join}}};
    auto secondExit = SctDocumentInstruction{jumpTwo, 10u};
    secondExit.fixedParameters = {{0u, SctInstructionReference{join}}};
    document.sections.push_back({document.allocateSectionId(), "test", SctScriptSectionContent{{
        selector, {caseOne, 125u}, firstExit, {caseTwo, 125u}, secondExit, {join, 12u},
    }}});
    const auto analysis = analyze(document);
    const auto* region = regionOf(analysis, SctStructuredRegionKind::Switch);
    ASSERT_NE(region, nullptr);
    ASSERT_EQ(region->arms.size(), 2u);
    EXPECT_EQ(region->arms[0].caseLabels.size(), 2u);
    EXPECT_EQ(region->arms[0].caseLabels[0].value, 1);
    EXPECT_EQ(region->arms[0].caseLabels[1].value, 2);
    EXPECT_EQ(region->controllerSite,
        (SctParameterSite{switchId, {0u, std::nullopt}}));
    EXPECT_EQ(region->arms[0].caseLabels[0].targetSite,
        (SctParameterSite{switchId, {3u, 0u}}));
    EXPECT_EQ(region->arms[0].caseLabels[1].targetSite,
        (SctParameterSite{switchId, {3u, 1u}}));
    EXPECT_EQ(region->join->entryInstruction, join);
    EXPECT_TRUE(std::ranges::any_of(region->evidence, [](const auto& evidence) {
        return evidence.kind == SctStructureEvidenceKind::CommonForwardExit;
    }));
}

TEST(SctStructuredControlFlow, AcceptsOnlyGraphConfirmedAdjacentCaseFallthrough) {
    SctDocument document;
    const auto switchId = document.allocateInstructionId();
    const auto caseOne = document.allocateInstructionId();
    const auto caseTwo = document.allocateInstructionId();
    const auto join = document.allocateInstructionId();
    auto selector = switchInstruction(document, {{1, caseOne}, {2, caseTwo}});
    selector.id = switchId;
    document.sections.push_back({document.allocateSectionId(), "test", SctScriptSectionContent{{
        selector, {caseOne, 125u}, {caseTwo, 125u}, {join, 12u},
    }}});
    const auto analysis = analyze(document);
    const auto* region = regionOf(analysis, SctStructuredRegionKind::Switch);
    ASSERT_NE(region, nullptr);
    ASSERT_EQ(region->arms.size(), 2u);
    EXPECT_TRUE(std::ranges::any_of(region->evidence, [](const auto& evidence) {
        return evidence.kind == SctStructureEvidenceKind::CaseFallthrough
            && evidence.source.has_value() && evidence.target.has_value();
    }));
}

TEST(SctStructuredControlFlow, RejectsSwitchWhoseCasesHaveDifferentTerminalExits) {
    SctDocument document;
    const auto switchId = document.allocateInstructionId();
    const auto caseOne = document.allocateInstructionId();
    const auto jumpOne = document.allocateInstructionId();
    const auto caseTwo = document.allocateInstructionId();
    const auto jumpTwo = document.allocateInstructionId();
    const auto exitOne = document.allocateInstructionId();
    const auto exitTwo = document.allocateInstructionId();
    auto selector = switchInstruction(document, {{1, caseOne}, {2, caseTwo}});
    selector.id = switchId;
    auto firstJump = SctDocumentInstruction{jumpOne, 10u};
    firstJump.fixedParameters = {{0u, SctInstructionReference{exitOne}}};
    auto secondJump = SctDocumentInstruction{jumpTwo, 10u};
    secondJump.fixedParameters = {{0u, SctInstructionReference{exitTwo}}};
    document.sections.push_back({document.allocateSectionId(), "test", SctScriptSectionContent{{
        selector, {caseOne, 125u}, firstJump, {caseTwo, 125u}, secondJump,
        {exitOne, 12u}, {exitTwo, 12u},
    }}});
    const auto analysis = analyze(document);
    EXPECT_EQ(regionOf(analysis, SctStructuredRegionKind::Switch), nullptr);
    EXPECT_TRUE(std::ranges::any_of(analysis.sections()[0].issues, [](const auto& issue) {
        return issue.kind == SctStructureIssueKind::AmbiguousSwitchCases;
    }));
}

TEST(SctStructuredControlFlow, RejectsExplicitCrossCaseJumpAsFallthrough) {
    SctDocument document;
    const auto switchId = document.allocateInstructionId();
    const auto caseOne = document.allocateInstructionId();
    const auto crossJump = document.allocateInstructionId();
    const auto caseTwo = document.allocateInstructionId();
    const auto join = document.allocateInstructionId();
    auto selector = switchInstruction(document, {{1, caseOne}, {2, caseTwo}});
    selector.id = switchId;
    auto cross = SctDocumentInstruction{crossJump, 10u};
    cross.fixedParameters = {{0u, SctInstructionReference{caseTwo}}};
    document.sections.push_back({document.allocateSectionId(), "test", SctScriptSectionContent{{
        selector, {caseOne, 125u}, cross, {caseTwo, 125u}, {join, 12u},
    }}});
    const auto analysis = analyze(document);
    EXPECT_EQ(regionOf(analysis, SctStructuredRegionKind::Switch), nullptr);
}

TEST(SctStructuredControlFlow, NestsConditionalWithinSwitch) {
    SctDocument document;
    const auto switchId = document.allocateInstructionId();
    const auto nestedIf = document.allocateInstructionId();
    const auto nestedBody = document.allocateInstructionId();
    const auto nestedJoin = document.allocateInstructionId();
    const auto caseTwo = document.allocateInstructionId();
    const auto caseTwoExit = document.allocateInstructionId();
    const auto join = document.allocateInstructionId();
    auto selector = SctDocumentInstruction{switchId, 3u};
    selector.fixedParameters = {{0u, SctExpressionFactory::encodedDecimalLiteral(0)}};
    selector.repeatedParameterGroups = {
        {{{2u, SctEncodedWordValue{1u}}, {3u, SctInstructionReference{nestedIf}}}},
        {{{2u, SctEncodedWordValue{2u}}, {3u, SctInstructionReference{caseTwo}}}},
    };
    auto conditional = SctDocumentInstruction{nestedIf, 0u};
    conditional.fixedParameters = {{0u, SctExpressionFactory::encodedDecimalLiteral(0)},
        {1u, SctInstructionReference{nestedJoin}}};
    auto firstExit = SctDocumentInstruction{nestedJoin, 10u};
    firstExit.fixedParameters = {{0u, SctInstructionReference{join}}};
    auto secondExit = SctDocumentInstruction{caseTwoExit, 10u};
    secondExit.fixedParameters = {{0u, SctInstructionReference{join}}};
    document.sections.push_back({document.allocateSectionId(), "test", SctScriptSectionContent{{
        selector, conditional, {nestedBody, 125u}, firstExit,
        {caseTwo, 125u}, secondExit, {join, 12u},
    }}});
    const auto analysis = analyze(document);
    const auto* switchRegion = regionOf(analysis, SctStructuredRegionKind::Switch);
    const auto* ifRegion = regionOf(analysis, SctStructuredRegionKind::If);
    ASSERT_NE(switchRegion, nullptr);
    ASSERT_NE(ifRegion, nullptr);
    EXPECT_EQ(ifRegion->parent, switchRegion->id);
}

TEST(SctStructuredControlFlow, NestsWhileLoopInsideFinalSwitchCase) {
    SctDocument document;
    const auto switchId = document.allocateInstructionId();
    const auto caseOne = document.allocateInstructionId();
    const auto caseOneExit = document.allocateInstructionId();
    const auto loopHeader = document.allocateInstructionId();
    const auto loopBody = document.allocateInstructionId();
    const auto loopBack = document.allocateInstructionId();
    const auto join = document.allocateInstructionId();
    auto selector = switchInstruction(document, {{1, caseOne}, {2, loopHeader}});
    selector.id = switchId;
    auto firstExit = SctDocumentInstruction{caseOneExit, 10u};
    firstExit.fixedParameters = {{0u, SctInstructionReference{join}}};
    auto conditional = SctDocumentInstruction{loopHeader, 0u};
    conditional.fixedParameters = {{0u, SctExpressionFactory::encodedDecimalLiteral(0)},
        {1u, SctInstructionReference{join}}};
    auto back = SctDocumentInstruction{loopBack, 10u};
    back.fixedParameters = {{0u, SctInstructionReference{loopHeader}}};
    document.sections.push_back({document.allocateSectionId(), "test", SctScriptSectionContent{{
        selector, {caseOne, 125u}, firstExit, conditional, {loopBody, 125u}, back,
        {join, 12u},
    }}});
    const auto analysis = analyze(document);
    const auto* switchRegion = regionOf(analysis, SctStructuredRegionKind::Switch);
    const auto* whileRegion = regionOf(analysis, SctStructuredRegionKind::While);
    ASSERT_NE(switchRegion, nullptr);
    ASSERT_NE(whileRegion, nullptr);
    EXPECT_EQ(whileRegion->parent, switchRegion->id);
}

TEST(SctStructuredControlFlow, ReportsMultipleEntryIrreducibleCycleWithoutGroupingIt) {
    SctDocument document;
    const auto branchId = document.allocateInstructionId();
    const auto left = document.allocateInstructionId();
    const auto right = document.allocateInstructionId();
    auto choose = SctDocumentInstruction{branchId, 0u};
    choose.fixedParameters = {{0u, SctExpressionFactory::encodedDecimalLiteral(0)},
        {1u, SctInstructionReference{right}}};
    auto leftJump = SctDocumentInstruction{left, 10u};
    leftJump.fixedParameters = {{0u, SctInstructionReference{right}}};
    auto rightJump = SctDocumentInstruction{right, 10u};
    rightJump.fixedParameters = {{0u, SctInstructionReference{left}}};
    document.sections.push_back({document.allocateSectionId(), "test",
        SctScriptSectionContent{{choose, leftJump, rightJump}}});
    const auto analysis = analyze(document);
    EXPECT_TRUE(std::ranges::any_of(analysis.sections()[0].issues, [](const auto& issue) {
        return issue.kind == SctStructureIssueKind::IrreducibleCycle;
    }));
    EXPECT_TRUE(analysis.sections()[0].regions.empty());
}

TEST(SctStructuredControlFlow, OpaqueGapHintProducesOnlyHistoricalCandidate) {
    auto fixture = importedBackwardGapFixture();
    auto& instructions = std::get<SctScriptSectionContent>(
        fixture.document.sections.front().content).instructions;
    auto& historicalSource = instructions[2];
    historicalSource.opcode = 125u;
    historicalSource.fixedParameters.clear();
    historicalSource.repeatedParameterGroups.clear();
    const auto analysis = SctStructuredControlFlowAnalysis::build(
        fixture.document, &fixture.evidence);
    ASSERT_EQ(analysis.sections().size(), 1u);
    EXPECT_TRUE(analysis.sections()[0].regions.empty());
    ASSERT_EQ(analysis.sections()[0].historicalCandidates.size(), 1u);
    const auto& candidate = analysis.sections()[0].historicalCandidates.front();
    EXPECT_EQ(candidate.suggestedKind, SctStructuredRegionKind::NaturalLoop);
    EXPECT_EQ(candidate.evidenceConfidence, SctSemanticConfidence::Heuristic);
    EXPECT_TRUE(std::ranges::any_of(candidate.evidence, [](const auto& evidence) {
        return evidence.kind == SctStructureEvidenceKind::ImportedOpaqueControlFlowGap
            && evidence.opaqueAttachments.size() >= 2u;
    }));
}

TEST(SctStructuredControlFlow, CurrentEdgeWinsWhenImportedOpaqueEvidenceConflicts) {
    auto fixture = importedBackwardGapFixture();
    auto& instructions = std::get<SctScriptSectionContent>(
        fixture.document.sections.front().content).instructions;
    auto& jump = instructions[2];
    jump.fixedParameters = {{0u, SctInstructionReference{jump.id}}};
    const auto analysis = SctStructuredControlFlowAnalysis::build(
        fixture.document, &fixture.evidence);
    ASSERT_EQ(analysis.sections().size(), 1u);
    EXPECT_TRUE(std::ranges::any_of(analysis.sections()[0].issues, [](const auto& issue) {
        return issue.kind == SctStructureIssueKind::HistoricalEdgeConflict;
    }));
    ASSERT_EQ(analysis.sections()[0].historicalCandidates.size(), 1u);
    EXPECT_EQ(analysis.sections()[0].historicalCandidates.front().rejectionReason,
        SctStructuredRejectionReason::HistoricalConflict);
}

TEST(SctStructuredControlFlow, CrossSectionBranchesRemainUnstructured) {
    SctDocument document;
    const auto branchId = document.allocateInstructionId();
    const auto local = document.allocateInstructionId();
    const auto remote = document.allocateInstructionId();
    auto conditional = SctDocumentInstruction{branchId, 0u};
    conditional.fixedParameters = {{0u, SctExpressionFactory::encodedDecimalLiteral(0)},
        {1u, SctInstructionReference{remote}}};
    document.sections.push_back({document.allocateSectionId(), "one",
        SctScriptSectionContent{{conditional, {local, 12u}}}});
    document.sections.push_back({document.allocateSectionId(), "two",
        SctScriptSectionContent{{{remote, 12u}}}});
    const auto analysis = analyze(document);
    EXPECT_EQ(regionOf(analysis, SctStructuredRegionKind::If), nullptr);
    EXPECT_TRUE(std::ranges::any_of(analysis.sections()[0].issues, [](const auto& issue) {
        return issue.kind == SctStructureIssueKind::CrossSectionNonCallControlFlow;
    }));
    EXPECT_TRUE(std::ranges::any_of(analysis.sections()[1].entryPoints,
        [remote](const auto& entry) {
            return entry.instruction == remote
                && entry.kind == SctSectionEntryKind::CrossSectionNonCallTarget;
        }));
}

TEST(SctStructuredControlFlow, BlocksContainEachInstructionExactlyOnce) {
    SctDocument document;
    const auto ifId = document.allocateInstructionId();
    const auto body = document.allocateInstructionId();
    const auto join = document.allocateInstructionId();
    auto conditional = SctDocumentInstruction{ifId, 0u};
    conditional.fixedParameters = {{0u, SctExpressionFactory::encodedDecimalLiteral(0)},
        {1u, SctInstructionReference{join}}};
    document.sections.push_back({document.allocateSectionId(), "test",
        SctScriptSectionContent{{conditional, {body, 125u}, {join, 12u}}}});
    const auto analysis = analyze(document);
    std::vector<SctInstructionId> found;
    for (const auto& block : analysis.sections()[0].blocks) {
        found.insert(found.end(), block.instructions.begin(), block.instructions.end());
    }
    std::ranges::sort(found);
    EXPECT_EQ(found, (std::vector<SctInstructionId>{ifId, body, join}));
}

TEST(SctStructuredControlFlow, AnalysisOwnsIdsAfterDocumentDestruction) {
    std::optional<SctStructuredControlFlowAnalysis> retained;
    SctInstructionId instructionId;
    {
        SctDocument document;
        instructionId = document.allocateInstructionId();
        document.sections.push_back({document.allocateSectionId(), "test",
            SctScriptSectionContent{{{instructionId, 12u}}}});
        retained = analyze(document);
    }
    ASSERT_TRUE(retained.has_value());
    ASSERT_NE(retained->blockContaining(instructionId), nullptr);
    EXPECT_EQ(retained->blockContaining(instructionId)->instructions.front(), instructionId);
}

TEST(SctStructuredControlFlow, RepeatedAnalysisIsDeterministicAndQueriesUseStableIds) {
    SctDocument document;
    const auto ifId = document.allocateInstructionId();
    const auto body = document.allocateInstructionId();
    const auto join = document.allocateInstructionId();
    auto conditional = SctDocumentInstruction{ifId, 0u};
    conditional.fixedParameters = {{0u, SctExpressionFactory::encodedDecimalLiteral(0)},
        {1u, SctInstructionReference{join}}};
    document.sections.push_back({document.allocateSectionId(), "test",
        SctScriptSectionContent{{conditional, {body, 125u}, {join, 12u}}}});
    const auto first = analyze(document);
    const auto second = analyze(document);
    ASSERT_EQ(first.sections().size(), second.sections().size());
    EXPECT_EQ(first.sections()[0], second.sections()[0]);
    ASSERT_EQ(first.sections()[0].blocks.size(), second.sections()[0].blocks.size());
    ASSERT_EQ(first.sections()[0].regions.size(), second.sections()[0].regions.size());
    EXPECT_EQ(first.findSection(first.sections()[0].section), &first.sections()[0]);
    for (std::size_t index = 0; index < first.sections()[0].blocks.size(); ++index)
        EXPECT_EQ(first.sections()[0].blocks[index].id, second.sections()[0].blocks[index].id);
    ASSERT_NE(first.blockContaining(body), nullptr);
    const auto containing = first.regionsContaining(body);
    ASSERT_EQ(containing.size(), 1u);
    ASSERT_NE(containing.front(), nullptr);
    EXPECT_EQ(first.findRegion(containing.front()->id)->id, containing.front()->id);
}

TEST(SctStructuredControlFlow, AggregateAndStandaloneBuildersProduceIdenticalCoreResults) {
    SctDocument document;
    const auto ifId = document.allocateInstructionId();
    const auto bodyId = document.allocateInstructionId();
    const auto joinId = document.allocateInstructionId();
    auto conditional = branch(document, joinId);
    conditional.id = ifId;
    document.sections.push_back({document.allocateSectionId(), "test",
        SctScriptSectionContent{{conditional, {bodyId, 125u}, {joinId, 12u}}}});

    const auto aggregate = SctDocumentAnalysis::build(document);
    const auto standalone = SctStructuredControlFlowAnalysis::build(document);
    EXPECT_TRUE(std::ranges::equal(
        aggregate.structuredControlFlow.sections(), standalone.sections()));
    ASSERT_NE(aggregate.structuredControlFlow.blockContaining(bodyId), nullptr);
    ASSERT_NE(standalone.blockContaining(bodyId), nullptr);
    EXPECT_EQ(aggregate.structuredControlFlow.blockContaining(bodyId)->id,
        standalone.blockContaining(bodyId)->id);
}

}  // namespace
