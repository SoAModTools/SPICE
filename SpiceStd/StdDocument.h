#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace spice::stdfile {

template <typename Tag>
struct StdId {
    std::uint64_t value{ 0U };
    [[nodiscard]] explicit operator bool() const noexcept { return value != 0U; }
    auto operator<=>(const StdId&) const = default;
};

using StdActionRowId = StdId<struct StdActionRowIdTag>;
using StdEntryRecordId = StdId<struct StdEntryRecordIdTag>;
using StdEntryPayloadId = StdId<struct StdEntryPayloadIdTag>;
using StdEntryTerminatorId = StdId<struct StdEntryTerminatorIdTag>;
using StdOpaqueFragmentId = StdId<struct StdOpaqueFragmentIdTag>;

struct StdActionRow {
    StdActionRowId id{};
    std::int16_t actionId{ 0 };
    std::int16_t rowType{ 0 };
    std::int16_t callbackIndex{ 0 };
    std::int16_t raw06{ 0 };
    std::uint32_t flags{ 0U };
    std::int16_t secondaryKey{ 0 };
    std::int16_t raw0e{ 0 };
    std::uint32_t raw10Bits{ 0U };
    std::uint32_t raw14Bits{ 0U };
    bool operator==(const StdActionRow&) const = default;
};

struct StdActionRowsContent {
    std::uint16_t rawCommandLow{ 0U };
    std::uint16_t rawCommandHigh{ 0U };
    std::uint32_t rawLoaderContextWord{ 0U };
    std::uint32_t rawRowTablePointerWord{ 0U };
    std::vector<StdActionRow> rows{};

    [[nodiscard]] std::uint32_t combinedCommandKind() const noexcept {
        return (static_cast<std::uint32_t>(rawCommandHigh) << 16U) | rawCommandLow;
    }
    bool operator==(const StdActionRowsContent&) const = default;
};

struct StdActionViewPayload {
    std::int16_t primaryActionKey{ 0 };
    std::int16_t routeSecondaryKey{ 0 };
    std::int16_t directSecondaryKey{ 0 };
    std::uint16_t rawLowFlags{ 0U };
    std::uint32_t raw08{ 0U };
    std::uint32_t raw0c{ 0U };
    std::uint32_t actionViewFlags{ 0U };
    std::uint32_t modeLocalValueBits{ 0U };
    std::int16_t startFrame{ 0 };
    std::uint16_t raw1a{ 0U };
    std::int16_t endFrame{ 0 };
    std::int16_t holdFrameCount{ 0 };
    std::int16_t stepFrameCount{ 0 };
    std::int16_t requestedMode{ 0 };
    bool operator==(const StdActionViewPayload&) const = default;
};

struct StdOpaquePayload {
    std::vector<std::uint8_t> bytes{};
    bool operator==(const StdOpaquePayload&) const = default;
};

using StdEntryPayloadContent = std::variant<StdActionViewPayload, StdOpaquePayload>;

struct StdEntryPayload {
    StdEntryPayloadId id{};
    StdEntryPayloadContent content{ StdOpaquePayload{} };
    bool operator==(const StdEntryPayload&) const = default;
};

struct StdEntryRecord {
    StdEntryRecordId id{};
    std::int16_t locationCode{ 0 };
    std::int16_t opcode{ 0 };
    std::uint32_t raw04{ 0U };
    std::optional<StdEntryPayloadId> payload{};

    [[nodiscard]] std::uint32_t combinedType() const noexcept {
        return (static_cast<std::uint32_t>(static_cast<std::uint16_t>(opcode)) << 16U) |
            static_cast<std::uint16_t>(locationCode);
    }
    bool operator==(const StdEntryRecord&) const = default;
};

struct StdEntryTerminator {
    StdEntryTerminatorId id{};
    std::int16_t negativeLocation{ -1 };
    std::int16_t raw02{ 0 };
    std::uint32_t raw04{ 0U };
    std::uint32_t raw08{ 0U };
    std::uint32_t raw0c{ 0U };
    bool operator==(const StdEntryTerminator&) const = default;
};

struct StdOpaqueFragment {
    StdOpaqueFragmentId id{};
    std::vector<std::uint8_t> bytes{};
    bool operator==(const StdOpaqueFragment&) const = default;
};

using StdEntryPayloadLayoutItem = std::variant<StdEntryPayloadId, StdOpaqueFragmentId>;

struct StdEntryTableContent {
    std::uint16_t kind{ 4U };
    std::uint32_t rawHeader04{ 0U };
    std::uint32_t rawHeader08{ 0U };
    std::vector<StdEntryRecord> records{};
    StdEntryTerminator terminator{};
    std::vector<StdEntryPayload> payloads{};
    std::vector<StdOpaqueFragment> opaqueFragments{};
    std::vector<StdEntryPayloadLayoutItem> payloadLayout{};
    bool operator==(const StdEntryTableContent&) const = default;
};

struct StdOpaqueContent {
    std::vector<std::uint8_t> decodedBytes{};
    bool operator==(const StdOpaqueContent&) const = default;
};

using StdDocumentContent = std::variant<StdActionRowsContent, StdEntryTableContent, StdOpaqueContent>;

struct StdDocument {
    StdDocumentContent content{ StdOpaqueContent{} };

    [[nodiscard]] StdActionRowId allocateActionRowId() const noexcept;
    [[nodiscard]] StdEntryRecordId allocateEntryRecordId() const noexcept;
    [[nodiscard]] StdEntryPayloadId allocateEntryPayloadId() const noexcept;
    [[nodiscard]] StdEntryTerminatorId allocateEntryTerminatorId() const noexcept;
    [[nodiscard]] StdOpaqueFragmentId allocateOpaqueFragmentId() const noexcept;
    [[nodiscard]] bool hasOpaqueContent() const noexcept;
    bool operator==(const StdDocument&) const = default;
};

[[nodiscard]] const StdEntryPayload* findEntryPayload(
    const StdEntryTableContent& table, StdEntryPayloadId id) noexcept;
[[nodiscard]] StdEntryPayload* findEntryPayload(
    StdEntryTableContent& table, StdEntryPayloadId id) noexcept;
[[nodiscard]] const StdOpaqueFragment* findOpaqueFragment(
    const StdEntryTableContent& table, StdOpaqueFragmentId id) noexcept;
[[nodiscard]] StdOpaqueFragment* findOpaqueFragment(
    StdEntryTableContent& table, StdOpaqueFragmentId id) noexcept;

inline constexpr std::uint32_t kStdActionViewCombinedType = 0x0003002aU;
inline constexpr std::uint32_t kStdActionViewPayloadSize = 0x24U;

} // namespace spice::stdfile
