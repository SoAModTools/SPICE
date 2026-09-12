#include "../SpiceStd/SpiceStd.h"
#include "../SpiceMLD/SpiceMLD.h"
#include "../SpiceRoot/Binary/EndianReader.h"
#include "../SpiceRoot/Binary/EndianWriter.h"
#include "../SpiceModeling/MotionDocument.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <array>

namespace {
using namespace spice::stdfile;
using namespace spice::mld;
using spice::root::Endian;
using spice::root::EndianSpanWriter;

// Synthetic bytes only: these fixtures test contracts, not game qualification.
std::vector<std::uint8_t> type53Bytes(Endian endian, std::uint32_t size = 0x90U) {
    std::vector<std::uint8_t> bytes(0x30U + size, 0U);
    EndianSpanWriter w(bytes, endian);
    w.write_u16_at(0, 2); w.write_u16_at(2, 4);
    w.write_u32_at(0xc, static_cast<std::uint32_t>(bytes.size() - 0x10U));
    w.write_i16_at(0x10, 0x53); w.write_i16_at(0x12, 3);
    w.write_u32_at(0x18, size); w.write_u32_at(0x1c, size ? 0x20U : 0U);
    w.write_i16_at(0x20, -1);
    for (std::size_t i = 0x30; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(i * 13U);
    if (size >= 0x16) {
        w.write_u32_at(0x38, 53); w.write_u32_at(0x40, 0x81234567);
        w.write_i16_at(0x44, -32760);
    }
    return bytes;
}

std::vector<std::uint8_t> listBytes(Endian endian = Endian::Big) {
    std::vector<std::uint8_t> bytes(0x240U, 0U);
    EndianSpanWriter w(bytes, endian);
    w.write_u32_at(0, 2); w.write_u32_at(4, 0x20);
    w.write_u32_at(8, 0x100); w.write_u32_at(0xc, 0x180); w.write_u32_at(0x10, 0x220);
    for (std::size_t ordinal = 0; ordinal < 2; ++ordinal) {
        const auto entry = 0x20 + ordinal * 0x68;
        w.write_u32_at(entry, ordinal == 0 ? 900 : 700);
        w.write_i32_at(entry + 4, -7);
        w.write_u32_at(entry + 0x1c, ordinal == 0 ? 0x100 : 0x120);
        for (auto offset : {0x5c, 0x60, 0x64}) w.write_u32_at(entry + offset, 0x3f800000);
    }
    w.write_u32_at(0x120, 3);
    w.write_u32_at(0x128, 0x180); // [null, opaque motion, null]
    bytes[0x180] = 'A'; bytes[0x181] = 'B'; bytes[0x182] = 'C'; bytes[0x183] = 'D';
    return bytes;
}

std::shared_ptr<const spice::modeling::MotionDocument> motionWithFrames(std::uint32_t frames) {
    std::vector<std::uint8_t> bytes(0x30U, 0U);
    bytes[0] = 'N'; bytes[1] = 'M'; bytes[2] = 'D'; bytes[3] = 'M';
    EndianSpanWriter w(bytes, Endian::Little);
    w.write_u32_at(4, 0x28); w.write_u32_at(0xc, frames);
    const auto decoded = spice::modeling::MotionDocumentCodec::decode(bytes, {});
    EXPECT_TRUE(decoded.ok());
    return decoded.document;
}
}

TEST(StdType53Projection, ProjectsSixProfilesAndPreservesNativeOpaqueBytes) {
    const std::array profiles{StdModelReferenceProfile::GameCubeUS, StdModelReferenceProfile::GameCubeEU,
        StdModelReferenceProfile::GameCubeJP, StdModelReferenceProfile::DreamcastUS,
        StdModelReferenceProfile::DreamcastEU, StdModelReferenceProfile::DreamcastJP};
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        const auto endian = i < 3 ? Endian::Big : Endian::Little;
        const auto bytes = type53Bytes(endian);
        const auto imported = StdDocumentImporter::importBytes(bytes);
        ASSERT_TRUE(imported.ok());
        const auto& table = std::get<StdEntryTableContent>(imported.document->content);
        const auto p = StdType53Projector::project(*imported.document, table.records[0].id, imported.receipt, profiles[i]);
        ASSERT_TRUE(p.ok());
        EXPECT_EQ(p.payloadId, table.records[0].payload);
        EXPECT_EQ(p.sourceRecordOffset, 0x10U); EXPECT_EQ(p.sourcePayloadOffset, 0x30U);
        EXPECT_EQ(p.sourceSha256, imported.receipt.sourceSha256); EXPECT_EQ(p.sourceSize, bytes.size());
        EXPECT_EQ(p.resource.encodedKey, 53U); EXPECT_EQ(p.resource.logicalName, "E0000053.MLD");
        EXPECT_EQ(p.resource.nameSpace, StdModelReferenceNamespace::RegisteredModelName);
        EXPECT_EQ(p.flags, 0x81234567U); EXPECT_EQ(p.delay, -32760); EXPECT_EQ(p.meshOrdinal, 0);
        const auto written = StdDocumentWriter::write(*imported.document,
            {i < 3 ? StdPlatform::GameCube : StdPlatform::Dreamcast, StdCompression::None}, &imported.receipt);
        ASSERT_TRUE(written.ok()); EXPECT_EQ(written.bytes, bytes);
        const auto reparsed = StdDocumentImporter::importBytes(written.bytes);
        ASSERT_TRUE(reparsed.ok()); EXPECT_EQ(*reparsed.document, *imported.document);
        EXPECT_FALSE(StdDocumentWriter::write(*imported.document,
            {i < 3 ? StdPlatform::Dreamcast : StdPlatform::GameCube, StdCompression::None}, &imported.receipt).ok());
    }
}

TEST(StdType53Projection, DecodesSignedFormatterRulesWithoutClaimingDatasetBinding) {
    const auto gc = StdModelReferenceProfile::GameCubeUS;
    const auto dc = StdModelReferenceProfile::DreamcastUS;
    EXPECT_EQ(decodeStdModelResourceReference(32768, gc).logicalName, "E00-327-68.MLD");
    EXPECT_EQ(decodeStdModelResourceReference(32768, dc).logicalName, "E0032768.MLD");
    EXPECT_EQ(decodeStdModelResourceReference(9900053, gc).fallbackDirectory, "BCHARA");
    EXPECT_EQ(decodeStdModelResourceReference(53, gc).fallbackDirectory, "BEFF");
    EXPECT_EQ(decodeStdModelResourceReference(10000017, gc).logicalName, "MA017.MLD");
    EXPECT_EQ(decodeStdModelResourceReference(10001017, gc).logicalName, "MB017.MLD");
    EXPECT_EQ(decodeStdModelResourceReference(10002017, gc).logicalName, "MG017.MLD");
    EXPECT_EQ(decodeStdModelResourceReference(10003017, dc).logicalName, "MA017.MLD");
    EXPECT_EQ(decodeStdModelResourceReference(75537017, gc).logicalName, "MB017.MLD");
    EXPECT_EQ(decodeStdModelResourceReference(42768000, gc).status, StdModelReferenceStatus::UnsupportedKey);
    EXPECT_EQ(decodeStdModelResourceReference(0xffffffff, gc).status, StdModelReferenceStatus::UnsupportedKey);
    EXPECT_EQ(decodeStdModelResourceReference(9999999, gc).status, StdModelReferenceStatus::Sentinel);
    EXPECT_EQ(decodeStdModelResourceReference(53, StdModelReferenceProfile::Unknown).status,
        StdModelReferenceStatus::UnsupportedProfile);
}

TEST(StdType53Projection, KeepsRecordPayloadAssociationAcrossPhysicalOrderAndAklzWrapping) {
    std::vector<std::uint8_t> bytes(0x160U, 0U);
    EndianSpanWriter w(bytes, Endian::Big);
    w.write_u16_at(0, 3); w.write_u16_at(2, 4); w.write_u32_at(0xc, 0x150);
    for (std::size_t offset : {0x10U, 0x20U}) {
        w.write_i16_at(offset, 0x53); w.write_i16_at(offset + 2, 3);
        w.write_u32_at(offset + 8, 0x90);
    }
    w.write_u32_at(0x1c, 0xc0); w.write_u32_at(0x2c, 0x30); // reversed payload order
    w.write_i16_at(0x30, -1);
    w.write_u32_at(0xd8, 53); w.write_u32_at(0x48, 71);
    const auto imported = StdDocumentImporter::importBytes(bytes);
    ASSERT_TRUE(imported.ok());
    const auto& table = std::get<StdEntryTableContent>(imported.document->content);
    ASSERT_EQ(table.records.size(), 2U);
    auto second = StdType53Projector::project(*imported.document, table.records[1].id,
        imported.receipt, StdModelReferenceProfile::GameCubeUS);
    ASSERT_TRUE(second.ok()); EXPECT_EQ(second.resource.encodedKey, 71U);
    EXPECT_EQ(second.payloadId, table.records[1].payload);
    EXPECT_EQ(second.sourceRecordOffset, 0x20U); EXPECT_EQ(second.sourcePayloadOffset, 0x40U);
    const auto written = StdDocumentWriter::write(*imported.document,
        {StdPlatform::GameCube, StdCompression::Aklz}, &imported.receipt);
    ASSERT_TRUE(written.ok());
    const auto reparsed = StdDocumentImporter::importBytes(written.bytes);
    ASSERT_TRUE(reparsed.ok()); EXPECT_EQ(reparsed.receipt.compression, StdCompression::Aklz);
    second = StdType53Projector::project(*reparsed.document,
        std::get<StdEntryTableContent>(reparsed.document->content).records[1].id,
        reparsed.receipt, StdModelReferenceProfile::GameCubeUS);
    EXPECT_TRUE(second.ok()); EXPECT_EQ(second.resource.encodedKey, 71U);
    EXPECT_NE(second.sourceSha256, imported.receipt.sourceSha256);
    auto reordered = *imported.document;
    auto& edited = std::get<StdEntryTableContent>(reordered.content);
    std::swap(edited.records[0], edited.records[1]);
    EXPECT_EQ(StdType53Projector::project(reordered, table.records[1].id, imported.receipt,
        StdModelReferenceProfile::GameCubeUS).status, StdType53ProjectionStatus::SourceChanged);
}

TEST(StdType53Projection, RejectsMissingMalformedUnqualifiedAndStaleInputs) {
    const auto profile = StdModelReferenceProfile::GameCubeUS;
    const auto imported = StdDocumentImporter::importBytes(type53Bytes(Endian::Big));
    ASSERT_TRUE(imported.ok());
    const auto id = std::get<StdEntryTableContent>(imported.document->content).records[0].id;
    const auto project = [&](const StdDocument& doc, const StdImportReceipt& receipt) {
        return StdType53Projector::project(doc, id, receipt, profile).status;
    };
    EXPECT_EQ(project(*imported.document, {}), StdType53ProjectionStatus::ReceiptRequired);
    const auto other = StdDocumentImporter::importBytes(type53Bytes(Endian::Big));
    EXPECT_EQ(project(*imported.document, other.receipt), StdType53ProjectionStatus::ReceiptMismatch);
    auto receipt = imported.receipt; ++receipt.sourceSize;
    EXPECT_EQ(project(*imported.document, receipt), StdType53ProjectionStatus::ReceiptMismatch);
    EXPECT_EQ(StdType53Projector::project(*imported.document, id, imported.receipt,
        StdModelReferenceProfile::DreamcastUS).status, StdType53ProjectionStatus::ReceiptMismatch);
    EXPECT_EQ(StdType53Projector::project(*imported.document, id, imported.receipt,
        StdModelReferenceProfile::Unknown).status, StdType53ProjectionStatus::UnsupportedProfile);
    auto doc = *imported.document;
    doc.sourceIdentity = {};
    EXPECT_EQ(project(doc, imported.receipt), StdType53ProjectionStatus::ReceiptMismatch);
    doc = *imported.document;
    auto& table = std::get<StdEntryTableContent>(doc.content);
    std::get<StdOpaquePayload>(table.payloads[0].content).bytes.back() ^= 1;
    EXPECT_EQ(project(doc, imported.receipt), StdType53ProjectionStatus::SourceChanged);
    table.payloads.push_back(table.payloads[0]);
    EXPECT_EQ(project(doc, imported.receipt), StdType53ProjectionStatus::InvalidIdentity);
    doc = *imported.document;
    std::get<StdEntryTableContent>(doc.content).records.push_back(std::get<StdEntryTableContent>(doc.content).records[0]);
    EXPECT_EQ(project(doc, imported.receipt), StdType53ProjectionStatus::InvalidIdentity);
    doc = *imported.document;
    std::get<StdEntryTableContent>(doc.content).records[0].payload = StdEntryPayloadId{999};
    EXPECT_EQ(project(doc, imported.receipt), StdType53ProjectionStatus::MissingPayload);
    doc = *imported.document;
    std::get<StdEntryTableContent>(doc.content).records[0].locationCode = 3;
    EXPECT_EQ(project(doc, imported.receipt), StdType53ProjectionStatus::UnsupportedCommand);
    EXPECT_EQ(StdType53Projector::project(*imported.document, StdEntryRecordId{999}, imported.receipt, profile).status,
        StdType53ProjectionStatus::MissingRecord);
    for (auto size : {0U, 0x8fU, 0x91U}) {
        const auto malformed = StdDocumentImporter::importBytes(type53Bytes(Endian::Big, size));
        ASSERT_TRUE(malformed.ok());
        EXPECT_EQ(project(*malformed.document, malformed.receipt), size ? StdType53ProjectionStatus::MalformedPayload
            : StdType53ProjectionStatus::MissingPayload);
    }
    auto sentinel = type53Bytes(Endian::Big);
    EndianSpanWriter(sentinel, Endian::Big).write_u32_at(0x38, 9999999);
    const auto unavailable = StdDocumentImporter::importBytes(sentinel);
    ASSERT_TRUE(unavailable.ok());
    const auto p = StdType53Projector::project(*unavailable.document, id, unavailable.receipt, profile);
    EXPECT_EQ(p.status, StdType53ProjectionStatus::UnsupportedResourceKey);
    EXPECT_EQ(p.resource.status, StdModelReferenceStatus::Sentinel);
}

TEST(MldMotionListProjection, SelectsExplicitOrdinalPreservingNullSlotsAndEmptyDistinction) {
    for (auto endian : {Endian::Big, Endian::Little}) {
        const auto imported = MldDocumentImporter::importBytes(listBytes(endian));
        ASSERT_TRUE(imported.ok()); ASSERT_EQ(imported.document->entries.size(), 2U);
        const auto empty = MldMotionListProjector::selectImportedEntry(*imported.document, 0, imported.receipt);
        ASSERT_TRUE(empty.verifiedPresentEmpty()); EXPECT_FALSE(empty.motionFrames.ok());
        EXPECT_EQ(empty.sourceEntryOffset, 0x20U); EXPECT_EQ(empty.sourceListPointer, 0x100U);
        EXPECT_EQ(empty.declaredSlotCount, 0U); EXPECT_EQ(empty.sourceSha256, imported.receipt.sourceSha256);
        const auto second = MldMotionListProjector::selectImportedEntry(*imported.document, 1, imported.receipt);
        ASSERT_TRUE(second.validPresentList()); EXPECT_FALSE(second.verifiedPresentEmpty());
        EXPECT_EQ(second.entryId, imported.document->entries[1].id);
        EXPECT_EQ(second.sourceOrdinal, 1U); EXPECT_EQ(second.sourceEntryOffset, 0x88U);
        EXPECT_EQ(second.sourceSlotReferences, (std::vector<std::uint32_t>{0, 0x180, 0}));
        ASSERT_EQ(second.motionFrames.slots.size(), 3U);
        EXPECT_EQ(second.motionFrames.slots[0].status, MldMotionFrameSlotStatus::EmptySlot);
        EXPECT_EQ(second.motionFrames.slots[1].status, MldMotionFrameSlotStatus::OpaqueMotion);
        EXPECT_EQ(second.motionFrames.slots[2].status, MldMotionFrameSlotStatus::EmptySlot);
        for (auto ordinal : {-1, 2, 700, 900})
            EXPECT_EQ(MldMotionListProjector::selectImportedEntry(*imported.document, ordinal, imported.receipt).bindingStatus,
                MldSourceBindingStatus::MissingEntry);
        EXPECT_EQ(MldMotionListProjector::project(*imported.document, MldEntryId{900}, imported.receipt).bindingStatus,
            MldSourceBindingStatus::MissingEntry);
    }
}

TEST(MldMotionListProjection, AbsentAndMalformedListsNeverCertifyEmpty) {
    struct Case { std::uint32_t pointer, count; MldMotionListStatus status; };
    for (auto endian : {Endian::Big, Endian::Little}) {
        for (const auto c : {Case{0, 0, MldMotionListStatus::Absent},
            Case{0x240, 0, MldMotionListStatus::PointerOutOfBounds},
            Case{0xffffffff, 0, MldMotionListStatus::PointerOutOfBounds},
            Case{0x23e, 0, MldMotionListStatus::TruncatedCount},
            Case{0x100, 65537, MldMotionListStatus::ExcessiveCount},
            Case{0x100, 81, MldMotionListStatus::TruncatedValues}}) {
            auto bytes = listBytes(endian);
            EndianSpanWriter w(bytes, endian);
            w.write_u32_at(0x3c, c.pointer);
            if (c.pointer == 0x100) w.write_u32_at(c.pointer, c.count);
            const auto imported = MldDocumentImporter::importBytes(bytes);
            ASSERT_TRUE(imported.document.has_value());
            const auto p = MldMotionListProjector::selectImportedEntry(*imported.document, 0, imported.receipt);
            EXPECT_EQ(p.bindingStatus, MldSourceBindingStatus::Matched);
            EXPECT_EQ(p.listStatus, c.status); EXPECT_FALSE(p.verifiedPresentEmpty());
            const MldWriteTarget target{endian == Endian::Big ? MldPlatform::GameCube : MldPlatform::Dreamcast, MldWrapper::Raw};
            const auto written = MldDocumentWriter::write(*imported.document, target, &imported.receipt);
            if (c.status == MldMotionListStatus::Absent) {
                ASSERT_TRUE(written.ok());
                const auto reimported = MldDocumentImporter::importBytes(written.bytes);
                ASSERT_TRUE(reimported.ok());
                EXPECT_EQ(MldMotionListProjector::selectImportedEntry(*reimported.document, 0, reimported.receipt).listStatus,
                    MldMotionListStatus::Absent);
                auto edited = *imported.document; edited.entries[0].motionSlots.push_back(std::nullopt);
                EXPECT_FALSE(MldDocumentWriter::write(edited, target, &imported.receipt).ok());
            } else EXPECT_FALSE(written.ok());
        }
    }
}

TEST(MldMotionListProjection, RejectsMismatchedReceiptsEditsAndDuplicateIds) {
    const auto imported = MldDocumentImporter::importBytes(listBytes());
    ASSERT_TRUE(imported.ok());
    const auto project = [&](const MldDocument& doc, const MldImportReceipt& receipt) {
        return MldMotionListProjector::selectImportedEntry(doc, 0, receipt).bindingStatus;
    };
    EXPECT_EQ(project(*imported.document, {}), MldSourceBindingStatus::ReceiptRequired);
    const auto other = MldDocumentImporter::importBytes(listBytes());
    EXPECT_EQ(project(*imported.document, other.receipt), MldSourceBindingStatus::ReceiptMismatch);
    auto receipt = imported.receipt; receipt.sourceSha256[0] ^= 1;
    EXPECT_EQ(project(*imported.document, receipt), MldSourceBindingStatus::ReceiptMismatch);
    auto doc = *imported.document; doc.sourceIdentity = {};
    EXPECT_EQ(project(doc, imported.receipt), MldSourceBindingStatus::ReceiptMismatch);
    doc = *imported.document; std::swap(doc.entries[0], doc.entries[1]);
    EXPECT_EQ(project(doc, imported.receipt), MldSourceBindingStatus::SourceChanged);
    doc = *imported.document; doc.entries[0].entryId++;
    EXPECT_EQ(project(doc, imported.receipt), MldSourceBindingStatus::SourceChanged);
    doc = *imported.document; doc.entries[1].id = doc.entries[0].id;
    EXPECT_EQ(project(doc, imported.receipt), MldSourceBindingStatus::InvalidIdentity);
    doc = *imported.document; doc.motions.push_back(doc.motions[0]);
    EXPECT_EQ(project(doc, imported.receipt), MldSourceBindingStatus::InvalidIdentity);
}

TEST(MldMotionListProjection, WritingEditedListDoesNotMutateImportEvidence) {
    const auto imported = MldDocumentImporter::importBytes(listBytes());
    ASSERT_TRUE(imported.ok());
    auto edited = *imported.document;
    edited.entries[0].motionSlots.push_back(std::nullopt);
    EXPECT_EQ(MldMotionListProjector::selectImportedEntry(edited, 0, imported.receipt).bindingStatus,
        MldSourceBindingStatus::SourceChanged);
    const auto written = MldDocumentWriter::write(edited, {MldPlatform::GameCube, MldWrapper::Raw}, &imported.receipt);
    ASSERT_TRUE(written.ok());
    EXPECT_TRUE(MldMotionListProjector::selectImportedEntry(*imported.document, 0, imported.receipt).verifiedPresentEmpty());
    const auto reparsed = MldDocumentImporter::importBytes(written.bytes);
    ASSERT_TRUE(reparsed.ok());
    const auto fresh = MldMotionListProjector::selectImportedEntry(*reparsed.document, 0, reparsed.receipt);
    EXPECT_EQ(fresh.listStatus, MldMotionListStatus::PresentNonempty);
    EXPECT_EQ(fresh.declaredSlotCount, 1U); EXPECT_FALSE(fresh.motionFrames.ok());
    const auto compressed = MldDocumentWriter::write(*imported.document,
        {MldPlatform::GameCube, MldWrapper::Aklz}, &imported.receipt);
    ASSERT_TRUE(compressed.ok());
    const auto wrapped = MldDocumentImporter::importBytes(compressed.bytes);
    ASSERT_TRUE(wrapped.ok()); EXPECT_EQ(wrapped.receipt.wrapper, MldWrapper::Aklz);
    EXPECT_TRUE(MldMotionListProjector::selectImportedEntry(*wrapped.document, 0, wrapped.receipt).verifiedPresentEmpty());
}

TEST(MldMotionListProjection, UnownableNonzeroSourceReferenceRejectsImport) {
    auto bytes = listBytes();
    EndianSpanWriter(bytes, Endian::Big).write_u32_at(0x128, 0xfffffff0U);
    const auto imported = MldDocumentImporter::importBytes(bytes);
    EXPECT_FALSE(imported.ok());
    EXPECT_FALSE(imported.document.has_value());
    EXPECT_FALSE(MldMotionListProjector::selectImportedEntry(MldDocument{}, 1, imported.receipt).verifiedPresentEmpty());
}

TEST(MldMotionListProjection, CurrentMotionClassificationDistinguishesMissingConflictingAndZeroFrames) {
    const auto imported = MldDocumentImporter::importBytes(listBytes());
    ASSERT_TRUE(imported.ok()); ASSERT_EQ(imported.document->motions.size(), 1U);
    auto doc = *imported.document;
    const auto project = [&]() { return MldMotionListProjector::selectImportedEntry(doc, 1, imported.receipt); };
    doc.motions.clear();
    auto p = project(); ASSERT_TRUE(p.validPresentList());
    EXPECT_EQ(p.motionFrames.slots[1].status, MldMotionFrameSlotStatus::MissingMotion);
    doc = *imported.document;
    doc.motions[0].payload = MldDecodedMotion{};
    p = project(); EXPECT_EQ(p.motionFrames.slots[1].status, MldMotionFrameSlotStatus::NoDecodedVariants);
    auto& decoded = std::get<MldDecodedMotion>(doc.motions[0].payload);
    decoded.variants.push_back({MldMotionVariantId{1}, motionWithFrames(0)});
    p = project(); ASSERT_TRUE(p.validPresentList());
    EXPECT_EQ(p.motionFrames.slots[1].status, MldMotionFrameSlotStatus::Resolved);
    EXPECT_EQ(p.motionFrames.slots[1].agreedDeclaredFrameCount, 0U);
    EXPECT_FALSE(p.verifiedPresentEmpty()); EXPECT_FALSE(p.motionFrames.ok()); // null slots remain
    decoded.variants.push_back({MldMotionVariantId{2}, motionWithFrames(17)});
    p = project(); EXPECT_EQ(p.motionFrames.slots[1].status, MldMotionFrameSlotStatus::ConflictingDeclaredFrameCounts);
    decoded.variants[1].id = decoded.variants[0].id;
    EXPECT_EQ(project().bindingStatus, MldSourceBindingStatus::InvalidIdentity);
    // The existing action projection still accepts a nonempty zero-frame motion.
    decoded.variants.pop_back(); doc.entries[1].motionSlots = {doc.motions[0].id};
    EXPECT_TRUE(MldMotionFrameProjector::project(doc, doc.entries[1].id).ok());
    EXPECT_EQ(project().bindingStatus, MldSourceBindingStatus::SourceChanged);
}
