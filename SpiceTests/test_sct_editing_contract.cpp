#include "../SpiceSCT/SpiceSCT.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <type_traits>
#include <vector>

namespace {
using namespace spice::sct;

SctParseResult makeLabelParse(std::string name, std::vector<std::uint8_t> unusualSuffix = {}) {
    SctParseResult parsed;
    parsed.parseOk = true;
    parsed.file.detectedEndian = "big";
    parsed.file.originalPayloadBytes.resize(32, 0);
    std::copy(name.begin(), name.end(), parsed.file.originalPayloadBytes.begin() + 16);
    if (name.size() < 16 && !unusualSuffix.empty()) {
        const auto suffixOffset = 17u + name.size();
        std::copy_n(unusualSuffix.begin(), std::min<std::size_t>(unusualSuffix.size(), 32u - suffixOffset),
            parsed.file.originalPayloadBytes.begin() + suffixOffset);
    }
    SctSection section;
    section.id.name = std::move(name);
    section.startOffset = 32;
    section.endOffset = 32;
    section.kind = SctSectionKind::Label;
    parsed.file.sections.push_back(std::move(section));
    return parsed;
}

SctDocumentExportOptions rawGameCubeOptions() {
    return SctDocumentExportOptions{SctPlatform::GameCube, SctTextProfile::GameCubeUs,
        SctDocumentOutputByteOrder::BigEndian,
        SctDocumentOutputWrapper::Raw, SctOpaquePreservationPolicy::RequirePreservation};
}

SctMessage message(std::string text) {
    return {std::nullopt, SctFormattedText{{SctTextChunk{std::move(text)}}}};
}

void appendBe32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

SctParseResult parseEditableStringFixture(std::string text) {
    std::vector<std::uint8_t> bytes(32, 0);
    bytes[11] = 1;
    const std::string name = "M00010000";
    std::copy(name.begin(), name.end(), bytes.begin() + 16);
    appendBe32(bytes, 9u);
    appendBe32(bytes, 0x04000000u);
    appendBe32(bytes, 0x3f800000u);
    appendBe32(bytes, 0x1du);
    bytes.insert(bytes.end(), text.begin(), text.end());
    bytes.push_back(0);
    while ((bytes.size() % 4u) != 0u) bytes.push_back(0);
    return SctParser{}.parse(bytes, "editable_string.sct");
}

std::vector<SctInstructionParameterOverride> requiredOverrides(
    const SctOpcodeSchema& schema, SctInstructionId target,
    SctFooterEntryId stringFooter, SctFooterEntryId sctStringFooter) {
    std::vector<SctInstructionParameterOverride> result;
    for (std::uint32_t index = 0; index < schema.parameterCatalogCount; ++index) {
        const auto& parameter = schema.parameterCatalog[index];
        SctParameterAddress address{index, parameter.belongsToRepeatedGroup
            ? std::optional<std::uint32_t>{0u} : std::nullopt};
        switch (parameter.referenceKind) {
        case SctOpcodeReferenceKind::Instruction:
            result.push_back({address, SctInstructionReference{target}});
            break;
        case SctOpcodeReferenceKind::Text:
            if (!parameter.textReference.has_value()) {
                ADD_FAILURE() << "Text parameter is missing its text-reference rule.";
                break;
            }
            if (parameter.textReference->storage == SctTextStorage::Footer) {
                const auto footer = parameter.textReference->kind == SctTextKind::SctString
                    ? sctStringFooter
                    : stringFooter;
                result.push_back({address, SctFooterEntryReference{footer}});
            }
            break;
        case SctOpcodeReferenceKind::None:
            if (parameter.defaultKind == SctOpcodeDefaultKind::Required) {
                result.push_back({address, SctEncodedWordValue{parameter.requiredBitValue}});
            }
            break;
        }
    }
    return result;
}

void acceptProvisionalSuggestions(SctInstructionDraft& draft) {
    for (auto& parameter : draft.parameters) {
        if (!parameter.value.has_value() && parameter.suggestedValue.has_value()) {
            parameter.value = parameter.suggestedValue;
        }
    }
}
} // namespace

TEST(SctDocumentImporter, OrdinaryIndexPaddingIsDerivedAndDoesNotBlockRenames) {
    const auto imported = SctDocumentImporter::import(makeLabelParse("A"), {{SctPlatform::GameCube}});
    ASSERT_TRUE(imported.document.has_value());
    EXPECT_TRUE(std::none_of(imported.document->opaqueAttachments.begin(),
        imported.document->opaqueAttachments.end(), [](const auto& attachment) {
            return attachment.reason == SctOpaqueReason::Padding;
        }));
    EXPECT_TRUE(std::any_of(imported.receipt.provenance.begin(), imported.receipt.provenance.end(),
        [](const auto& provenance) { return provenance.coverageKind == SctSourceCoverageKind::DerivedLayout; }));

    for (const std::string name : {std::string{}, std::string{"B"}, std::string{"LONGER_NAME"},
             std::string(16, 'X')}) {
        auto document = *imported.document;
        document.sections.front().nameBytes = name;
        const auto exported = SctDocumentExporter::exportDocument(document, rawGameCubeOptions(), &imported.receipt);
        ASSERT_TRUE(exported.success) << name;
        const auto reparsed = SctParser{}.parse(exported.bytes, "renamed.sct");
        ASSERT_TRUE(reparsed.parseOk);
        ASSERT_EQ(reparsed.file.sections.size(), 1u);
        if (name.empty()) {
            EXPECT_TRUE(std::all_of(exported.bytes.begin() + 16, exported.bytes.begin() + 32,
                [](std::uint8_t value) { return value == 0; }));
        } else {
            EXPECT_EQ(reparsed.file.sections.front().id.name, name);
        }
    }
}

TEST(SctDocumentImporter, UnusualIndexSuffixIsPreservedAndOnlyOverlappingGrowthIsRejected) {
    const auto imported = SctDocumentImporter::import(
        makeLabelParse("A", {0x7e, 0x55}), {{SctPlatform::GameCube}});
    ASSERT_TRUE(imported.document.has_value());
    const auto padding = std::find_if(imported.document->opaqueAttachments.begin(),
        imported.document->opaqueAttachments.end(), [](const auto& attachment) {
            return attachment.reason == SctOpaqueReason::Padding;
        });
    ASSERT_NE(padding, imported.document->opaqueAttachments.end());
    ASSERT_TRUE(padding->fixedOffset.has_value());
    EXPECT_EQ(*padding->fixedOffset, 18u);

    auto safe = *imported.document;
    safe.sections.front().nameBytes = "AB";
    EXPECT_TRUE(SctDocumentExporter::exportDocument(safe, rawGameCubeOptions(), &imported.receipt).success);

    auto overlap = *imported.document;
    overlap.sections.front().nameBytes = "ABC";
    const auto rejected = SctDocumentExporter::exportDocument(overlap, rawGameCubeOptions(), &imported.receipt);
    EXPECT_FALSE(rejected.success);
    EXPECT_TRUE(std::any_of(rejected.diagnostics.begin(), rejected.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.code == SctDiagnosticCode::OpaquePlacementUnsatisfied;
    }));
}

TEST(SctDocumentBuilder, ReconstitutesSparseIdsAndRejectsInvalidIdentityState) {
    SctDocument external;
    SctDocumentInstruction instruction;
    instruction.id = SctInstructionId{42};
    instruction.opcode = 12;
    external.sections.push_back({SctSectionId{7}, "SCRIPT", SctScriptSectionContent{{instruction}}});
    external.strings.push_back({SctStringId{3}, message("text")});
    external.sections.push_back({SctSectionId{20}, "STRING", SctStringSectionContent{SctStringId{3}}});
    external.footerEntries.push_back({SctFooterEntryId{9}, SctDocumentFooterEntryKind::String, SctPlainText{"footer"}});
    external.opaqueAttachments.push_back({SctOpaqueAttachmentId{11}, {0xaa}, SctSectionId{7},
        SctOpaquePlacement::FixedOffset, 200u, 1, SctOpaqueRelocationSupport::FixedOnly,
        SctOpaqueReason::UnknownEncoding});

    auto rebuilt = SctDocumentBuilder::reconstitute(std::move(external));
    ASSERT_TRUE(rebuilt.document.has_value());
    EXPECT_EQ(rebuilt.document->allocateSectionId().value(), 21u);
    EXPECT_EQ(rebuilt.document->allocateInstructionId().value(), 43u);
    EXPECT_EQ(rebuilt.document->allocateStringId().value(), 4u);
    EXPECT_EQ(rebuilt.document->allocateFooterEntryId().value(), 10u);
    EXPECT_EQ(rebuilt.document->allocateOpaqueAttachmentId().value(), 12u);

    SctDocument invalid;
    invalid.sections.push_back({SctSectionId{}, "ZERO", SctLabelSectionContent{}});
    invalid.sections.push_back({SctSectionId{2}, "A", SctLabelSectionContent{}});
    invalid.sections.push_back({SctSectionId{2}, "B", SctLabelSectionContent{}});
    const auto rejected = SctDocumentBuilder::reconstitute(std::move(invalid));
    EXPECT_FALSE(rejected.document.has_value());
    EXPECT_EQ(rejected.diagnostics.size(), 2u);
}

TEST(SctDocumentIndex, DerivesAttachmentsAndTypedReferenceDirectionsFromTheCurrentRevision) {
    SctDocument document;
    const auto sectionId = document.allocateSectionId();
    const auto jumpId = document.allocateInstructionId();
    const auto targetId = document.allocateInstructionId();
    const auto attachmentId = document.allocateOpaqueAttachmentId();
    const auto stringId = document.allocateStringId();
    const auto footerId = document.allocateFooterEntryId();
    SctDocumentInstruction jump{jumpId, 10};
    jump.fixedParameters.push_back({0, SctInstructionReference{targetId}});
    SctDocumentInstruction target{targetId, 12};
    document.sections.push_back({sectionId, "SCRIPT", SctScriptSectionContent{{jump, target}}});
    const auto stringSectionId = document.allocateSectionId();
    document.strings.push_back({stringId, message("text")});
    document.sections.push_back({stringSectionId, "STRING", SctStringSectionContent{stringId}});
    document.footerEntries.push_back({footerId, SctDocumentFooterEntryKind::String, SctPlainText{"footer"}});
    document.opaqueAttachments.push_back({attachmentId, {0xaa}, sectionId,
        SctOpaquePlacement::FixedOffset, 100u, 1, SctOpaqueRelocationSupport::FixedOnly, SctOpaqueReason::Gap});

    const auto first = SctDocumentIndex::build(document);
    EXPECT_NE(first.find(sectionId), nullptr);
    EXPECT_NE(first.find(targetId), nullptr);
    EXPECT_EQ(first.attachmentsFor(sectionId).size(), 1u);
    EXPECT_EQ(first.outboundReferences(jumpId).size(), 1u);
    EXPECT_EQ(first.inboundReferences(SctDocumentReferenceTarget{targetId}).size(), 1u);
    ASSERT_EQ(first.sectionOrdinal(sectionId), 0u);
    ASSERT_TRUE(first.instructionLocation(targetId).has_value());
    EXPECT_EQ(first.instructionLocation(targetId)->sectionId, sectionId);
    EXPECT_EQ(first.instructionLocation(targetId)->instructionOrdinal, 1u);
    EXPECT_EQ(first.owningSection(targetId), &document.sections.front());
    EXPECT_EQ(first.opaqueAttachmentOrdinal(attachmentId), 0u);
    ASSERT_TRUE(first.stringLocation(stringId).has_value());
    EXPECT_EQ(first.stringLocation(stringId)->stringOrdinal, 0u);
    EXPECT_EQ(first.stringLocation(stringId)->sectionId, stringSectionId);
    EXPECT_EQ(first.stringLocation(stringId)->sectionOrdinal, 1u);
    EXPECT_EQ(first.footerEntryOrdinal(footerId), 0u);

    const auto movedSectionId = document.allocateSectionId();
    auto& originalInstructions = std::get<SctScriptSectionContent>(document.sections.front().content).instructions;
    SctDocumentInstruction movedTarget = originalInstructions.back();
    originalInstructions.pop_back();
    document.sections.push_back({movedSectionId, "MOVED", SctScriptSectionContent{{movedTarget}}});
    const auto rebuilt = SctDocumentIndex::build(document);
    ASSERT_TRUE(rebuilt.instructionLocation(targetId).has_value());
    EXPECT_EQ(rebuilt.instructionLocation(targetId)->sectionId, movedSectionId);
    EXPECT_EQ(rebuilt.instructionLocation(targetId)->sectionOrdinal, 2u);
    EXPECT_EQ(rebuilt.owningSection(targetId), &document.sections[2]);
    EXPECT_EQ(rebuilt.outboundReferences(jumpId).size(), 1u);
}

static_assert(!std::is_default_constructible_v<SctDocumentExportOptions>);

TEST(SctExpressionFactory, RejectsLossyVariableAndOpaqueOperatorConstruction) {
    const auto oversized = SctExpressionFactory::variable(
        SctExpressionVariableKind::Integer, 0x01000000u);
    EXPECT_FALSE(oversized.expression.has_value());
    EXPECT_TRUE(std::any_of(oversized.diagnostics.begin(), oversized.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.code == SctDiagnosticCode::ExpressionInvalid;
    }));

    SctCanonicalExpression opaque{SctOpaqueExpression{{0x50000001u, 0x1du}},
        SctExpressionTermination::StopCode};
    const auto rejected = SctExpressionFactory::binaryOperator(
        SctExpressionBinaryOperator::Add, opaque, SctExpressionFactory::decimalLiteral(1));
    EXPECT_FALSE(rejected.expression.has_value());

    const auto variable = SctExpressionFactory::variable(SctExpressionVariableKind::Integer, 0x00ffffffu);
    ASSERT_TRUE(variable.expression.has_value());
    EXPECT_EQ(std::get<SctCanonicalExpressionNode>(variable.expression->root).encodingCode, 0x50ffffffu);
}

TEST(SctDocumentValidator, ReportsParameterGroupAndExpressionChildLocations) {
    SctDocument document;
    const auto sectionId = document.allocateSectionId();
    SctDocumentInstruction instruction;
    instruction.id = document.allocateInstructionId();
    instruction.opcode = 100;
    SctCanonicalExpressionNode invalidOperator;
    invalidOperator.kind = SctCanonicalExpressionNodeKind::ArithmeticOperator;
    invalidOperator.encodingCode = 0x0eu;
    invalidOperator.children.push_back(std::get<SctCanonicalExpressionNode>(SctExpressionFactory::decimalLiteral(1).root));
    instruction.fixedParameters.push_back({0, SctCanonicalExpression{invalidOperator, SctExpressionTermination::StopCode}});
    instruction.fixedParameters.push_back({1, SctExpressionFactory::decimalLiteral(0)});
    document.sections.push_back({sectionId, "SCRIPT", SctScriptSectionContent{{instruction}}});

    const auto validation = SctDocumentValidator::validateDocument(document);
    const auto found = std::find_if(validation.diagnostics.begin(), validation.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.code == SctDiagnosticCode::ExpressionInvalid && diagnostic.parameter.has_value();
    });
    ASSERT_NE(found, validation.diagnostics.end());
    EXPECT_EQ(found->parameter->schemaIndex, 0u);
    EXPECT_FALSE(found->parameter->repeatedGroupOrdinal.has_value());
    EXPECT_TRUE(found->expressionChildPath.empty());
}

TEST(SctOpcodeAuthoringCatalog, CoversEveryOpcodeShapeWithoutInventingLegalDomains) {
    ASSERT_EQ(sctOpcodeSchemas().size(), 266u);
    for (const auto& schema : sctOpcodeSchemas()) {
        EXPECT_FALSE(schema.semantic.mnemonic.empty()) << schema.opcode;
        const auto minimumCatalogCount = schema.parameters.loopEndParam >= 0
            ? std::max<std::uint32_t>(schema.parameters.paramCount,
                static_cast<std::uint32_t>(schema.parameters.loopEndParam) + 1u)
            : schema.parameters.paramCount;
        ASSERT_EQ(schema.parameterCatalogCount, minimumCatalogCount) << schema.opcode;
        for (std::uint32_t index = 0; index < schema.parameterCatalogCount; ++index) {
            const auto& parameter = schema.parameterCatalog[index];
            EXPECT_EQ(parameter.schemaIndex, index) << schema.opcode;
            EXPECT_EQ(parameter.binaryConfidence, SctOpcodeContractConfidence::Confirmed) << schema.opcode;
            if (!parameter.hasConfirmedRange && parameter.defaultKind == SctOpcodeDefaultKind::ProvisionalZero) {
                EXPECT_EQ(parameter.defaultConfidence, SctOpcodeContractConfidence::Provisional) << schema.opcode;
            }
        }
    }
}

TEST(SctInstructionFactory, CreatesNeutralDraftsWithExplicitAvailabilityForEveryOpcode) {
    for (const auto& schema : sctOpcodeSchemas()) {
        SctInstructionFactoryRequest request;
        request.opcode = schema.opcode;
        if (sctOpcodeRepeatedGroup(schema)) request.repeatedGroupCount = 1;
        const auto draft = SctInstructionFactory::createDraft(request);
        const bool availableAnywhere = sctOpcodeAvailability(schema, SctPlatform::GameCube)
                == SctOpcodeAvailability::Available
            || sctOpcodeAvailability(schema, SctPlatform::Dreamcast)
                == SctOpcodeAvailability::Available;
        if (availableAnywhere && schema.documentRole == SctOpcodeDocumentRole::Instruction) {
            ASSERT_TRUE(draft.draft.has_value()) << schema.opcode;
            EXPECT_FALSE(draft.draft->skipRefresh) << schema.opcode;
            EXPECT_EQ(draft.draft->availability.gameCube,
                sctOpcodeAvailability(schema, SctPlatform::GameCube)) << schema.opcode;
            EXPECT_EQ(draft.draft->availability.dreamcast,
                sctOpcodeAvailability(schema, SctPlatform::Dreamcast)) << schema.opcode;
        } else {
            EXPECT_FALSE(draft.draft.has_value()) << schema.opcode;
            EXPECT_TRUE(std::any_of(draft.diagnostics.begin(), draft.diagnostics.end(), [](const auto& diagnostic) {
                return diagnostic.code == SctDiagnosticCode::OpcodeUnavailable
                    || diagnostic.code == SctDiagnosticCode::EncodingUnsupported;
            })) << schema.opcode;
        }
    }

    SctInstructionFactoryRequest gameCubeOnly;
    gameCubeOnly.opcode = 265;
    const auto opcode265 = SctInstructionFactory::createDraft(gameCubeOnly);
    ASSERT_TRUE(opcode265.draft.has_value());
    EXPECT_EQ(opcode265.draft->availability.gameCube, SctOpcodeAvailability::Available);
    EXPECT_EQ(opcode265.draft->availability.dreamcast,
        SctOpcodeAvailability::UnavailableInvalidStub);
}

TEST(SctInstructionFactory, RepresentsScheduledOpcode129AsAnInstructionModifier) {
    SctInstructionFactoryRequest modifierRequest;
    modifierRequest.opcode = 129;
    const auto modifier = SctInstructionFactory::createDraft(modifierRequest);
    EXPECT_FALSE(modifier.draft.has_value());
    EXPECT_TRUE(std::any_of(modifier.diagnostics.begin(), modifier.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.code == SctDiagnosticCode::EncodingUnsupported;
    }));

    SctInstructionFactoryRequest instructionRequest;
    instructionRequest.opcode = 12;
    instructionRequest.scheduledExpression = SctExpressionFactory::decimalLiteral(3);
    auto draft = SctInstructionFactory::createDraft(instructionRequest);
    ASSERT_TRUE(draft.draft.has_value());
    SctDocument document;
    const auto created = SctInstructionFactory::materialize(document, *draft.draft);
    ASSERT_TRUE(created.instruction.has_value());
    const auto sectionId = document.allocateSectionId();
    document.sections.push_back({sectionId, "SCRIPT", SctScriptSectionContent{{*created.instruction}}});
    ASSERT_TRUE(SctDocumentValidator::validateDocument(document).validDocument);

    const auto exported = SctDocumentExporter::exportDocument(document, rawGameCubeOptions());
    ASSERT_TRUE(exported.success);
    const auto parsed = SctParser{}.parse(exported.bytes, "scheduled_factory.sct");
    ASSERT_TRUE(parsed.parseOk);
    ASSERT_EQ(parsed.file.sections.size(), 1u);
    ASSERT_EQ(parsed.file.sections.front().instructions.size(), 1u);
    EXPECT_EQ(parsed.file.sections.front().instructions.front().opcode, 12u);
    EXPECT_TRUE(parsed.file.sections.front().instructions.front().scheduled.present);
}

TEST(SctInstructionFactory, RejectsInvalidDraftExpressionsWithoutAssigningEntityIdentity) {
    SctCanonicalExpression invalidExpression;
    invalidExpression.root = SctCanonicalExpressionNode{
        SctCanonicalExpressionNodeKind::ArithmeticOperator, 0x0eu, {}, {}};

    SctInstructionFactoryRequest request;
    request.opcode = 12;
    request.scheduledExpression = invalidExpression;
    const auto draft = SctInstructionFactory::createDraft(request);
    ASSERT_TRUE(draft.draft.has_value());

    SctDocument document;
    const auto nextIdBeforeFailure = document.nextInstructionIdValue();
    const auto materialized = SctInstructionFactory::materialize(document, *draft.draft);
    EXPECT_FALSE(materialized.instruction.has_value());
    EXPECT_EQ(document.nextInstructionIdValue(), nextIdBeforeFailure);
    EXPECT_TRUE(std::any_of(materialized.diagnostics.begin(), materialized.diagnostics.end(),
        [](const auto& diagnostic) {
            return diagnostic.code == SctDiagnosticCode::ExpressionInvalid
                && !diagnostic.entity.has_value();
        }));
}

TEST(SctInstructionFactory, CreatedInstructionValidatesExportsReparsesAndReimports) {
    SctDocument document;
    const auto sectionId = document.allocateSectionId();
    const auto targetId = document.allocateInstructionId();
    const auto stringFooter = document.allocateFooterEntryId();
    const auto sctStringFooter = document.allocateFooterEntryId();
    document.footerEntries.push_back({stringFooter, SctDocumentFooterEntryKind::String, SctPlainText{"file.mld"}});
    document.footerEntries.push_back({sctStringFooter, SctDocumentFooterEntryKind::SctString, message("message")});

    const auto& schema = *findSctOpcodeSchema(3);
    SctInstructionFactoryRequest request;
    request.opcode = 3;
    request.repeatedGroupCount = 2;
    request.parameterOverrides = requiredOverrides(schema, targetId, stringFooter, sctStringFooter);
    request.parameterOverrides.push_back({{3, 1u}, SctInstructionReference{targetId}});
    SctDocumentInstruction target{targetId, 12};
    document.sections.push_back({sectionId, "SCRIPT", SctScriptSectionContent{{target}}});
    auto draft = SctInstructionFactory::createDraft(request);
    ASSERT_TRUE(draft.draft.has_value());
    EXPECT_TRUE(std::any_of(draft.draft->parameters.begin(), draft.draft->parameters.end(), [](const auto& parameter) {
        return parameter.suggestedValue.has_value() && !parameter.value.has_value();
    }));
    const auto nextIdBeforeFailure = document.nextInstructionIdValue();
    const auto unresolved = SctInstructionFactory::materialize(document, *draft.draft);
    EXPECT_FALSE(unresolved.instruction.has_value());
    EXPECT_EQ(document.nextInstructionIdValue(), nextIdBeforeFailure);
    acceptProvisionalSuggestions(*draft.draft);
    const auto created = SctInstructionFactory::materialize(document, *draft.draft);
    ASSERT_TRUE(created.instruction.has_value());
    auto& instructions = std::get<SctScriptSectionContent>(document.sections.front().content).instructions;
    instructions.insert(instructions.begin(), *created.instruction);
    ASSERT_TRUE(SctDocumentValidator::validateDocument(document).validDocument);
    const auto exported = SctDocumentExporter::exportDocument(document, rawGameCubeOptions());
    ASSERT_TRUE(exported.success);
    const auto parsed = SctParser{}.parse(exported.bytes, "factory.sct");
    ASSERT_TRUE(parsed.parseOk);
    const auto imported = SctDocumentImporter::import(
        parsed, {{SctPlatform::GameCube}, SctTextProfile::GameCubeUs});
    ASSERT_TRUE(imported.document.has_value());
}

TEST(SctDocumentEditing, StructuralAndTextEditsExportWithoutChangingStableIdentity) {
    SctDocument document;
    const auto scriptSectionId = document.allocateSectionId();
    const auto jumpId = document.allocateInstructionId();
    const auto oldTargetId = document.allocateInstructionId();

    SctDocumentInstruction jump{jumpId, 10};
    jump.fixedParameters.push_back({0, SctInstructionReference{oldTargetId}});
    SctDocumentInstruction oldTarget{oldTargetId, 12};
    document.sections.push_back({scriptSectionId, "SCRIPT", SctScriptSectionContent{{jump, oldTarget}}});

    auto& script = std::get<SctScriptSectionContent>(document.sections.front().content).instructions;
    const auto insertedId = document.allocateInstructionId();
    SctDocumentInstruction inserted{insertedId, 12};
    script.insert(script.begin() + 1, inserted);
    auto jumpIt = std::find_if(script.begin(), script.end(), [&](const auto& instruction) { return instruction.id == jumpId; });
    ASSERT_NE(jumpIt, script.end());
    EXPECT_NE(jumpIt->id, insertedId);
    std::get<SctInstructionReference>(jumpIt->fixedParameters.front().value).target = insertedId;
    std::erase_if(script, [&](const auto& instruction) { return instruction.id == oldTargetId; });

    ASSERT_TRUE(SctDocumentValidator::validateDocument(document).validDocument);
    const auto exported = SctDocumentExporter::exportDocument(document, rawGameCubeOptions());
    ASSERT_TRUE(exported.success);
    const auto reparsed = SctParser{}.parse(exported.bytes, "edited.sct");
    ASSERT_TRUE(reparsed.parseOk);
    const auto reimported = SctDocumentImporter::import(reparsed, {{SctPlatform::GameCube}});
    ASSERT_TRUE(reimported.document.has_value());
    const auto index = SctDocumentIndex::build(*reimported.document);
    const auto& reimportedScript = std::get<SctScriptSectionContent>(
        reimported.document->sections.front().content).instructions;
    ASSERT_FALSE(reimportedScript.empty());
    const auto outbound = index.outboundReferences(reimportedScript.front().id);
    ASSERT_EQ(outbound.size(), 1u);
    EXPECT_EQ(index.inboundReferences(outbound.front().target).size(), 1u);
}

TEST(SctDocumentEditing, ImportedPhysicalStringCanGrowAndReimportAsEditableText) {
    const auto parsed = parseEditableStringFixture("short");
    ASSERT_TRUE(parsed.parseOk);
    const auto imported = SctDocumentImporter::import(
        parsed, {{SctPlatform::GameCube}, SctTextProfile::GameCubeUs});
    ASSERT_TRUE(imported.document.has_value());
    ASSERT_EQ(imported.document->strings.size(), 1u);
    auto document = *imported.document;
    std::get<SctTextChunk>(std::get<SctMessage>(document.strings.front().value).body.elements.front()).utf8 =
        "a substantially longer string";
    const auto exported = SctDocumentExporter::exportDocument(document, rawGameCubeOptions(), &imported.receipt);
    ASSERT_TRUE(exported.success);
    const auto reparsed = SctParser{}.parse(exported.bytes, "edited_string.sct");
    ASSERT_TRUE(reparsed.parseOk);
    ASSERT_EQ(reparsed.file.sections.size(), 1u);
    EXPECT_EQ(reparsed.file.sections.front().kind, SctSectionKind::String);
    const auto reimported = SctDocumentImporter::import(
        reparsed, {{SctPlatform::GameCube}, SctTextProfile::GameCubeUs});
    ASSERT_TRUE(reimported.document.has_value());
    ASSERT_EQ(reimported.document->strings.size(), 1u);
    EXPECT_EQ(std::get<SctTextChunk>(std::get<SctMessage>(
        reimported.document->strings.front().value).body.elements.front()).utf8,
        "a substantially longer string");
}

TEST(SctDocumentEditing, TypedExpressionEditsExportAndReimport) {
    SctDocument document;
    const auto sectionId = document.allocateSectionId();
    SctInstructionFactoryRequest request;
    request.opcode = 100;
    auto draft = SctInstructionFactory::createDraft(request);
    ASSERT_TRUE(draft.draft.has_value());
    acceptProvisionalSuggestions(*draft.draft);
    const auto created = SctInstructionFactory::materialize(document, *draft.draft);
    ASSERT_TRUE(created.instruction.has_value());
    document.sections.push_back({sectionId, "EXPRESSIONS", SctScriptSectionContent{{*created.instruction}}});

    auto& instruction = std::get<SctScriptSectionContent>(document.sections.front().content).instructions.front();
    const auto variable = SctExpressionFactory::variable(SctExpressionVariableKind::Integer, 8);
    ASSERT_TRUE(variable.expression.has_value());
    const auto builtOperation = SctExpressionFactory::binaryOperator(
        SctExpressionBinaryOperator::Add,
        *variable.expression,
        SctExpressionFactory::decimalLiteral(12, 128));
    ASSERT_TRUE(builtOperation.expression.has_value());
    instruction.fixedParameters[0].value = *builtOperation.expression;
    instruction.fixedParameters[1].value = SctExpressionFactory::floatLiteral(1.5f);

    ASSERT_TRUE(SctDocumentValidator::validateDocument(document).validDocument);
    const auto exported = SctDocumentExporter::exportDocument(document, rawGameCubeOptions());
    ASSERT_TRUE(exported.success);
    const auto reparsed = SctParser{}.parse(exported.bytes, "expression_edits.sct");
    ASSERT_TRUE(reparsed.parseOk);
    const auto reimported = SctDocumentImporter::import(reparsed, {{SctPlatform::GameCube}});
    ASSERT_TRUE(reimported.document.has_value());

    const auto& result = std::get<SctScriptSectionContent>(
        reimported.document->sections.front().content).instructions.front();
    const auto& editedExpression = std::get<SctCanonicalExpression>(result.fixedParameters[0].value);
    const auto& operation = std::get<SctCanonicalExpressionNode>(editedExpression.root);
    EXPECT_EQ(operation.kind, SctCanonicalExpressionNodeKind::ArithmeticOperator);
    EXPECT_EQ(operation.encodingCode, 0x0eu);
    ASSERT_EQ(operation.children.size(), 2u);
    EXPECT_EQ(operation.children[0].kind, SctCanonicalExpressionNodeKind::IntVariable);
    EXPECT_EQ(operation.children[0].encodingCode, 0x50000008u);
    EXPECT_EQ(operation.children[1].kind, SctCanonicalExpressionNodeKind::DecimalLiteral);
    EXPECT_EQ(operation.children[1].encodingCode, 0x08000c80u);

    const auto& floatExpression = std::get<SctCanonicalExpression>(result.fixedParameters[1].value);
    const auto& floatNode = std::get<SctCanonicalExpressionNode>(floatExpression.root);
    EXPECT_EQ(floatNode.kind, SctCanonicalExpressionNodeKind::FloatLiteral);
    ASSERT_EQ(floatNode.payloadWords.size(), 1u);
    EXPECT_EQ(floatNode.payloadWords.front(), 0x3fc00000u);
}

TEST(SctDocumentEditing, RepeatedGroupsCanBeAddedRemovedAndReordered) {
    SctDocument document;
    const auto sectionId = document.allocateSectionId();
    SctInstructionFactoryRequest request;
    request.opcode = 119;
    request.repeatedGroupCount = 1;
    auto draft = SctInstructionFactory::createDraft(request);
    ASSERT_TRUE(draft.draft.has_value());
    acceptProvisionalSuggestions(*draft.draft);
    const auto created = SctInstructionFactory::materialize(document, *draft.draft);
    ASSERT_TRUE(created.instruction.has_value());
    document.sections.push_back({sectionId, "REPEATED", SctScriptSectionContent{{*created.instruction}}});

    auto& instruction = std::get<SctScriptSectionContent>(document.sections.front().content).instructions.front();
    instruction.repeatedParameterGroups[0].parameters[0].value = SctExpressionFactory::decimalLiteral(10);
    instruction.repeatedParameterGroups.push_back({{{2, SctExpressionFactory::decimalLiteral(20)}}});
    std::swap(instruction.repeatedParameterGroups[0], instruction.repeatedParameterGroups[1]);
    instruction.repeatedParameterGroups.erase(instruction.repeatedParameterGroups.begin() + 1);
    instruction.repeatedParameterGroups.push_back({{{2, SctExpressionFactory::decimalLiteral(30)}}});

    ASSERT_TRUE(SctDocumentValidator::validateDocument(document).validDocument);
    const auto exported = SctDocumentExporter::exportDocument(document, rawGameCubeOptions());
    ASSERT_TRUE(exported.success);
    const auto reparsed = SctParser{}.parse(exported.bytes, "repeated_edits.sct");
    ASSERT_TRUE(reparsed.parseOk);
    ASSERT_EQ(reparsed.file.sections.front().instructions.front().parameters[1].rawWords.front(), 2u);
    const auto reimported = SctDocumentImporter::import(reparsed, {{SctPlatform::GameCube}});
    ASSERT_TRUE(reimported.document.has_value());
    const auto& result = std::get<SctScriptSectionContent>(
        reimported.document->sections.front().content).instructions.front();
    ASSERT_EQ(result.repeatedParameterGroups.size(), 2u);
    const auto& first = std::get<SctCanonicalExpression>(result.repeatedParameterGroups[0].parameters[0].value);
    const auto& second = std::get<SctCanonicalExpression>(result.repeatedParameterGroups[1].parameters[0].value);
    EXPECT_EQ(std::get<SctCanonicalExpressionNode>(first.root).encodingCode, 0x08001400u);
    EXPECT_EQ(std::get<SctCanonicalExpressionNode>(second.root).encodingCode, 0x08001e00u);
}

TEST(SctDocumentEditing, MovesTargetsAcrossSectionsAndRetargetsSwitchAndCallReferences) {
    SctDocument document;
    const auto switchSectionId = document.allocateSectionId();
    const auto callSectionId = document.allocateSectionId();
    const auto firstTargetSectionId = document.allocateSectionId();
    const auto secondTargetSectionId = document.allocateSectionId();
    const auto switchId = document.allocateInstructionId();
    const auto callId = document.allocateInstructionId();
    const auto firstTargetId = document.allocateInstructionId();
    const auto secondTargetId = document.allocateInstructionId();

    SctDocumentInstruction branch{switchId, 3};
    branch.fixedParameters.push_back({0, SctExpressionFactory::decimalLiteral(0)});
    branch.repeatedParameterGroups.push_back({{{2, SctEncodedWordValue{1}},
        {3, SctInstructionReference{firstTargetId}}}});
    branch.repeatedParameterGroups.push_back({{{2, SctEncodedWordValue{2}},
        {3, SctInstructionReference{firstTargetId}}}});
    SctDocumentInstruction call{callId, 11};
    call.fixedParameters.push_back({0, SctInstructionReference{firstTargetId}});
    SctDocumentInstruction firstTarget{firstTargetId, 12};
    SctDocumentInstruction secondTarget{secondTargetId, 12};
    document.sections.push_back({switchSectionId, "DUPLICATE", SctScriptSectionContent{{branch}}});
    document.sections.push_back({callSectionId, "DUPLICATE", SctScriptSectionContent{{call}}});
    document.sections.push_back({firstTargetSectionId, "TARGET_A", SctScriptSectionContent{{firstTarget}}});
    document.sections.push_back({secondTargetSectionId, "TARGET_B", SctScriptSectionContent{{secondTarget}}});

    auto& firstTargetScript = std::get<SctScriptSectionContent>(document.sections[2].content).instructions;
    auto& secondTargetScript = std::get<SctScriptSectionContent>(document.sections[3].content).instructions;
    std::swap(firstTargetScript.front(), secondTargetScript.front());
    document.sections[0].nameBytes = "RENAMED";
    auto& editedSwitch = std::get<SctScriptSectionContent>(document.sections[0].content).instructions.front();
    std::get<SctInstructionReference>(editedSwitch.repeatedParameterGroups[1].parameters[1].value).target = secondTargetId;
    auto& editedCall = std::get<SctScriptSectionContent>(document.sections[1].content).instructions.front();
    std::get<SctInstructionReference>(editedCall.fixedParameters[0].value).target = secondTargetId;

    const auto currentIndex = SctDocumentIndex::build(document);
    EXPECT_EQ(currentIndex.inboundReferences(SctDocumentReferenceTarget{firstTargetId}).size(), 1u);
    EXPECT_EQ(currentIndex.inboundReferences(SctDocumentReferenceTarget{secondTargetId}).size(), 2u);
    ASSERT_TRUE(SctDocumentValidator::validateDocument(document).validDocument);
    const auto exported = SctDocumentExporter::exportDocument(document, rawGameCubeOptions());
    ASSERT_TRUE(exported.success);
    ASSERT_TRUE(exported.layout.has_value());
    const auto movedLayout = std::find_if(exported.layout->instructions.begin(), exported.layout->instructions.end(),
        [&](const auto& record) { return record.id == firstTargetId; });
    const auto secondTargetSectionLayout = std::find_if(
        exported.layout->sections.begin(), exported.layout->sections.end(),
        [&](const auto& record) { return record.id == secondTargetSectionId; });
    ASSERT_NE(movedLayout, exported.layout->instructions.end());
    ASSERT_NE(secondTargetSectionLayout, exported.layout->sections.end());
    EXPECT_GE(movedLayout->span.offset, secondTargetSectionLayout->payloadSpan.offset);
    EXPECT_LT(movedLayout->span.offset,
        secondTargetSectionLayout->payloadSpan.offset + secondTargetSectionLayout->payloadSpan.size);

    const auto reparsed = SctParser{}.parse(exported.bytes, "structural_edits.sct");
    ASSERT_TRUE(reparsed.parseOk);
    ASSERT_EQ(reparsed.file.sections.size(), 4u);
    EXPECT_EQ(reparsed.file.sections[0].id.name, "RENAMED");
    EXPECT_EQ(reparsed.file.sections[1].id.name, "DUPLICATE");
    const auto reimported = SctDocumentImporter::import(reparsed, {{SctPlatform::GameCube}});
    ASSERT_TRUE(reimported.document.has_value());
    const auto& importedSwitches = std::get<SctScriptSectionContent>(
        reimported.document->sections[0].content).instructions;
    const auto& importedCalls = std::get<SctScriptSectionContent>(
        reimported.document->sections[1].content).instructions;
    const auto& importedSecondTargets = std::get<SctScriptSectionContent>(
        reimported.document->sections[2].content).instructions;
    const auto& importedFirstTargets = std::get<SctScriptSectionContent>(
        reimported.document->sections[3].content).instructions;
    ASSERT_EQ(importedSwitches.size(), 1u);
    ASSERT_EQ(importedCalls.size(), 1u);
    ASSERT_EQ(importedSecondTargets.size(), 1u);
    ASSERT_EQ(importedFirstTargets.size(), 1u);
    const auto importedIndex = SctDocumentIndex::build(*reimported.document);
    const auto switchReferences = importedIndex.outboundReferences(importedSwitches.front().id);
    const auto callReferences = importedIndex.outboundReferences(importedCalls.front().id);
    ASSERT_EQ(switchReferences.size(), 2u);
    ASSERT_EQ(callReferences.size(), 1u);
    EXPECT_EQ(switchReferences[0].target, SctDocumentReferenceTarget{importedFirstTargets.front().id});
    EXPECT_EQ(switchReferences[1].target, SctDocumentReferenceTarget{importedSecondTargets.front().id});
    EXPECT_EQ(callReferences[0].target, SctDocumentReferenceTarget{importedSecondTargets.front().id});
}

TEST(SctDocumentEditing, ReferencedFooterTextCanGrowAndReimport) {
    SctDocument document;
    const auto sectionId = document.allocateSectionId();
    const auto loadId = document.allocateInstructionId();
    const auto returnId = document.allocateInstructionId();
    const auto footerId = document.allocateFooterEntryId();
    SctDocumentInstruction load{loadId, 23};
    load.fixedParameters.push_back({0, SctFooterEntryReference{footerId}});
    document.sections.push_back({sectionId, "FOOTER", SctScriptSectionContent{{load, {returnId, 12}}}});
    document.footerEntries.push_back({footerId, SctDocumentFooterEntryKind::String, SctPlainText{"a"}});
    std::get<SctPlainText>(document.footerEntries.front().value).utf8 =
        "a much longer footer resource name.mld";

    ASSERT_TRUE(SctDocumentValidator::validateDocument(document).validDocument);
    const auto exported = SctDocumentExporter::exportDocument(document, rawGameCubeOptions());
    ASSERT_TRUE(exported.success);
    ASSERT_TRUE(exported.layout.has_value());
    ASSERT_EQ(exported.layout->footerEntries.size(), 1u);
    EXPECT_EQ(exported.layout->footerEntries.front().span.size, 39u);
    const auto reparsed = SctParser{}.parse(exported.bytes, "footer_edit.sct");
    ASSERT_TRUE(reparsed.parseOk);
    const auto reimported = SctDocumentImporter::import(
        reparsed, {{SctPlatform::GameCube}, SctTextProfile::GameCubeUs});
    ASSERT_TRUE(reimported.document.has_value());
    ASSERT_EQ(reimported.document->footerEntries.size(), 1u);
    EXPECT_EQ(std::get<SctPlainText>(reimported.document->footerEntries.front().value).utf8,
        "a much longer footer resource name.mld");
    const auto& importedInstructions = std::get<SctScriptSectionContent>(
        reimported.document->sections.front().content).instructions;
    ASSERT_FALSE(importedInstructions.empty());
    EXPECT_EQ(SctDocumentIndex::build(*reimported.document).outboundReferences(importedInstructions.front().id).size(), 1u);
}

TEST(SctDocumentValidator, LocatesNestedExpressionErrorsInsideRepeatedGroups) {
    SctDocument document;
    const auto sectionId = document.allocateSectionId();
    SctDocumentInstruction instruction{document.allocateInstructionId(), 119};
    instruction.fixedParameters.push_back({0, SctExpressionFactory::decimalLiteral(0)});
    instruction.repeatedParameterGroups.push_back({{{2, SctExpressionFactory::decimalLiteral(1)}}});

    SctCanonicalExpressionNode invalidChild;
    invalidChild.kind = SctCanonicalExpressionNodeKind::ArithmeticOperator;
    invalidChild.encodingCode = 0x0eu;
    invalidChild.children.push_back(std::get<SctCanonicalExpressionNode>(SctExpressionFactory::decimalLiteral(2).root));
    SctCanonicalExpressionNode root;
    root.kind = SctCanonicalExpressionNodeKind::ArithmeticOperator;
    root.encodingCode = 0x0eu;
    root.children.push_back(std::get<SctCanonicalExpressionNode>(SctExpressionFactory::decimalLiteral(3).root));
    root.children.push_back(std::move(invalidChild));
    instruction.repeatedParameterGroups.push_back({{{2,
        SctCanonicalExpression{std::move(root), SctExpressionTermination::StopCode}}}});
    document.sections.push_back({sectionId, "INVALID", SctScriptSectionContent{{instruction}}});

    const auto validation = SctDocumentValidator::validateDocument(document);
    const auto found = std::find_if(validation.diagnostics.begin(), validation.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.code == SctDiagnosticCode::ExpressionInvalid
            && diagnostic.parameter.has_value()
            && diagnostic.parameter->repeatedGroupOrdinal == 1u;
    });
    ASSERT_NE(found, validation.diagnostics.end());
    EXPECT_EQ(found->parameter->schemaIndex, 2u);
    EXPECT_EQ(found->expressionChildPath, std::vector<std::uint32_t>({1u}));
}

TEST(SctDocumentDeterminism, IndexLayoutDiagnosticsAndPreservationFollowDocumentOrder) {
    SctDocument document;
    const auto sectionId = document.allocateSectionId();
    const auto sourceId = document.allocateInstructionId();
    const auto firstTargetId = document.allocateInstructionId();
    const auto secondTargetId = document.allocateInstructionId();
    const auto firstAttachmentId = document.allocateOpaqueAttachmentId();
    const auto secondAttachmentId = document.allocateOpaqueAttachmentId();
    SctDocumentInstruction branch{sourceId, 3};
    branch.fixedParameters.push_back({0, SctExpressionFactory::decimalLiteral(0)});
    branch.repeatedParameterGroups.push_back({{{2, SctEncodedWordValue{1}}, {3, SctInstructionReference{firstTargetId}}}});
    branch.repeatedParameterGroups.push_back({{{2, SctEncodedWordValue{2}}, {3, SctInstructionReference{secondTargetId}}}});
    document.sections.push_back({sectionId, "ORDER", SctScriptSectionContent{
        {branch, {firstTargetId, 12}, {secondTargetId, 12}}}});
    document.opaqueAttachments.push_back({secondAttachmentId, {0xbb}, sectionId,
        SctOpaquePlacement::Before, std::nullopt, 1, SctOpaqueRelocationSupport::Relocatable, SctOpaqueReason::Gap});
    document.opaqueAttachments.push_back({firstAttachmentId, {0xaa}, sectionId,
        SctOpaquePlacement::After, std::nullopt, 1, SctOpaqueRelocationSupport::Relocatable, SctOpaqueReason::Gap});

    const auto index = SctDocumentIndex::build(document);
    const auto attachments = index.attachmentsFor(sectionId);
    ASSERT_EQ(attachments.size(), 2u);
    EXPECT_EQ(attachments[0]->id, secondAttachmentId);
    EXPECT_EQ(attachments[1]->id, firstAttachmentId);
    const auto references = index.outboundReferences(sourceId);
    ASSERT_EQ(references.size(), 2u);
    EXPECT_EQ(references[0].parameter, (SctParameterAddress{3, 0u}));
    EXPECT_EQ(references[1].parameter, (SctParameterAddress{3, 1u}));

    SctDocumentImportReceipt receipt;
    receipt.declaredSourcePlatform = SctPlatform::GameCube;
    const auto first = SctDocumentExporter::exportDocument(document, rawGameCubeOptions(), &receipt);
    const auto second = SctDocumentExporter::exportDocument(document, rawGameCubeOptions(), &receipt);
    ASSERT_TRUE(first.success);
    ASSERT_TRUE(second.success);
    EXPECT_EQ(first.bytes, second.bytes);
    ASSERT_TRUE(first.layout.has_value());
    ASSERT_TRUE(second.layout.has_value());
    ASSERT_EQ(first.layout->relocations.size(), 2u);
    EXPECT_EQ(first.layout->relocations[0].parameter, (SctParameterAddress{3, 0u}));
    EXPECT_EQ(first.layout->relocations[1].parameter, (SctParameterAddress{3, 1u}));
    ASSERT_EQ(first.preservation.attachments.size(), 2u);
    EXPECT_EQ(first.preservation.attachments[0].id, secondAttachmentId);
    EXPECT_EQ(first.preservation.attachments[1].id, firstAttachmentId);

    auto invalid = document;
    invalid.sections.front().nameBytes = std::string(17, 'X');
    invalid.sections.front().id = {};
    const auto validationA = SctDocumentValidator::validateDocument(invalid);
    const auto validationB = SctDocumentValidator::validateDocument(invalid);
    ASSERT_EQ(validationA.diagnostics.size(), validationB.diagnostics.size());
    for (std::size_t i = 0; i < validationA.diagnostics.size(); ++i) {
        EXPECT_EQ(validationA.diagnostics[i].code, validationB.diagnostics[i].code);
        EXPECT_EQ(validationA.diagnostics[i].message, validationB.diagnostics[i].message);
        EXPECT_EQ(validationA.diagnostics[i].parameter, validationB.diagnostics[i].parameter);
        EXPECT_EQ(validationA.diagnostics[i].expressionChildPath, validationB.diagnostics[i].expressionChildPath);
    }
}
