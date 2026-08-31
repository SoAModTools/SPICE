#include "../SpiceSCT/SpiceSCT.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <utility>

namespace {
using namespace spice::sct;

SctDocumentExportOptions gameCubeOptions() {
    return {SctPlatform::GameCube, SctDocumentOutputByteOrder::BigEndian,
        SctDocumentOutputWrapper::Raw, SctOpaquePreservationPolicy::RequirePreservation};
}
} // namespace

// This translation unit deliberately consumes only the public umbrella header.
// It exercises the boundary expected by a SALSA-style immutable-revision workflow:
// import, reconstitute, inspect, construct a candidate edit, validate, and export.
TEST(SctPublicWorkflow, SalsaStyleCandidateEditUsesOnlyPublicBoundary) {
    SctDocumentBuilder seedBuilder;
    const auto sectionId = seedBuilder.allocateSectionId();
    SctInstructionFactoryRequest returnRequest;
    returnRequest.opcode = 12;
    returnRequest.targetPlatform = SctPlatform::GameCube;
    const auto initialReturn = SctInstructionFactory::create(seedBuilder.document(), returnRequest);
    ASSERT_TRUE(initialReturn.instruction.has_value());
    seedBuilder.document().sections.push_back(
        {sectionId, "SCRIPT", SctScriptSectionContent{{*initialReturn.instruction}}});

    const auto seedDocument = std::move(seedBuilder).finish();
    const auto seedExport = SctDocumentExporter::exportDocument(seedDocument, gameCubeOptions());
    ASSERT_TRUE(seedExport.success);
    const auto parsed = SctParser{}.parse(seedExport.bytes, "public_workflow.sct");
    ASSERT_TRUE(parsed.parseOk);
    auto imported = SctDocumentImporter::import(parsed, {{SctPlatform::GameCube}});
    ASSERT_TRUE(imported.document.has_value());

    auto candidateResult = SctDocumentBuilder::reconstitute(std::move(*imported.document));
    ASSERT_TRUE(candidateResult.document.has_value());
    auto candidate = std::move(*candidateResult.document);
    auto beforeEdit = SctDocumentIndex::build(candidate);
    ASSERT_NE(beforeEdit.find(candidate.sections.front().id), nullptr);

    candidate.sections.front().nameBytes = "EDITED";
    SctInstructionFactoryRequest insertedInstructionRequest;
    insertedInstructionRequest.opcode = 15;
    insertedInstructionRequest.targetPlatform = SctPlatform::GameCube;
    const auto insertedInstruction = SctInstructionFactory::create(candidate, insertedInstructionRequest);
    ASSERT_TRUE(insertedInstruction.instruction.has_value());
    const auto appendedId = insertedInstruction.instruction->id;
    auto& candidateInstructions = std::get<SctScriptSectionContent>(candidate.sections.front().content).instructions;
    candidateInstructions.insert(candidateInstructions.begin(), *insertedInstruction.instruction);

    const auto afterEdit = SctDocumentIndex::build(candidate);
    EXPECT_NE(afterEdit.find(appendedId), nullptr);
    const auto validation = SctDocumentValidator::validate(
        candidate, {SctPlatform::GameCube}, &imported.receipt);
    ASSERT_TRUE(validation.validForLayout);
    const auto exported = SctDocumentExporter::exportDocument(
        candidate, gameCubeOptions(), &imported.receipt);
    ASSERT_TRUE(exported.success);

    const auto reparsed = SctParser{}.parse(exported.bytes, "public_workflow_edited.sct");
    ASSERT_TRUE(reparsed.parseOk);
    ASSERT_EQ(reparsed.file.sections.size(), 1u);
    EXPECT_EQ(reparsed.file.sections.front().id.name, "EDITED");
    ASSERT_EQ(reparsed.file.sections.front().instructions.size(), 2u);
    EXPECT_EQ(reparsed.file.sections.front().instructions[0].opcode, 15u);
    EXPECT_EQ(reparsed.file.sections.front().instructions[1].opcode, 12u);
}
