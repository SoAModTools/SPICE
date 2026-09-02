#include "../SpiceSCT/SpiceSCT.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <variant>

using namespace spice::sct;

namespace {

SctDocumentParameter parameter(std::uint32_t index, SctDocumentParameterValue value) {
    return {index, std::move(value)};
}

SctDocumentInstruction instruction(SctInstructionId id, std::uint16_t opcode) {
    SctDocumentInstruction result;
    result.id = id;
    result.opcode = opcode;
    return result;
}

SctParseResult importedGapParse() {
    SctParseResult parsed;
    parsed.parseOk = true;
    parsed.file.detectedEndian = "big";
    parsed.file.originalPayloadBytes.resize(56u, 0u);
    parsed.file.originalPayloadBytes[11] = 1u;
    parsed.file.originalPayloadBytes[16] = 'M';
    parsed.file.originalPayloadBytes[17] = 'A';
    parsed.file.originalPayloadBytes[18] = 'I';
    parsed.file.originalPayloadBytes[19] = 'N';
    std::fill(parsed.file.originalPayloadBytes.begin() + 40,
        parsed.file.originalPayloadBytes.begin() + 52, 0xacu);

    SctInstruction jump;
    jump.offset = 0u;
    jump.payloadOffset = 0u;
    jump.opcode = 10u;
    jump.rawWords = {10u, 12u};
    jump.parameters = {{0u, "offset", SctParameterValueKind::Link,
        SctSemanticConfidence::Known, {12u}}};
    jump.sizeBytes = 8u;
    jump.decodeOk = true;

    SctInstruction target;
    target.offset = 20u;
    target.payloadOffset = 20u;
    target.opcode = 12u;
    target.rawWords = {12u};
    target.sizeBytes = 4u;
    target.decodeOk = true;

    SctSection section;
    section.id = {0u, "MAIN"};
    section.startOffset = 32u;
    section.endOffset = 56u;
    section.kind = SctSectionKind::Script;
    section.instructions = {jump, target};
    section.edges.push_back({SctEdgeType::Jump, SctSemanticConfidence::Known,
        0u, 20u, 0u, 20u, 10u, "jump"});
    parsed.file.sections.push_back(std::move(section));
    return parsed;
}

SctDocument controlFlowDocument() {
    SctDocument document;
    const auto sectionId = document.allocateSectionId();
    std::array<SctInstructionId, 8> ids{};
    for (auto& id : ids) id = document.allocateInstructionId();

    auto branch = instruction(ids[0], 0u);
    branch.fixedParameters = {
        parameter(0u, SctCanonicalExpression{SctCanonicalExpressionNode{},
            SctExpressionTermination::InlineValue}),
        parameter(1u, SctInstructionReference{ids[6]}),
    };

    auto switchInstruction = instruction(ids[1], 3u);
    switchInstruction.fixedParameters = {
        parameter(0u, SctCanonicalExpression{SctCanonicalExpressionNode{},
            SctExpressionTermination::InlineValue}),
    };
    switchInstruction.repeatedParameterGroups = {
        {{parameter(2u, SctEncodedWordValue{1u}), parameter(3u, SctInstructionReference{ids[6]})}},
        {{parameter(2u, SctEncodedWordValue{2u}), parameter(3u, SctInstructionReference{ids[7]})}},
    };

    auto call = instruction(ids[2], 11u);
    call.fixedParameters = {parameter(0u, SctInstructionReference{ids[6]})};
    auto ordinary = instruction(ids[3], 114u);
    ordinary.fixedParameters = {parameter(0u, SctEncodedWordValue{4u}),
        parameter(1u, SctEncodedWordValue{2u})};
    auto jump = instruction(ids[4], 10u);
    jump.fixedParameters = {parameter(0u, SctInstructionReference{ids[7]})};
    auto unresolvedJump = instruction(ids[5], 10u);
    unresolvedJump.fixedParameters = {parameter(0u, SctUnresolvedReferenceValue{
        {SctReferenceTargetStorage::Instruction, std::nullopt}, {0xfffffff0u}})};
    auto firstReturn = instruction(ids[6], 12u);
    auto secondReturn = instruction(ids[7], 12u);

    document.sections.push_back({sectionId, "MAIN", SctScriptSectionContent{{
        branch, switchInstruction, call, ordinary, jump, unresolvedJump, firstReturn, secondReturn}}});
    return document;
}

const SctControlFlowEdge* edgeOf(std::span<const SctControlFlowEdge> edges,
    SctInstructionId source, SctControlFlowKind kind, std::optional<SctInstructionId> target) {
    const auto found = std::find_if(edges.begin(), edges.end(), [&](const auto& edge) {
        return edge.sourceInstruction == source && edge.kind == kind
            && edge.targetInstruction == target;
    });
    return found == edges.end() ? nullptr : &*found;
}

SctCanonicalExpression variableExpression() {
    SctCanonicalExpressionNode root;
    root.kind = SctCanonicalExpressionNodeKind::ArithmeticOperator;
    root.encodingCode = 7u;
    root.children = {
        {SctCanonicalExpressionNodeKind::IntVariable, 0x50000018u},
        {SctCanonicalExpressionNodeKind::NegatedIntVariable, 0x50000008u},
        {SctCanonicalExpressionNodeKind::NegatedIntVariableLow16Comparison, 0x5000000fu},
        {SctCanonicalExpressionNodeKind::FloatVariable, 0x40000003u},
        {SctCanonicalExpressionNodeKind::BitVariable, 0x20000004u},
        {SctCanonicalExpressionNodeKind::ByteVariable, 0x10000005u},
    };
    return {std::move(root), SctExpressionTermination::InlineValue};
}

} // namespace

TEST(SctImportedSourceMap, ProvidesExactCoverageTopologyAndImportedCrossings) {
    const auto imported = SctDocumentImporter::import(importedGapParse());
    ASSERT_TRUE(imported.document.has_value());
    ASSERT_TRUE(imported.context.receipt.sourceMap.hasCompleteLeafCoverage());
    ASSERT_EQ(imported.document->sections.size(), 1u);
    const auto& script = std::get<SctScriptSectionContent>(imported.document->sections[0].content);
    ASSERT_EQ(script.instructions.size(), 2u);
    ASSERT_EQ(imported.document->opaqueAttachments.size(), 1u);

    const SctDocumentEntityId source{script.instructions[0].id};
    const SctDocumentEntityId target{script.instructions[1].id};
    const SctDocumentEntityId gap{imported.document->opaqueAttachments[0].id};
    EXPECT_EQ(imported.context.receipt.sourceMap.location(source)->primarySpan, (SctImportedByteSpan{32u, 8u}));
    EXPECT_EQ(imported.context.receipt.sourceMap.location(target)->primarySpan, (SctImportedByteSpan{52u, 4u}));
    EXPECT_EQ(imported.context.receipt.sourceMap.location(gap)->primarySpan, (SctImportedByteSpan{40u, 12u}));
    EXPECT_EQ(imported.context.receipt.sourceMap.previousSemanticEntity(gap), source);
    EXPECT_EQ(imported.context.receipt.sourceMap.nextSemanticEntity(gap), target);
    EXPECT_EQ(imported.context.receipt.sourceMap.relationship(source, target), SctSourceRelationship::Before);
    EXPECT_EQ(imported.context.receipt.sourceMap.relationship(
        SctDocumentEntityId{imported.document->sections[0].id}, source),
        SctSourceRelationship::Contains);
    EXPECT_TRUE(imported.context.receipt.sourceMap.semanticEntitiesBetween(source, target).empty());
    const auto containingGap = imported.context.receipt.sourceMap.recordsContaining({40u, 12u});
    EXPECT_TRUE(std::any_of(containingGap.begin(), containingGap.end(), [](const auto& record) {
        return record.role == SctSourceSpanRole::SectionPayload;
    }));
    const auto sourceRecords = imported.context.receipt.sourceMap.recordsFor(source);
    EXPECT_TRUE(std::any_of(sourceRecords.begin(), sourceRecords.end(), [](const auto& record) {
        return record.role == SctSourceSpanRole::Instruction
            && record.layer == SctSourceSpanLayer::Envelope && record.primaryEntityLocation;
    }));
    EXPECT_TRUE(std::any_of(sourceRecords.begin(), sourceRecords.end(), [](const auto& record) {
        return record.role == SctSourceSpanRole::InstructionOpcode
            && record.layer == SctSourceSpanLayer::Leaf && !record.primaryEntityLocation;
    }));

    const auto atGap = imported.context.receipt.sourceMap.recordsAt(44u);
    EXPECT_TRUE(std::any_of(atGap.begin(), atGap.end(), [](const auto& record) {
        return record.role == SctSourceSpanRole::SectionPayload
            && record.layer == SctSourceSpanLayer::Envelope;
    }));
    EXPECT_TRUE(std::any_of(atGap.begin(), atGap.end(), [](const auto& record) {
        return record.role == SctSourceSpanRole::OpaqueAttachment
            && record.layer == SctSourceSpanLayer::Leaf;
    }));

    const auto evidence = imported.context.bind(imported.context.revisionProvenance);
    ASSERT_TRUE(evidence.has_value());
    const auto analysis = SctDocumentAnalysis::build(*imported.document, &*evidence);
    ASSERT_EQ(analysis.controlFlow.importedEdges().size(), 1u);
    EXPECT_EQ(analysis.controlFlow.importedEdges()[0].targetInstruction, script.instructions[1].id);
    EXPECT_EQ(analysis.controlFlow.importedEdges()[0].crossedOpaqueAttachments,
        std::vector<SctOpaqueAttachmentId>{imported.document->opaqueAttachments[0].id});
    const auto* context = analysis.opaqueContext.find(imported.document->opaqueAttachments[0].id);
    ASSERT_NE(context, nullptr);
    ASSERT_EQ(context->interpretations.size(), 1u);
    EXPECT_EQ(context->interpretations[0].kind, SctOpaqueInterpretationKind::ControlFlowGap);

    auto edited = *imported.document;
    auto& editedInstructions = std::get<SctScriptSectionContent>(edited.sections[0].content).instructions;
    std::get<SctInstructionReference>(editedInstructions[0].fixedParameters[0].value).target
        = editedInstructions[0].id;
    const auto editedAnalysis = SctDocumentAnalysis::build(edited, &*evidence);
    ASSERT_EQ(editedAnalysis.controlFlow.currentEdges().size(), 2u);
    EXPECT_EQ(editedAnalysis.controlFlow.currentEdges()[0].targetInstruction, editedInstructions[0].id);
    ASSERT_EQ(editedAnalysis.controlFlow.importedEdges().size(), 1u);
    EXPECT_EQ(editedAnalysis.controlFlow.importedEdges()[0].targetInstruction, editedInstructions[1].id);
    EXPECT_EQ(imported.context.receipt.sourceMap.location(target)->primarySpan, (SctImportedByteSpan{52u, 4u}));
}

TEST(SctControlFlowIndex, DerivesStableIdEdgesAndReflectsDocumentEdits) {
    auto document = controlFlowDocument();
    auto& instructions = std::get<SctScriptSectionContent>(document.sections[0].content).instructions;
    const auto branchId = instructions[0].id;
    const auto switchId = instructions[1].id;
    const auto callId = instructions[2].id;
    const auto ordinaryId = instructions[3].id;
    const auto jumpId = instructions[4].id;
    const auto unresolvedId = instructions[5].id;
    const auto firstReturnId = instructions[6].id;
    const auto secondReturnId = instructions[7].id;

    const auto before = SctControlFlowIndex::build(document);
    EXPECT_NE(edgeOf(before.currentEdges(), branchId, SctControlFlowKind::BranchTrue, switchId), nullptr);
    EXPECT_NE(edgeOf(before.currentEdges(), branchId, SctControlFlowKind::BranchFalse, firstReturnId), nullptr);
    EXPECT_NE(edgeOf(before.currentEdges(), switchId, SctControlFlowKind::SwitchCase, firstReturnId), nullptr);
    EXPECT_NE(edgeOf(before.currentEdges(), switchId, SctControlFlowKind::SwitchCase, secondReturnId), nullptr);
    EXPECT_NE(edgeOf(before.currentEdges(), callId, SctControlFlowKind::Call, firstReturnId), nullptr);
    EXPECT_NE(edgeOf(before.currentEdges(), callId, SctControlFlowKind::Fallthrough, ordinaryId), nullptr);
    EXPECT_NE(edgeOf(before.currentEdges(), ordinaryId, SctControlFlowKind::Fallthrough, jumpId), nullptr);
    EXPECT_NE(edgeOf(before.currentEdges(), jumpId, SctControlFlowKind::Jump, secondReturnId), nullptr);
    EXPECT_NE(edgeOf(before.currentEdges(), unresolvedId, SctControlFlowKind::Jump, std::nullopt), nullptr);
    EXPECT_NE(edgeOf(before.currentEdges(), firstReturnId, SctControlFlowKind::Return, std::nullopt), nullptr);
    EXPECT_EQ(before.currentOutbound(switchId).size(), 2u);
    EXPECT_GE(before.currentInbound(firstReturnId).size(), 3u);

    std::get<SctInstructionReference>(instructions[0].fixedParameters[1].value).target = secondReturnId;
    std::swap(instructions[2], instructions[3]);
    const auto after = SctControlFlowIndex::build(document);
    EXPECT_NE(edgeOf(after.currentEdges(), branchId, SctControlFlowKind::BranchFalse, secondReturnId), nullptr);
    EXPECT_EQ(edgeOf(after.currentEdges(), branchId, SctControlFlowKind::BranchFalse, firstReturnId), nullptr);
    EXPECT_NE(edgeOf(after.currentEdges(), ordinaryId, SctControlFlowKind::Fallthrough, callId), nullptr);
}

TEST(SctSemanticUsageIndex, RecordsSitesReferencesVariablesAndOpaqueValues) {
    SctDocument document;
    const auto sectionId = document.allocateSectionId();
    const auto sourceId = document.allocateInstructionId();
    const auto targetId = document.allocateInstructionId();
    const auto stringId = document.allocateStringId();
    const auto footerId = document.allocateFooterEntryId();

    auto source = instruction(sourceId, 100u);
    source.scheduledExpression = SctCanonicalExpression{SctOpaqueExpression{{0x17u, 0x1du}},
        SctExpressionTermination::StopCode};
    source.fixedParameters = {
        parameter(0u, variableExpression()),
        parameter(1u, SctInstructionReference{targetId}),
        parameter(2u, SctStringReference{stringId}),
        parameter(3u, SctFooterEntryReference{footerId}),
        parameter(4u, SctUnresolvedReferenceValue{{SctReferenceTargetStorage::Instruction,
            std::nullopt}, {4u}}),
        parameter(5u, SctOpaqueParameterValue{{1u, 2u}}),
    };
    document.sections.push_back({sectionId, "MAIN", SctScriptSectionContent{{source,
        instruction(targetId, 12u)}}});

    const auto usage = SctSemanticUsageIndex::build(document);
    EXPECT_EQ(usage.usagesForOpcode(100u).size(), 1u);
    EXPECT_EQ(usage.referenceUsages().size(), 3u);
    EXPECT_EQ(usage.outboundReferences(sourceId).size(), 3u);
    EXPECT_EQ(usage.inboundReferences(SctDocumentReferenceTarget{targetId}).size(), 1u);
    EXPECT_EQ(usage.variableUsages().size(), 6u);
    EXPECT_EQ(usage.usagesForVariable({SctVariableKind::Integer, 24u}).size(), 1u);
    EXPECT_TRUE(std::any_of(usage.variableUsages().begin(), usage.variableUsages().end(), [](const auto& item) {
        return item.encodedForm == SctCanonicalExpressionNodeKind::NegatedIntVariableLow16Comparison
            && item.source.childPath == std::vector<std::uint32_t>{2u};
    }));
    ASSERT_EQ(usage.unresolvedReferences().size(), 1u);
    EXPECT_EQ(usage.unresolvedReferences()[0].source.parameter.schemaIndex, 4u);
    ASSERT_EQ(usage.opaqueParameters().size(), 1u);
    EXPECT_EQ(usage.opaqueParameters()[0].wordCount, 2u);
    ASSERT_EQ(usage.opaqueExpressions().size(), 1u);
    EXPECT_TRUE(std::holds_alternative<SctScheduledExpressionSite>(
        usage.opaqueExpressions()[0].source.owner));
}

TEST(SctOpaqueContextIndex, LabelsSwitchCrossingsWithoutAssigningOwnership) {
    const auto imported = SctDocumentImporter::import(importedGapParse());
    ASSERT_TRUE(imported.document.has_value());
    auto context = imported.context;
    ASSERT_EQ(context.receipt.controlFlow.size(), 1u);
    context.receipt.controlFlow[0].kind = SctControlFlowKind::SwitchCase;
    context.receipt.controlFlow[0].origin = SctParameterSite{context.receipt.controlFlow[0].sourceInstruction,
        {3u, 0u}};
    const auto evidence = context.bind(context.revisionProvenance);
    ASSERT_TRUE(evidence);

    const auto flow = SctControlFlowIndex::build(*imported.document, &*evidence);
    const auto contexts = SctOpaqueContextIndex::build(*imported.document, &*evidence, flow);
    const auto attachment = imported.document->opaqueAttachments[0].id;
    const auto* opaqueContext = contexts.find(attachment);
    ASSERT_NE(opaqueContext, nullptr);
    EXPECT_EQ(opaqueContext->crossingImportedEdges.size(), 1u);
    EXPECT_TRUE(std::any_of(opaqueContext->interpretations.begin(), opaqueContext->interpretations.end(), [](const auto& value) {
        return value.kind == SctOpaqueInterpretationKind::ControlFlowGap;
    }));
    EXPECT_TRUE(std::any_of(opaqueContext->interpretations.begin(), opaqueContext->interpretations.end(), [](const auto& value) {
        return value.kind == SctOpaqueInterpretationKind::SwitchDispatchGap;
    }));
    EXPECT_EQ(imported.document->opaqueAttachments[0].anchor,
        SctOpaqueAnchor{imported.document->sections[0].id});
}

TEST(SctOpcodeEffectIndex, ExposesConfirmedSitesWithoutExternalResolution) {
    SctDocument document;
    const auto sectionId = document.allocateSectionId();
    const auto loadMldId = document.allocateInstructionId();
    const auto loadScriptId = document.allocateInstructionId();
    const auto groundId = document.allocateInstructionId();
    const auto mldText = document.allocateFooterEntryId();
    const auto scriptText = document.allocateFooterEntryId();
    auto loadMld = instruction(loadMldId, 23u);
    loadMld.fixedParameters = {parameter(0u, SctFooterEntryReference{mldText})};
    auto loadScript = instruction(loadScriptId, 43u);
    loadScript.fixedParameters = {parameter(0u, SctFooterEntryReference{scriptText})};
    auto groundInstruction = instruction(groundId, 114u);
    groundInstruction.fixedParameters = {
        parameter(0u, SctExpressionFactory::encodedDecimalLiteral(1)),
        parameter(1u, SctExpressionFactory::encodedDecimalLiteral(2))};
    document.sections.push_back({sectionId, "MAIN", SctScriptSectionContent{{
        loadMld, loadScript, groundInstruction}}});
    document.footerEntries.push_back({mldText, SctTextKind::PlainString, SctPlainText{"mld"}});
    document.footerEntries.push_back({scriptText, SctTextKind::PlainString, SctPlainText{"script"}});

    const auto effects = SctOpcodeEffectIndex::build(document);
    ASSERT_EQ(effects.effects().size(), 3u);
    const auto mld = effects.effectsForInstruction(loadMldId);
    ASSERT_EQ(mld.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<SctResourceLoadEffect>(mld[0].effect));
    EXPECT_EQ(mld[0].usability, SctOpcodeEffectUsability::Usable);
    EXPECT_EQ(std::get<SctResourceLoadEffect>(mld[0].effect).resource, SctResourceKind::Mld);
    EXPECT_EQ(std::get<SctResourceLoadEffect>(mld[0].effect).resourceParameter.parameter.schemaIndex, 0u);

    const auto script = effects.effectsForInstruction(loadScriptId);
    ASSERT_EQ(script.size(), 1u);
    EXPECT_EQ(std::get<SctResourceLoadEffect>(script[0].effect).resource, SctResourceKind::Script);

    const auto ground = effects.effectsForInstruction(groundId);
    ASSERT_EQ(ground.size(), 1u);
    const auto& selection = std::get<SctGroundVariantSelectionEffect>(ground[0].effect);
    EXPECT_EQ(selection.tableIdParameter.parameter.schemaIndex, 0u);
    EXPECT_EQ(selection.variantParameter.parameter.schemaIndex, 1u);
    EXPECT_EQ(selection.confidence, SctSemanticConfidence::Known);
    EXPECT_EQ(effects.usableEffects().size(), 3u);
}

TEST(SctDocumentIndex, RetainedAnalysisOwnsOnlyRevisionLocations) {
    SctDocumentAnalysis retained;
    SctSectionId sectionId;
    SctInstructionId instructionId;
    {
        auto document = controlFlowDocument();
        sectionId = document.sections.front().id;
        instructionId = std::get<SctScriptSectionContent>(
            document.sections.front().content).instructions.front().id;
        retained = SctDocumentAnalysis::build(document);
        ASSERT_NE(retained.entities.find(document, instructionId), nullptr);
    }

    EXPECT_EQ(retained.entities.sectionOrdinal(sectionId), 0u);
    ASSERT_TRUE(retained.entities.instructionLocation(instructionId));

    SctDocument unrelated;
    EXPECT_EQ(retained.entities.find(unrelated, sectionId), nullptr);
    EXPECT_EQ(retained.entities.find(unrelated, instructionId), nullptr);

    auto changedLayout = controlFlowDocument();
    changedLayout.sections.insert(changedLayout.sections.begin(),
        {changedLayout.allocateSectionId(), "INSERTED", SctLabelSectionContent{}});
    EXPECT_EQ(retained.entities.find(changedLayout, sectionId), nullptr);
    EXPECT_EQ(retained.entities.find(changedLayout, instructionId), nullptr);
}

TEST(SctImportedSourceMap, BuilderRejectsEveryUnsafeConstructionClass) {
    const auto hasIssue = [](const SctImportedSourceMap::BuildResult& built,
                              SctSourceMapIssueCode code) {
        return std::any_of(built.issues.begin(), built.issues.end(),
            [&](const auto& issue) { return issue.code == code; });
    };
    const auto leaf = [](std::uint32_t offset, std::uint32_t size,
                         std::optional<SctImportedSourceTarget> target = std::nullopt) {
        return SctSourceSpanRecord{{offset, size}, SctSourceSpanRole::Header,
            SctSourceSpanLayer::Leaf, SctSourceCoverageKind::SourceObservation,
            std::move(target), std::nullopt, std::nullopt, SctSourceRegion::Header, false};
    };

    const auto gap = SctImportedSourceMap::build(8u, {leaf(0u, 3u), leaf(4u, 4u)});
    EXPECT_FALSE(gap.map); EXPECT_TRUE(hasIssue(gap, SctSourceMapIssueCode::LeafGap));
    const auto overlap = SctImportedSourceMap::build(8u, {leaf(0u, 5u), leaf(4u, 4u)});
    EXPECT_FALSE(overlap.map); EXPECT_TRUE(hasIssue(overlap, SctSourceMapIssueCode::LeafOverlap));
    const auto outside = SctImportedSourceMap::build(8u, {leaf(0u, 9u)});
    EXPECT_FALSE(outside.map); EXPECT_TRUE(hasIssue(outside, SctSourceMapIssueCode::OutOfBounds));
    const auto zero = SctImportedSourceMap::build(8u, {leaf(0u, 0u), leaf(0u, 8u)});
    EXPECT_FALSE(zero.map); EXPECT_TRUE(hasIssue(zero, SctSourceMapIssueCode::ZeroLengthLeaf));
    const auto invalidTarget = SctImportedSourceMap::build(8u,
        {leaf(0u, 8u, SctImportedSourceTarget{SctParameterSite{SctInstructionId{}, {0u, std::nullopt}}})});
    EXPECT_FALSE(invalidTarget.map);
    EXPECT_TRUE(hasIssue(invalidTarget, SctSourceMapIssueCode::InvalidTarget));

    const SctSectionId section{1u};
    const SctDocumentEntityId sectionEntity{section};
    SctSourceSpanRecord primary{{0u, 8u}, SctSourceSpanRole::SectionPayload,
        SctSourceSpanLayer::Envelope, SctSourceCoverageKind::SemanticEntity,
        SctImportedSourceTarget{sectionEntity}, std::nullopt, std::nullopt,
        SctSourceRegion::SectionPayload, true};
    auto duplicatePrimary = SctImportedSourceMap::build(8u,
        {primary, primary, leaf(0u, 8u)});
    EXPECT_FALSE(duplicatePrimary.map);
    EXPECT_TRUE(hasIssue(duplicatePrimary, SctSourceMapIssueCode::DuplicatePrimaryLocation));

    const auto missingPrimary = SctImportedSourceMap::build(8u,
        {leaf(0u, 8u, SctImportedSourceTarget{sectionEntity})});
    EXPECT_FALSE(missingPrimary.map);
    EXPECT_TRUE(hasIssue(missingPrimary, SctSourceMapIssueCode::MissingPrimaryLocation));

    auto containedPrimary = primary;
    containedPrimary.span.size = 4u;
    SctSourceSpanRecord outsideSection{{4u, 4u}, SctSourceSpanRole::Instruction,
        SctSourceSpanLayer::Envelope, SctSourceCoverageKind::SemanticEntity,
        std::nullopt, section, 4u, SctSourceRegion::SectionPayload, false};
    const auto invalidContainment = SctImportedSourceMap::build(8u,
        {containedPrimary, outsideSection, leaf(0u, 8u)});
    EXPECT_FALSE(invalidContainment.map);
    EXPECT_TRUE(hasIssue(invalidContainment, SctSourceMapIssueCode::InvalidContainingSection));

    auto wrongRelative = outsideSection;
    wrongRelative.span = {2u, 2u};
    wrongRelative.sectionRelativeOffset = 3u;
    const auto invalidRelative = SctImportedSourceMap::build(8u,
        {primary, wrongRelative, leaf(0u, 8u)});
    EXPECT_FALSE(invalidRelative.map);
    EXPECT_TRUE(hasIssue(invalidRelative, SctSourceMapIssueCode::InvalidSectionRelativeOffset));

    SctSourceSpanRecord firstEnvelope{{0u, 6u}, SctSourceSpanRole::FooterRegion,
        SctSourceSpanLayer::Envelope, SctSourceCoverageKind::SourceObservation};
    SctSourceSpanRecord crossingEnvelope{{4u, 4u}, SctSourceSpanRole::FooterEntry,
        SctSourceSpanLayer::Envelope, SctSourceCoverageKind::SourceObservation};
    const auto illegalOverlap = SctImportedSourceMap::build(8u,
        {firstEnvelope, crossingEnvelope, leaf(0u, 8u)});
    EXPECT_FALSE(illegalOverlap.map);
    EXPECT_TRUE(hasIssue(illegalOverlap, SctSourceMapIssueCode::IllegalEnvelopeOverlap));
}

TEST(SctImportLineage, BindingRejectsOtherImportsAndTracksHistoricalAddressability) {
    const auto first = SctDocumentImporter::import(importedGapParse());
    auto secondParse = importedGapParse();
    secondParse.file.originalPayloadBytes[0] = 1u;
    const auto second = SctDocumentImporter::import(secondParse);
    ASSERT_TRUE(first.document && second.document);
    EXPECT_NE(first.context.receipt.lineage, second.context.receipt.lineage);
    EXPECT_FALSE(first.context.bind(second.context.revisionProvenance));

    const auto platformVariant = SctDocumentImporter::import(importedGapParse(),
        {{SctPlatform::Dreamcast}, std::nullopt});
    const auto encodingVariant = SctDocumentImporter::import(importedGapParse(),
        {std::nullopt, kSctWindows1252Byte7FEncoding});
    auto wrapperParse = importedGapParse();
    wrapperParse.file.originalCompressedAklz = true;
    const auto wrapperVariant = SctDocumentImporter::import(wrapperParse);
    ASSERT_TRUE(platformVariant.document && encodingVariant.document && wrapperVariant.document);
    EXPECT_NE(first.context.receipt.lineage, platformVariant.context.receipt.lineage);
    EXPECT_NE(first.context.receipt.lineage, encodingVariant.context.receipt.lineage);
    EXPECT_NE(first.context.receipt.lineage, wrapperVariant.context.receipt.lineage);

    const auto evidence = first.context.bind(first.context.revisionProvenance);
    ASSERT_TRUE(evidence);
    EXPECT_EQ(SctDocumentAnalysis::build(*first.document, &*evidence).importedSiteAddressability,
        SctDocumentAnalysis::ImportedSiteAddressability::FullyAddressable);

    auto partiallyEdited = *first.document;
    auto& instructions = std::get<SctScriptSectionContent>(
        partiallyEdited.sections.front().content).instructions;
    instructions.erase(instructions.begin() + 1);
    EXPECT_EQ(SctDocumentAnalysis::build(partiallyEdited, &*evidence).importedSiteAddressability,
        SctDocumentAnalysis::ImportedSiteAddressability::PartiallyAddressable);

    SctDocument unrelated;
    EXPECT_EQ(SctDocumentAnalysis::build(unrelated, &*evidence).importedSiteAddressability,
        SctDocumentAnalysis::ImportedSiteAddressability::NoLongerAddressable);
}

TEST(SctImportedSourceMap, RecordsExactInstructionExpressionAndTextSitesInBothByteOrders) {
    for (const auto byteOrder : {SctDocumentOutputByteOrder::BigEndian,
                                 SctDocumentOutputByteOrder::LittleEndian}) {
        SctDocument document;
        const auto scriptSection = document.allocateSectionId();
        const auto instructionId = document.allocateInstructionId();
        auto expression = SctExpressionFactory::binaryOperator(SctExpressionBinaryOperator::Add,
            SctExpressionFactory::encodedDecimalLiteral(1),
            SctExpressionFactory::encodedDecimalLiteral(2));
        ASSERT_TRUE(expression.expression);
        SctDocumentInstruction instructionValue{instructionId, 114u};
        instructionValue.skipRefresh = true;
        instructionValue.scheduledExpression = SctExpressionFactory::encodedDecimalLiteral(3);
        instructionValue.fixedParameters = {
            parameter(0u, *expression.expression),
            parameter(1u, SctExpressionFactory::encodedDecimalLiteral(4)),
        };
        document.sections.push_back({scriptSection, "SCRIPT",
            SctScriptSectionContent{{instructionValue}}});

        const auto textSection = document.allocateSectionId();
        const auto stringId = document.allocateStringId();
        const auto command = SctTextBuilder::noArgumentCommand(SctMessageCommandCode::C);
        ASSERT_TRUE(command.command);
        const auto message = SctTextBuilder::message(std::string{"Speaker"},
            {SctTextChunk{"Hello world\n"}, *command.command});
        ASSERT_TRUE(message.message);
        document.sections.push_back({textSection, "TEXT", SctStringSectionContent{
            SctDocumentString{stringId, *message.message, SctTextKind::SctString},
            {9u, 0x1du}}});

        const SctDocumentExportOptions options{SctPlatform::GameCube,
            kSctShiftJisByte7FEncoding, byteOrder};
        const auto exported = SctDocumentExporter::exportDocument(document, options);
        ASSERT_TRUE(exported.success);
        const auto parsed = SctParser{}.parse(exported.bytes, "source-sites.sct");
        ASSERT_TRUE(parsed.parseOk);
        const auto imported = SctDocumentImporter::import(parsed,
            {{SctPlatform::GameCube}, kSctShiftJisByte7FEncoding});
        ASSERT_TRUE(imported.document);
        const auto records = imported.context.receipt.sourceMap.records();

        EXPECT_TRUE(std::any_of(records.begin(), records.end(), [](const auto& record) {
            return record.role == SctSourceSpanRole::InstructionModifier
                && record.layer == SctSourceSpanLayer::Leaf;
        }));
        EXPECT_TRUE(std::any_of(records.begin(), records.end(), [](const auto& record) {
            return record.role == SctSourceSpanRole::InstructionOpcode
                && record.layer == SctSourceSpanLayer::Leaf;
        }));
        EXPECT_TRUE(std::any_of(records.begin(), records.end(), [](const auto& record) {
            return record.role == SctSourceSpanRole::InstructionParameter
                && record.layer == SctSourceSpanLayer::Leaf && record.target
                && std::holds_alternative<SctParameterSite>(*record.target);
        }));
        EXPECT_TRUE(std::any_of(records.begin(), records.end(), [](const auto& record) {
            if (record.role != SctSourceSpanRole::Expression || !record.target
                || !std::holds_alternative<SctExpressionSite>(*record.target)) return false;
            return !std::get<SctExpressionSite>(*record.target).childPath.empty();
        }));
        EXPECT_TRUE(std::any_of(records.begin(), records.end(), [](const auto& record) {
            if (record.role != SctSourceSpanRole::Expression || !record.target
                || !std::holds_alternative<SctExpressionSite>(*record.target)) return false;
            return std::holds_alternative<SctScheduledExpressionSite>(
                std::get<SctExpressionSite>(*record.target).owner);
        }));
        EXPECT_TRUE(std::any_of(records.begin(), records.end(), [](const auto& record) {
            return record.role == SctSourceSpanRole::TextElement && record.target
                && std::holds_alternative<SctTextSite>(*record.target)
                && std::get<SctTextSite>(*record.target).region == SctTextRegion::Header;
        }));
        EXPECT_TRUE(std::any_of(records.begin(), records.end(), [](const auto& record) {
            return record.role == SctSourceSpanRole::TextTerminator && record.target
                && std::holds_alternative<SctTextSite>(*record.target);
        }));
    }
}

TEST(SctOpcodeEffectIndex, KeepsDeclaredEffectsVisibleAcrossEveryUsabilityState) {
    const auto buildEffect = [](SctDocumentParameterValue value, bool includeParameter = true) {
        SctDocument document;
        const auto section = document.allocateSectionId();
        SctDocumentInstruction load{document.allocateInstructionId(), 23u};
        if (includeParameter) load.fixedParameters.push_back(parameter(0u, std::move(value)));
        document.sections.push_back({section, "EFFECT", SctScriptSectionContent{{load}}});
        return SctOpcodeEffectIndex::build(document);
    };

    const SctFooterEntryId target{1u};
    const auto usable = buildEffect(SctFooterEntryReference{target});
    const auto unresolved = buildEffect(SctUnresolvedReferenceValue{
        {SctReferenceTargetStorage::FooterEntry, SctTextKind::PlainString},
        {0u}});
    const auto opaque = buildEffect(SctOpaqueParameterValue{{0u}});
    const auto missing = buildEffect(SctEncodedWordValue{}, false);
    const auto incompatible = buildEffect(SctEncodedWordValue{0u});
    const struct { const SctOpcodeEffectIndex* index; SctOpcodeEffectUsability expected; } cases[]{
        {&usable, SctOpcodeEffectUsability::Usable},
        {&unresolved, SctOpcodeEffectUsability::UnresolvedInput},
        {&opaque, SctOpcodeEffectUsability::OpaqueInput},
        {&missing, SctOpcodeEffectUsability::MissingInput},
        {&incompatible, SctOpcodeEffectUsability::IncompatibleInput},
    };
    for (const auto& item : cases) {
        ASSERT_EQ(item.index->effects().size(), 1u);
        EXPECT_EQ(item.index->effects().front().usability, item.expected);
        EXPECT_EQ(item.index->usableEffects().size(),
            item.expected == SctOpcodeEffectUsability::Usable ? 1u : 0u);
    }
}
