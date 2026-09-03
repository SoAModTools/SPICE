#include "../SpiceSCT/SpiceSCT.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <future>
#include <iterator>
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
        parameter(0u, SctExpressionFactory::oneWordValue(
            SctExpressionOneWordValue::Value7F7FFFFF)),
        parameter(1u, SctInstructionReference{ids[6]}),
    };

    auto switchInstruction = instruction(ids[1], 3u);
    switchInstruction.fixedParameters = {
        parameter(0u, SctExpressionFactory::oneWordValue(
            SctExpressionOneWordValue::Value7F7FFFFF)),
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

bool sourceSpanContains(SctImportedByteSpan outer, SctImportedByteSpan inner) {
    return outer.offset <= inner.offset && outer.endOffset() >= inner.endOffset();
}

SctSourceRecordSummary sourceSummary(
    std::span<const SctSourceSpanRecord> records, std::uint32_t ordinal) {
    const auto& record = records[ordinal];
    return {{ordinal}, record.span, record.role, record.layer, record.target};
}

SctSourceRecordNeighborhood linearNeighborhoodReference(
    std::span<const SctSourceSpanRecord> records, SctImportedByteSpan span,
    std::optional<std::uint32_t> excludedOrdinal = std::nullopt) {
    SctSourceRecordNeighborhood result;
    for (std::uint32_t ordinal = 0; ordinal < records.size(); ++ordinal) {
        if (excludedOrdinal && ordinal == *excludedOrdinal) continue;
        const auto& record = records[ordinal];
        if (!record.target || record.layer != SctSourceSpanLayer::Leaf) continue;
        if (record.span.endOffset() <= span.offset) {
            result.precedingTargetedLeaf = sourceSummary(records, ordinal);
            continue;
        }
        if (record.span.offset >= span.endOffset()) {
            result.followingTargetedLeaf = sourceSummary(records, ordinal);
            break;
        }
    }
    for (std::uint32_t ordinal = 0; ordinal < records.size(); ++ordinal) {
        if (excludedOrdinal && ordinal == *excludedOrdinal) continue;
        const auto& record = records[ordinal];
        if (record.target && record.layer == SctSourceSpanLayer::Envelope
            && sourceSpanContains(record.span, span)) {
            result.containingTargetedEnvelopes.push_back(sourceSummary(records, ordinal));
        }
    }
    std::stable_sort(result.containingTargetedEnvelopes.begin(),
        result.containingTargetedEnvelopes.end(), [](const auto& left, const auto& right) {
            if (left.span.size != right.span.size) return left.span.size < right.span.size;
            return left.ordinal.value > right.ordinal.value;
        });
    return result;
}

void expectSourceSummaryEqual(const std::optional<SctSourceRecordSummary>& actual,
    const std::optional<SctSourceRecordSummary>& expected) {
    ASSERT_EQ(actual.has_value(), expected.has_value());
    if (!actual) return;
    EXPECT_EQ(actual->ordinal, expected->ordinal);
    EXPECT_EQ(actual->span, expected->span);
    EXPECT_EQ(actual->role, expected->role);
    EXPECT_EQ(actual->layer, expected->layer);
    EXPECT_EQ(actual->target, expected->target);
}

void expectNeighborhoodEqual(const SctSourceRecordNeighborhood& actual,
    const SctSourceRecordNeighborhood& expected) {
    expectSourceSummaryEqual(actual.precedingTargetedLeaf, expected.precedingTargetedLeaf);
    expectSourceSummaryEqual(actual.followingTargetedLeaf, expected.followingTargetedLeaf);
    ASSERT_EQ(actual.containingTargetedEnvelopes.size(),
        expected.containingTargetedEnvelopes.size());
    for (std::size_t index = 0; index < actual.containingTargetedEnvelopes.size(); ++index) {
        expectSourceSummaryEqual(actual.containingTargetedEnvelopes[index],
            expected.containingTargetedEnvelopes[index]);
    }
}

SctCanonicalExpression variableExpression() {
    return {SctTypedScptProgram{{
        SctScptValueOperation{SctScptValueKind::FloatBackedIntegerVariable, 0x50000018u, {}},
        SctScptValueOperation{SctScptValueKind::IntegerVariable, 0x50000008u, {}},
        SctScptValueOperation{SctScptValueKind::IntegerVariableLow16Comparison, 0x5000000fu, {}},
        SctScptValueOperation{SctScptValueKind::FloatVariable, 0x40000003u, {}},
        SctScptValueOperation{SctScptValueKind::BitVariable, 0x20000004u, {}},
        SctScptValueOperation{SctScptValueKind::ByteVariable, 0x10000005u, {}}}},
        SctExpressionTermination::StopCode};
}

SctDocument semanticCompositionDocument() {
    SctDocument document;
    const auto firstSection = document.allocateSectionId();
    const auto labelSection = document.allocateSectionId();
    const auto secondSection = document.allocateSectionId();
    const auto richId = document.allocateInstructionId();
    const auto loadMldId = document.allocateInstructionId();
    const auto groundId = document.allocateInstructionId();
    const auto loadScriptId = document.allocateInstructionId();
    const auto secondRichId = document.allocateInstructionId();
    const auto targetId = document.allocateInstructionId();
    const auto stringId = document.allocateStringId();
    const auto footerId = document.allocateFooterEntryId();

    auto rich = instruction(richId, 100u);
    rich.scheduledExpression = SctCanonicalExpression{SctOpaqueExpression{{0x17u, 0x1du}},
        SctExpressionTermination::StopCode};
    rich.fixedParameters = {
        parameter(0u, variableExpression()),
        parameter(1u, SctInstructionReference{targetId}),
        parameter(2u, SctStringReference{stringId}),
        parameter(3u, SctFooterEntryReference{footerId}),
        parameter(4u, SctUnresolvedReferenceValue{{SctReferenceTargetStorage::Instruction,
            std::nullopt}, {4u}}),
        parameter(5u, SctOpaqueParameterValue{{1u, 2u}}),
    };

    auto loadMld = instruction(loadMldId, 23u);
    loadMld.fixedParameters = {parameter(0u, SctFooterEntryReference{footerId})};

    auto ground = instruction(groundId, 114u);
    ground.fixedParameters = {parameter(0u, SctExpressionFactory::oneWordValue(
        SctExpressionOneWordValue::Value7F7FFFFF))};

    auto loadScript = instruction(loadScriptId, 43u);
    loadScript.fixedParameters = {parameter(0u, SctFooterEntryReference{footerId})};

    auto secondRich = instruction(secondRichId, 100u);
    secondRich.fixedParameters = {parameter(0u, variableExpression())};

    document.sections.push_back({firstSection, "FIRST",
        SctScriptSectionContent{{std::move(rich), std::move(loadMld)}}});
    document.sections.push_back({labelSection, "BETWEEN", SctOpaqueSectionContent{}});
    document.sections.push_back({secondSection, "SECOND", SctScriptSectionContent{{
        std::move(ground), std::move(loadScript), std::move(secondRich),
        instruction(targetId, 12u)}}});
    return document;
}

template <typename Record, typename Member>
std::vector<Record> flattenContributions(
    std::span<const SctInstructionSemanticContribution> contributions, Member member) {
    std::vector<Record> result;
    for (const auto& contribution : contributions) {
        const auto& records = contribution.*member;
        result.insert(result.end(), records.begin(), records.end());
    }
    return result;
}

void expectOpcodeUsagesEqual(std::span<const SctOpcodeUsage> expected,
    std::span<const SctOpcodeUsage> actual) {
    ASSERT_EQ(expected.size(), actual.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        SCOPED_TRACE(index);
        EXPECT_EQ(expected[index].opcode, actual[index].opcode);
        EXPECT_EQ(expected[index].instruction, actual[index].instruction);
    }
}

void expectReferenceUsagesEqual(std::span<const SctReferenceUsage> expected,
    std::span<const SctReferenceUsage> actual) {
    ASSERT_EQ(expected.size(), actual.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        SCOPED_TRACE(index);
        EXPECT_EQ(expected[index].source, actual[index].source);
        EXPECT_EQ(expected[index].target, actual[index].target);
    }
}

void expectVariableUsagesEqual(std::span<const SctVariableUsage> expected,
    std::span<const SctVariableUsage> actual) {
    ASSERT_EQ(expected.size(), actual.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        SCOPED_TRACE(index);
        EXPECT_EQ(expected[index].variable, actual[index].variable);
        EXPECT_EQ(expected[index].encodedForm, actual[index].encodedForm);
        EXPECT_EQ(expected[index].source, actual[index].source);
    }
}

void expectUnresolvedUsagesEqual(std::span<const SctUnresolvedReferenceUsage> expected,
    std::span<const SctUnresolvedReferenceUsage> actual) {
    ASSERT_EQ(expected.size(), actual.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        SCOPED_TRACE(index);
        EXPECT_EQ(expected[index].source, actual[index].source);
        EXPECT_EQ(expected[index].expectedTarget, actual[index].expectedTarget);
        EXPECT_EQ(expected[index].encodedWordCount, actual[index].encodedWordCount);
    }
}

void expectOpaqueParameterUsagesEqual(std::span<const SctOpaqueParameterUsage> expected,
    std::span<const SctOpaqueParameterUsage> actual) {
    ASSERT_EQ(expected.size(), actual.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        SCOPED_TRACE(index);
        EXPECT_EQ(expected[index].source, actual[index].source);
        EXPECT_EQ(expected[index].wordCount, actual[index].wordCount);
    }
}

void expectOpaqueExpressionUsagesEqual(std::span<const SctOpaqueExpressionUsage> expected,
    std::span<const SctOpaqueExpressionUsage> actual) {
    ASSERT_EQ(expected.size(), actual.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        SCOPED_TRACE(index);
        EXPECT_EQ(expected[index].source, actual[index].source);
        EXPECT_EQ(expected[index].wordCount, actual[index].wordCount);
    }
}

void expectEffectsEqual(std::span<const SctOpcodeEffectOccurrence> expected,
    std::span<const SctOpcodeEffectOccurrence> actual) {
    ASSERT_EQ(expected.size(), actual.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        SCOPED_TRACE(index);
        EXPECT_EQ(expected[index].usability, actual[index].usability);
        ASSERT_EQ(expected[index].effect.index(), actual[index].effect.index());
        if (const auto* expectedLoad = std::get_if<SctResourceLoadEffect>(&expected[index].effect)) {
            const auto& actualLoad = std::get<SctResourceLoadEffect>(actual[index].effect);
            EXPECT_EQ(expectedLoad->sourceInstruction, actualLoad.sourceInstruction);
            EXPECT_EQ(expectedLoad->resource, actualLoad.resource);
            EXPECT_EQ(expectedLoad->resourceParameter, actualLoad.resourceParameter);
            EXPECT_EQ(expectedLoad->confidence, actualLoad.confidence);
        } else {
            const auto& expectedGround = std::get<SctGroundVariantSelectionEffect>(
                expected[index].effect);
            const auto& actualGround = std::get<SctGroundVariantSelectionEffect>(
                actual[index].effect);
            EXPECT_EQ(expectedGround.sourceInstruction, actualGround.sourceInstruction);
            EXPECT_EQ(expectedGround.tableIdParameter, actualGround.tableIdParameter);
            EXPECT_EQ(expectedGround.variantParameter, actualGround.variantParameter);
            EXPECT_EQ(expectedGround.confidence, actualGround.confidence);
        }
    }
}

std::vector<SctInstructionSemanticContribution> contributionsInPhysicalOrder(
    const SctDocument& document) {
    std::vector<SctInstructionSemanticContribution> result;
    for (const auto& section : document.sections) {
        const auto* script = std::get_if<SctScriptSectionContent>(&section.content);
        if (script == nullptr) continue;
        for (const auto& value : script->instructions) {
            result.push_back(SctInstructionSemanticAnalyzer::build(value));
        }
    }
    return result;
}

} // namespace

TEST(SctImportedSourceMap, ProvidesExactCoverageTopologyAndImportedCrossings) {
    const auto imported = SctDocumentImporter::import(importedGapParse());
    ASSERT_TRUE(imported.document.has_value());
    ASSERT_TRUE(imported.context.receipt().sourceMap.hasCompleteLeafCoverage());
    ASSERT_EQ(imported.document->sections.size(), 1u);
    const auto& script = std::get<SctScriptSectionContent>(imported.document->sections[0].content);
    ASSERT_EQ(script.instructions.size(), 2u);
    ASSERT_EQ(imported.document->opaqueAttachments.size(), 1u);

    const SctDocumentEntityId source{script.instructions[0].id};
    const SctDocumentEntityId target{script.instructions[1].id};
    const SctDocumentEntityId gap{imported.document->opaqueAttachments[0].id};
    EXPECT_EQ(imported.context.receipt().sourceMap.location(source)->primarySpan, (SctImportedByteSpan{32u, 8u}));
    EXPECT_EQ(imported.context.receipt().sourceMap.location(target)->primarySpan, (SctImportedByteSpan{52u, 4u}));
    EXPECT_EQ(imported.context.receipt().sourceMap.location(gap)->primarySpan, (SctImportedByteSpan{40u, 12u}));
    EXPECT_EQ(imported.context.receipt().sourceMap.previousSemanticEntity(gap), source);
    EXPECT_EQ(imported.context.receipt().sourceMap.nextSemanticEntity(gap), target);
    EXPECT_EQ(imported.context.receipt().sourceMap.relationship(source, target), SctSourceRelationship::Before);
    EXPECT_EQ(imported.context.receipt().sourceMap.relationship(
        SctDocumentEntityId{imported.document->sections[0].id}, source),
        SctSourceRelationship::Contains);
    EXPECT_TRUE(imported.context.receipt().sourceMap.semanticEntitiesBetween(source, target).empty());
    const auto containingGap = imported.context.receipt().sourceMap.recordsContaining({40u, 12u});
    EXPECT_TRUE(std::any_of(containingGap.begin(), containingGap.end(), [](const auto& record) {
        return record.role == SctSourceSpanRole::SectionPayload;
    }));
    const auto sourceRecords = imported.context.receipt().sourceMap.recordsFor(source);
    EXPECT_TRUE(std::any_of(sourceRecords.begin(), sourceRecords.end(), [](const auto& record) {
        return record.role == SctSourceSpanRole::Instruction
            && record.layer == SctSourceSpanLayer::Envelope && record.primaryEntityLocation;
    }));
    EXPECT_TRUE(std::any_of(sourceRecords.begin(), sourceRecords.end(), [](const auto& record) {
        return record.role == SctSourceSpanRole::InstructionOpcode
            && record.layer == SctSourceSpanLayer::Leaf && !record.primaryEntityLocation;
    }));

    const auto atGap = imported.context.receipt().sourceMap.recordsAt(44u);
    EXPECT_TRUE(std::any_of(atGap.begin(), atGap.end(), [](const auto& record) {
        return record.role == SctSourceSpanRole::SectionPayload
            && record.layer == SctSourceSpanLayer::Envelope;
    }));
    EXPECT_TRUE(std::any_of(atGap.begin(), atGap.end(), [](const auto& record) {
        return record.role == SctSourceSpanRole::OpaqueAttachment
            && record.layer == SctSourceSpanLayer::Leaf;
    }));

    const auto evidence = imported.context.bind(imported.context.revisionProvenance());
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
    EXPECT_EQ(imported.context.receipt().sourceMap.location(target)->primarySpan, (SctImportedByteSpan{52u, 4u}));
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
        return item.encodedForm == SctScptValueKind::IntegerVariableLow16Comparison
            && item.source.operationOrdinal == 2u;
    }));
    ASSERT_EQ(usage.unresolvedReferences().size(), 1u);
    EXPECT_EQ(usage.unresolvedReferences()[0].source.parameter.schemaIndex, 4u);
    ASSERT_EQ(usage.opaqueParameters().size(), 1u);
    EXPECT_EQ(usage.opaqueParameters()[0].wordCount, 2u);
    ASSERT_EQ(usage.opaqueExpressions().size(), 1u);
    EXPECT_TRUE(std::holds_alternative<SctScheduledExpressionSite>(
        usage.opaqueExpressions()[0].source.owner));
}

TEST(SctInstructionSemanticAnalyzer, ContributionsComposeIntoWholeDocumentIndexes) {
    auto document = semanticCompositionDocument();
    const auto contributions = contributionsInPhysicalOrder(document);
    std::vector<SctOpcodeUsage> expectedOpcodes;
    for (const auto& contribution : contributions) expectedOpcodes.push_back(contribution.opcode);
    const auto expectedReferences = flattenContributions<SctReferenceUsage>(contributions,
        &SctInstructionSemanticContribution::references);
    const auto expectedVariables = flattenContributions<SctVariableUsage>(contributions,
        &SctInstructionSemanticContribution::variables);
    const auto expectedUnresolved = flattenContributions<SctUnresolvedReferenceUsage>(contributions,
        &SctInstructionSemanticContribution::unresolvedReferences);
    const auto expectedOpaqueParameters = flattenContributions<SctOpaqueParameterUsage>(contributions,
        &SctInstructionSemanticContribution::opaqueParameters);
    const auto expectedOpaqueExpressions = flattenContributions<SctOpaqueExpressionUsage>(contributions,
        &SctInstructionSemanticContribution::opaqueExpressions);
    const auto expectedEffects = flattenContributions<SctOpcodeEffectOccurrence>(contributions,
        &SctInstructionSemanticContribution::effects);
    ASSERT_FALSE(expectedOpcodes.empty());
    ASSERT_FALSE(expectedReferences.empty());
    ASSERT_FALSE(expectedVariables.empty());
    ASSERT_FALSE(expectedUnresolved.empty());
    ASSERT_FALSE(expectedOpaqueParameters.empty());
    ASSERT_FALSE(expectedOpaqueExpressions.empty());
    ASSERT_FALSE(expectedEffects.empty());

    const auto directUsage = SctSemanticUsageIndex::build(document);
    const auto composedUsage = SctSemanticUsageIndex::build(contributions);
    for (const auto* usage : {&directUsage, &composedUsage}) {
        expectOpcodeUsagesEqual(expectedOpcodes, usage->opcodeUsages());
        expectReferenceUsagesEqual(expectedReferences, usage->referenceUsages());
        expectVariableUsagesEqual(expectedVariables, usage->variableUsages());
        expectUnresolvedUsagesEqual(expectedUnresolved, usage->unresolvedReferences());
        expectOpaqueParameterUsagesEqual(expectedOpaqueParameters, usage->opaqueParameters());
        expectOpaqueExpressionUsagesEqual(expectedOpaqueExpressions, usage->opaqueExpressions());
    }

    const auto directEffects = SctOpcodeEffectIndex::build(document);
    const auto composedEffects = SctOpcodeEffectIndex::build(contributions);
    expectEffectsEqual(expectedEffects, directEffects.effects());
    expectEffectsEqual(expectedEffects, composedEffects.effects());

    const auto& firstScript = std::get<SctScriptSectionContent>(
        document.sections[0].content).instructions;
    const auto& secondScript = std::get<SctScriptSectionContent>(
        document.sections[2].content).instructions;
    const auto richId = firstScript[0].id;
    const auto loadMldId = firstScript[1].id;

    std::vector<SctOpcodeUsage> expectedOpcode100;
    std::copy_if(expectedOpcodes.begin(), expectedOpcodes.end(),
        std::back_inserter(expectedOpcode100),
        [](const auto& usage) { return usage.opcode == 100u; });
    expectOpcodeUsagesEqual(expectedOpcode100, composedUsage.usagesForOpcode(100u));

    std::vector<SctReferenceUsage> expectedOutbound;
    std::copy_if(expectedReferences.begin(), expectedReferences.end(),
        std::back_inserter(expectedOutbound), [richId](const auto& usage) {
            return usage.source.instruction == richId;
        });
    expectReferenceUsagesEqual(expectedOutbound, composedUsage.outboundReferences(richId));

    const SctVariableIdentity repeatedVariable{SctVariableKind::Integer, 24u};
    std::vector<SctVariableUsage> expectedVariableQuery;
    std::copy_if(expectedVariables.begin(), expectedVariables.end(),
        std::back_inserter(expectedVariableQuery), [repeatedVariable](const auto& usage) {
            return usage.variable == repeatedVariable;
        });
    expectVariableUsagesEqual(expectedVariableQuery,
        composedUsage.usagesForVariable(repeatedVariable));

    std::vector<SctOpcodeEffectOccurrence> expectedInstructionEffects;
    std::copy_if(expectedEffects.begin(), expectedEffects.end(),
        std::back_inserter(expectedInstructionEffects), [loadMldId](const auto& occurrence) {
            return std::visit([loadMldId](const auto& effect) {
                return effect.sourceInstruction == loadMldId;
            }, occurrence.effect);
        });
    expectEffectsEqual(expectedInstructionEffects,
        composedEffects.effectsForInstruction(loadMldId));

    std::vector<SctOpcodeEffectOccurrence> expectedUsableEffects;
    std::copy_if(expectedEffects.begin(), expectedEffects.end(),
        std::back_inserter(expectedUsableEffects), [](const auto& occurrence) {
            return occurrence.usability == SctOpcodeEffectUsability::Usable;
        });
    ASSERT_GE(expectedUsableEffects.size(), 2u);
    EXPECT_TRUE(std::any_of(expectedEffects.begin(), expectedEffects.end(), [](const auto& occurrence) {
        return occurrence.usability != SctOpcodeEffectUsability::Usable;
    }));
    expectEffectsEqual(expectedUsableEffects, composedEffects.usableEffects());

    auto reversedContributions = contributions;
    std::reverse(reversedContributions.begin(), reversedContributions.end());
    std::vector<SctOpcodeUsage> reversedOpcodes;
    for (const auto& contribution : reversedContributions) {
        reversedOpcodes.push_back(contribution.opcode);
    }
    const auto reversedEffects = flattenContributions<SctOpcodeEffectOccurrence>(
        reversedContributions, &SctInstructionSemanticContribution::effects);
    expectOpcodeUsagesEqual(reversedOpcodes,
        SctSemanticUsageIndex::build(reversedContributions).opcodeUsages());
    expectEffectsEqual(reversedEffects,
        SctOpcodeEffectIndex::build(reversedContributions).effects());

    auto edited = firstScript.front();
    const auto unchangedBefore = SctInstructionSemanticAnalyzer::build(secondScript[1]);
    edited.opcode = 12u;
    edited.fixedParameters.clear();
    const auto editedContribution = SctInstructionSemanticAnalyzer::build(edited);
    const auto unchangedAfter = SctInstructionSemanticAnalyzer::build(secondScript[1]);
    EXPECT_NE(editedContribution.opcode.opcode, contributions.front().opcode.opcode);
    EXPECT_EQ(unchangedBefore.opcode.opcode, unchangedAfter.opcode.opcode);
    expectReferenceUsagesEqual(unchangedBefore.references, unchangedAfter.references);
    expectEffectsEqual(unchangedBefore.effects, unchangedAfter.effects);
}

TEST(SctOpaqueContextIndex, LabelsSwitchCrossingsWithoutAssigningOwnership) {
    const auto imported = SctDocumentImporter::import(importedGapParse());
    ASSERT_TRUE(imported.document.has_value());
    auto context = imported.context;
    auto receipt = context.receipt();
    ASSERT_EQ(receipt.controlFlow.size(), 1u);
    receipt.controlFlow[0].kind = SctControlFlowKind::SwitchCase;
    receipt.controlFlow[0].origin = SctParameterSite{receipt.controlFlow[0].sourceInstruction,
        {3u, 0u}};
    context = SctDocumentImportContext{std::move(receipt)};
    const auto evidence = context.bind(context.revisionProvenance());
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
        {changedLayout.allocateSectionId(), "INSERTED", SctOpaqueSectionContent{}});
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

TEST(SctImportedSourceMap, NeighborhoodsExposeTargetedSitesWithoutInferringOwnership) {
    const SctInstructionId instructionId{1u};
    const SctOpaqueAttachmentId attachmentId{1u};
    const SctParameterSite firstParameter{instructionId, {0u, std::nullopt}};
    const SctParameterSite secondParameter{instructionId, {1u, std::nullopt}};
    const SctExpressionSite expression{instructionId, SctParameterAddress{0u, std::nullopt}};
    const SctDocumentEntityId instructionEntity{instructionId};
    const SctDocumentEntityId attachmentEntity{attachmentId};
    auto built = SctImportedSourceMap::build(16u, {
        {{0u, 16u}, SctSourceSpanRole::Instruction, SctSourceSpanLayer::Envelope,
            SctSourceCoverageKind::SemanticEntity, instructionEntity, std::nullopt,
            std::nullopt, SctSourceRegion::SectionPayload, true},
        {{0u, 4u}, SctSourceSpanRole::InstructionParameter, SctSourceSpanLayer::Leaf,
            SctSourceCoverageKind::SemanticEntity, SctImportedSourceTarget{firstParameter}},
        {{4u, 8u}, SctSourceSpanRole::Expression, SctSourceSpanLayer::Envelope,
            SctSourceCoverageKind::SemanticEntity, SctImportedSourceTarget{expression}},
        {{4u, 4u}, SctSourceSpanRole::DerivedPadding, SctSourceSpanLayer::Leaf,
            SctSourceCoverageKind::DerivedLayout},
        {{8u, 4u}, SctSourceSpanRole::OpaqueAttachment, SctSourceSpanLayer::Leaf,
            SctSourceCoverageKind::OpaqueAttachment, attachmentEntity, std::nullopt,
            std::nullopt, SctSourceRegion::SectionPayload, true},
        {{12u, 4u}, SctSourceSpanRole::InstructionParameter, SctSourceSpanLayer::Leaf,
            SctSourceCoverageKind::SemanticEntity, SctImportedSourceTarget{secondParameter}},
    });
    ASSERT_TRUE(built.map);

    std::optional<SctSourceRecordOrdinal> attachmentOrdinal;
    const auto records = built.map->records();
    for (std::uint32_t ordinal = 0; ordinal < records.size(); ++ordinal) {
        if (records[ordinal].target == std::optional<SctImportedSourceTarget>{attachmentEntity}) {
            attachmentOrdinal = SctSourceRecordOrdinal{ordinal};
            break;
        }
    }
    ASSERT_TRUE(attachmentOrdinal);
    const auto neighborhood = built.map->neighborhood(*attachmentOrdinal);
    ASSERT_TRUE(neighborhood);
    ASSERT_TRUE(neighborhood->precedingTargetedLeaf);
    ASSERT_TRUE(neighborhood->followingTargetedLeaf);
    EXPECT_EQ(neighborhood->precedingTargetedLeaf->target,
        std::optional<SctImportedSourceTarget>{firstParameter});
    EXPECT_EQ(neighborhood->followingTargetedLeaf->target,
        std::optional<SctImportedSourceTarget>{secondParameter});
    ASSERT_EQ(neighborhood->containingTargetedEnvelopes.size(), 2u);
    EXPECT_EQ(neighborhood->containingTargetedEnvelopes[0].target,
        std::optional<SctImportedSourceTarget>{expression});
    EXPECT_EQ(neighborhood->containingTargetedEnvelopes[1].target,
        std::optional<SctImportedSourceTarget>{instructionEntity});
    EXPECT_FALSE(built.map->neighborhood({0u, 4u}).precedingTargetedLeaf);
    EXPECT_FALSE(built.map->recordSummary({999u}));
}

TEST(SctImportedSourceMap, IndexedNeighborhoodsMatchLinearReference) {
    constexpr std::uint32_t payloadSize = 4096u;
    const SctInstructionId instructionId{1u};
    std::vector<SctSourceSpanRecord> records{
        {{0u, payloadSize}, SctSourceSpanRole::Instruction,
            SctSourceSpanLayer::Envelope, SctSourceCoverageKind::SemanticEntity,
            SctImportedSourceTarget{SctExpressionSite{
                instructionId, SctScheduledExpressionSite{}}}},
        {{256u, 2048u}, SctSourceSpanRole::Expression,
            SctSourceSpanLayer::Envelope, SctSourceCoverageKind::SemanticEntity,
            SctImportedSourceTarget{SctExpressionSite{
                instructionId, SctParameterAddress{0u, std::nullopt}}}},
        {{256u, 2048u}, SctSourceSpanRole::Expression,
            SctSourceSpanLayer::Envelope, SctSourceCoverageKind::SemanticEntity,
            SctImportedSourceTarget{SctExpressionSite{
                instructionId, SctParameterAddress{1u, std::nullopt}}}},
        {{512u, 512u}, SctSourceSpanRole::ExpressionOperation,
            SctSourceSpanLayer::Envelope, SctSourceCoverageKind::SemanticEntity,
            SctImportedSourceTarget{SctExpressionOperationSite{
                {instructionId, SctParameterAddress{1u, std::nullopt}}, 0u}}},
    };
    for (std::uint32_t offset = 0u; offset < payloadSize; offset += 4u) {
        const auto ordinal = offset / 4u;
        const bool targeted = (ordinal % 11u) == 0u;
        records.push_back({{offset, 4u}, targeted
                ? SctSourceSpanRole::InstructionParameter
                : SctSourceSpanRole::DerivedPadding,
            SctSourceSpanLayer::Leaf, targeted
                ? SctSourceCoverageKind::SemanticEntity
                : SctSourceCoverageKind::DerivedLayout,
            targeted ? std::optional<SctImportedSourceTarget>{SctParameterSite{
                instructionId, {ordinal, std::nullopt}}} : std::nullopt});
    }

    auto built = SctImportedSourceMap::build(payloadSize, std::move(records));
    ASSERT_TRUE(built.map);
    const auto sourceRecords = built.map->records();
    const std::array spans{
        SctImportedByteSpan{0u, 0u}, SctImportedByteSpan{0u, 4u},
        SctImportedByteSpan{252u, 8u}, SctImportedByteSpan{512u, 4u},
        SctImportedByteSpan{800u, 0u}, SctImportedByteSpan{1024u, 64u},
        SctImportedByteSpan{2304u, 0u}, SctImportedByteSpan{4090u, 6u},
        SctImportedByteSpan{4096u, 0u}};
    for (const auto span : spans) {
        SCOPED_TRACE(testing::PrintToString(span.offset));
        expectNeighborhoodEqual(built.map->neighborhood(span),
            linearNeighborhoodReference(sourceRecords, span));
    }

    for (std::uint32_t ordinal = 0u; ordinal < sourceRecords.size(); ordinal += 29u) {
        SCOPED_TRACE(ordinal);
        const auto actual = built.map->neighborhood(SctSourceRecordOrdinal{ordinal});
        ASSERT_TRUE(actual);
        expectNeighborhoodEqual(*actual, linearNeighborhoodReference(
            sourceRecords, sourceRecords[ordinal].span, ordinal));
    }
}

TEST(SctImportLineage, BindingRejectsOtherImportsAndTracksHistoricalAddressability) {
    const auto first = SctDocumentImporter::import(importedGapParse());
    auto secondParse = importedGapParse();
    secondParse.file.originalPayloadBytes[0] = 1u;
    const auto second = SctDocumentImporter::import(secondParse);
    ASSERT_TRUE(first.document && second.document);
    EXPECT_NE(first.context.receipt().lineage, second.context.receipt().lineage);
    EXPECT_FALSE(first.context.bind(second.context.revisionProvenance()));

    const auto platformVariant = SctDocumentImporter::import(importedGapParse(),
        {{SctPlatform::Dreamcast}, std::nullopt});
    const auto encodingVariant = SctDocumentImporter::import(importedGapParse(),
        {std::nullopt, kSctWindows1252Byte7FEncoding});
    auto wrapperParse = importedGapParse();
    wrapperParse.file.originalCompressedAklz = true;
    const auto wrapperVariant = SctDocumentImporter::import(wrapperParse);
    ASSERT_TRUE(platformVariant.document && encodingVariant.document && wrapperVariant.document);
    EXPECT_NE(first.context.receipt().lineage, platformVariant.context.receipt().lineage);
    EXPECT_NE(first.context.receipt().lineage, encodingVariant.context.receipt().lineage);
    EXPECT_NE(first.context.receipt().lineage, wrapperVariant.context.receipt().lineage);

    const auto evidence = first.context.bind(first.context.revisionProvenance());
    ASSERT_TRUE(evidence);
    const auto originalAnalysis = SctDocumentAnalysis::build(*first.document, &*evidence);
    ASSERT_TRUE(originalAnalysis.importedSites);
    EXPECT_EQ(originalAnalysis.importedSites->summary(),
        SctImportedAddressabilitySummary::FullyAddressable);

    auto partiallyEdited = *first.document;
    auto& instructions = std::get<SctScriptSectionContent>(
        partiallyEdited.sections.front().content).instructions;
    instructions.erase(instructions.begin() + 1);
    const auto partialAnalysis = SctDocumentAnalysis::build(partiallyEdited, &*evidence);
    ASSERT_TRUE(partialAnalysis.importedSites);
    EXPECT_EQ(partialAnalysis.importedSites->summary(),
        SctImportedAddressabilitySummary::PartiallyAddressable);

    SctDocument unrelated;
    const auto unrelatedAnalysis = SctDocumentAnalysis::build(unrelated, &*evidence);
    ASSERT_TRUE(unrelatedAnalysis.importedSites);
    EXPECT_EQ(unrelatedAnalysis.importedSites->summary(),
        SctImportedAddressabilitySummary::NoLongerAddressable);
}

TEST(SctImportLineage, BoundEvidenceOwnsImmutableReceiptAcrossContextLifetime) {
    SctDocument document;
    const auto evidence = [&]() {
        auto imported = SctDocumentImporter::import(importedGapParse(),
            {{SctPlatform::GameCube}, kSctShiftJisByte7FEncoding});
        EXPECT_TRUE(imported.document.has_value());
        document = *imported.document;
        auto movedContext = std::move(imported.context);
        auto copiedContext = movedContext;
        const auto bound = copiedContext.bind(copiedContext.revisionProvenance());
        EXPECT_TRUE(bound.has_value());
        return *bound;
    }();

    EXPECT_TRUE(evidence.receipt().sourceMap.hasCompleteLeafCoverage());
    const auto analysis = SctDocumentAnalysis::build(document, &evidence);
    ASSERT_TRUE(analysis.importedSites);
    EXPECT_EQ(analysis.importedSites->summary(),
        SctImportedAddressabilitySummary::FullyAddressable);
    EXPECT_TRUE(SctDocumentValidator::validateForTarget(document,
        SctPlatform::GameCube, kSctShiftJisByte7FEncoding, &evidence).validForTarget);
    EXPECT_TRUE(SctReferenceRepair::analyze(document, &evidence).diagnostics.empty());

    const SctDocumentExportOptions options{SctPlatform::GameCube,
        kSctShiftJisByte7FEncoding, SctDocumentOutputByteOrder::BigEndian};
    EXPECT_TRUE(SctDocumentLayoutEngine::layout(document, options, &evidence).success);
    EXPECT_TRUE(SctDocumentExporter::exportDocument(document, options, &evidence).success);

    std::array<std::future<std::size_t>, 4> reads;
    for (auto& read : reads) {
        read = std::async(std::launch::async, [evidence] {
            return evidence.receipt().sourceMap.records().size();
        });
    }
    for (auto& read : reads) EXPECT_GT(read.get(), 0u);
}

TEST(SctImportedSiteAddressability, DistinguishesExactParentEntityAndMissingSites) {
    SctDocument document;
    const auto scriptSectionId = document.allocateSectionId();
    const auto instructionId = document.allocateInstructionId();
    const SctCanonicalExpression expressionProgram{SctTypedScptProgram{{
        SctScptValueOperation{SctScptValueKind::DecimalLiteral, 0x08000001u, {}},
        SctScptValueOperation{SctScptValueKind::DecimalLiteral, 0x08000002u, {}},
        SctScptBinaryOperation{SctScptBinaryOperationKind::Comparison, 7u}}},
        SctExpressionTermination::StopCode};
    auto scripted = instruction(instructionId, 3u);
    scripted.scheduledExpression = SctExpressionFactory::encodedDecimalLiteral(1);
    scripted.fixedParameters = {parameter(0u, expressionProgram)};
    scripted.repeatedParameterGroups = {{{parameter(3u, SctEncodedWordValue{4u})}}};
    document.sections.push_back({scriptSectionId, "SCRIPT",
        SctScriptSectionContent{{scripted}}});

    const auto stringSectionId = document.allocateSectionId();
    const auto stringId = document.allocateStringId();
    SctMessage message;
    message.headerUtf8 = "Head";
    message.body.elements.push_back(SctTextChunk{"Body"});
    document.sections.push_back({stringSectionId, "TEXT", SctStringSectionContent{
        SctDocumentString{stringId, message, SctTextKind::SctString}, {9u, 0x1du}}});

    const auto footerId = document.allocateFooterEntryId();
    document.footerEntries.push_back(
        {footerId, SctTextKind::PlainString, SctPlainText{"Plain"}});

    const SctParameterSite fixedSite{instructionId, {0u, std::nullopt}};
    const SctParameterSite repeatedSite{instructionId, {3u, 0u}};
    const SctExpressionSite scheduledSite{instructionId, SctScheduledExpressionSite{}};
    const SctExpressionOperationSite operationSite{
        SctExpressionSite{instructionId, SctParameterAddress{0u, std::nullopt}}, 1u};
    const SctTextSite headerSite{SctTextEntityId{stringId}, SctTextRegion::Header,
        std::nullopt, {0u, 2u}};
    const SctTextSite bodySite{SctTextEntityId{stringId}, SctTextRegion::Body,
        0u, {0u, 2u}};
    const SctTextSite plainSite{SctTextEntityId{footerId}, SctTextRegion::Body,
        std::nullopt, {0u, 2u}};
    const SctDocumentEntityId instructionEntity{instructionId};
    const SctDocumentEntityId stringEntity{stringId};
    const SctDocumentEntityId footerEntity{footerId};
    auto sourceMap = SctImportedSourceMap::build(20u, {
        {{0u, 8u}, SctSourceSpanRole::Instruction, SctSourceSpanLayer::Envelope,
            SctSourceCoverageKind::SemanticEntity, instructionEntity, std::nullopt,
            std::nullopt, SctSourceRegion::SectionPayload, true},
        {{0u, 2u}, SctSourceSpanRole::InstructionParameter, SctSourceSpanLayer::Envelope,
            SctSourceCoverageKind::SemanticEntity, SctImportedSourceTarget{fixedSite}},
        {{0u, 1u}, SctSourceSpanRole::Expression, SctSourceSpanLayer::Envelope,
            SctSourceCoverageKind::SemanticEntity, SctImportedSourceTarget{operationSite}},
        {{2u, 2u}, SctSourceSpanRole::InstructionParameter, SctSourceSpanLayer::Envelope,
            SctSourceCoverageKind::SemanticEntity, SctImportedSourceTarget{repeatedSite}},
        {{4u, 4u}, SctSourceSpanRole::Expression, SctSourceSpanLayer::Envelope,
            SctSourceCoverageKind::SemanticEntity, SctImportedSourceTarget{scheduledSite}},
        {{8u, 8u}, SctSourceSpanRole::IndexedStringRecord, SctSourceSpanLayer::Envelope,
            SctSourceCoverageKind::SemanticEntity, stringEntity, std::nullopt,
            std::nullopt, SctSourceRegion::SectionPayload, true},
        {{8u, 2u}, SctSourceSpanRole::TextElement, SctSourceSpanLayer::Envelope,
            SctSourceCoverageKind::SemanticEntity, SctImportedSourceTarget{headerSite}},
        {{10u, 2u}, SctSourceSpanRole::TextElement, SctSourceSpanLayer::Envelope,
            SctSourceCoverageKind::SemanticEntity, SctImportedSourceTarget{bodySite}},
        {{16u, 4u}, SctSourceSpanRole::FooterEntry, SctSourceSpanLayer::Envelope,
            SctSourceCoverageKind::SemanticEntity, footerEntity, std::nullopt,
            std::nullopt, SctSourceRegion::Footer, true},
        {{16u, 2u}, SctSourceSpanRole::TextElement, SctSourceSpanLayer::Envelope,
            SctSourceCoverageKind::SemanticEntity, SctImportedSourceTarget{plainSite}},
        {{0u, 20u}, SctSourceSpanRole::DerivedPadding, SctSourceSpanLayer::Leaf,
            SctSourceCoverageKind::DerivedLayout},
    });
    ASSERT_TRUE(sourceMap.map);

    const auto assess = [&](const SctDocument& candidate) {
        const auto entities = SctDocumentIndex::build(candidate);
        return SctImportedSiteAddressabilityIndex::build(candidate, entities, *sourceMap.map);
    };
    const auto exact = assess(document);
    EXPECT_EQ(exact.summary(), SctImportedAddressabilitySummary::FullyAddressable);
    ASSERT_NE(exact.find(operationSite), nullptr);
    EXPECT_EQ(exact.find(operationSite)->addressability, SctImportedSiteAddressability::ExactSite);
    EXPECT_EQ(exact.find(repeatedSite)->addressability, SctImportedSiteAddressability::ExactSite);
    EXPECT_EQ(exact.find(scheduledSite)->addressability, SctImportedSiteAddressability::ExactSite);
    EXPECT_EQ(exact.find(plainSite)->addressability, SctImportedSiteAddressability::ExactSite);

    auto changedChild = document;
    auto& changedExpression = std::get<SctCanonicalExpression>(
        std::get<SctScriptSectionContent>(changedChild.sections[0].content)
            .instructions[0].fixedParameters[0].value);
    std::get<SctTypedScptProgram>(changedExpression.body).operations.resize(1u);
    EXPECT_EQ(assess(changedChild).find(operationSite)->addressability,
        SctImportedSiteAddressability::ParentSiteOnly);

    auto changedOwner = document;
    std::get<SctScriptSectionContent>(changedOwner.sections[0].content)
        .instructions[0].fixedParameters[0].value = SctEncodedWordValue{0u};
    EXPECT_EQ(assess(changedOwner).find(operationSite)->addressability,
        SctImportedSiteAddressability::OwningEntityOnly);

    auto removedScheduled = document;
    std::get<SctScriptSectionContent>(removedScheduled.sections[0].content)
        .instructions[0].scheduledExpression.reset();
    EXPECT_EQ(assess(removedScheduled).find(scheduledSite)->addressability,
        SctImportedSiteAddressability::OwningEntityOnly);

    auto removedRepeated = document;
    std::get<SctScriptSectionContent>(removedRepeated.sections[0].content)
        .instructions[0].repeatedParameterGroups.clear();
    EXPECT_EQ(assess(removedRepeated).find(repeatedSite)->addressability,
        SctImportedSiteAddressability::OwningEntityOnly);

    auto removedParameter = document;
    std::get<SctScriptSectionContent>(removedParameter.sections[0].content)
        .instructions[0].fixedParameters.clear();
    EXPECT_EQ(assess(removedParameter).find(fixedSite)->addressability,
        SctImportedSiteAddressability::OwningEntityOnly);
    EXPECT_EQ(assess(removedParameter).find(operationSite)->addressability,
        SctImportedSiteAddressability::OwningEntityOnly);

    auto changedText = document;
    auto& changedMessage = std::get<SctMessage>(
        std::get<SctStringSectionContent>(changedText.sections[1].content).string.value);
    *changedMessage.headerUtf8 = "H";
    std::get<SctTextChunk>(changedMessage.body.elements[0]).utf8 = "B";
    EXPECT_EQ(assess(changedText).find(headerSite)->addressability,
        SctImportedSiteAddressability::ParentSiteOnly);
    EXPECT_EQ(assess(changedText).find(bodySite)->addressability,
        SctImportedSiteAddressability::ParentSiteOnly);
    changedMessage.body.elements.clear();
    EXPECT_EQ(assess(changedText).find(bodySite)->addressability,
        SctImportedSiteAddressability::ParentSiteOnly);
    changedMessage.headerUtf8.reset();
    EXPECT_EQ(assess(changedText).find(headerSite)->addressability,
        SctImportedSiteAddressability::OwningEntityOnly);

    auto changedPlain = document;
    std::get<SctPlainText>(changedPlain.footerEntries[0].value).utf8 = "P";
    EXPECT_EQ(assess(changedPlain).find(plainSite)->addressability,
        SctImportedSiteAddressability::ParentSiteOnly);
    changedPlain.footerEntries[0].value = SctOpaqueText{{'P'}};
    EXPECT_EQ(assess(changedPlain).find(plainSite)->addressability,
        SctImportedSiteAddressability::OwningEntityOnly);

    auto removedEntities = document;
    std::get<SctScriptSectionContent>(removedEntities.sections[0].content).instructions.clear();
    removedEntities.sections.erase(removedEntities.sections.begin() + 1u);
    removedEntities.footerEntries.clear();
    const auto missing = assess(removedEntities);
    EXPECT_EQ(missing.find(fixedSite)->addressability,
        SctImportedSiteAddressability::MissingEntity);
    EXPECT_EQ(missing.find(headerSite)->addressability,
        SctImportedSiteAddressability::MissingEntity);
    EXPECT_EQ(missing.find(plainSite)->addressability,
        SctImportedSiteAddressability::MissingEntity);
    EXPECT_EQ(missing.summary(), SctImportedAddressabilitySummary::NoLongerAddressable);
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
        instructionValue.scheduledExpression = SctExpressionFactory::floatLiteral(3.0f);
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
        const auto records = imported.context.receipt().sourceMap.records();

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
            return record.role == SctSourceSpanRole::ExpressionOperation && record.target
                && std::holds_alternative<SctExpressionOperationSite>(*record.target);
        }));
        EXPECT_TRUE(std::any_of(records.begin(), records.end(), [](const auto& record) {
            return record.role == SctSourceSpanRole::ExpressionPayload && record.target
                && std::holds_alternative<SctExpressionOperationSite>(*record.target);
        }));
        EXPECT_TRUE(std::any_of(records.begin(), records.end(), [](const auto& record) {
            return record.role == SctSourceSpanRole::ExpressionTerminator && record.target
                && std::holds_alternative<SctExpressionSite>(*record.target);
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
