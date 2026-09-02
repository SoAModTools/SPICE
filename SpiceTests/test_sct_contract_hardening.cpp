#include "../SpiceSCT/SpiceSCT.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

namespace {
using namespace spice::sct;

SctCanonicalExpression noLoop(std::uint32_t word = 0x00800000u) {
    return {SctTypedScptProgram{{
        SctScptValueOperation{SctScptValueKind::InlineValue, word, {}}}},
        SctExpressionTermination::InlineValue};
}

SctParameter parsedParameter(std::uint32_t index, std::uint32_t word, bool expression = false) {
    SctParameter parameter;
    parameter.index = index;
    parameter.rawWords = {word};
    if (expression) {
        parameter.expression = SctExpression{};
        parameter.expression->program = SctTypedScptProgram{{
            SctScptValueOperation{SctScptValueKind::InlineValue, word, {}}}};
    }
    return parameter;
}

struct UnresolvedParseCase {
    std::uint16_t opcode;
    std::vector<SctParameter> parameters;
    std::optional<SctEdgeType> edgeType;
    SctParameterAddress address;
    SctExpectedReferenceTarget expected;
};

SctParseResult unresolvedParse(const UnresolvedParseCase& testCase) {
    SctParseResult parsed;
    parsed.parseOk = true;
    parsed.file.detectedEndian = "big";
    const auto byteSize = static_cast<std::uint32_t>(
        (1u + testCase.parameters.size()) * sizeof(std::uint32_t));
    parsed.file.originalPayloadBytes.resize(32u + byteSize, 0u);
    SctSection section;
    section.id.name = "SCRIPT";
    section.startOffset = 32u;
    section.endOffset = 32u + byteSize;
    section.kind = SctSectionKind::Script;
    SctInstruction instruction;
    instruction.offset = 0u;
    instruction.payloadOffset = 0u;
    instruction.opcodeWordIndex = 0u;
    instruction.opcode = testCase.opcode;
    instruction.sizeBytes = byteSize;
    instruction.decodeOk = true;
    instruction.parameters = testCase.parameters;
    section.instructions.push_back(std::move(instruction));
    if (testCase.edgeType) {
        SctEdge edge;
        edge.type = *testCase.edgeType;
        edge.fromPayloadOffset = 0u;
        edge.toPayloadOffset = 100u;
        section.edges.push_back(std::move(edge));
    }
    parsed.file.sections.push_back(std::move(section));
    return parsed;
}

const SctDocumentParameter* findParameter(
    const SctDocumentInstruction& instruction, SctParameterAddress address) {
    const std::vector<SctDocumentParameter>* parameters = &instruction.fixedParameters;
    if (address.repeatedGroupOrdinal) {
        if (*address.repeatedGroupOrdinal >= instruction.repeatedParameterGroups.size()) return nullptr;
        parameters = &instruction.repeatedParameterGroups[*address.repeatedGroupOrdinal].parameters;
    }
    const auto found = std::find_if(parameters->begin(), parameters->end(), [&](const auto& parameter) {
        return parameter.schemaIndex == address.schemaIndex;
    });
    return found == parameters->end() ? nullptr : &*found;
}

SctDocumentExportOptions gameCubeOptions() {
    return SctDocumentExportOptions{SctPlatform::GameCube, kSctShiftJisByte7FEncoding,
        SctDocumentOutputByteOrder::BigEndian, SctDocumentOutputWrapper::Raw};
}

SctMessage message(std::string text) {
    return {std::nullopt, SctFormattedText{{SctTextChunk{std::move(text)}}}};
}

std::string diagnosticMessages(const std::vector<SctDocumentDiagnostic>& diagnostics) {
    std::string result;
    for (const auto& diagnostic : diagnostics) {
        if (!result.empty()) result += " | ";
        result += diagnostic.message;
    }
    return result;
}

SctDocument makeUnresolvedJumpDocument() {
    SctDocument document;
    const auto sectionId = document.allocateSectionId();
    const auto sourceId = document.allocateInstructionId();
    const auto targetId = document.allocateInstructionId();
    SctDocumentInstruction source{sourceId, 10u};
    source.fixedParameters.push_back({0u, SctUnresolvedReferenceValue{
        {SctReferenceTargetStorage::Instruction, std::nullopt}, {100u}}});
    SctDocumentInstruction target{targetId, 12u};
    document.sections.push_back({sectionId, "SCRIPT", SctScriptSectionContent{{source, target}}});
    return document;
}

} // namespace

TEST(SctReferenceSafety, ImportClassifiesEveryKnownUnresolvedReferenceKind) {
    const std::array cases{
        UnresolvedParseCase{0u,
            {parsedParameter(0u, 0x00800000u, true), parsedParameter(1u, 100u)},
            SctEdgeType::BranchFalse, {1u, std::nullopt},
            {SctReferenceTargetStorage::Instruction, std::nullopt}},
        UnresolvedParseCase{3u,
            {parsedParameter(0u, 0x00800000u, true), parsedParameter(1u, 1u),
                parsedParameter(2u, 0u), parsedParameter(3u, 100u)},
            SctEdgeType::SwitchCase, {3u, 0u},
            {SctReferenceTargetStorage::Instruction, std::nullopt}},
        UnresolvedParseCase{11u, {parsedParameter(0u, 100u)},
            SctEdgeType::CallSubscript, {0u, std::nullopt},
            {SctReferenceTargetStorage::Instruction, std::nullopt}},
        UnresolvedParseCase{144u,
            {parsedParameter(0u, 100u), parsedParameter(1u, 0x00800000u, true)},
            std::nullopt, {0u, std::nullopt},
            {SctReferenceTargetStorage::IndexedString, SctTextKind::SctString}},
        UnresolvedParseCase{23u, {parsedParameter(0u, 100u)},
            std::nullopt, {0u, std::nullopt},
            {SctReferenceTargetStorage::FooterEntry, SctTextKind::PlainString}},
    };

    for (const auto& testCase : cases) {
        SCOPED_TRACE(testCase.opcode);
        const auto imported = SctDocumentImporter::import(unresolvedParse(testCase));
        ASSERT_TRUE(imported.document.has_value());
        const auto& instruction = std::get<SctScriptSectionContent>(
            imported.document->sections.front().content).instructions.front();
        const auto* parameter = findParameter(instruction, testCase.address);
        ASSERT_NE(parameter, nullptr);
        const auto* unresolved = std::get_if<SctUnresolvedReferenceValue>(&parameter->value);
        ASSERT_NE(unresolved, nullptr);
        EXPECT_EQ(unresolved->expectedTarget, testCase.expected);
        EXPECT_EQ(unresolved->encodedWords, (std::vector<std::uint32_t>{100u}));
        ASSERT_EQ(imported.context.receipt().unresolvedReferences.size(), 1u);
        EXPECT_EQ(imported.context.receipt().unresolvedReferences.front().sourceInstruction, instruction.id);
        EXPECT_EQ(imported.context.receipt().unresolvedReferences.front().parameter, testCase.address);
        EXPECT_EQ(imported.context.receipt().unresolvedReferences.front().sourceInstructionPayloadOffset, 32u);
        ASSERT_TRUE(imported.context.receipt().unresolvedReferences.front().operandPayloadOffset.has_value());
        EXPECT_GE(*imported.context.receipt().unresolvedReferences.front().operandPayloadOffset, 36u);
        EXPECT_TRUE(imported.context.receipt().unresolvedReferences.front().calculatedTargetPayloadOffset.has_value());
        const auto structural = SctDocumentValidator::validateDocument(*imported.document);
        EXPECT_TRUE(structural.validDocument) << diagnosticMessages(structural.diagnostics);
        const auto evidence = imported.context.bind(imported.context.revisionProvenance());
        ASSERT_TRUE(evidence);
        EXPECT_FALSE(SctDocumentValidator::validateForTarget(*imported.document,
            SctPlatform::GameCube, kSctShiftJisByte7FEncoding, &*evidence).validForTarget);
    }
}

TEST(SctReferenceSafety, OpaqueWordsCannotBypassAKnownReferenceContract) {
    auto document = makeUnresolvedJumpDocument();
    auto& instruction = std::get<SctScriptSectionContent>(document.sections.front().content).instructions.front();
    instruction.fixedParameters.front().value = SctOpaqueParameterValue{{100u}};
    const auto validation = SctDocumentValidator::validateDocument(document);
    EXPECT_FALSE(validation.validDocument);
    EXPECT_TRUE(std::any_of(validation.diagnostics.begin(), validation.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.code == SctDiagnosticCode::ParameterMismatch;
    }));
}

TEST(SctReferenceRepair, ReportsExactEvidenceCandidatesAndReturnsTypedReplacementWithoutMutation) {
    auto document = makeUnresolvedJumpDocument();
    const auto& instructions = std::get<SctScriptSectionContent>(
        document.sections.front().content).instructions;
    const auto sourceId = instructions[0].id;
    const auto targetId = instructions[1].id;
    const auto withoutReceipt = SctReferenceRepair::analyze(document);
    ASSERT_EQ(withoutReceipt.issues.size(), 1u);
    EXPECT_TRUE(withoutReceipt.issues.front().candidates.empty());
    SctDocumentImportReceipt receipt;
    receipt.lineage.sha256[0] = 1u;
    receipt.unresolvedReferences.push_back({sourceId, {0u, std::nullopt}, 0u, 4u, 100});
    auto builtMap = SctImportedSourceMap::build(104u, {
        {{0u, 100u}, SctSourceSpanRole::Header, SctSourceSpanLayer::Leaf,
            SctSourceCoverageKind::SourceObservation},
        {{100u, 4u}, SctSourceSpanRole::Instruction, SctSourceSpanLayer::Leaf,
            SctSourceCoverageKind::SemanticEntity, SctDocumentEntityId{targetId},
            std::nullopt, std::nullopt, SctSourceRegion::SectionPayload, true}});
    ASSERT_TRUE(builtMap.map);
    receipt.sourceMap = std::move(*builtMap.map);
    SctDocumentImportContext context{std::move(receipt)};
    const auto evidence = context.bind(context.revisionProvenance());
    ASSERT_TRUE(evidence);

    const auto analysis = SctReferenceRepair::analyze(document, &*evidence);
    ASSERT_EQ(analysis.issues.size(), 1u);
    ASSERT_TRUE(analysis.issues.front().sourceObservation.has_value());
    ASSERT_EQ(analysis.issues.front().candidates.size(), 1u);
    EXPECT_EQ(analysis.issues.front().candidates.front().target,
        SctDocumentReferenceTarget{targetId});

    auto ambiguous = document;
    const auto secondTargetId = ambiguous.allocateInstructionId();
    std::get<SctScriptSectionContent>(ambiguous.sections.front().content).instructions.push_back(
        SctDocumentInstruction{secondTargetId, 12u});
    auto ambiguousReceipt = context.receipt();
    auto ambiguousRecords = std::vector<SctSourceSpanRecord>(
        ambiguousReceipt.sourceMap.records().begin(), ambiguousReceipt.sourceMap.records().end());
    ambiguousRecords.push_back({{100u, 4u}, SctSourceSpanRole::Instruction,
        SctSourceSpanLayer::Envelope, SctSourceCoverageKind::SemanticEntity,
        SctDocumentEntityId{secondTargetId}, std::nullopt, std::nullopt,
        SctSourceRegion::SectionPayload, true});
    auto ambiguousMap = SctImportedSourceMap::build(104u, std::move(ambiguousRecords));
    ASSERT_TRUE(ambiguousMap.map);
    ambiguousReceipt.sourceMap = std::move(*ambiguousMap.map);
    SctDocumentImportContext ambiguousContext{std::move(ambiguousReceipt)};
    const auto ambiguousEvidence = ambiguousContext.bind(ambiguousContext.revisionProvenance());
    ASSERT_TRUE(ambiguousEvidence);
    const auto multiple = SctReferenceRepair::analyze(ambiguous, &*ambiguousEvidence);
    ASSERT_EQ(multiple.issues.size(), 1u);
    EXPECT_EQ(multiple.issues.front().candidates.size(), 2u);

    const auto replacement = SctReferenceRepair::resolve(document, sourceId,
        {0u, std::nullopt}, targetId);
    ASSERT_TRUE(replacement.value.has_value());
    EXPECT_TRUE(std::holds_alternative<SctInstructionReference>(*replacement.value));
    EXPECT_TRUE(std::holds_alternative<SctUnresolvedReferenceValue>(instructions[0].fixedParameters[0].value));

    auto repaired = document;
    std::get<SctScriptSectionContent>(repaired.sections.front().content)
        .instructions.front().fixedParameters.front().value = *replacement.value;
    EXPECT_TRUE(SctDocumentValidator::validateDocument(repaired).validDocument);
    const auto exported = SctDocumentExporter::exportDocument(repaired, gameCubeOptions());
    ASSERT_TRUE(exported.success);
    const auto reparsed = SctParser{}.parse(exported.bytes, "repaired_reference.sct");
    ASSERT_TRUE(reparsed.parseOk);
    const auto reimported = SctDocumentImporter::import(reparsed);
    ASSERT_TRUE(reimported.document.has_value());
    const auto& reimportedInstructions = std::get<SctScriptSectionContent>(
        reimported.document->sections.front().content).instructions;
    ASSERT_FALSE(reimportedInstructions.empty());
    EXPECT_EQ(SctSemanticUsageIndex::build(*reimported.document)
        .outboundReferences(reimportedInstructions.front().id).size(), 1u);
}

TEST(SctReferenceRepair, CreatesOnlySchemaCompatibleInstructionIndexedAndFooterReferences) {
    SctDocument document;
    const auto scriptSection = document.allocateSectionId();
    const auto jumpId = document.allocateInstructionId();
    const auto indexedSourceId = document.allocateInstructionId();
    const auto footerSourceId = document.allocateInstructionId();
    const auto targetId = document.allocateInstructionId();
    const auto stringId = document.allocateStringId();
    const auto footerId = document.allocateFooterEntryId();
    SctDocumentInstruction jump{jumpId, 10u};
    jump.fixedParameters.push_back({0u, SctUnresolvedReferenceValue{
        {SctReferenceTargetStorage::Instruction, std::nullopt}, {0u}}});
    SctDocumentInstruction indexed{indexedSourceId, 144u};
    indexed.fixedParameters = {{0u, SctUnresolvedReferenceValue{
        {SctReferenceTargetStorage::IndexedString, SctTextKind::SctString}, {0u}}},
        {1u, noLoop()}};
    SctDocumentInstruction footer{footerSourceId, 23u};
    footer.fixedParameters.push_back({0u, SctUnresolvedReferenceValue{
        {SctReferenceTargetStorage::FooterEntry, SctTextKind::PlainString}, {0u}}});
    SctDocumentInstruction target{targetId, 12u};
    document.sections.push_back({scriptSection, "SCRIPT",
        SctScriptSectionContent{{jump, indexed, footer, target}}});
    const auto stringSection = document.allocateSectionId();
    document.sections.push_back({stringSection, "STRING",
        SctStringSectionContent{SctDocumentString{stringId, message("text")}}});
    document.footerEntries.push_back({footerId, SctTextKind::PlainString, SctPlainText{"footer"}});

    EXPECT_TRUE(SctReferenceRepair::createReferenceValue(document, jumpId, {0u, std::nullopt}, targetId).value);
    EXPECT_TRUE(SctReferenceRepair::createReferenceValue(document, indexedSourceId,
        {0u, std::nullopt}, stringId).value);
    EXPECT_TRUE(SctReferenceRepair::createReferenceValue(document, footerSourceId,
        {0u, std::nullopt}, footerId).value);
    EXPECT_TRUE(SctReferenceRepair::createReferenceValue(document, 144u,
        {0u, std::nullopt}, stringId).value);
    EXPECT_FALSE(SctReferenceRepair::createReferenceValue(document, indexedSourceId,
        {0u, std::nullopt}, footerId).value);
    EXPECT_FALSE(SctReferenceRepair::createReferenceValue(document, footerSourceId,
        {0u, std::nullopt}, stringId).value);
}

TEST(SctDocumentWorkflow, ClassifiesUnavailableInspectableStructuralAndExportReadyStates) {
    const auto unavailable = SctDocumentWorkflow::importForEditing(SctParseResult{});
    EXPECT_EQ(unavailable.readiness, SctDocumentReadiness::Unavailable);
    const UnresolvedParseCase repairableCase{11u, {parsedParameter(0u, 100u)},
        SctEdgeType::CallSubscript, {0u, std::nullopt},
        {SctReferenceTargetStorage::Instruction, std::nullopt}};
    EXPECT_EQ(SctDocumentWorkflow::importForEditing(unresolvedParse(repairableCase)).readiness,
        SctDocumentReadiness::StructurallyValid);

    SctDocument invalid;
    const auto duplicate = invalid.allocateSectionId();
    invalid.sections.push_back({duplicate, "A", SctOpaqueSectionContent{}});
    invalid.sections.push_back({duplicate, "B", SctOpaqueSectionContent{}});
    EXPECT_EQ(SctDocumentWorkflow::assessForExport(invalid, gameCubeOptions()).readiness,
        SctDocumentReadiness::Inspectable);

    const auto unresolved = makeUnresolvedJumpDocument();
    EXPECT_EQ(SctDocumentWorkflow::assessForExport(unresolved, gameCubeOptions()).readiness,
        SctDocumentReadiness::StructurallyValid);

    SctDocument ready;
    const auto sectionId = ready.allocateSectionId();
    const auto instructionId = ready.allocateInstructionId();
    ready.sections.push_back({sectionId, "SCRIPT",
        SctScriptSectionContent{{SctDocumentInstruction{instructionId, 12u}}}});
    EXPECT_EQ(SctDocumentWorkflow::assessForExport(ready, gameCubeOptions()).readiness,
        SctDocumentReadiness::ExportReady);

    SctDocument gameCubeOnly;
    const auto gcScriptId = gameCubeOnly.allocateSectionId();
    const auto gcInstructionId = gameCubeOnly.allocateInstructionId();
    const auto gcStringId = gameCubeOnly.allocateStringId();
    SctDocumentInstruction opcode265{gcInstructionId, 265u};
    opcode265.fixedParameters = {{0u, noLoop()}, {1u, SctStringReference{gcStringId}}};
    gameCubeOnly.sections.push_back({gcScriptId, "SCRIPT", SctScriptSectionContent{{opcode265}}});
    const auto gcStringSectionId = gameCubeOnly.allocateSectionId();
    gameCubeOnly.sections.push_back({gcStringSectionId, "STRING",
        SctStringSectionContent{SctDocumentString{gcStringId, message("text")}}});
    auto dreamcastOptions = gameCubeOptions();
    dreamcastOptions.targetPlatform = SctPlatform::Dreamcast;
    EXPECT_EQ(SctDocumentWorkflow::assessForExport(gameCubeOnly, dreamcastOptions).readiness,
        SctDocumentReadiness::StructurallyValid);

    SctDocument blocked;
    const auto blockedSection = blocked.allocateSectionId();
    const auto attachmentId = blocked.allocateOpaqueAttachmentId();
    blocked.sections.push_back({blockedSection, "LABEL", SctOpaqueSectionContent{}});
    blocked.opaqueAttachments.push_back({attachmentId, {0xaa}, blockedSection,
        SctOpaquePlacement::FixedOffset, 0u, 1u, SctOpaqueRelocationSupport::FixedOnly,
        SctOpaqueReason::Gap});
    SctDocumentImportReceipt receipt;
    receipt.lineage.sha256[0] = 1u;
    receipt.declaredSourcePlatform = SctPlatform::GameCube;
    SctDocumentImportContext context{std::move(receipt)};
    const auto evidence = context.bind(context.revisionProvenance());
    ASSERT_TRUE(evidence);
    const auto blockedAssessment = SctDocumentWorkflow::assessForExport(
        blocked, gameCubeOptions(), &*evidence);
    EXPECT_EQ(blockedAssessment.readiness, SctDocumentReadiness::StructurallyValid);
    EXPECT_FALSE(blockedAssessment.layout.success);
}

TEST(SctDocumentEntityFactory, ConstructsDetachedEntitiesWithoutConsumingIdsOnFailure) {
    SctDocument document;
    const auto invalidSection = SctDocumentEntityFactory::createScriptSection(
        document, std::string(17u, 'X'));
    EXPECT_FALSE(invalidSection.section.has_value());
    EXPECT_EQ(document.nextSectionIdValue(), 1u);

    const auto script = SctDocumentEntityFactory::createScriptSection(document, "SCRIPT");
    ASSERT_TRUE(script.section.has_value());
    EXPECT_TRUE(std::holds_alternative<SctScriptSectionContent>(script.section->content));
    EXPECT_TRUE(document.sections.empty());
    const auto marker = SctDocumentEntityFactory::createStringGroupMarkerSection(document, "LABEL");
    ASSERT_TRUE(marker.section.has_value());
    EXPECT_TRUE(std::holds_alternative<SctStringGroupMarkerSectionContent>(marker.section->content));

    const auto indexed = SctDocumentEntityFactory::createIndexedStringSection(
        document, "STRING", message("hello"));
    ASSERT_TRUE(indexed.section.has_value());
    const auto& string = std::get<SctStringSectionContent>(indexed.section->content).string;
    EXPECT_TRUE(string.id);
    EXPECT_EQ(string.kind, SctTextKind::SctString);

    const auto badFooter = SctDocumentEntityFactory::createFooterEntry(
        document, SctTextKind::PlainString, message("wrong kind"));
    EXPECT_FALSE(badFooter.entry.has_value());
    EXPECT_EQ(document.nextFooterEntryIdValue(), 1u);
    const auto footer = SctDocumentEntityFactory::createFooterEntry(
        document, SctTextKind::PlainString, SctPlainText{"plain"});
    EXPECT_TRUE(footer.entry.has_value());

    SctOpaqueSectionAttachmentRequest invalidOpaque;
    const auto rejectedOpaque = SctDocumentEntityFactory::createOpaqueSection(
        document, "OPAQUE", invalidOpaque);
    EXPECT_FALSE(rejectedOpaque.section.has_value());
    const auto sectionNext = document.nextSectionIdValue();
    SctOpaqueSectionAttachmentRequest opaque;
    opaque.bytes = {0xde, 0xad};
    const auto acceptedOpaque = SctDocumentEntityFactory::createOpaqueSection(
        document, "OPAQUE", std::move(opaque));
    ASSERT_TRUE(acceptedOpaque.section.has_value());
    ASSERT_TRUE(acceptedOpaque.attachment.has_value());
    EXPECT_EQ(acceptedOpaque.section->id.value(), sectionNext);
    EXPECT_EQ(std::get<SctSectionId>(acceptedOpaque.attachment->anchor), acceptedOpaque.section->id);
}

TEST(SctInstructionFactory, CreatesDetachedSchemaCompleteRepeatedGroups) {
    const auto draft = SctInstructionFactory::createRepeatedGroupDraft(3u,
        {{2u, SctEncodedWordValue{0xffffffffu}},
            {3u, SctInstructionReference{SctInstructionId{7u}}}});
    ASSERT_TRUE(draft.draft.has_value());
    ASSERT_EQ(draft.draft->parameters.size(), 2u);
    const auto materialized = SctInstructionFactory::materializeRepeatedGroup(*draft.draft);
    ASSERT_TRUE(materialized.group.has_value());
    EXPECT_EQ(materialized.group->parameters.size(), 2u);

    auto unresolved = *draft.draft;
    unresolved.parameters.front().value.reset();
    EXPECT_FALSE(SctInstructionFactory::materializeRepeatedGroup(unresolved).group.has_value());
    EXPECT_FALSE(SctInstructionFactory::createRepeatedGroupDraft(12u).draft.has_value());
}
