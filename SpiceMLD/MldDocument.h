#pragma once

#include "Model/MldTextureArchiveModel.h"
#include "Model/Types.h"
#include "../SpiceModeling/ModelDocument.h"
#include "../SpiceModeling/MotionDocument.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace spice::mld {

template <typename Tag>
struct MldId {
    std::uint64_t value{ 0U };
    [[nodiscard]] explicit operator bool() const noexcept { return value != 0U; }
    auto operator<=>(const MldId&) const = default;
};

using MldEntryId = MldId<struct MldEntryIdTag>;
using MldObjectId = MldId<struct MldObjectIdTag>;
using MldMotionId = MldId<struct MldMotionIdTag>;
using MldMotionVariantId = MldId<struct MldMotionVariantIdTag>;
using MldGroundId = MldId<struct MldGroundIdTag>;
using MldTextureListId = MldId<struct MldTextureListIdTag>;
using MldTextureArchiveId = MldId<struct MldTextureArchiveIdTag>;
using MldOpaqueMemberId = MldId<struct MldOpaqueMemberIdTag>;

struct MldOpaquePayload {
    std::vector<std::uint8_t> bytes{};
    bool operator==(const MldOpaquePayload&) const = default;
};

struct MldEntry {
    MldEntryId id{};
    std::uint32_t entryId{ 0U };
    std::int32_t tableId{ 0 };
    std::string functionName{};
    model::Transform transform{};
    std::vector<std::uint32_t> groundLinks{};
    std::vector<std::uint32_t> parameterList2{};
    std::vector<std::uint32_t> functionParameters{};
    std::vector<std::optional<MldObjectId>> objectSlots{};
    std::vector<std::optional<MldGroundId>> groundSlots{};
    std::vector<std::optional<MldMotionId>> motionSlots{};
    std::optional<MldTextureListId> textureList{};
};

using MldObjectPayload = std::variant<std::shared_ptr<const modeling::ModelDocument>, MldOpaquePayload>;

struct MldObjectResource {
    MldObjectId id{};
    MldObjectPayload payload{ MldOpaquePayload{} };
};

struct MldMotionVariant {
    MldMotionVariantId id{};
    std::shared_ptr<const modeling::MotionDocument> document{};
};

struct MldDecodedMotion {
    modeling::MotionKind kind{ modeling::MotionKind::Unknown };
    std::vector<MldMotionVariant> variants{};
};

using MldMotionPayload = std::variant<MldDecodedMotion, MldOpaquePayload>;

struct MldMotionResource {
    MldMotionId id{};
    MldMotionPayload payload{ MldOpaquePayload{} };
};

struct MldGrndCell {
    std::vector<std::size_t> triangleIndices{};
};

struct MldGrndDocument {
    std::vector<std::uint8_t> outerHeaderOpaque{};
    std::vector<std::uint8_t> innerHeaderOpaque{};
    float gridOriginX{ 0.0F };
    float gridOriginZ{ 0.0F };
    std::uint16_t gridX{ 0U };
    std::uint16_t gridZ{ 0U };
    std::uint16_t cellSizeX{ 0U };
    std::uint16_t cellSizeZ{ 0U };
    model::MeshData mesh{};
    std::vector<MldGrndCell> cells{};
};

struct MldGobjNode {
    std::vector<std::uint8_t> nodeOpaque{};
    std::vector<std::uint8_t> attachPrefixOpaque{};
    std::uint32_t vertexHeaderWord0{ 0U };
    std::uint32_t vertexHeaderWord1{ 0U };
    model::Transform transform{};
    std::optional<std::size_t> parentNode{};
    std::vector<std::size_t> childNodes{};
    model::MeshData mesh{};
};

struct MldGobjDocument {
    std::vector<std::uint8_t> outerHeaderOpaque{};
    std::vector<MldGobjNode> nodes{};
    std::vector<std::size_t> rootNodes{};
};

enum class MldGroundKind { Grnd, Gobj, Unknown };
using MldGroundPayload = std::variant<MldGrndDocument, MldGobjDocument, MldOpaquePayload>;

struct MldGroundResource {
    MldGroundId id{};
    MldGroundKind kind{ MldGroundKind::Unknown };
    MldGroundPayload payload{ MldOpaquePayload{} };
};

struct MldTextureList {
    MldTextureListId id{};
    std::vector<std::string> names{};
};

struct MldTexture {
    model::MldTextureEncoding encoding{ model::MldTextureEncoding::Unknown };
    std::string name{};
    std::vector<std::uint8_t> encodedBytes{};
    bool hasGlobalIndex{ false };
    std::uint32_t globalIndex{ 0U };
    std::uint8_t pixelFormat{ 0U };
    std::uint8_t dataFormat{ 0U };
    std::string format{};
    std::string paletteFormat{};
    bool hasMipmaps{ false };
    bool hasInternalPalette{ false };
    std::uint16_t width{ 0U };
    std::uint16_t height{ 0U };
    bool decoded{ false };
    std::vector<std::uint8_t> rgba8{};
};

struct MldTextureArchive {
    MldTextureArchiveId id{};
    std::vector<MldTexture> textures{};
};

struct MldOpaqueMember {
    MldOpaqueMemberId id{};
    std::string role{};
    MldOpaquePayload payload{};
};

using MldLayoutItem = std::variant<MldEntryId, MldObjectId, MldMotionId, MldGroundId,
    MldTextureListId, MldTextureArchiveId, MldOpaqueMemberId>;

struct MldDocument {
    std::vector<MldEntry> entries{};
    std::vector<MldObjectResource> objects{};
    std::vector<MldMotionResource> motions{};
    std::vector<MldGroundResource> grounds{};
    std::vector<MldTextureList> textureLists{};
    std::vector<MldTextureArchive> textureArchives{};
    std::vector<MldOpaqueMember> opaqueMembers{};
    std::vector<MldLayoutItem> layout{};

    [[nodiscard]] MldEntryId allocateEntryId() const noexcept;
    [[nodiscard]] MldObjectId allocateObjectId() const noexcept;
    [[nodiscard]] MldMotionId allocateMotionId() const noexcept;
    [[nodiscard]] MldMotionVariantId allocateMotionVariantId() const noexcept;
    [[nodiscard]] MldGroundId allocateGroundId() const noexcept;
    [[nodiscard]] MldTextureListId allocateTextureListId() const noexcept;
    [[nodiscard]] MldTextureArchiveId allocateTextureArchiveId() const noexcept;
    [[nodiscard]] MldOpaqueMemberId allocateOpaqueMemberId() const noexcept;
    [[nodiscard]] bool hasOpaqueContent() const noexcept;
};

} // namespace spice::mld
