#include "../SpiceSCT/SpiceSCT.h"
#include "../SpiceSCT/SctTextCodec.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {
using namespace spice::sct;

SctMessage messageWithText(std::string text) {
    return {std::nullopt, SctFormattedText{{SctTextChunk{std::move(text)}}}};
}

void appendBe32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

} // namespace

TEST(SctSemanticText, BuilderNormalizesLineEndingsCoalescesChunksAndRejectsInvalidUtf8) {
    auto built = SctTextBuilder::build({SctTextChunk{"first\r\n"}, SctTextChunk{"second\r"},
        SctInlineCommand{SctMessageCommandCode::C, SctNoCommandArgument{}},
        SctTextChunk{"third"}, SctTextChunk{" fourth"}});
    ASSERT_TRUE(built.success);
    ASSERT_EQ(built.text.elements.size(), 3u);
    EXPECT_EQ(std::get<SctTextChunk>(built.text.elements[0]).utf8, "first\nsecond\n");
    EXPECT_EQ(std::get<SctInlineCommand>(built.text.elements[1]).code, SctMessageCommandCode::C);
    EXPECT_EQ(std::get<SctTextChunk>(built.text.elements[2]).utf8, "third fourth");

    const auto decimal = SctTextBuilder::decimalCommand(SctMessageCommandCode::A, std::nullopt);
    ASSERT_TRUE(decimal.command.has_value());
    EXPECT_FALSE(std::get<SctDecimalCommandArgument>(decimal.command->argument).value.has_value());
    EXPECT_FALSE(SctTextBuilder::noArgumentCommand(SctMessageCommandCode::A).command.has_value());
    const auto emptyList = SctTextBuilder::byteListCommand({});
    ASSERT_TRUE(emptyList.command.has_value());
    EXPECT_TRUE(std::get<SctByteListCommandArgument>(emptyList.command->argument).values.empty());
    const auto builtMessage = SctTextBuilder::message(std::string{"Header"},
        {SctTextChunk{"a\r\nb"}});
    ASSERT_TRUE(builtMessage.message.has_value());
    EXPECT_EQ(std::get<SctTextChunk>(builtMessage.message->body.elements.front()).utf8, "a\nb");
    EXPECT_FALSE(SctTextBuilder::message(std::string{"bad\nheader"}, {}).message.has_value());
    EXPECT_TRUE(SctTextBuilder::plainText("plain").text.has_value());

    const std::string invalidUtf8(1, static_cast<char>(0xff));
    const auto invalid = SctTextBuilder::build({SctTextChunk{invalidUtf8}});
    EXPECT_FALSE(invalid.success);
    ASSERT_FALSE(invalid.diagnostics.empty());
    EXPECT_EQ(invalid.diagnostics.front().code, SctDiagnosticCode::TextInvalid);
}

TEST(SctSemanticText, WesternMessageRoundTripsHeaderWhitespaceCommandsAndUnknownEscapes) {
    const std::vector<std::uint8_t> encoded{'\\', 'h', '(', 'C', 'a', 'p', 't', 'a', 'i', 'n', ')',
        'H', 'e', 'l', 'l', 'o', 0x7f, 'W', 'o', 'r', 'l', 'd', '\\', 'n',
        '\\', 'a', '(', '5', '0', ')', '\\', 'p', '(', ')', '\\', 'c', '\\', 'e',
        '\\', 'q', 0};
    const auto decoded = decodeSctTextRecord(encoded, SctTextKind::SctString,
        SctTextStorage::Footer, SctTextProfile::GameCubeUs);
    ASSERT_TRUE(decoded.value.has_value()) << decoded.error;
    const auto* message = std::get_if<SctMessage>(&*decoded.value);
    ASSERT_NE(message, nullptr);
    ASSERT_TRUE(message->headerUtf8.has_value());
    EXPECT_EQ(*message->headerUtf8, "Captain");
    ASSERT_EQ(message->body.elements.size(), 6u);
    EXPECT_EQ(std::get<SctTextChunk>(message->body.elements[0]).utf8, "Hello World\n");
    EXPECT_EQ(std::get<SctInlineCommand>(message->body.elements[1]).code, SctMessageCommandCode::A);
    EXPECT_EQ(std::get<SctDecimalCommandArgument>(
        std::get<SctInlineCommand>(message->body.elements[1]).argument).value, 50u);
    EXPECT_TRUE(std::get<SctByteListCommandArgument>(
        std::get<SctInlineCommand>(message->body.elements[2]).argument).values.empty());
    EXPECT_EQ(std::get<SctTextChunk>(message->body.elements[5]).utf8, "\\q");

    const auto reencoded = encodeSctTextRecord(*decoded.value, SctTextKind::SctString,
        SctTextStorage::Footer, SctTextProfile::GameCubeUs);
    ASSERT_TRUE(reencoded.bytes.has_value()) << reencoded.error;
    EXPECT_EQ(*reencoded.bytes, encoded);
}

TEST(SctSemanticText, ProfilesDecodeStrictRegionalTextAndPreserveEmptyIndexedRecords) {
    const std::vector<std::uint8_t> eu{'c', 'a', 'f', 0xe9, 0};
    const auto decodedEu = decodeSctTextRecord(eu, SctTextKind::PlainString,
        SctTextStorage::Footer, SctTextProfile::GameCubeEu);
    ASSERT_TRUE(decodedEu.value.has_value()) << decodedEu.error;
    EXPECT_EQ(std::get<SctPlainText>(*decodedEu.value).utf8, "caf\xc3\xa9");

    const std::vector<std::uint8_t> jp{0x93, 0xfa, 0x81, 0x40, 0x96, 0x7b, 0};
    const auto decodedJp = decodeSctTextRecord(jp, SctTextKind::SctString,
        SctTextStorage::IndexedSection, SctTextProfile::GameCubeJp);
    ASSERT_TRUE(decodedJp.value.has_value()) << decodedJp.error;
    const auto& japanese = std::get<SctMessage>(*decodedJp.value);
    ASSERT_EQ(japanese.body.elements.size(), 1u);
    EXPECT_EQ(std::get<SctTextChunk>(japanese.body.elements.front()).utf8, "\xe6\x97\xa5 \xe6\x9c\xac");

    const auto empty = decodeSctTextRecord({}, SctTextKind::SctString,
        SctTextStorage::IndexedSection, SctTextProfile::GameCubeUs);
    ASSERT_TRUE(empty.value.has_value());
    EXPECT_TRUE(std::holds_alternative<SctEmptyIndexedText>(*empty.value));

    const std::vector<std::uint8_t> invalidEu{0x81, 0};
    EXPECT_FALSE(decodeSctTextRecord(invalidEu, SctTextKind::PlainString,
        SctTextStorage::Footer, SctTextProfile::GameCubeEu).value.has_value());
}

TEST(SctSemanticText, TargetValidationRequiresMatchingProfilesAndRejectsLiteralCommandSpellings) {
    SctDocument document;
    const auto footerId = document.allocateFooterEntryId();
    document.footerEntries.push_back({footerId, SctTextKind::SctString, messageWithText("literal \\c")});
    EXPECT_TRUE(SctDocumentValidator::validateDocument(document).validDocument);
    const auto invalidLiteral = SctDocumentValidator::validateForTarget(document,
        SctPlatform::GameCube, SctTextProfile::GameCubeUs);
    EXPECT_FALSE(invalidLiteral.validForTarget);
    EXPECT_TRUE(std::any_of(invalidLiteral.diagnostics.begin(), invalidLiteral.diagnostics.end(),
        [](const auto& item) { return item.code == SctDiagnosticCode::EncodingUnsupported; }));

    document.footerEntries.front().value = messageWithText("safe");
    EXPECT_FALSE(SctDocumentValidator::validateForTarget(document,
        SctPlatform::GameCube, SctTextProfile::DreamcastUs).validForTarget);
    EXPECT_TRUE(SctDocumentValidator::validateForTarget(document,
        SctPlatform::Dreamcast, SctTextProfile::DreamcastUs).validForTarget);
}

TEST(SctSemanticText, StructuralDiagnosticsLocateHeaderAndBodyElements) {
    SctDocument document;
    const auto id = document.allocateFooterEntryId();
    SctMessage invalid;
    invalid.headerUtf8 = "bad\rheader";
    invalid.body.elements = {SctTextChunk{"left"}, SctTextChunk{"right"}};
    document.footerEntries.push_back({id, SctTextKind::SctString, std::move(invalid)});
    const auto result = SctDocumentValidator::validateDocument(document);
    EXPECT_FALSE(result.validDocument);
    EXPECT_TRUE(std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& item) {
        return item.textLocation.has_value()
            && item.textLocation->region == SctDocumentDiagnostic::TextRegion::Header;
    }));
    EXPECT_TRUE(std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& item) {
        return item.textLocation.has_value()
            && item.textLocation->region == SctDocumentDiagnostic::TextRegion::Body
            && item.textLocation->elementOrdinal == 1u;
    }));
}

TEST(SctSemanticText, SemanticMessagesCanExportAcrossCompatiblePlatforms) {
    SctDocument document;
    const auto id = document.allocateFooterEntryId();
    document.footerEntries.push_back({id, SctTextKind::SctString, messageWithText("Cross platform")});
    SctDocumentExportOptions options{SctPlatform::Dreamcast, SctTextProfile::DreamcastUs,
        SctDocumentOutputByteOrder::LittleEndian};
    const auto exported = SctDocumentExporter::exportDocument(document, options);
    ASSERT_TRUE(exported.success);
    const std::vector<std::uint8_t> expected{'C', 'r', 'o', 's', 's', 0x7f,
        'p', 'l', 'a', 't', 'f', 'o', 'r', 'm', 0};
    EXPECT_NE(std::search(exported.bytes.begin(), exported.bytes.end(), expected.begin(), expected.end()),
        exported.bytes.end());
}

TEST(SctSemanticText, ImportKeepsAmbiguousIndexedPhysicalRegionWhollyOpaque) {
    std::vector<std::uint8_t> bytes(32, 0);
    bytes[11] = 1;
    const std::string name = "M99990010";
    std::copy(name.begin(), name.end(), bytes.begin() + 16);
    appendBe32(bytes, 9u);
    appendBe32(bytes, 0x1du);
    bytes.insert(bytes.end(), {'f', 'i', 'r', 's', 't', 0, 's', 'e', 'c', 'o', 'n', 'd', 0});
    while ((bytes.size() % 4u) != 0u) bytes.push_back(0);
    SctParseResult parsed;
    parsed.parseOk = true;
    parsed.file.detectedEndian = "big";
    parsed.file.originalPayloadBytes = bytes;
    SctSection section;
    section.id.name = name;
    section.startOffset = 32u;
    section.endOffset = static_cast<std::uint32_t>(bytes.size());
    section.kind = SctSectionKind::String;
    SctStringEntry entry;
    entry.hasPreamble = true;
    entry.preambleEndOffset = 8u;
    entry.textStartOffset = 8u;
    entry.preambleWords = {9u, 0x1du};
    entry.rawTextBytes = {'f', 'i', 'r', 's', 't', 0};
    section.stringEntry = std::move(entry);
    parsed.file.sections.push_back(std::move(section));
    const auto imported = SctDocumentImporter::import(parsed,
        {{SctPlatform::GameCube}, SctTextProfile::GameCubeUs});
    ASSERT_TRUE(imported.document.has_value());
    ASSERT_EQ(imported.document->strings.size(), 1u);
    const auto* opaque = std::get_if<SctOpaqueText>(&imported.document->strings.front().value);
    ASSERT_NE(opaque, nullptr);
    EXPECT_EQ(opaque->bytes, (std::vector<std::uint8_t>{
        'f', 'i', 'r', 's', 't', 0, 's', 'e', 'c', 'o', 'n', 'd', 0, 0, 0, 0}));
}

TEST(SctHeaderProvenance, MaterializesReceiptCanonicalCrossEndianAndExplicitHeaders) {
    SctDocument document;
    const std::array<std::uint8_t, 8> raw{0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};
    SctDocumentImportReceipt receipt;
    receipt.declaredSourcePlatform = SctPlatform::GameCube;
    receipt.sourceTextProfile = SctTextProfile::GameCubeUs;
    receipt.source.byteOrder = SctSourceByteOrder::BigEndian;
    receipt.source.header = {raw, {0x1234, 0x5678, 0x9abc, 0xdef0}, true};

    SctDocumentExportOptions same{SctPlatform::GameCube, SctTextProfile::GameCubeUs};
    const auto preserved = SctDocumentExporter::exportDocument(document, same, &receipt);
    ASSERT_TRUE(preserved.success);
    ASSERT_GE(preserved.bytes.size(), 12u);
    EXPECT_TRUE(std::equal(raw.begin(), raw.end(), preserved.bytes.begin()));
    ASSERT_TRUE(preserved.preservation.header.has_value());
    EXPECT_EQ(preserved.preservation.header->status,
        SctHeaderMaterializationStatus::PreservedSourceBytes);

    SctDocumentExportOptions little{SctPlatform::GameCube, SctTextProfile::GameCubeUs,
        SctDocumentOutputByteOrder::LittleEndian};
    const auto reencoded = SctDocumentExporter::exportDocument(document, little, &receipt);
    ASSERT_TRUE(reencoded.success);
    EXPECT_EQ((std::vector<std::uint8_t>(reencoded.bytes.begin(), reencoded.bytes.begin() + 8)),
        (std::vector<std::uint8_t>{0x34, 0x12, 0x78, 0x56, 0xbc, 0x9a, 0xf0, 0xde}));
    EXPECT_EQ(reencoded.preservation.header->status,
        SctHeaderMaterializationStatus::ReencodedSourceValues);

    SctDocumentExportOptions canonical{SctPlatform::GameCube, SctTextProfile::GameCubeUs};
    const auto synthesized = SctDocumentExporter::exportDocument(document, canonical);
    ASSERT_TRUE(synthesized.success);
    EXPECT_EQ((std::vector<std::uint8_t>(synthesized.bytes.begin(), synthesized.bytes.begin() + 8)),
        (std::vector<std::uint8_t>{0x07, 0xd2, 0, 6, 0, 14, 0, 0}));
    EXPECT_EQ(synthesized.preservation.header->status,
        SctHeaderMaterializationStatus::CanonicalDefault);

    SctHeaderExportOptions explicitHeader;
    explicitHeader.mode = SctHeaderExportMode::ExplicitValues;
    explicitHeader.explicitValues = {1, 2, 3, 4};
    SctDocumentExportOptions overridden{SctPlatform::GameCube, SctTextProfile::GameCubeUs,
        SctDocumentOutputByteOrder::BigEndian, SctDocumentOutputWrapper::Raw,
        SctOpaquePreservationPolicy::RequirePreservation, explicitHeader};
    const auto explicitResult = SctDocumentExporter::exportDocument(document, overridden, &receipt);
    ASSERT_TRUE(explicitResult.success);
    EXPECT_EQ((std::vector<std::uint8_t>(explicitResult.bytes.begin(), explicitResult.bytes.begin() + 8)),
        (std::vector<std::uint8_t>{0, 1, 0, 2, 0, 3, 0, 4}));
    EXPECT_EQ(explicitResult.preservation.header->status,
        SctHeaderMaterializationStatus::ExplicitValues);
}

TEST(SctHeaderProvenance, ImportClaimsHeaderAsReceiptObservationNotOpaqueDocumentState) {
    SctParseResult parsed;
    parsed.parseOk = true;
    parsed.file.detectedEndian = "big";
    parsed.file.originalPayloadBytes = {0x07, 0xd2, 0, 6, 0, 14, 0x12, 0x34, 0, 0, 0, 0};
    const auto imported = SctDocumentImporter::import(parsed);
    ASSERT_TRUE(imported.document.has_value());
    EXPECT_TRUE(imported.receipt.source.header.available);
    EXPECT_EQ(imported.receipt.source.header.values, (SctHeaderValues{2002, 6, 14, 0x1234}));
    EXPECT_TRUE(imported.document->opaqueAttachments.empty());
    const auto headerCoverage = std::find_if(imported.receipt.provenance.begin(),
        imported.receipt.provenance.end(), [](const auto& item) {
            return item.decodedPayloadOffset == 0u && item.byteSize == 8u;
        });
    ASSERT_NE(headerCoverage, imported.receipt.provenance.end());
    EXPECT_EQ(headerCoverage->coverageKind, SctSourceCoverageKind::SourceObservation);
}
