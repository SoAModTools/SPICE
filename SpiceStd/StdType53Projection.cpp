#include "StdType53Projection.h"
#include "StdProjectionEvidence.h"
#include "../SpiceRoot/Binary/EndianReader.h"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <set>

namespace spice::stdfile {
namespace {
std::optional<StdPlatform> platformFor(StdModelReferenceProfile profile) {
    switch (profile) {
    case StdModelReferenceProfile::GameCubeUS:
    case StdModelReferenceProfile::GameCubeEU:
    case StdModelReferenceProfile::GameCubeJP: return StdPlatform::GameCube;
    case StdModelReferenceProfile::DreamcastUS:
    case StdModelReferenceProfile::DreamcastEU:
    case StdModelReferenceProfile::DreamcastJP: return StdPlatform::Dreamcast;
    default: return std::nullopt;
    }
}
}

StdModelResourceReference decodeStdModelResourceReference(
    std::uint32_t encodedKey, StdModelReferenceProfile profile) {
    StdModelResourceReference result{ .encodedKey = encodedKey, .profile = profile };
    const auto platform = platformFor(profile);
    if (!platform) return result;
    result.status = StdModelReferenceStatus::UnsupportedKey;
    const auto key = std::bit_cast<std::int32_t>(encodedKey);
    if (key < 0) return result;
    if (key == 9999999) { result.status = StdModelReferenceStatus::Sentinel; return result; }
    char buffer[64]{};
    if (key < 10000000) {
        const auto a = key / 100000;
        auto remainder = key % 100000;
        if (*platform == StdPlatform::GameCube)
            remainder = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(remainder));
        std::snprintf(buffer, sizeof(buffer), "E%02d%03d%02d.MLD", a, remainder / 100, remainder % 100);
        result.fallbackDirectory = a == 99 ? "BCHARA" : "BEFF";
    } else {
        const auto value = key - 10000000;
        auto family = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value / 1000));
        if (family < 0) return result;
        if (family > 2) family = 0;
        constexpr char families[]{'A', 'B', 'G'};
        std::snprintf(buffer, sizeof(buffer), "M%c%03d.MLD", families[family], value % 1000);
        result.fallbackDirectory = "BCHARA";
    }
    result.logicalName = buffer;
    result.status = StdModelReferenceStatus::Resolved;
    return result;
}

StdType53Projection StdType53Projector::project(const StdDocument& document,
    StdEntryRecordId id, const StdImportReceipt& receipt, StdModelReferenceProfile profile) {
    StdType53Projection out;
    out.recordId = id;
    const auto fail = [&](StdType53ProjectionStatus status) { out.status = status; return out; };
    const auto platform = platformFor(profile);
    if (!platform) return fail(StdType53ProjectionStatus::UnsupportedProfile);
    const auto* table = std::get_if<StdEntryTableContent>(&document.content);
    if (!table) return out;
    std::set<std::uint64_t> recordIds, payloadIds, ownedPayloads;
    for (const auto& record : table->records) {
        if (!record.id || !recordIds.insert(record.id.value).second)
            return fail(StdType53ProjectionStatus::InvalidIdentity);
        if (record.payload && (!*record.payload || !ownedPayloads.insert(record.payload->value).second))
            return fail(StdType53ProjectionStatus::InvalidIdentity);
    }
    for (const auto& payload : table->payloads)
        if (!payload.id || !payloadIds.insert(payload.id.value).second)
            return fail(StdType53ProjectionStatus::InvalidIdentity);
    const auto record = std::find_if(table->records.begin(), table->records.end(),
        [&](const auto& value) { return value.id == id; });
    if (record == table->records.end()) return fail(StdType53ProjectionStatus::MissingRecord);
    if (record->combinedType() != kStdType53CombinedType) return fail(StdType53ProjectionStatus::UnsupportedCommand);
    out.payloadId = record->payload;
    if (!record->payload) return fail(StdType53ProjectionStatus::MissingPayload);
    const auto* payload = findEntryPayload(*table, *record->payload);
    if (!payload) return fail(StdType53ProjectionStatus::MissingPayload);
    const auto* opaque = std::get_if<StdOpaquePayload>(&payload->content);
    if (!opaque || opaque->bytes.size() != kStdType53PayloadSize)
        return fail(StdType53ProjectionStatus::MalformedPayload);
    const auto& bound = receipt.projectionEvidence_;
    if (!bound) return fail(StdType53ProjectionStatus::ReceiptRequired);
    if (document.sourceIdentity.token_ != bound->token
        || receipt.sourceSha256 != bound->sourceHash || receipt.sourceSize != bound->sourceSize
        || receipt.decodedSize != bound->decodedSize || receipt.byteOrder != bound->endian
        || receipt.compression != bound->compression || receipt.byteOrderSelection != bound->byteOrderSelection
        || receipt.byteOrder != byteOrderFor(*platform)) return fail(StdType53ProjectionStatus::ReceiptMismatch);
    if (detail::projectionDocumentHash(document) != bound->documentHash)
        return fail(StdType53ProjectionStatus::SourceChanged);
    const auto ordinal = static_cast<std::size_t>(record - table->records.begin());
    if (ordinal >= bound->payloadOffsets.size()) return fail(StdType53ProjectionStatus::ReceiptMismatch);
    out.sourceRecordOffset = 0x10U + ordinal * 0x10U;
    out.sourcePayloadOffset = bound->payloadOffsets[ordinal];
    out.sourceSha256 = bound->sourceHash;
    out.sourceSize = bound->sourceSize;
    const spice::root::EndianReader reader(opaque->bytes, bound->endian);
    out.resource = decodeStdModelResourceReference(reader.read_u32(8U), profile);
    out.flags = reader.read_u32(0x10U);
    out.delay = reader.read_i16(0x14U);
    out.status = out.resource.ok() ? StdType53ProjectionStatus::Resolved : StdType53ProjectionStatus::UnsupportedResourceKey;
    return out;
}
} // namespace spice::stdfile
