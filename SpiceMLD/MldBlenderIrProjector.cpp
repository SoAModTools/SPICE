#include "MldBlenderIrProjector.h"

#include "Internal/MldGroundDocumentConversion.h"
#include "Parsing/Sa3dBlenderIrBuilder.h"

#include <exception>
#include <memory>

namespace spice::mld {
namespace {

template <typename Id>
[[nodiscard]] std::uint32_t address(const Id id, const std::uint32_t base) {
    return base + static_cast<std::uint32_t>(id.value * 0x100U);
}

template <typename Id>
[[nodiscard]] std::shared_ptr<model::U32List> makeSlots(
    const std::vector<std::optional<Id>>& slots,
    const std::uint32_t base) {
    auto result = std::make_shared<model::U32List>();
    result->valid = true;
    for (const auto& slot : slots) result->values.push_back(slot ? address(*slot, base) : 0U);
    return result;
}

[[nodiscard]] std::shared_ptr<model::U32List> makeValues(const std::vector<std::uint32_t>& values) {
    auto result = std::make_shared<model::U32List>();
    result->valid = true;
    result->values = values;
    return result;
}

[[nodiscard]] model::MldFile adapt(const MldDocument& document) {
    model::MldFile file{};
    file.parseStatus = model::MldParseStatus::Complete;
    file.assetStatus = model::MldResourceStatus::Complete;
    file.header.entryCount = static_cast<std::uint32_t>(document.entries.size());

    for (const auto& object : document.objects) {
        model::MldObjectResource resource{};
        resource.status = model::MldResourceStatus::Complete;
        resource.sourceAddress = address(object.id, 0x100000U);
        resource.blockOffset = resource.sourceAddress;
        if (const auto* decoded = std::get_if<std::shared_ptr<const modeling::ModelDocument>>(&object.payload)) {
            if (*decoded) {
                const auto encoded = modeling::ModelDocumentCodec::encode(**decoded);
                resource.rawBytes = encoded.bytes;
                auto modelFile = std::make_shared<modeling::File::ModelFile>();
                modelFile->format = (*decoded)->format();
                modelFile->model = std::const_pointer_cast<modeling::ObjectData::Node>((*decoded)->root());
                resource.model = std::move(modelFile);
            }
        } else {
            resource.rawBytes = std::get<MldOpaquePayload>(object.payload).bytes;
        }
        resource.blockSize = resource.rawBytes.size();
        file.objectResources.emplace(resource.sourceAddress, std::move(resource));
    }

    for (const auto& motion : document.motions) {
        model::MldMotionResource resource{};
        resource.status = model::MldResourceStatus::Complete;
        resource.sourceAddress = address(motion.id, 0x200000U);
        resource.blockOffset = resource.sourceAddress;
        if (const auto* decoded = std::get_if<MldDecodedMotion>(&motion.payload)) {
            if (decoded->kind == modeling::MotionKind::Node) resource.structure.header.kind = modeling::File::NinjaMotionKind::Node;
            else if (decoded->kind == modeling::MotionKind::Shape) resource.structure.header.kind = modeling::File::NinjaMotionKind::Shape;
            else if (decoded->kind == modeling::MotionKind::Camera) resource.structure.header.kind = modeling::File::NinjaMotionKind::Camera;
            for (const auto& value : decoded->variants) {
                if (!value.document) continue;
                const auto encoded = modeling::MotionDocumentCodec::encode(*value.document);
                if (resource.rawBytes.empty()) resource.rawBytes = encoded.bytes;
                resource.variants.push_back({
                    .nodeCount = value.document->targetLayout().lane_count(),
                    .shortRot = value.document->eulerWidth() == modeling::Animation::EulerRecordWidth::Short16,
                    .targetLayout = value.document->targetLayout(),
                    .motion = std::make_shared<const modeling::Animation::Motion>(value.document->motion()),
                });
            }
        } else {
            resource.rawBytes = std::get<MldOpaquePayload>(motion.payload).bytes;
        }
        resource.blockSize = resource.rawBytes.size();
        file.motionResources.emplace(resource.sourceAddress, std::move(resource));
    }

    for (const auto& ground : document.grounds) {
        model::MldGroundResource resource{};
        resource.sourceAddress = address(ground.id, 0x300000U);
        resource.status = model::MldResourceStatus::Complete;
        if (const auto* grnd = std::get_if<MldGrndDocument>(&ground.payload)) {
            resource.kind = model::MldGroundResource::Kind::Grnd;
            resource.grnd = detail::fromDocument(*grnd);
        } else if (const auto* gobj = std::get_if<MldGobjDocument>(&ground.payload)) {
            resource.kind = model::MldGroundResource::Kind::Gobj;
            resource.gobj = detail::fromDocument(*gobj);
        } else {
            resource.rawBytes = std::get<MldOpaquePayload>(ground.payload).bytes;
        }
        file.groundResources.emplace(resource.sourceAddress, std::move(resource));
    }

    for (std::size_t index = 0U; index < document.entries.size(); ++index) {
        const auto& source = document.entries[index];
        model::MldIndexEntryRecord record{};
        record.entry.tableIndex = index;
        record.entry.entryId = source.entryId;
        record.entry.tblId = source.tableId;
        record.entry.fxnName = source.functionName;
        record.entry.transform = source.transform;
        record.entry.groundLinks = makeValues(source.groundLinks);
        record.entry.paramList2 = makeValues(source.parameterList2);
        record.entry.functionParameters = makeValues(source.functionParameters);
        record.entry.objectAddresses = makeSlots(source.objectSlots, 0x100000U);
        record.entry.motionAddresses = makeSlots(source.motionSlots, 0x200000U);
        record.entry.groundAddresses = makeSlots(source.groundSlots, 0x300000U);
        record.entry.objectCount = source.objectSlots.size();
        record.entry.motionCount = source.motionSlots.size();
        record.entry.groundCount = source.groundSlots.size();
        file.entries.push_back(std::move(record));
    }
    if (!document.textureArchives.empty()) {
        model::MldTextureArchive archive{ .status = model::MldResourceStatus::Complete };
        for (const auto& texture : document.textureArchives.front().textures) archive.entries.push_back({
            .status = model::MldResourceStatus::Complete,
            .encoding = texture.encoding,
            .hasGlobalIndex = texture.hasGlobalIndex,
            .globalIndex = texture.globalIndex,
            .textureName = texture.name,
            .pixelFormat = texture.pixelFormat,
            .dataFormat = texture.dataFormat,
            .sourceFormat = texture.format,
            .sourcePaletteFormat = texture.paletteFormat,
            .hasMipmaps = texture.hasMipmaps,
            .hasInternalPalette = texture.hasInternalPalette,
            .width = texture.width,
            .height = texture.height,
            .encodedData = texture.encodedBytes,
            .decoded = texture.decoded,
            .rgba8 = texture.rgba8,
        });
        file.textureArchive = std::move(archive);
    }
    return file;
}

} // namespace

MldBlenderIrProjectionResult MldBlenderIrProjector::project(const MldDocument& document) {
    MldBlenderIrProjectionResult result{};
    try {
        result.scene = parsing::Sa3dBlenderIrBuilder{}.build(adapt(document));
        result.diagnostics = result.scene->diagnostics;
    } catch (const std::exception& error) {
        result.diagnostics.push_back(error.what());
    }
    return result;
}

} // namespace spice::mld
