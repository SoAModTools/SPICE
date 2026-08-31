#include "../SpiceSCT/SpiceSCT.h"

#include "../Compression/Aklz.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace {
using namespace spice::sct;

SctCanonicalExpression noLoop(std::uint32_t value = 0x7fffffffu) {
    return {SctCanonicalExpressionNode{SctCanonicalExpressionNodeKind::NoLoopValue, value, {}, {}},
        SctExpressionTermination::InlineValue};
}

SctDocumentExportOptions rawOptions(
    SctDocumentOutputByteOrder byteOrder = SctDocumentOutputByteOrder::BigEndian,
    SctPlatform platform = SctPlatform::GameCube) {
    return {platform, byteOrder, SctDocumentOutputWrapper::Raw,
        SctOpaquePreservationPolicy::RequirePreservation};
}

std::uint32_t readWord(const std::vector<std::uint8_t>& bytes, std::uint32_t offset,
    SctDocumentOutputByteOrder byteOrder) {
    if (byteOrder == SctDocumentOutputByteOrder::BigEndian) {
        return (static_cast<std::uint32_t>(bytes[offset]) << 24)
            | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16)
            | (static_cast<std::uint32_t>(bytes[offset + 2]) << 8)
            | bytes[offset + 3];
    }
    return bytes[offset]
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

SctDocument makeJumpDocument() {
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
    document.sections.push_back({sectionId, "SCRIPT", SctScriptSectionContent{{jump, target}}, {}});
    return document;
}

std::string diagnosticMessages(const std::vector<SctDocumentDiagnostic>& diagnostics) {
    std::string messages;
    for (const auto& diagnostic : diagnostics) messages += diagnostic.message + "\n";
    return messages;
}

} // namespace

TEST(SctDocumentValidationContext, DoesNotInferPlatformFromByteOrderAndRequiresReceiptOnlyForOpaqueData) {
    auto document = makeJumpDocument();
    EXPECT_TRUE(SctDocumentValidator::validate(document, {SctPlatform::Dreamcast}).validForLayout);

    const auto attachmentId = document.allocateOpaqueAttachmentId();
    document.opaqueAttachments.push_back({attachmentId, {0xaa}, SctDocumentAnchor{},
        SctOpaquePlacement::FixedOffset, 0, 1, SctOpaqueRelocationSupport::FixedOnly, SctOpaqueReason::Header});
    EXPECT_FALSE(SctDocumentValidator::validate(document, {SctPlatform::GameCube}).validForLayout);

    SctDocumentImportReceipt receipt;
    receipt.source.byteOrder = SctSourceByteOrder::LittleEndian;
    receipt.declaredSourcePlatform = SctPlatform::GameCube;
    EXPECT_TRUE(SctDocumentValidator::validate(document, {SctPlatform::GameCube}, &receipt).validForLayout);
    const auto mismatch = SctDocumentValidator::validate(document, {SctPlatform::Dreamcast}, &receipt);
    EXPECT_FALSE(mismatch.validForLayout);
    EXPECT_TRUE(std::any_of(mismatch.diagnostics.begin(), mismatch.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.code == SctDiagnosticCode::OpaquePlatformUnverified;
    }));
}

TEST(SctDocumentExporter, EmitsDeterministicBigAndLittleEndianJumpRelocations) {
    const auto document = makeJumpDocument();
    const auto originalJumpId = std::get<SctScriptSectionContent>(document.sections[0].content).instructions[0].id;
    for (const auto byteOrder : {SctDocumentOutputByteOrder::BigEndian, SctDocumentOutputByteOrder::LittleEndian}) {
        const auto first = SctDocumentExporter::exportDocument(document, rawOptions(byteOrder));
        const auto second = SctDocumentExporter::exportDocument(document, rawOptions(byteOrder));
        const auto layoutOnly = SctDocumentLayoutEngine::layout(document, rawOptions(byteOrder));
        ASSERT_TRUE(first.success) << diagnosticMessages(first.diagnostics);
        ASSERT_TRUE(second.success) << diagnosticMessages(second.diagnostics);
        ASSERT_TRUE(layoutOnly.success) << diagnosticMessages(layoutOnly.diagnostics);
        EXPECT_EQ(first.bytes, second.bytes);
        ASSERT_TRUE(first.layout.has_value());
        ASSERT_EQ(first.layout->instructions.size(), 2u);
        ASSERT_EQ(first.layout->relocations.size(), 1u);
        ASSERT_TRUE(layoutOnly.layout.has_value());
        EXPECT_EQ(layoutOnly.layout->decodedPayloadSize, first.layout->decodedPayloadSize);
        const auto& relocation = first.layout->relocations.front();
        EXPECT_EQ(relocation.formula, SctRelocationFormula::InstructionEndMinusWord);
        EXPECT_EQ(relocation.encodedValue, 4u);
        EXPECT_EQ(readWord(first.bytes, relocation.operandSpan.offset, byteOrder), 4u);

        const auto reparsed = SctParser{}.parse(first.bytes, "document_jump.sct");
        ASSERT_TRUE(reparsed.parseOk);
        ASSERT_EQ(reparsed.file.sections.size(), 1u);
        ASSERT_EQ(reparsed.file.sections[0].instructions.size(), 2u);
        EXPECT_EQ(reparsed.file.sections[0].instructions[0].opcode, 10u);
        EXPECT_EQ(reparsed.file.sections[0].instructions[1].opcode, 12u);
    }
    EXPECT_EQ(std::get<SctScriptSectionContent>(document.sections[0].content).instructions[0].id, originalJumpId);
}

TEST(SctDocumentExporter, EncodesForwardBranchAndBackwardCallRelocations) {
    SctDocument document;
    const auto branchSection = document.allocateSectionId();
    const auto targetSection = document.allocateSectionId();
    const auto callSection = document.allocateSectionId();
    const auto branchId = document.allocateInstructionId();
    const auto targetId = document.allocateInstructionId();
    const auto callId = document.allocateInstructionId();
    SctDocumentInstruction branch;
    branch.id = branchId;
    branch.opcode = 0;
    branch.fixedParameters = {{0, noLoop()}, {1, SctInstructionReference{targetId}}};
    SctDocumentInstruction target;
    target.id = targetId;
    target.opcode = 12;
    SctDocumentInstruction call;
    call.id = callId;
    call.opcode = 11;
    call.fixedParameters = {{0, SctInstructionReference{targetId}}};
    document.sections.push_back({branchSection, "BRANCH", SctScriptSectionContent{{branch}}, {}});
    document.sections.push_back({targetSection, "TARGET", SctScriptSectionContent{{target}}, {}});
    document.sections.push_back({callSection, "CALL", SctScriptSectionContent{{call}}, {}});

    const auto exported = SctDocumentExporter::exportDocument(document, rawOptions());
    ASSERT_TRUE(exported.success) << diagnosticMessages(exported.diagnostics);
    ASSERT_TRUE(exported.layout.has_value());
    ASSERT_EQ(exported.layout->relocations.size(), 2u);
    const auto branchRelocation = std::find_if(exported.layout->relocations.begin(), exported.layout->relocations.end(),
        [&](const auto& relocation) { return relocation.sourceInstruction == branchId; });
    const auto callRelocation = std::find_if(exported.layout->relocations.begin(), exported.layout->relocations.end(),
        [&](const auto& relocation) { return relocation.sourceInstruction == callId; });
    ASSERT_NE(branchRelocation, exported.layout->relocations.end());
    ASSERT_NE(callRelocation, exported.layout->relocations.end());
    EXPECT_EQ(branchRelocation->formula, SctRelocationFormula::InstructionEndMinusWord);
    EXPECT_EQ(callRelocation->formula, SctRelocationFormula::InstructionEndMinusWord);
    EXPECT_GT(static_cast<std::int32_t>(branchRelocation->encodedValue), 0);
    EXPECT_LT(static_cast<std::int32_t>(callRelocation->encodedValue), 0);
}

TEST(SctDocumentExporter, EncodesSwitchFooterAndScheduledContractsFromTheSchema) {
    SctDocument document;
    const auto switchSection = document.allocateSectionId();
    const auto footerSection = document.allocateSectionId();
    const auto targetSection = document.allocateSectionId();
    const auto switchId = document.allocateInstructionId();
    const auto footerLoadId = document.allocateInstructionId();
    const auto signedFooterLoadId = document.allocateInstructionId();
    const auto targetLeadId = document.allocateInstructionId();
    const auto targetId = document.allocateInstructionId();
    const auto footerId = document.allocateFooterEntryId();

    SctDocumentInstruction sw;
    sw.id = switchId;
    sw.opcode = 3;
    sw.fixedParameters.push_back({0, noLoop()});
    sw.repeatedParameterGroups.push_back({{{2, SctEncodedWordValue{7}},
        {3, SctInstructionReference{targetId}}}});

    SctDocumentInstruction footerLoad;
    footerLoad.id = footerLoadId;
    footerLoad.opcode = 43;
    footerLoad.skipRefresh = true;
    footerLoad.scheduledExpression = noLoop(0x00800000u);
    footerLoad.fixedParameters.push_back({0, SctFooterEntryReference{footerId}});

    SctDocumentInstruction signedFooterLoad;
    signedFooterLoad.id = signedFooterLoadId;
    signedFooterLoad.opcode = 23;
    signedFooterLoad.fixedParameters.push_back({0, SctFooterEntryReference{footerId}});

    SctDocumentInstruction targetLead;
    targetLead.id = targetLeadId;
    targetLead.opcode = 15;
    SctDocumentInstruction target;
    target.id = targetId;
    target.opcode = 12;
    document.sections.push_back({switchSection, "SWITCH", SctScriptSectionContent{{sw}}, {}});
    document.sections.push_back({footerSection, "FOOTER", SctScriptSectionContent{{footerLoad, signedFooterLoad}}, {}});
    document.sections.push_back({targetSection, "TARGET", SctScriptSectionContent{{targetLead, target}}, {}});
    document.footerEntries.push_back({footerId, SctDocumentFooterEntryKind::String, SctEditableText{"entry"}});

    const auto exported = SctDocumentExporter::exportDocument(document, rawOptions());
    ASSERT_TRUE(exported.success) << diagnosticMessages(exported.diagnostics);
    ASSERT_TRUE(exported.layout.has_value());
    ASSERT_EQ(exported.layout->relocations.size(), 3u);
    EXPECT_TRUE(std::any_of(exported.layout->relocations.begin(), exported.layout->relocations.end(),
        [](const auto& relocation) {
            return relocation.parameter.repeatedGroupOrdinal == 0
                && std::holds_alternative<SctInstructionId>(relocation.target);
        }));
    EXPECT_TRUE(std::any_of(exported.layout->relocations.begin(), exported.layout->relocations.end(),
        [](const auto& relocation) { return std::holds_alternative<SctFooterEntryId>(relocation.target); }));

    const auto reparsed = SctParser{}.parse(exported.bytes, "document_contracts.sct");
    ASSERT_TRUE(reparsed.parseOk);
    ASSERT_EQ(reparsed.file.sections.size(), 3u);
    ASSERT_FALSE(reparsed.file.sections[0].instructions.empty());
    ASSERT_GE(reparsed.file.sections[0].instructions[0].parameters.size(), 2u);
    ASSERT_EQ(reparsed.file.sections[0].instructions[0].parameters[1].rawWords.size(), 1u);
    EXPECT_EQ(reparsed.file.sections[0].instructions[0].parameters[1].rawWords[0], 1u);
    ASSERT_FALSE(reparsed.file.sections[1].instructions.empty());
    EXPECT_TRUE(reparsed.file.sections[1].instructions[0].skipRefresh);
    EXPECT_TRUE(reparsed.file.sections[1].instructions[0].scheduled.present);
    EXPECT_EQ(reparsed.file.sections[1].instructions[0].scheduled.instructionByteLength, 8u);
    ASSERT_TRUE(reparsed.file.footer.has_value());
    std::string parseMessages;
    for (const auto& diagnostic : reparsed.diagnostics) parseMessages += diagnostic.message + "\n";
    ASSERT_EQ(reparsed.file.footer->entries.size(), 1u) << parseMessages;
    EXPECT_EQ(reparsed.file.footer->entries[0].rawBytes,
        (std::vector<std::uint8_t>{'e', 'n', 't', 'r', 'y', 0}));
}

TEST(SctDocumentExporter, PreservesFixedAndRelocatableOpaqueAttachmentsOrRejectsConflict) {
    SctDocument document;
    const auto sectionId = document.allocateSectionId();
    const auto headerId = document.allocateOpaqueAttachmentId();
    const auto paddingId = document.allocateOpaqueAttachmentId();
    const auto contentId = document.allocateOpaqueAttachmentId();
    document.sections.push_back({sectionId, "A", SctLabelSectionContent{}, {paddingId, contentId}});
    document.opaqueAttachments.push_back({headerId, {1, 2, 3, 4, 5, 6, 7, 8}, SctDocumentAnchor{},
        SctOpaquePlacement::FixedOffset, 0, 1, SctOpaqueRelocationSupport::FixedOnly, SctOpaqueReason::Header});
    document.opaqueAttachments.push_back({paddingId, std::vector<std::uint8_t>(15, 0), sectionId,
        SctOpaquePlacement::FixedOffset, 17, 1, SctOpaqueRelocationSupport::FixedOnly, SctOpaqueReason::Padding});
    document.opaqueAttachments.push_back({contentId, {0xaa, 0xbb}, sectionId,
        SctOpaquePlacement::FixedOffset, 32, 1, SctOpaqueRelocationSupport::FixedOnly, SctOpaqueReason::UnknownEncoding});
    SctDocumentImportReceipt receipt;
    receipt.declaredSourcePlatform = SctPlatform::GameCube;

    const auto exported = SctDocumentExporter::exportDocument(document, rawOptions(), &receipt);
    ASSERT_TRUE(exported.success) << diagnosticMessages(exported.diagnostics);
    ASSERT_TRUE(exported.layout.has_value());
    ASSERT_EQ(exported.preservation.attachments.size(), 3u);
    EXPECT_EQ(std::vector<std::uint8_t>(exported.bytes.begin(), exported.bytes.begin() + 8),
        (std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 7, 8}));
    EXPECT_EQ(exported.bytes[32], 0xaa);
    EXPECT_EQ(exported.bytes[33], 0xbb);
    EXPECT_TRUE(std::all_of(exported.preservation.attachments.begin(), exported.preservation.attachments.end(),
        [](const auto& item) { return item.status == SctOpaquePreservationStatus::PreservedByteIdentically; }));

    auto conflicting = document;
    const auto conflictId = conflicting.allocateOpaqueAttachmentId();
    conflicting.opaqueAttachments.push_back({conflictId, {9, 9, 9, 9}, SctDocumentAnchor{},
        SctOpaquePlacement::FixedOffset, 8, 1, SctOpaqueRelocationSupport::FixedOnly, SctOpaqueReason::Gap});
    const auto rejected = SctDocumentExporter::exportDocument(conflicting, rawOptions(), &receipt);
    EXPECT_FALSE(rejected.success);
    ASSERT_EQ(rejected.preservation.attachments.size(), conflicting.opaqueAttachments.size());
    EXPECT_TRUE(std::all_of(rejected.preservation.attachments.begin(), rejected.preservation.attachments.end(),
        [](const auto& item) { return item.status == SctOpaquePreservationStatus::Rejected; }));
    EXPECT_TRUE(std::any_of(rejected.diagnostics.begin(), rejected.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.code == SctDiagnosticCode::OpaquePlacementUnsatisfied;
    }));
}

TEST(SctDocumentExporter, PlacesSupportedRelativeOpaqueAttachmentsAndReportsRelocation) {
    SctDocument document;
    const auto sectionId = document.allocateSectionId();
    const auto attachmentId = document.allocateOpaqueAttachmentId();
    document.sections.push_back({sectionId, "LABEL", SctLabelSectionContent{}, {attachmentId}});
    document.opaqueAttachments.push_back({attachmentId, {0xde, 0xad}, sectionId,
        SctOpaquePlacement::Before, std::nullopt, 1, SctOpaqueRelocationSupport::Relocatable,
        SctOpaqueReason::Preamble});
    SctDocumentImportReceipt receipt;
    receipt.declaredSourcePlatform = SctPlatform::GameCube;

    const auto exported = SctDocumentExporter::exportDocument(document, rawOptions(), &receipt);
    ASSERT_TRUE(exported.success) << diagnosticMessages(exported.diagnostics);
    ASSERT_EQ(exported.preservation.attachments.size(), 1u);
    EXPECT_EQ(exported.preservation.attachments[0].status, SctOpaquePreservationStatus::RelocatedUnderRule);
    const auto span = exported.preservation.attachments[0].span;
    EXPECT_EQ(std::vector<std::uint8_t>(exported.bytes.begin() + span.offset,
        exported.bytes.begin() + span.offset + span.size), (std::vector<std::uint8_t>{0xde, 0xad}));
}

TEST(SctDocumentExporter, WrapsTheCompletedPayloadWithAklz) {
    const auto document = makeJumpDocument();
    auto options = rawOptions();
    options.wrapper = SctDocumentOutputWrapper::Aklz;
    const auto exported = SctDocumentExporter::exportDocument(document, options);
    ASSERT_TRUE(exported.success) << diagnosticMessages(exported.diagnostics);
    EXPECT_TRUE(spice::compression::aklz::isAklz(exported.bytes));
    const auto decoded = spice::compression::aklz::decompress(exported.bytes);
    ASSERT_TRUE(decoded.ok());
    EXPECT_EQ(decoded.bytes.size(), exported.decodedPayloadSize);
    EXPECT_TRUE(SctParser{}.parse(exported.bytes, "document_jump.aklz").parseOk);
}
