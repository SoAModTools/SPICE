#pragma once

#include "StdDocumentValidator.h"

namespace spice::stdfile {

// Explicit interpretation scope, not release detection from byte order.
enum class StdModelReferenceProfile { Unknown, GameCubeUS, GameCubeEU, GameCubeJP, DreamcastUS, DreamcastEU, DreamcastJP };
enum class StdModelReferenceStatus { Resolved, UnsupportedProfile, Sentinel, UnsupportedKey };
enum class StdModelReferenceNamespace { RegisteredModelName };

struct StdModelResourceReference {
    std::uint32_t encodedKey{ 0U };
    StdModelReferenceProfile profile{ StdModelReferenceProfile::Unknown };
    StdModelReferenceStatus status{ StdModelReferenceStatus::UnsupportedProfile };
    StdModelReferenceNamespace nameSpace{ StdModelReferenceNamespace::RegisteredModelName };
    std::string logicalName{};
    // Cold standalone fallback only; this does not identify a cached/MLK source.
    std::string fallbackDirectory{};
    [[nodiscard]] bool ok() const noexcept { return status == StdModelReferenceStatus::Resolved; }
};

[[nodiscard]] StdModelResourceReference decodeStdModelResourceReference(
    std::uint32_t encodedKey, StdModelReferenceProfile profile);

enum class StdType53ProjectionStatus {
    Resolved, UnsupportedProfile, UnsupportedDocument, MissingRecord, InvalidIdentity,
    UnsupportedCommand, MissingPayload, MalformedPayload, ReceiptRequired,
    ReceiptMismatch, SourceChanged, UnsupportedResourceKey,
};

struct StdType53Projection {
    StdType53ProjectionStatus status{ StdType53ProjectionStatus::UnsupportedDocument };
    StdEntryRecordId recordId{};
    std::optional<StdEntryPayloadId> payloadId{};
    std::optional<std::uint64_t> sourceRecordOffset{};
    std::optional<std::uint64_t> sourcePayloadOffset{};
    std::array<std::uint8_t, 32U> sourceSha256{};
    std::uint64_t sourceSize{ 0U };
    StdModelResourceReference resource{};
    std::uint32_t flags{ 0U };
    std::int16_t delay{ 0 };
    // Qualified producer argument, not a serialized field or a key digit.
    std::int16_t meshOrdinal{ 0 };
    [[nodiscard]] bool ok() const noexcept { return status == StdType53ProjectionStatus::Resolved; }
};

class StdType53Projector {
public:
    // Source-only projection: edits require write/reimport. Unknown bytes stay
    // in StdOpaquePayload and keep its existing same-byte-order write contract.
    [[nodiscard]] static StdType53Projection project(const StdDocument& document,
        StdEntryRecordId record, const StdImportReceipt& receipt, StdModelReferenceProfile profile);
};

inline constexpr std::uint32_t kStdType53CombinedType = 0x00030053U;
inline constexpr std::uint32_t kStdType53PayloadSize = 0x90U;

} // namespace spice::stdfile
