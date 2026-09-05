#include "MldDocumentImporter.h"

#include "Internal/MldDocumentReceiptState.h"
#include "Internal/MldGroundDocumentConversion.h"
#include "Internal/MldSha256.h"
#include "Parsing/MldParser.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <unordered_map>

namespace spice::mld {
namespace {

template <typename Id>
[[nodiscard]] std::optional<Id> resolveSlot(
    const std::uint32_t address,
    const std::unordered_map<std::uint32_t, Id>& ids) {
    if (address == 0U) return std::nullopt;
    const auto found = ids.find(address);
    return found == ids.end() ? std::nullopt : std::optional<Id>{ found->second };
}

template <typename Id>
[[nodiscard]] std::vector<std::optional<Id>> resolveSlots(
    const std::shared_ptr<model::U32List>& list,
    const std::unordered_map<std::uint32_t, Id>& ids) {
    std::vector<std::optional<Id>> result{};
    if (!list) return result;
    result.reserve(list->values.size());
    for (const auto value : list->values) result.push_back(resolveSlot(value, ids));
    return result;
}

[[nodiscard]] std::vector<std::uint32_t> values(const std::shared_ptr<model::U32List>& list) {
    return list ? list->values : std::vector<std::uint32_t>{};
}

[[nodiscard]] MldDocumentDiagnostic convertDiagnostic(const model::MldDiagnostic& diagnostic) {
    MldDiagnosticSeverity severity = MldDiagnosticSeverity::Info;
    if (diagnostic.severity == model::MldDiagnostic::Severity::Warning) severity = MldDiagnosticSeverity::Warning;
    if (diagnostic.severity == model::MldDiagnostic::Severity::Error) {
        severity = diagnostic.scope == model::MldDiagnosticScope::Resource
            ? MldDiagnosticSeverity::Warning : MldDiagnosticSeverity::Error;
    }
    return { severity, diagnostic.message, diagnostic.sourceOffset };
}

void preserveFragment(detail::MldImportState& state,
    const std::span<const std::uint8_t> decoded,
    const std::size_t offset,
    const std::size_t size) {
    if (size == 0U || offset > decoded.size() || size > decoded.size() - offset) return;
    detail::MldPreservedFragment fragment{ .decodedOffset = offset };
    fragment.bytes.assign(decoded.begin() + static_cast<std::ptrdiff_t>(offset),
        decoded.begin() + static_cast<std::ptrdiff_t>(offset + size));
    state.preservedFragments.push_back(std::move(fragment));
}

[[nodiscard]] detail::MldImportState makeImportState(model::MldFile source) {
    detail::MldImportState state{};

    // Preserve only byte ranges whose exact layout is not reconstructed from the
    // semantic document: entry padding, ground codec layout, texture-list layout,
    // and texture-archive layout. Unclassified bytes are owned by MldDocument and
    // are therefore not duplicated here.
    for (const auto& record : source.entries) {
        const auto offset = static_cast<std::size_t>(source.header.indexTableOffset)
            + static_cast<std::size_t>(record.entry.tableIndex) * 0x68U;
        preserveFragment(state, source.decodedBytes, offset, record.rawBytes.size());
    }
    for (const auto& [address, resource] : source.groundResources)
        preserveFragment(state, source.decodedBytes, address, resource.blockSize);
    for (const auto& [address, resource] : source.textureListResources) {
        if (resource.wrapperRange.has_value())
            preserveFragment(state, source.decodedBytes,
                resource.wrapperRange->offset, resource.wrapperRange->size);
        preserveFragment(state, source.decodedBytes,
            resource.listRange.offset, resource.listRange.size);
        for (const auto& entry : resource.entries) if (entry.nameRange.has_value())
            preserveFragment(state, source.decodedBytes, entry.nameRange->offset, entry.nameRange->size);
    }
    if (source.textureArchive.has_value()) {
        const auto& archive = *source.textureArchive;
        preserveFragment(state, source.decodedBytes, archive.archiveStartOffset,
            archive.archiveEndOffset - archive.archiveStartOffset);
    }

    source.sourceBytes.clear();
    source.decodedBytes.clear();
    source.originalBytes.clear();
    source.rawDataBlocks.clear();
    source.sourceRanges.clear();
    source.paddingAndUnknownRanges.clear();
    source.parseDiagnostics.clear();
    source.motionRelations.clear();
    source.animationBindings.clear();
    for (auto& record : source.entries) record.rawBytes.clear();
    for (auto& [address, resource] : source.objectResources) {
        resource.rawBytes.clear();
        resource.model.reset();
        resource.originalModel.reset();
        resource.diagnostics.clear();
    }
    for (auto& [address, resource] : source.motionResources) {
        resource.rawBytes.clear();
        resource.structure = {};
        resource.variants.clear();
        resource.diagnostics.clear();
    }
    for (auto& [address, resource] : source.groundResources) {
        resource.rawBytes.clear();
        resource.grnd.reset();
        resource.gobj.reset();
        resource.diagnostics.clear();
    }
    for (auto& [address, resource] : source.textureListResources) {
        resource.sourceBytes.clear();
        resource.wrapperBytes.clear();
        resource.listBytes.clear();
        resource.diagnostics.clear();
        for (auto& entry : resource.entries) {
            entry.rawRecordBytes.clear();
            entry.rawNameBytes.clear();
        }
    }
    if (source.textureArchive.has_value()) {
        source.textureArchive->archivePrefixBytes.clear();
        source.textureArchive->diagnostics.clear();
        for (auto& entry : source.textureArchive->entries) {
            entry.alignmentPrefixBytes.clear();
            entry.trailingBlockBytes.clear();
            entry.encodedData.clear();
            entry.rgba8.clear();
            entry.diagnostics.clear();
        }
    }
    state.encodingSkeleton = std::move(source);
    return state;
}

[[nodiscard]] MldDocument makeDocument(const model::MldFile& source, MldImportReceipt& receipt) {
    MldDocument document{};
    std::unordered_map<std::uint32_t, MldObjectId> objectIds{};
    std::unordered_map<std::uint32_t, MldMotionId> motionIds{};
    std::unordered_map<std::uint32_t, MldGroundId> groundIds{};
    std::unordered_map<std::uint32_t, MldTextureListId> textureListIds{};
    std::vector<std::pair<std::uint64_t, MldLayoutItem>> ordered{};
    std::uint64_t nextMotionVariantId = 1U;
    const auto opaqueBytesAt = [&](const std::uint32_t address) {
        const auto found = std::find_if(source.rawDataBlocks.begin(), source.rawDataBlocks.end(),
            [&](const auto& block) { return block.offset == address; });
        return found == source.rawDataBlocks.end() ? std::vector<std::uint8_t>{} : found->bytes;
    };

    for (const auto& [address, resource] : source.objectResources) {
        const auto id = MldObjectId{ document.objects.size() + 1U };
        objectIds.emplace(address, id);
        MldObjectResource output{ .id = id };
        const auto decoded = modeling::ModelDocumentCodec::decode(resource.rawBytes,
            resource.modelReadOffset.has_value() ? static_cast<std::uint32_t>(*resource.modelReadOffset) : 0U);
        if (decoded.ok()) output.payload = decoded.document;
        else output.payload = MldOpaquePayload{ resource.rawBytes };
        document.objects.push_back(std::move(output));
        ordered.emplace_back(resource.blockOffset, id);
        receipt.layout.push_back({ id, resource.blockOffset, resource.blockSize, address });
    }

    for (const auto& [address, resource] : source.motionResources) {
        const auto id = MldMotionId{ document.motions.size() + 1U };
        motionIds.emplace(address, id);
        MldMotionResource output{ .id = id };
        modeling::MotionKind kind = modeling::MotionKind::Unknown;
        if (resource.structure.header.kind == modeling::File::NinjaMotionKind::Node) kind = modeling::MotionKind::Node;
        else if (resource.structure.header.kind == modeling::File::NinjaMotionKind::Shape) kind = modeling::MotionKind::Shape;
        else if (resource.structure.header.kind == modeling::File::NinjaMotionKind::Camera) kind = modeling::MotionKind::Camera;
        MldDecodedMotion decoded{ .kind = kind };
        for (const auto& variant : resource.variants) {
            modeling::MotionDecodeContext context{
                .targetLayout = variant.targetLayout,
                .eulerWidth = variant.shortRot ? modeling::Animation::EulerRecordWidth::Short16
                                               : modeling::Animation::EulerRecordWidth::Full32,
            };
            const auto result = modeling::MotionDocumentCodec::decode(resource.rawBytes, context);
            if (result.ok()) decoded.variants.push_back({ MldMotionVariantId{ nextMotionVariantId++ }, result.document });
        }
        if (!decoded.variants.empty()) output.payload = std::move(decoded);
        else output.payload = MldOpaquePayload{ resource.rawBytes };
        document.motions.push_back(std::move(output));
        ordered.emplace_back(resource.blockOffset, id);
        receipt.layout.push_back({ id, resource.blockOffset, resource.blockSize, address });
    }

    for (const auto& [address, resource] : source.groundResources) {
        const auto id = MldGroundId{ document.grounds.size() + 1U };
        groundIds.emplace(address, id);
        MldGroundResource output{ .id = id };
        if (resource.grnd.has_value()) {
            output.kind = MldGroundKind::Grnd;
            output.payload = detail::toDocument(*resource.grnd);
        } else if (resource.gobj.has_value()) {
            output.kind = MldGroundKind::Gobj;
            output.payload = detail::toDocument(*resource.gobj);
        } else {
            output.payload = MldOpaquePayload{ resource.rawBytes };
        }
        document.grounds.push_back(std::move(output));
        ordered.emplace_back(address, id);
        receipt.layout.push_back({ id, address, resource.blockSize, address });
    }

    for (const auto& [address, resource] : source.textureListResources) {
        const auto id = MldTextureListId{ document.textureLists.size() + 1U };
        textureListIds.emplace(address, id);
        MldTextureList output{ .id = id };
        for (const auto& entry : resource.entries) output.names.push_back(entry.name);
        document.textureLists.push_back(std::move(output));
        ordered.emplace_back(resource.resolvedListOffset, id);
        receipt.layout.push_back({ id, resource.resolvedListOffset, resource.listRange.size, address });
    }

    for (const auto& record : source.entries) {
        if (record.entry.objectAddresses) for (const auto address : record.entry.objectAddresses->values) {
            if (address == 0U || objectIds.contains(address)) continue;
            const auto id = MldObjectId{ document.objects.size() + 1U };
            objectIds.emplace(address, id);
            const auto bytes = opaqueBytesAt(address);
            document.objects.push_back({ id, MldOpaquePayload{ bytes } });
            ordered.emplace_back(address, id);
            receipt.layout.push_back({ id, address, bytes.size(), address });
        }
        if (record.entry.motionAddresses) for (const auto address : record.entry.motionAddresses->values) {
            if (address == 0U || motionIds.contains(address)) continue;
            const auto id = MldMotionId{ document.motions.size() + 1U };
            motionIds.emplace(address, id);
            const auto bytes = opaqueBytesAt(address);
            document.motions.push_back({ id, MldOpaquePayload{ bytes } });
            ordered.emplace_back(address, id);
            receipt.layout.push_back({ id, address, bytes.size(), address });
        }
        if (record.entry.groundAddresses) for (const auto address : record.entry.groundAddresses->values) {
            if (address == 0U || groundIds.contains(address)) continue;
            const auto id = MldGroundId{ document.grounds.size() + 1U };
            groundIds.emplace(address, id);
            const auto bytes = opaqueBytesAt(address);
            document.grounds.push_back({ id, MldGroundKind::Unknown, MldOpaquePayload{ bytes } });
            ordered.emplace_back(address, id);
            receipt.layout.push_back({ id, address, bytes.size(), address });
        }
    }

    if (source.textureArchive.has_value()) {
        const auto id = MldTextureArchiveId{ 1U };
        MldTextureArchive archive{ .id = id };
        for (const auto& texture : source.textureArchive->entries) archive.textures.push_back({
            .encoding = texture.encoding,
            .name = texture.textureName,
            .encodedBytes = texture.encodedData,
            .hasGlobalIndex = texture.hasGlobalIndex,
            .globalIndex = texture.globalIndex,
            .pixelFormat = texture.pixelFormat,
            .dataFormat = texture.dataFormat,
            .format = texture.sourceFormat,
            .paletteFormat = texture.sourcePaletteFormat,
            .hasMipmaps = texture.hasMipmaps,
            .hasInternalPalette = texture.hasInternalPalette,
            .width = texture.width,
            .height = texture.height,
            .decoded = texture.decoded,
            .rgba8 = texture.rgba8,
        });
        document.textureArchives.push_back(std::move(archive));
        ordered.emplace_back(source.textureArchive->archiveStartOffset, id);
        receipt.layout.push_back({ id, source.textureArchive->archiveStartOffset,
            source.textureArchive->archiveEndOffset - source.textureArchive->archiveStartOffset,
            source.textureArchive->tableOffset });
    }

    for (const auto& record : source.entries) {
        const auto id = MldEntryId{ document.entries.size() + 1U };
        const auto& entry = record.entry;
        MldEntry output{
            .id = id,
            .entryId = entry.entryId,
            .tableId = entry.tblId,
            .functionName = entry.fxnName,
            .transform = entry.transform,
            .groundLinks = values(entry.groundLinks),
            .parameterList2 = values(entry.paramList2),
            .functionParameters = values(entry.functionParameters),
            .objectSlots = resolveSlots(entry.objectAddresses, objectIds),
            .groundSlots = resolveSlots(entry.groundAddresses, groundIds),
            .motionSlots = resolveSlots(entry.motionAddresses, motionIds),
            .textureList = resolveSlot(entry.texturesPointer, textureListIds),
        };
        document.entries.push_back(std::move(output));
        const auto offset = source.header.indexTableOffset + entry.tableIndex * 0x68U;
        ordered.emplace_back(offset, id);
        receipt.layout.push_back({ id, offset, 0x68U, offset });
    }

    for (const auto& unknown : source.paddingAndUnknownRanges) {
        if (unknown.bytes.empty()) continue;
        const auto id = MldOpaqueMemberId{ document.opaqueMembers.size() + 1U };
        document.opaqueMembers.push_back({ id, unknown.label, { unknown.bytes } });
        ordered.emplace_back(unknown.offset, id);
        receipt.layout.push_back({ id, unknown.offset, unknown.size, unknown.offset });
    }

    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    for (auto& [offset, item] : ordered) document.layout.push_back(std::move(item));
    return document;
}

[[nodiscard]] bool hasIncompleteOwnedPayload(const MldDocument& document) {
    for (const auto& resource : document.objects)
        if (const auto* opaque = std::get_if<MldOpaquePayload>(&resource.payload); opaque && opaque->bytes.empty())
            return true;
    for (const auto& resource : document.motions)
        if (const auto* opaque = std::get_if<MldOpaquePayload>(&resource.payload); opaque && opaque->bytes.empty())
            return true;
    for (const auto& resource : document.grounds)
        if (const auto* opaque = std::get_if<MldOpaquePayload>(&resource.payload); opaque && opaque->bytes.empty())
            return true;
    for (const auto& archive : document.textureArchives)
        for (const auto& texture : archive.textures) if (texture.encodedBytes.empty()) return true;
    return false;
}

} // namespace

bool MldDocumentImportResult::ok() const noexcept {
    return document.has_value() && std::none_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.severity == MldDiagnosticSeverity::Error;
    });
}

MldDocumentImportResult MldDocumentImporter::importBytes(
    const std::span<const std::uint8_t> bytes,
    const MldImportOptions& options) {
    MldDocumentImportResult result{};
    if (bytes.empty()) {
        result.diagnostics.push_back({ MldDiagnosticSeverity::Error, "MLD input is empty." });
        return result;
    }
    auto parsed = parsing::MldParser{}.parseBytes(bytes);
    for (const auto& diagnostic : parsed.parseDiagnostics) result.diagnostics.push_back(convertDiagnostic(diagnostic));
    if (parsed.parseStatus == model::MldParseStatus::Failed) return result;

    const auto detected = parsed.sourcePlatform == model::TargetPlatform::Dreamcast
        ? MldPlatform::Dreamcast : MldPlatform::GameCube;
    if (options.platformHint.has_value() && *options.platformHint != detected) {
        result.diagnostics.push_back({ MldDiagnosticSeverity::Error,
            "The caller platform hint conflicts with the automatically detected MLD encoding." });
        return result;
    }
    result.receipt.platform = detected;
    result.receipt.wrapper = parsed.sourceWasCompressedAklz ? MldWrapper::Aklz : MldWrapper::Raw;
    result.receipt.endian = parsed.endian;
    result.receipt.sourceSha256 = detail::sha256(bytes);
    result.receipt.sourceSize = bytes.size();
    result.receipt.decodedSize = parsed.decodedBytes.size();
    result.document = makeDocument(parsed, result.receipt);
    if (hasIncompleteOwnedPayload(*result.document)) {
        result.diagnostics.push_back({ MldDiagnosticSeverity::Error,
            "The MLD contains a referenced resource whose bounded bytes could not be owned." });
        result.document.reset();
        return result;
    }
    result.receipt.state_ = std::make_shared<detail::MldImportState>(makeImportState(std::move(parsed)));
    return result;
}

MldDocumentImportResult MldDocumentImporter::importFile(
    const std::filesystem::path& path,
    const MldImportOptions& options) {
    MldDocumentImportResult result{};
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        result.diagnostics.push_back({ MldDiagnosticSeverity::Error, "Unable to open MLD input file." });
        return result;
    }
    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    result = importBytes(bytes, options);
    result.receipt.path = path;
    return result;
}

} // namespace spice::mld
