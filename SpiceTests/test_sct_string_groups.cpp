#include "../SpiceSCT/SpiceSCT.h"
#include "../SpiceSCT/SctIndexedPreamble.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace spice::sct;

void appendBe32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
}

std::vector<std::uint8_t> words(std::initializer_list<std::uint32_t> values) {
    std::vector<std::uint8_t> result;
    for (const auto value : values) appendBe32(result, value);
    return result;
}

std::vector<std::uint8_t> indexedString(std::string text) {
    auto result = words({9u, 0x1du});
    result.insert(result.end(), text.begin(), text.end());
    result.push_back(0u);
    while ((result.size() % 4u) != 0u) result.push_back(0u);
    return result;
}

std::vector<std::uint8_t> namedSct(
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& sections) {
    std::vector<std::uint8_t> result{0x07u, 0xd2u, 0u, 6u, 0u, 14u, 0u, 0u};
    appendBe32(result, static_cast<std::uint32_t>(sections.size()));
    std::uint32_t offset = 0;
    for (const auto& [name, payload] : sections) {
        appendBe32(result, offset);
        for (std::size_t index = 0; index < 16u; ++index) {
            result.push_back(index < name.size() ? static_cast<std::uint8_t>(name[index]) : 0u);
        }
        offset += static_cast<std::uint32_t>(payload.size());
    }
    for (const auto& [name, payload] : sections) {
        (void)name;
        result.insert(result.end(), payload.begin(), payload.end());
    }
    return result;
}

SctDocumentSection makeStringSection(
    SctDocument& document, std::string name, SctSectionId& sectionId, SctStringId& stringId) {
    sectionId = document.allocateSectionId();
    stringId = document.allocateStringId();
    return {sectionId, std::move(name),
        SctStringSectionContent{SctDocumentString{stringId, SctEmptyIndexedText{}}}};
}

TEST(SctIndexedPreamble, SharedStructuralRuleCoversEveryShape) {
    struct Case {
        std::vector<std::uint32_t> words;
        bool valid;
    };
    const std::vector<Case> cases{
        {{9u, 0x1du}, true},
        {{9u, 0x7ffffffeu, 0x1du}, true},
        {{}, false},
        {{9u}, false},
        {{8u, 0x1du}, false},
        {{9u, 0x7ffffffeu}, false},
        {{9u, 0x1du, 0x7ffffffeu}, false},
        {{9u, 0x1du, 0x1du}, false},
    };

    for (const auto& test : cases) {
        SCOPED_TRACE(testing::PrintToString(test.words));
        EXPECT_EQ(detail::isValidIndexedStringPreamble(test.words), test.valid);

        SctDocument stringDocument;
        const auto stringSectionId = stringDocument.allocateSectionId();
        const auto stringId = stringDocument.allocateStringId();
        stringDocument.sections.push_back({stringSectionId, "STRING",
            SctStringSectionContent{
                SctDocumentString{stringId, SctEmptyIndexedText{}}, test.words}});
        EXPECT_EQ(SctDocumentValidator::validateDocument(stringDocument).validDocument, test.valid);

        SctDocument markerDocument;
        const auto markerSectionId = markerDocument.allocateSectionId();
        markerDocument.sections.push_back({markerSectionId, "MARKER",
            SctStringGroupMarkerSectionContent{test.words}});
        EXPECT_EQ(SctDocumentValidator::validateDocument(markerDocument).validDocument, test.valid);

        SctDocument factoryDocument;
        const auto stringFactory = SctDocumentEntityFactory::createIndexedStringSection(
            factoryDocument, "STRING", SctEmptyIndexedText{}, test.words);
        const auto markerFactory = SctDocumentEntityFactory::createStringGroupMarkerSection(
            factoryDocument, "MARKER", test.words);
        EXPECT_EQ(stringFactory.section.has_value(), test.valid);
        EXPECT_EQ(markerFactory.section.has_value(), test.valid);
        EXPECT_EQ(factoryDocument.nextSectionIdValue(), test.valid ? 3u : 1u);
        EXPECT_EQ(factoryDocument.nextStringIdValue(), test.valid ? 2u : 1u);
    }
}

TEST(SctIndexedStringGroups, DerivesMarkedUnmarkedAndEmptyGroupsFromFlatOrder) {
    SctDocument document;
    const auto firstMarker = document.allocateSectionId();
    document.sections.push_back({firstMarker, "GROUP_A", SctStringGroupMarkerSectionContent{}});
    SctSectionId firstSection, secondSection, unmarkedSection, finalSection;
    SctStringId firstString, secondString, unmarkedString, finalString;
    document.sections.push_back(makeStringSection(
        document, "M00000001", firstSection, firstString));
    document.sections.push_back(makeStringSection(
        document, "M00000002", secondSection, secondString));
    document.sections.push_back(
        {document.allocateSectionId(), "SCRIPT", SctScriptSectionContent{}});
    document.sections.push_back(makeStringSection(
        document, "M00000003", unmarkedSection, unmarkedString));
    const auto emptyMarker = document.allocateSectionId();
    document.sections.push_back({emptyMarker, "EMPTY", SctStringGroupMarkerSectionContent{}});
    const auto finalMarker = document.allocateSectionId();
    document.sections.push_back({finalMarker, "GROUP_B", SctStringGroupMarkerSectionContent{}});
    document.sections.push_back(makeStringSection(
        document, "M00000004", finalSection, finalString));

    const auto index = SctIndexedStringGroupIndex::build(document);
    ASSERT_EQ(index.currentGroups().size(), 4u);
    EXPECT_EQ(index.currentGroups()[0].basis, SctIndexedStringGroupBasis::ExplicitMarker);
    EXPECT_EQ(index.currentGroups()[0].markerSection, firstMarker);
    EXPECT_EQ(index.currentGroups()[0].memberSections,
        (std::vector<SctSectionId>{firstSection, secondSection}));
    EXPECT_EQ(index.currentGroups()[0].strings,
        (std::vector<SctStringId>{firstString, secondString}));
    EXPECT_EQ(index.currentGroups()[1].basis,
        SctIndexedStringGroupBasis::UnmarkedContiguousRun);
    EXPECT_FALSE(index.currentGroups()[1].markerSection);
    EXPECT_EQ(index.currentGroups()[1].strings,
        (std::vector<SctStringId>{unmarkedString}));
    EXPECT_EQ(index.currentGroups()[2].markerSection, emptyMarker);
    EXPECT_TRUE(index.currentGroups()[2].strings.empty());
    EXPECT_EQ(index.currentGroups()[3].markerSection, finalMarker);
    EXPECT_EQ(index.currentGroups()[3].strings,
        (std::vector<SctStringId>{finalString}));

    EXPECT_EQ(index.currentByMarker(firstMarker), &index.currentGroups()[0]);
    EXPECT_EQ(index.currentContainingSection(secondSection), &index.currentGroups()[0]);
    EXPECT_EQ(index.currentContainingString(unmarkedString), &index.currentGroups()[1]);
    EXPECT_EQ(index.currentContainingSection(emptyMarker), nullptr);
}

TEST(SctIndexedStringGroups, ImportPromotesMarkerAndRetainsHistoricalGrouping) {
    const auto bytes = namedSct({
        {"GROUP", words({9u, 0x1du})},
        {"M00000001", indexedString("One")},
        {"M00000002", indexedString("Two")},
    });
    const auto parsed = SctParser{}.parse(bytes, "string_groups.sct");
    ASSERT_TRUE(parsed.parseOk);
    ASSERT_EQ(parsed.file.stringGroups.size(), 1u);

    SctDocumentImportOptions options;
    options.sourceTextEncoding = kSctWindows1252Byte7FEncoding;
    const auto imported = SctDocumentImporter::import(parsed, options);
    ASSERT_TRUE(imported.document);
    ASSERT_EQ(imported.document->sections.size(), 3u);
    const auto* marker = std::get_if<SctStringGroupMarkerSectionContent>(
        &imported.document->sections[0].content);
    ASSERT_NE(marker, nullptr);
    EXPECT_EQ(marker->preambleWords, (std::vector<std::uint32_t>{9u, 0x1du}));
    EXPECT_TRUE(std::holds_alternative<SctStringSectionContent>(
        imported.document->sections[1].content));
    EXPECT_TRUE(std::holds_alternative<SctStringSectionContent>(
        imported.document->sections[2].content));
    EXPECT_TRUE(std::none_of(imported.document->opaqueAttachments.begin(),
        imported.document->opaqueAttachments.end(), [&](const auto& attachment) {
            return attachment.anchor == SctOpaqueAnchor{imported.document->sections[0].id};
        }));
    EXPECT_TRUE(std::any_of(imported.context.receipt().sourceMap.records().begin(),
        imported.context.receipt().sourceMap.records().end(), [](const auto& record) {
            return record.role == SctSourceSpanRole::IndexedStringGroupMarkerPreamble;
        }));

    const auto evidence = imported.context.bind(imported.context.revisionProvenance());
    ASSERT_TRUE(evidence);
    const auto analysis = SctDocumentAnalysis::build(*imported.document, &*evidence);
    ASSERT_EQ(analysis.stringGroups.currentGroups().size(), 1u);
    ASSERT_EQ(analysis.stringGroups.importedGroups().size(), 1u);
    const auto& current = analysis.stringGroups.currentGroups().front();
    const auto& historical = analysis.stringGroups.importedGroups().front();
    EXPECT_EQ(current.markerSection, imported.document->sections[0].id);
    EXPECT_EQ(current.memberSections,
        (std::vector<SctSectionId>{imported.document->sections[1].id,
            imported.document->sections[2].id}));
    EXPECT_EQ(historical.markerSection, current.markerSection);
    EXPECT_EQ(historical.memberSections, current.memberSections);
    EXPECT_EQ(historical.strings, current.strings);
    EXPECT_EQ(historical.confidence, SctSemanticConfidence::Heuristic);
}

TEST(SctIndexedStringGroups, CurrentGroupingTracksEditsWhileImportedEvidenceStaysHistorical) {
    const auto parsed = SctParser{}.parse(namedSct({
        {"GROUP", words({9u, 0x1du})},
        {"M00000001", indexedString("One")},
        {"M00000002", indexedString("Two")},
    }), "string_group_edits.sct");
    const auto imported = SctDocumentImporter::import(parsed);
    ASSERT_TRUE(imported.document);
    const auto evidence = imported.context.bind(imported.context.revisionProvenance());
    ASSERT_TRUE(evidence);
    const auto originalHistorical = imported.context.receipt().indexedStringGroups;

    auto edited = *imported.document;
    std::swap(edited.sections[1], edited.sections[2]);
    edited.sections.insert(edited.sections.begin() + 2,
        {edited.allocateSectionId(), "BREAK", SctOpaqueSectionContent{}});
    const auto analysis = SctDocumentAnalysis::build(edited, &*evidence);
    ASSERT_EQ(analysis.stringGroups.currentGroups().size(), 2u);
    EXPECT_EQ(analysis.stringGroups.currentGroups()[0].markerSection,
        edited.sections[0].id);
    EXPECT_EQ(analysis.stringGroups.currentGroups()[0].memberSections,
        (std::vector<SctSectionId>{edited.sections[1].id}));
    EXPECT_EQ(analysis.stringGroups.currentGroups()[1].basis,
        SctIndexedStringGroupBasis::UnmarkedContiguousRun);
    EXPECT_EQ(analysis.stringGroups.currentGroups()[1].memberSections,
        (std::vector<SctSectionId>{edited.sections[3].id}));
    EXPECT_EQ(std::vector<SctImportedIndexedStringGroupObservation>(
        analysis.stringGroups.importedGroups().begin(),
        analysis.stringGroups.importedGroups().end()).front().memberSections,
        originalHistorical.front().memberSections);
}

TEST(SctIndexedStringGroups, ExportSerializesOnlyFlatSectionsAndRoundTripsMarkerPreamble) {
    SctDocument document;
    const auto markerId = document.allocateSectionId();
    document.sections.push_back({markerId, "GROUP",
        SctStringGroupMarkerSectionContent{{9u, 0x7ffffffeu, 0x1du}}});
    SctSectionId stringSection;
    SctStringId stringId;
    document.sections.push_back(makeStringSection(
        document, "M00000001", stringSection, stringId));
    std::get<SctStringSectionContent>(document.sections.back().content).string.value =
        SctMessage{std::nullopt, SctFormattedText{{SctTextChunk{"Text"}}}};
    const SctDocumentExportOptions options{
        SctPlatform::GameCube, kSctWindows1252Byte7FEncoding};

    const auto beforeAnalysis = SctDocumentExporter::exportDocument(document, options);
    ASSERT_TRUE(beforeAnalysis.success);
    const auto analysis = SctDocumentAnalysis::build(document);
    ASSERT_EQ(analysis.stringGroups.currentGroups().size(), 1u);
    const auto afterAnalysis = SctDocumentExporter::exportDocument(document, options);
    ASSERT_TRUE(afterAnalysis.success);
    EXPECT_EQ(afterAnalysis.bytes, beforeAnalysis.bytes);

    const auto reparsed = SctParser{}.parse(afterAnalysis.bytes, "flat_group_export.sct");
    ASSERT_TRUE(reparsed.parseOk);
    ASSERT_EQ(reparsed.file.stringGroups.size(), 1u);
    const auto reimported = SctDocumentImporter::import(reparsed);
    ASSERT_TRUE(reimported.document);
    const auto* marker = std::get_if<SctStringGroupMarkerSectionContent>(
        &reimported.document->sections[0].content);
    ASSERT_NE(marker, nullptr);
    EXPECT_EQ(marker->preambleWords,
        (std::vector<std::uint32_t>{9u, 0x7ffffffeu, 0x1du}));
}

TEST(SctIndexedStringGroups, MarkerFactoryValidatesWithoutConsumingIdsOnFailure) {
    SctDocument document;
    const auto invalid = SctDocumentEntityFactory::createStringGroupMarkerSection(
        document, "GROUP", {9u, 0x1du, 0x1du});
    EXPECT_FALSE(invalid.section);
    EXPECT_EQ(document.nextSectionIdValue(), 1u);

    const auto valid = SctDocumentEntityFactory::createStringGroupMarkerSection(
        document, "GROUP", {9u, 0x7ffffffeu, 0x1du});
    ASSERT_TRUE(valid.section);
    EXPECT_EQ(valid.section->id, SctSectionId{1u});
    EXPECT_EQ(std::get<SctStringGroupMarkerSectionContent>(valid.section->content).preambleWords,
        (std::vector<std::uint32_t>{9u, 0x7ffffffeu, 0x1du}));
}

} // namespace
