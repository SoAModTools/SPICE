#include "../SpiceSCT/SpiceSCT.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <type_traits>

namespace {
using namespace spice::sct;

SctParseResult makeOpcode265Parse() {
    SctParseResult parsed;
    parsed.parseOk = true;
    parsed.file.detectedEndian = "big";
    parsed.file.originalPayloadBytes.resize(44, 0);
    SctSection section;
    section.id.index = 0;
    section.id.name = "DUPLICATE";
    section.startOffset = 32;
    section.endOffset = 44;
    section.kind = SctSectionKind::Script;
    SctInstruction instruction;
    instruction.offset = 0;
    instruction.payloadOffset = 0;
    instruction.opcode = 265;
    instruction.sizeBytes = 12;
    instruction.decodeOk = true;
    SctParameter expression;
    expression.index = 0;
    expression.rawWords = {0x0000002au};
    expression.expression = SctExpression{};
    expression.expression->hitStopCode = true;
    expression.expression->ast = SctScptAstNode{SctScptAstNodeKind::RawValue, {}, {}, {0x0000002au}, {}};
    SctParameter footer;
    footer.index = 1;
    footer.rawWords = {0};
    instruction.parameters = {expression, footer};
    section.instructions.push_back(std::move(instruction));
    parsed.file.sections.push_back(std::move(section));
    return parsed;
}

SctDocument makeReferenceDocument() {
    SctDocument document;
    const auto sectionId = document.allocateSectionId();
    const auto jumpId = document.allocateInstructionId();
    const auto targetId = document.allocateInstructionId();
    SctDocumentInstruction jump;
    jump.id = jumpId;
    jump.opcode = 10;
    jump.fixedParameters.push_back({0, SctInstructionReference{targetId}});
    SctDocumentInstruction target;
    target.id = targetId;
    target.opcode = 12;
    document.sections.push_back({sectionId, "SCRIPT", SctScriptSectionContent{{jump, target}}});
    return document;
}
}

static_assert(!std::is_same_v<SctSectionId, SctInstructionId>);
static_assert(!std::is_convertible_v<SctSectionId, SctInstructionId>);
static_assert(std::is_copy_constructible_v<SctDocument> && std::is_move_constructible_v<SctDocument>);

TEST(SctDocumentModel, AllocatesSeparatedMonotonicIdentityDomains) {
    SctDocument document;
    EXPECT_EQ(document.allocateSectionId().value(), 1u);
    EXPECT_EQ(document.allocateSectionId().value(), 2u);
    EXPECT_EQ(document.allocateInstructionId().value(), 1u);
    EXPECT_EQ(document.allocateStringId().value(), 1u);
    EXPECT_EQ(document.allocateFooterEntryId().value(), 1u);
    EXPECT_EQ(document.allocateOpaqueAttachmentId().value(), 1u);
}

TEST(SctDocumentImporter, IsDeterministicAndClaimsEveryDecodedPayloadByte) {
    const auto parsed = makeOpcode265Parse();
    const auto first = SctDocumentImporter::import(parsed, {{SctPlatform::GameCube}});
    const auto second = SctDocumentImporter::import(parsed, {{SctPlatform::GameCube}});
    ASSERT_TRUE(first.document.has_value());
    ASSERT_TRUE(second.document.has_value());
    ASSERT_EQ(first.document->sections.size(), 1u);
    const auto& firstScript = std::get<SctScriptSectionContent>(first.document->sections[0].content);
    const auto& secondScript = std::get<SctScriptSectionContent>(second.document->sections[0].content);
    ASSERT_EQ(firstScript.instructions.size(), 1u);
    ASSERT_EQ(secondScript.instructions.size(), 1u);
    EXPECT_EQ(first.document->sections[0].id, second.document->sections[0].id);
    EXPECT_EQ(firstScript.instructions[0].id, secondScript.instructions[0].id);
    EXPECT_EQ(first.receipt.source.byteOrder, SctSourceByteOrder::BigEndian);
    ASSERT_TRUE(first.receipt.declaredSourcePlatform.has_value());
    EXPECT_EQ(*first.receipt.declaredSourcePlatform, SctPlatform::GameCube);

    std::vector<unsigned> coverage(parsed.file.originalPayloadBytes.size(), 0);
    for (const auto& provenance : first.receipt.provenance) {
        ASSERT_LE(static_cast<std::size_t>(provenance.decodedPayloadOffset) + provenance.byteSize, coverage.size());
        for (std::size_t i = provenance.decodedPayloadOffset;
             i < provenance.decodedPayloadOffset + provenance.byteSize; ++i) ++coverage[i];
    }
    EXPECT_TRUE(std::all_of(coverage.begin(), coverage.end(), [](unsigned claims) { return claims == 1; }));
}

TEST(SctDocumentImporter, RecordsEncodingObservationsWithoutInferringAPlatform) {
    auto parsed = makeOpcode265Parse();
    parsed.file.detectedEndian = "little";
    const auto imported = SctDocumentImporter::import(parsed);
    ASSERT_TRUE(imported.document.has_value());
    EXPECT_EQ(imported.receipt.source.byteOrder, SctSourceByteOrder::LittleEndian);
    EXPECT_FALSE(imported.receipt.declaredSourcePlatform.has_value());
}

TEST(SctDocumentImporter, PreservesDuplicatePhysicalNamesWithoutMergingRows) {
    SctParseResult parsed;
    parsed.parseOk = true;
    parsed.file.originalPayloadBytes.resize(52, 0);
    const std::string name = "SAME";
    std::copy(name.begin(), name.end(), parsed.file.originalPayloadBytes.begin() + 16);
    std::copy(name.begin(), name.end(), parsed.file.originalPayloadBytes.begin() + 36);
    for (std::uint32_t i = 0; i < 2; ++i) {
        SctSection section;
        section.id.index = i;
        section.id.name = "SAME";
        section.startOffset = 52;
        section.endOffset = 52;
        section.kind = SctSectionKind::Label;
        parsed.file.sections.push_back(std::move(section));
    }
    const auto imported = SctDocumentImporter::import(parsed);
    ASSERT_TRUE(imported.document.has_value());
    ASSERT_EQ(imported.document->sections.size(), 2u);
    EXPECT_EQ(imported.document->sections[0].nameBytes, "SAME");
    EXPECT_EQ(imported.document->sections[0].nameBytes, imported.document->sections[1].nameBytes);
    EXPECT_NE(imported.document->sections[0].id, imported.document->sections[1].id);
}

TEST(SctDocumentImporter, ConvertsControlAndFooterOffsetsToStableEntityReferences) {
    SctParseResult parsed;
    parsed.parseOk = true;
    parsed.file.originalPayloadBytes.resize(54, 0);
    SctSection section;
    section.id.name = "REFS";
    section.startOffset = 32;
    section.endOffset = 52;
    section.kind = SctSectionKind::Script;
    SctInstruction jump;
    jump.opcode = 10;
    jump.payloadOffset = 0;
    jump.offset = 0;
    jump.sizeBytes = 8;
    jump.decodeOk = true;
    jump.parameters.push_back({0, {}, SctParameterValueKind::Raw, SctSemanticConfidence::Known, {0}, {}, std::nullopt});
    SctInstruction target;
    target.opcode = 12;
    target.payloadOffset = 8;
    target.offset = 8;
    target.sizeBytes = 4;
    target.decodeOk = true;
    SctInstruction footerLoad;
    footerLoad.opcode = 43;
    footerLoad.payloadOffset = 12;
    footerLoad.offset = 12;
    footerLoad.sizeBytes = 8;
    footerLoad.decodeOk = true;
    footerLoad.parameters.push_back({0, {}, SctParameterValueKind::Raw, SctSemanticConfidence::Known, {0}, {}, std::nullopt});
    section.instructions = {jump, target, footerLoad};
    section.edges.push_back({SctEdgeType::Jump, SctSemanticConfidence::Known, {}, {}, 0, 8, 10});
    parsed.file.sections.push_back(std::move(section));
    SctFooter footer;
    footer.present = true;
    footer.payloadStartOffset = 20;
    footer.payloadEndOffset = 22;
    footer.rawBytes = {'A', 0};
    SctFooterEntry entry;
    entry.kind = SctFooterEntryKind::String;
    entry.payloadOffset = 20;
    entry.rawBytes = {'A', 0};
    entry.references.push_back({12, 0, 0, 20, 43});
    footer.entries.push_back(std::move(entry));
    parsed.file.footer = std::move(footer);

    const auto imported = SctDocumentImporter::import(parsed, {{SctPlatform::GameCube}});
    ASSERT_TRUE(imported.document.has_value());
    const auto& instructions = std::get<SctScriptSectionContent>(imported.document->sections[0].content).instructions;
    ASSERT_EQ(instructions.size(), 3u);
    ASSERT_TRUE(std::holds_alternative<SctInstructionReference>(instructions[0].fixedParameters[0].value));
    EXPECT_EQ(std::get<SctInstructionReference>(instructions[0].fixedParameters[0].value).target, instructions[1].id);
    ASSERT_TRUE(std::holds_alternative<SctFooterEntryReference>(instructions[2].fixedParameters[0].value));
    EXPECT_EQ(std::get<SctFooterEntryReference>(instructions[2].fixedParameters[0].value).target,
        imported.document->footerEntries[0].id);
    EXPECT_EQ(imported.document->footerEntries[0].kind, SctDocumentFooterEntryKind::String);
    const auto validation = SctDocumentValidator::validateForTarget(
        *imported.document, SctPlatform::GameCube, &imported.receipt);
    std::string messages;
    for (const auto& diagnostic : validation.diagnostics) messages += diagnostic.message + "\n";
    EXPECT_TRUE(validation.validForTarget) << messages;
}

TEST(SctDocumentImporter, ConvertsBranchSwitchCallAndJumpTargetsInBothDirections) {
    SctParseResult parsed;
    parsed.parseOk = true;
    parsed.file.originalPayloadBytes.resize(84, 0);
    SctSection section;
    section.startOffset = 32;
    section.endOffset = 84;
    section.kind = SctSectionKind::Script;
    const auto expression = [](std::uint32_t index) {
        SctParameter parameter;
        parameter.index = index;
        parameter.rawWords = {1};
        parameter.expression = SctExpression{};
        parameter.expression->hitStopCode = true;
        parameter.expression->ast = SctScptAstNode{SctScptAstNodeKind::RawValue, {}, {}, {1}, {}};
        return parameter;
    };
    const auto raw = [](std::uint32_t index, std::uint32_t value) {
        SctParameter parameter;
        parameter.index = index;
        parameter.rawWords = {value};
        return parameter;
    };
    SctInstruction branch;
    branch.opcode = 0; branch.payloadOffset = 0; branch.offset = 0; branch.sizeBytes = 12; branch.decodeOk = true;
    branch.parameters = {expression(0), raw(1, 0)};
    SctInstruction sw;
    sw.opcode = 3; sw.payloadOffset = 12; sw.offset = 12; sw.sizeBytes = 20; sw.decodeOk = true;
    sw.parameters = {expression(0), raw(1, 1), raw(2, 9), raw(3, 0)};
    SctInstruction call;
    call.opcode = 11; call.payloadOffset = 32; call.offset = 32; call.sizeBytes = 8; call.decodeOk = true;
    call.parameters = {raw(0, 0)};
    SctInstruction ret;
    ret.opcode = 12; ret.payloadOffset = 40; ret.offset = 40; ret.sizeBytes = 4; ret.decodeOk = true;
    SctInstruction jump;
    jump.opcode = 10; jump.payloadOffset = 44; jump.offset = 44; jump.sizeBytes = 8; jump.decodeOk = true;
    jump.parameters = {raw(0, 0)};
    section.instructions = {branch, sw, call, ret, jump};
    section.edges.push_back({SctEdgeType::BranchFalse, SctSemanticConfidence::Known, {}, {}, 0, 40, 0});
    section.edges.push_back({SctEdgeType::SwitchCase, SctSemanticConfidence::Known, {}, {}, 12, 40, 3});
    section.edges.push_back({SctEdgeType::CallSubscript, SctSemanticConfidence::Known, {}, {}, 32, 0, 11});
    section.edges.push_back({SctEdgeType::Jump, SctSemanticConfidence::Known, {}, {}, 44, 0, 10});
    parsed.file.sections.push_back(std::move(section));

    const auto imported = SctDocumentImporter::import(parsed, {{SctPlatform::GameCube}});
    ASSERT_TRUE(imported.document.has_value());
    const auto& instructions = std::get<SctScriptSectionContent>(imported.document->sections[0].content).instructions;
    ASSERT_EQ(instructions.size(), 5u);
    EXPECT_EQ(std::get<SctInstructionReference>(instructions[0].fixedParameters[1].value).target, instructions[3].id);
    EXPECT_EQ(std::get<SctInstructionReference>(instructions[1].repeatedParameterGroups[0].parameters[1].value).target,
        instructions[3].id);
    EXPECT_EQ(std::get<SctInstructionReference>(instructions[2].fixedParameters[0].value).target, instructions[0].id);
    EXPECT_EQ(std::get<SctInstructionReference>(instructions[4].fixedParameters[0].value).target, instructions[0].id);
    const auto validation = SctDocumentValidator::validateForTarget(
        *imported.document, SctPlatform::GameCube, &imported.receipt);
    std::string messages;
    for (const auto& diagnostic : validation.diagnostics) messages += diagnostic.message + "\n";
    EXPECT_TRUE(validation.validForTarget) << messages;
}

TEST(SctDocumentImporter, RemovesDerivedCountAndSplitsSchemaRepeatedGroups) {
    SctParseResult parsed;
    parsed.parseOk = true;
    parsed.file.originalPayloadBytes.resize(48, 0);
    SctSection section;
    section.id.name = "LOOP";
    section.startOffset = 32;
    section.endOffset = 48;
    section.kind = SctSectionKind::Script;
    SctInstruction instruction;
    instruction.opcode = 119;
    instruction.payloadOffset = 0;
    instruction.offset = 0;
    instruction.sizeBytes = 16;
    instruction.decodeOk = true;
    const auto expression = [](std::uint32_t index, std::uint32_t word) {
        SctParameter parameter;
        parameter.index = index;
        parameter.rawWords = {word};
        parameter.expression = SctExpression{};
        parameter.expression->hitStopCode = true;
        parameter.expression->ast = SctScptAstNode{SctScptAstNodeKind::RawValue, {}, {}, {word}, {}};
        return parameter;
    };
    auto count = SctParameter{};
    count.index = 1;
    count.rawWords = {1};
    instruction.parameters = {expression(0, 3), count, expression(2, 7)};
    section.instructions.push_back(std::move(instruction));
    parsed.file.sections.push_back(std::move(section));

    const auto imported = SctDocumentImporter::import(parsed, {{SctPlatform::GameCube}});
    ASSERT_TRUE(imported.document.has_value());
    const auto& canonical = std::get<SctScriptSectionContent>(imported.document->sections[0].content).instructions[0];
    ASSERT_EQ(canonical.fixedParameters.size(), 1u);
    ASSERT_EQ(canonical.repeatedParameterGroups.size(), 1u);
    ASSERT_EQ(canonical.repeatedParameterGroups[0].parameters.size(), 1u);
    EXPECT_EQ(canonical.repeatedParameterGroups[0].parameters[0].schemaIndex, 2u);
    EXPECT_TRUE(SctDocumentValidator::validateForTarget(
        *imported.document, SctPlatform::GameCube, &imported.receipt).validForTarget);
}

TEST(SctDocumentImporter, ReturnsPartialDocumentWithOpaqueFallbackForContradictoryEvidence) {
    auto parsed = makeOpcode265Parse();
    parsed.file.originalPayloadBytes.resize(48, 0);
    parsed.file.sections[0].endOffset = 48;
    auto overlapping = parsed.file.sections[0].instructions[0];
    overlapping.payloadOffset = 4;
    overlapping.offset = 4;
    parsed.file.sections[0].instructions.push_back(std::move(overlapping));
    const auto imported = SctDocumentImporter::import(parsed);
    ASSERT_TRUE(imported.document.has_value());
    const auto& script = std::get<SctScriptSectionContent>(imported.document->sections[0].content);
    EXPECT_TRUE(script.instructions.empty());
    EXPECT_FALSE(imported.document->opaqueAttachments.empty());
    EXPECT_TRUE(std::any_of(imported.diagnostics.begin(), imported.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.code == SctDiagnosticCode::OverlappingSourceClaims;
    }));
}

TEST(SctDocumentImporter, PreservesAmbiguousScptAsOpaqueExpression) {
    auto parsed = makeOpcode265Parse();
    parsed.file.sections[0].instructions[0].parameters[0].expression.reset();
    const auto imported = SctDocumentImporter::import(parsed);
    ASSERT_TRUE(imported.document.has_value());
    const auto& instruction = std::get<SctScriptSectionContent>(
        imported.document->sections[0].content).instructions[0];
    const auto* expression = std::get_if<SctCanonicalExpression>(&instruction.fixedParameters[0].value);
    ASSERT_NE(expression, nullptr);
    EXPECT_TRUE(std::holds_alternative<SctOpaqueExpression>(expression->root));
}

TEST(SctDocumentImporter, FailedParseDoesNotProduceDocument) {
    const auto imported = SctDocumentImporter::import(SctParseResult{});
    EXPECT_FALSE(imported.document.has_value());
    ASSERT_FALSE(imported.diagnostics.empty());
    EXPECT_EQ(imported.diagnostics.front().code, SctDiagnosticCode::ParseFailed);
}

TEST(SctDocumentValidator, AppliesExplicitPlatformAvailabilityWithoutChangingTheContract) {
    const auto imported = SctDocumentImporter::import(makeOpcode265Parse(), {{SctPlatform::GameCube}});
    ASSERT_TRUE(imported.document.has_value());
    const auto structural = SctDocumentValidator::validateDocument(*imported.document);
    const auto gameCube = SctDocumentValidator::validateForTarget(
        *imported.document, SctPlatform::GameCube, &imported.receipt);
    const auto dreamcast = SctDocumentValidator::validateForTarget(
        *imported.document, SctPlatform::Dreamcast, &imported.receipt);
    EXPECT_TRUE(structural.validDocument);
    EXPECT_TRUE(gameCube.validForTarget);
    EXPECT_FALSE(dreamcast.validForTarget);
    EXPECT_TRUE(std::any_of(dreamcast.diagnostics.begin(), dreamcast.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.code == SctDiagnosticCode::OpcodeUnavailable;
    }));
}

TEST(SctDocumentValidator, NeutralValidationRejectsUniversallyInvalidAndFoldedOpcodes) {
    SctDocument document;
    const auto sectionId = document.allocateSectionId();
    const auto invalidId = document.allocateInstructionId();
    const auto modifierId = document.allocateInstructionId();
    document.sections.push_back({sectionId, "SCRIPT", SctScriptSectionContent{{
        SctDocumentInstruction{invalidId, 13},
        SctDocumentInstruction{modifierId, 129},
    }}});

    const auto validation = SctDocumentValidator::validateDocument(document);
    EXPECT_FALSE(validation.validDocument);
    EXPECT_TRUE(std::any_of(validation.diagnostics.begin(), validation.diagnostics.end(),
        [&](const auto& diagnostic) {
            return diagnostic.code == SctDiagnosticCode::OpcodeUnavailable
                && diagnostic.entity == SctDocumentEntityId{invalidId};
        }));
    EXPECT_TRUE(std::any_of(validation.diagnostics.begin(), validation.diagnostics.end(),
        [&](const auto& diagnostic) {
            return diagnostic.code == SctDiagnosticCode::InvalidContent
                && diagnostic.entity == SctDocumentEntityId{modifierId};
        }));
}

TEST(SctDocumentValidator, KeepsIdentityStableAcrossReorderingAndCatchesDeletedTargets) {
    auto document = makeReferenceDocument();
    auto& instructions = std::get<SctScriptSectionContent>(document.sections.front().content).instructions;
    const auto jumpId = instructions[0].id;
    const auto targetId = instructions[1].id;
    std::reverse(instructions.begin(), instructions.end());
    EXPECT_EQ(instructions[0].id, targetId);
    EXPECT_EQ(instructions[1].id, jumpId);
    EXPECT_TRUE(SctDocumentValidator::validateDocument(document).validDocument);
    instructions.erase(instructions.begin());
    const auto invalid = SctDocumentValidator::validateDocument(document);
    EXPECT_FALSE(invalid.validDocument);
    EXPECT_TRUE(std::any_of(invalid.diagnostics.begin(), invalid.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.code == SctDiagnosticCode::UnresolvedReference;
    }));
}

TEST(SctDocumentValidator, RejectsInvalidNamesRepeatedGroupsAndExpressionArity) {
    SctDocument document;
    const auto sectionId = document.allocateSectionId();
    const auto instructionId = document.allocateInstructionId();
    SctDocumentInstruction instruction;
    instruction.id = instructionId;
    instruction.opcode = 3;
    SctCanonicalExpression expression;
    expression.root = SctCanonicalExpressionNode{SctCanonicalExpressionNodeKind::ArithmeticOperator, 0x14, {}, {}};
    instruction.fixedParameters.push_back({0, expression});
    instruction.repeatedParameterGroups.push_back({{{2, SctEncodedWordValue{1}}}});
    document.sections.push_back({sectionId, std::string(17, 'X'), SctScriptSectionContent{{instruction}}});
    const auto validation = SctDocumentValidator::validateDocument(document);
    EXPECT_FALSE(validation.validDocument);
    EXPECT_TRUE(std::any_of(validation.diagnostics.begin(), validation.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.code == SctDiagnosticCode::InvalidName;
    }));
    EXPECT_TRUE(std::any_of(validation.diagnostics.begin(), validation.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.code == SctDiagnosticCode::ExpressionInvalid;
    }));
    EXPECT_TRUE(std::any_of(validation.diagnostics.begin(), validation.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.code == SctDiagnosticCode::ParameterMismatch;
    }));
}

TEST(SctDocumentValidator, RejectsZeroDuplicateOutOfAllocatorIdsAndBrokenAttachments) {
    SctDocument document;
    const auto sectionId = document.allocateSectionId();
    document.sections.push_back({sectionId, "A", SctLabelSectionContent{}});
    document.sections.push_back({sectionId, "B", SctLabelSectionContent{}});
    document.strings.push_back({SctStringId{}, SctEditableText{"bad"}});
    const auto attachmentId = document.allocateOpaqueAttachmentId();
    document.opaqueAttachments.push_back({attachmentId, {1}, SctInstructionId{99},
        SctOpaquePlacement::FixedOffset, std::nullopt, 3, SctOpaqueRelocationSupport::FixedOnly,
        SctOpaqueReason::ContradictoryEvidence});
    const auto validation = SctDocumentValidator::validateDocument(document);
    EXPECT_FALSE(validation.validDocument);
    const auto hasCode = [&](SctDiagnosticCode code) {
        return std::any_of(validation.diagnostics.begin(), validation.diagnostics.end(),
            [&](const auto& diagnostic) { return diagnostic.code == code; });
    };
    EXPECT_TRUE(hasCode(SctDiagnosticCode::InvalidId));
    EXPECT_TRUE(hasCode(SctDiagnosticCode::DuplicateId));
    EXPECT_TRUE(hasCode(SctDiagnosticCode::AttachmentInvalid));
}
