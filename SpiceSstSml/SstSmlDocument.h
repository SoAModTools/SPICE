#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace spice::sstsml {

template <typename Tag>
struct SstSmlId {
    std::uint64_t value{ 0U };
    [[nodiscard]] explicit operator bool() const noexcept { return value != 0U; }
    auto operator<=>(const SstSmlId&) const = default;
};

using SstSmlStageMemberId = SstSmlId<struct SstSmlStageMemberIdTag>;
using SmlRecordId = SstSmlId<struct SmlRecordIdTag>;
using SmlEmbeddedResourceId = SstSmlId<struct SmlEmbeddedResourceIdTag>;
using SstRecordId = SstSmlId<struct SstRecordIdTag>;
using SstCommandBlockId = SstSmlId<struct SstCommandBlockIdTag>;
using SstCommandId = SstSmlId<struct SstCommandIdTag>;
using SstCommandFieldId = SstSmlId<struct SstCommandFieldIdTag>;
using SstPlacementId = SstSmlId<struct SstPlacementIdTag>;
using SstLightingRowId = SstSmlId<struct SstLightingRowIdTag>;
using SstBattleGridTerrainId = SstSmlId<struct SstBattleGridTerrainIdTag>;
using SstSmlOpaqueBlockId = SstSmlId<struct SstSmlOpaqueBlockIdTag>;

struct SstSmlOpaqueBlock {
    SstSmlOpaqueBlockId id{};
    std::vector<std::uint8_t> bytes{};
    bool operator==(const SstSmlOpaqueBlock&) const = default;
};

struct SmlEmbeddedResource {
    SmlEmbeddedResourceId id{};
    std::vector<std::uint8_t> bytes{};
    bool operator==(const SmlEmbeddedResource&) const = default;
};

struct SmlStageRecord {
    SmlRecordId id{};
    std::uint32_t resourceIndexWord{ 0U };
    std::uint32_t reservedWord{ 0U };
    SmlEmbeddedResource resource{};
    bool operator==(const SmlStageRecord&) const = default;
};

using SstCommandFieldValue = std::variant<
    std::int8_t,
    std::uint8_t,
    std::int16_t,
    std::uint16_t,
    std::uint32_t,
    float>;

struct SstCommandField {
    SstCommandFieldId id{};
    std::string name{};
    SstCommandFieldValue value{};
    bool operator==(const SstCommandField&) const = default;
};

struct SstPlacement {
    SstPlacementId id{};
    float positionX{ 0.0F };
    float positionY{ 0.0F };
    float positionZ{ 0.0F };
    std::uint32_t rotationAngleX{ 0U };
    std::uint32_t rotationAngleY{ 0U };
    std::uint32_t rotationAngleZ{ 0U };
    float scaleX{ 1.0F };
    float scaleY{ 1.0F };
    float scaleZ{ 1.0F };
    bool operator==(const SstPlacement&) const = default;
};

struct SstLightingRow {
    SstLightingRowId id{};
    std::int8_t state{ 0 };
    std::int16_t classSelector{ 0 };
    std::uint32_t flags{ 0U };
    std::int16_t runtimeSlotId{ 0 };
    std::array<float, 3U> lightVector{};
    std::array<float, 3U> slotRgb{};
    std::array<float, 3U> globalRgb{};
    float attenuationOrSpot0{ 0.0F };
    float attenuationOrSpot1{ 0.0F };
    std::uint32_t rawTailWord{ 0U };
    bool sentinel{ false };
    bool operator==(const SstLightingRow&) const = default;
};

struct SstStageCommand {
    SstCommandId id{};
    std::int16_t type{ 0 };
    std::int16_t argument{ 0 };
    std::uint32_t rawWord4{ 0U };
    std::uint32_t rawWord8{ 0U };
    std::uint32_t onDiskWord12{ 0U };
    std::vector<std::uint8_t> payloadBytes{};
    bool payloadSpanKnown{ false };
    std::vector<SstCommandField> fields{};
    std::optional<SstPlacement> placement{};
    std::vector<SstLightingRow> lightingRows{};
    bool operator==(const SstStageCommand&) const = default;
};

struct SstBattleGridTerrain {
    SstBattleGridTerrainId id{};
    std::array<std::uint8_t, 81U> values{};
    bool operator==(const SstBattleGridTerrain&) const = default;
};

struct SstStageCommandBlock {
    SstCommandBlockId id{};
    std::vector<SstStageCommand> commands{};
    std::int16_t sentinelType{ -1 };
    std::int16_t sentinelArgument{ 0 };
    std::uint32_t sentinelRawWord4{ 0U };
    std::uint32_t sentinelRawWord8{ 0U };
    std::uint32_t sentinelRawWord12{ 0U };
    std::optional<SstBattleGridTerrain> battleGrid{};
    std::optional<SstSmlOpaqueBlock> trailingOpaque{};
    bool operator==(const SstStageCommandBlock&) const = default;
};

struct SstStageRecord {
    SstRecordId id{};
    std::optional<std::uint32_t> previousCommandBlockLength{};
    std::optional<std::uint32_t> reservedWord{};
    std::uint32_t recordIndexWord{ 0U };
    SstStageCommandBlock commandBlock{};
    bool operator==(const SstStageRecord&) const = default;
};

struct SstSmlStageMember {
    SstSmlStageMemberId id{};
    SmlStageRecord sml{};
    SstStageRecord sst{};
    bool operator==(const SstSmlStageMember&) const = default;
};

using SmlBodyItem = std::variant<SmlEmbeddedResourceId, SstSmlOpaqueBlock>;
using SstBodyItem = std::variant<SstCommandBlockId, SstSmlOpaqueBlock>;

struct SstSmlDocument {
    std::uint16_t stageId{ 0U };
    std::uint16_t stageHeaderSentinel{ 0U };
    std::uint16_t recordCountSentinel{ 0U };
    std::vector<SstSmlStageMember> members{};
    std::vector<SmlBodyItem> smlBodyLayout{};
    std::vector<SstBodyItem> sstBodyLayout{};
    bool operator==(const SstSmlDocument&) const = default;
};

} // namespace spice::sstsml
