#include "MldDocumentWriter.h"

#include "Export/MldFileWriter.h"
#include "Internal/MldDocumentReceiptState.h"
#include "Internal/MldGroundDocumentConversion.h"
#include "../SpiceRoot/Binary/EndianWriter.h"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>

namespace spice::mld {
namespace {

template <typename Id>
[[nodiscard]] std::unordered_map<std::uint64_t, std::uint32_t> addresses(
    const std::vector<MldReceiptLayoutItem>& layout) {
    std::unordered_map<std::uint64_t, std::uint32_t> result{};
    for (const auto& item : layout) if (const auto* id = std::get_if<Id>(&item.item))
        result.emplace(id->value, static_cast<std::uint32_t>(item.encodedReference));
    return result;
}

template <typename Id>
[[nodiscard]] std::vector<std::uint32_t> encodeSlots(
    const std::vector<std::optional<Id>>& slots,
    const std::unordered_map<std::uint64_t, std::uint32_t>& addressById) {
    std::vector<std::uint32_t> result{};
    result.reserve(slots.size());
    for (const auto& slot : slots) {
        if (!slot.has_value()) result.push_back(0U);
        else if (const auto found = addressById.find(slot->value); found != addressById.end()) result.push_back(found->second);
        else result.push_back(0U);
    }
    return result;
}

void assignList(const std::shared_ptr<model::U32List>& list, std::vector<std::uint32_t> values) {
    if (list) list->values = std::move(values);
}

void copyAt(std::vector<std::uint8_t>& destination, const std::size_t offset,
    const std::span<const std::uint8_t> source) {
    if (offset > destination.size() || source.size() > destination.size() - offset) return;
    std::copy(source.begin(), source.end(), destination.begin() + static_cast<std::ptrdiff_t>(offset));
}

void writeU32(std::vector<std::uint8_t>& bytes, const std::size_t offset,
    const std::uint32_t value, const spice::root::Endian endian) {
    if (offset > bytes.size() || 4U > bytes.size() - offset) return;
    spice::root::EndianSpanWriter(bytes, endian).write_u32_at(offset, value);
}

[[nodiscard]] std::uint32_t appendAligned(std::vector<std::uint8_t>& bytes,
    const std::span<const std::uint8_t> source, const std::size_t alignment = 4U) {
    const auto aligned = (bytes.size() + alignment - 1U) & ~(alignment - 1U);
    if (aligned > std::numeric_limits<std::uint32_t>::max()
        || source.size() > std::numeric_limits<std::uint32_t>::max() - aligned)
        return std::numeric_limits<std::uint32_t>::max();
    bytes.resize(aligned, 0U);
    const auto offset = static_cast<std::uint32_t>(aligned);
    bytes.insert(bytes.end(), source.begin(), source.end());
    return offset;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> buildTextureList(
    const MldTextureList& list, const spice::root::Endian endian) {
    constexpr std::size_t headerSize = 8U;
    constexpr std::size_t payloadHeaderSize = 8U;
    constexpr std::size_t recordSize = 12U;
    if (list.names.size() > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
    std::size_t payloadSize = payloadHeaderSize + list.names.size() * recordSize;
    for (const auto& name : list.names) {
        if (name.find('\0') != std::string::npos) return std::nullopt;
        if (name.size() > std::numeric_limits<std::uint32_t>::max() - payloadSize - 1U)
            return std::nullopt;
        payloadSize += name.size() + 1U;
    }
    if (payloadSize > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
    std::vector<std::uint8_t> bytes(headerSize + payloadSize, 0U);
    const char* tag = endian == spice::root::Endian::Little ? "NJTL" : "GJTL";
    std::copy_n(tag, 4U, bytes.begin());
    writeU32(bytes, 4U, static_cast<std::uint32_t>(payloadSize), endian);
    writeU32(bytes, headerSize + 4U, static_cast<std::uint32_t>(list.names.size()), endian);
    std::size_t nameOffset = payloadHeaderSize + list.names.size() * recordSize;
    for (std::size_t index = 0U; index < list.names.size(); ++index) {
        writeU32(bytes, headerSize + payloadHeaderSize + index * recordSize,
            static_cast<std::uint32_t>(nameOffset), endian);
        std::copy(list.names[index].begin(), list.names[index].end(),
            bytes.begin() + static_cast<std::ptrdiff_t>(headerSize + nameOffset));
        nameOffset += list.names[index].size() + 1U;
    }
    return bytes;
}

template <typename Value, typename Id>
[[nodiscard]] const Value* findById(const std::vector<Value>& values, const Id id) {
    const auto found = std::find_if(values.begin(), values.end(), [&](const auto& value) { return value.id == id; });
    return found == values.end() ? nullptr : &*found;
}

[[nodiscard]] std::optional<model::MldFile> buildConstructiveFile(
    const MldDocument& document,
    const MldWriteTarget& target,
    std::string& reason) {
    constexpr std::size_t headerSize = 0x14U;
    constexpr std::size_t entrySize = 0x68U;
    if (document.entries.size() > std::numeric_limits<std::uint32_t>::max()) {
        reason = "The MLD entry count exceeds the format limit.";
        return std::nullopt;
    }

    model::MldFile file{};
    file.parseStatus = model::MldParseStatus::Complete;
    file.sourcePlatform = target.platform == MldPlatform::Dreamcast
        ? model::TargetPlatform::Dreamcast : model::TargetPlatform::GameCube;
    file.endian = target.platform == MldPlatform::Dreamcast
        ? spice::root::Endian::Little : spice::root::Endian::Big;
    file.sourceWasCompressedAklz = false;
    file.header.entryCount = static_cast<std::uint32_t>(document.entries.size());
    file.header.indexTableOffset = static_cast<std::uint32_t>(headerSize);
    file.decodedBytes.resize(headerSize + document.entries.size() * entrySize, 0U);

    std::unordered_map<std::uint64_t, std::uint32_t> objectAddresses{};
    std::unordered_map<std::uint64_t, std::uint32_t> motionAddresses{};
    std::unordered_map<std::uint64_t, std::uint32_t> groundAddresses{};
    std::unordered_map<std::uint64_t, std::uint32_t> textureListAddresses{};
    std::optional<std::uint32_t> textureArchiveAddress{};
    std::optional<std::uint32_t> firstResourceAddress{};

    const auto noteResource = [&](const std::uint32_t address) {
        if (!firstResourceAddress.has_value()) firstResourceAddress = address;
    };
    for (const auto& item : document.layout) {
        bool ok = true;
        std::visit([&](const auto id) {
            using Id = std::decay_t<decltype(id)>;
            if constexpr (std::is_same_v<Id, MldObjectId>) {
                const auto* resource = findById(document.objects, id);
                if (resource == nullptr) { ok = false; return; }
                const auto* decoded = std::get_if<std::shared_ptr<const modeling::ModelDocument>>(&resource->payload);
                if (decoded == nullptr || !*decoded) { ok = false; return; }
                const auto encoded = modeling::ModelDocumentCodec::encode(**decoded);
                if (!encoded.ok()) { ok = false; return; }
                const auto address = appendAligned(file.decodedBytes, encoded.bytes);
                if (address == std::numeric_limits<std::uint32_t>::max()) { ok = false; return; }
                objectAddresses.emplace(id.value, address);
                model::MldObjectResource output{};
                output.status = model::MldResourceStatus::Complete;
                output.sourceAddress = address;
                output.blockOffset = address;
                output.blockSize = encoded.bytes.size();
                output.rawBytes = encoded.bytes;
                file.objectResources.emplace(address, std::move(output));
                noteResource(address);
            } else if constexpr (std::is_same_v<Id, MldMotionId>) {
                const auto* resource = findById(document.motions, id);
                const auto* decoded = resource == nullptr ? nullptr : std::get_if<MldDecodedMotion>(&resource->payload);
                if (decoded == nullptr || decoded->variants.empty() || !decoded->variants.front().document) { ok = false; return; }
                const auto encoded = modeling::MotionDocumentCodec::encode(*decoded->variants.front().document);
                if (!encoded.ok()) { ok = false; return; }
                const auto address = appendAligned(file.decodedBytes, encoded.bytes);
                if (address == std::numeric_limits<std::uint32_t>::max()) { ok = false; return; }
                motionAddresses.emplace(id.value, address);
                model::MldMotionResource output{};
                output.status = model::MldResourceStatus::Complete;
                output.sourceAddress = address;
                output.blockOffset = address;
                output.blockSize = encoded.bytes.size();
                output.rawBytes = encoded.bytes;
                file.motionResources.emplace(address, std::move(output));
                noteResource(address);
            } else if constexpr (std::is_same_v<Id, MldGroundId>) {
                const auto* resource = findById(document.grounds, id);
                if (resource == nullptr || std::holds_alternative<MldOpaquePayload>(resource->payload)) { ok = false; return; }
                const std::array<std::uint8_t, 4U> placeholder{};
                const auto address = appendAligned(file.decodedBytes, placeholder);
                if (address == std::numeric_limits<std::uint32_t>::max()) { ok = false; return; }
                groundAddresses.emplace(id.value, address);
                model::MldGroundResource output{};
                output.status = model::MldResourceStatus::Complete;
                output.sourceAddress = address;
                output.blockSize = 0U;
                if (const auto* grnd = std::get_if<MldGrndDocument>(&resource->payload)) {
                    const auto encoded = detail::fromDocument(*grnd);
                    output.kind = model::MldGroundResource::Kind::Grnd;
                    output.tag = "GRND";
                    output.grnd = encoded;
                    output.originalSemanticHash = model::semanticHash(encoded) + 1U;
                } else if (const auto* gobj = std::get_if<MldGobjDocument>(&resource->payload)) {
                    const auto encoded = detail::fromDocument(*gobj);
                    output.kind = model::MldGroundResource::Kind::Gobj;
                    output.tag = "GOBJ";
                    output.gobj = encoded;
                    output.originalSemanticHash = model::semanticHash(encoded) + 1U;
                }
                file.groundResources.emplace(address, std::move(output));
                noteResource(address);
            } else if constexpr (std::is_same_v<Id, MldTextureListId>) {
                const auto* resource = findById(document.textureLists, id);
                if (resource == nullptr) { ok = false; return; }
                const auto encoded = buildTextureList(*resource, file.endian);
                if (!encoded.has_value()) { ok = false; return; }
                const auto address = appendAligned(file.decodedBytes, *encoded);
                if (address == std::numeric_limits<std::uint32_t>::max()) { ok = false; return; }
                textureListAddresses.emplace(id.value, address);
                noteResource(address);
            } else if constexpr (std::is_same_v<Id, MldTextureArchiveId>) {
                const auto* resource = findById(document.textureArchives, id);
                if (resource == nullptr) { ok = false; return; }
                const std::array<std::uint8_t, 4U> placeholder{};
                const auto address = appendAligned(file.decodedBytes, placeholder,
                    file.endian == spice::root::Endian::Little ? 32U : 4U);
                if (address == std::numeric_limits<std::uint32_t>::max()) { ok = false; return; }
                model::MldTextureArchive archive{};
                archive.status = model::MldResourceStatus::Complete;
                archive.tableOffset = address;
                archive.archiveStartOffset = address;
                archive.archiveEndOffset = address;
                for (const auto& texture : resource->textures) archive.entries.push_back({
                    .status = model::MldResourceStatus::Complete,
                    .archiveTextureIndex = static_cast<std::uint32_t>(archive.entries.size()),
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
                textureArchiveAddress = address;
                noteResource(address);
            } else if constexpr (std::is_same_v<Id, MldOpaqueMemberId>) {
                ok = false;
            }
        }, item);
        if (!ok) {
            reason = "The document contains a resource that cannot be constructively encoded.";
            return std::nullopt;
        }
    }

    const auto appendList = [&](std::vector<std::uint32_t> values) {
        std::vector<std::uint8_t> encoded(4U + values.size() * 4U, 0U);
        writeU32(encoded, 0U, static_cast<std::uint32_t>(values.size()), file.endian);
        for (std::size_t index = 0U; index < values.size(); ++index)
            writeU32(encoded, 4U + index * 4U, values[index], file.endian);
        const auto address = appendAligned(file.decodedBytes, encoded);
        auto list = std::make_shared<model::U32List>();
        list->pointer = address;
        list->valid = true;
        list->values = std::move(values);
        file.u32Lists.emplace(address, list);
        return list;
    };
    for (std::size_t index = 0U; index < document.entries.size(); ++index) {
        const auto& source = document.entries[index];
        model::MldIndexEntryRecord record{};
        record.entry.tableIndex = index;
        record.entry.entryId = source.entryId;
        record.entry.tblId = source.tableId;
        record.entry.fxnName = source.functionName;
        record.entry.transform = source.transform;
        record.entry.groundLinks = appendList(source.groundLinks);
        record.entry.paramList2 = appendList(source.parameterList2);
        record.entry.functionParameters = appendList(source.functionParameters);
        record.entry.objectAddresses = appendList(encodeSlots(source.objectSlots, objectAddresses));
        record.entry.groundAddresses = appendList(encodeSlots(source.groundSlots, groundAddresses));
        record.entry.motionAddresses = appendList(encodeSlots(source.motionSlots, motionAddresses));
        record.entry.texturesPointer = source.textureList.has_value()
            ? textureListAddresses.at(source.textureList->value) : 0U;
        record.groundLinksPointer = record.entry.groundLinks->pointer;
        record.paramList2Pointer = record.entry.paramList2->pointer;
        record.functionParametersPointer = record.entry.functionParameters->pointer;
        record.objectAddressesPointer = record.entry.objectAddresses->pointer;
        record.groundAddressesPointer = record.entry.groundAddresses->pointer;
        record.motionAddressesPointer = record.entry.motionAddresses->pointer;
        file.entries.push_back(std::move(record));
    }
    file.header.functionParametersOffset = file.entries.front().functionParametersPointer;
    file.header.realDataOffset = firstResourceAddress.value_or(static_cast<std::uint32_t>(file.decodedBytes.size()));
    if (textureArchiveAddress.has_value()) file.header.textureTableOffset = *textureArchiveAddress;
    else {
        const std::array<std::uint8_t, 4U> sentinel{};
        file.header.textureTableOffset = appendAligned(file.decodedBytes, sentinel);
    }
    return file;
}

[[nodiscard]] MldDocumentDiagnostic convertDiagnostic(const model::MldDiagnostic& diagnostic) {
    MldDiagnosticSeverity severity = MldDiagnosticSeverity::Info;
    if (diagnostic.severity == model::MldDiagnostic::Severity::Warning) severity = MldDiagnosticSeverity::Warning;
    if (diagnostic.severity == model::MldDiagnostic::Severity::Error) severity = MldDiagnosticSeverity::Error;
    return { severity, diagnostic.message, diagnostic.sourceOffset };
}

} // namespace

bool MldDocumentWriteResult::ok() const noexcept {
    return !bytes.empty() && std::none_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.severity == MldDiagnosticSeverity::Error;
    });
}

MldDocumentWriteResult MldDocumentWriter::write(
    const MldDocument& document,
    const MldWriteTarget& target,
    const MldImportReceipt* receipt) {
    MldDocumentWriteResult result{};
    const auto validation = MldDocumentValidator::validate(document, target, receipt);
    result.diagnostics = validation.diagnostics;
    if (!validation.ok()) return result;
    if (receipt == nullptr || !receipt->state_) {
        std::string reason{};
        auto output = buildConstructiveFile(document, target, reason);
        if (!output.has_value()) {
            result.diagnostics.push_back({ MldDiagnosticSeverity::Error, std::move(reason) });
            return result;
        }
        const auto platform = target.platform == MldPlatform::Dreamcast
            ? model::TargetPlatform::Dreamcast : model::TargetPlatform::GameCube;
        const auto written = exporting::MldFileWriter{}.write(*output, exporting::MldWriteOptions{
            .platform = platform,
            .compressAklz = target.wrapper == MldWrapper::Aklz,
        });
        for (const auto& diagnostic : written.diagnostics) result.diagnostics.push_back(convertDiagnostic(diagnostic));
        if (written.ok()) result.bytes = written.bytes;
        return result;
    }

    auto output = receipt->state_->encodingSkeleton;
    if (receipt->decodedSize > std::numeric_limits<std::size_t>::max()) {
        result.diagnostics.push_back({ MldDiagnosticSeverity::Error, "The imported decoded MLD size is not representable." });
        return result;
    }
    output.decodedBytes.assign(static_cast<std::size_t>(receipt->decodedSize), 0U);
    for (const auto& fragment : receipt->state_->preservedFragments)
        copyAt(output.decodedBytes, fragment.decodedOffset, fragment.bytes);

    // Recreate list payloads from semantic values. Entry updates below replace
    // their values before the legacy encoder performs any relocation.
    for (const auto& [offset, list] : output.u32Lists) {
        if (!list) continue;
        writeU32(output.decodedBytes, offset, static_cast<std::uint32_t>(list->values.size()), output.endian);
        for (std::size_t index = 0U; index < list->values.size(); ++index)
            writeU32(output.decodedBytes, offset + 4U + index * 4U, list->values[index], output.endian);
    }
    const auto objectAddresses = addresses<MldObjectId>(receipt->layout);
    const auto motionAddresses = addresses<MldMotionId>(receipt->layout);
    const auto groundAddresses = addresses<MldGroundId>(receipt->layout);
    auto textureListAddresses = addresses<MldTextureListId>(receipt->layout);

    const auto targetEndian = target.platform == MldPlatform::Dreamcast
        ? spice::root::Endian::Little : spice::root::Endian::Big;
    for (const auto& source : document.textureLists) {
        const auto layout = std::find_if(receipt->layout.begin(), receipt->layout.end(), [&](const auto& item) {
            const auto* id = std::get_if<MldTextureListId>(&item.item);
            return id != nullptr && *id == source.id;
        });
        if (layout == receipt->layout.end()) continue;
        const auto original = output.textureListResources.find(static_cast<std::uint32_t>(layout->encodedReference));
        std::vector<std::string> originalNames{};
        if (original != output.textureListResources.end())
            for (const auto& entry : original->second.entries) originalNames.push_back(entry.name);
        if (targetEndian == output.endian && source.names == originalNames) continue;
        const auto encoded = buildTextureList(source, targetEndian);
        if (!encoded.has_value()) {
            result.diagnostics.push_back({ MldDiagnosticSeverity::Error,
                "Failed to encode an edited MLD texture list." });
            return result;
        }
        const auto offset = static_cast<std::size_t>(layout->decodedOffset);
        const auto available = static_cast<std::size_t>(layout->encodedSize);
        std::uint32_t outputAddress = 0U;
        if (targetEndian == output.endian && offset <= output.decodedBytes.size()
            && available <= output.decodedBytes.size() - offset && encoded->size() <= available) {
            copyAt(output.decodedBytes, offset, *encoded);
            std::fill(output.decodedBytes.begin() + static_cast<std::ptrdiff_t>(offset + encoded->size()),
                output.decodedBytes.begin() + static_cast<std::ptrdiff_t>(offset + available), 0U);
            outputAddress = static_cast<std::uint32_t>(layout->encodedReference);
        } else {
            outputAddress = appendAligned(output.decodedBytes, *encoded);
            if (outputAddress == std::numeric_limits<std::uint32_t>::max()) {
                result.diagnostics.push_back({ MldDiagnosticSeverity::Error,
                    "An edited MLD texture list exceeded the 32-bit output address space." });
                return result;
            }
        }
        textureListAddresses[source.id.value] = outputAddress;
    }

    for (std::size_t i = 0; i < document.entries.size(); ++i) {
        const auto& source = document.entries[i];
        auto& destination = output.entries[i].entry;
        destination.entryId = source.entryId;
        destination.tblId = source.tableId;
        destination.fxnName = source.functionName;
        destination.transform = source.transform;
        assignList(destination.groundLinks, source.groundLinks);
        assignList(destination.paramList2, source.parameterList2);
        assignList(destination.functionParameters, source.functionParameters);
        assignList(destination.objectAddresses, encodeSlots(source.objectSlots, objectAddresses));
        assignList(destination.groundAddresses, encodeSlots(source.groundSlots, groundAddresses));
        assignList(destination.motionAddresses, encodeSlots(source.motionSlots, motionAddresses));
        destination.texturesPointer = source.textureList.has_value()
            ? textureListAddresses.at(source.textureList->value) : 0U;
    }

    for (const auto& member : document.opaqueMembers) {
        const auto found = std::find_if(receipt->layout.begin(), receipt->layout.end(), [&](const auto& item) {
            const auto* id = std::get_if<MldOpaqueMemberId>(&item.item);
            return id != nullptr && *id == member.id;
        });
        if (found != receipt->layout.end()) copyAt(output.decodedBytes,
            static_cast<std::size_t>(found->decodedOffset), member.payload.bytes);
    }

    const auto populateOpaqueOrEncoded = [&](const auto& resources, const auto& addressById,
        auto& destinations, const auto& encodeDecoded) -> bool {
        for (const auto& source : resources) {
            const auto address = addressById.at(source.id.value);
            auto destination = destinations.find(address);
            std::vector<std::uint8_t> encoded{};
            if (const auto* opaque = std::get_if<MldOpaquePayload>(&source.payload)) encoded = opaque->bytes;
            else if (!encodeDecoded(source.payload, encoded)) return false;
            const auto layout = std::find_if(receipt->layout.begin(), receipt->layout.end(), [&](const auto& item) {
                const auto* id = std::get_if<std::decay_t<decltype(source.id)>>(&item.item);
                return id != nullptr && *id == source.id;
            });
            if (layout == receipt->layout.end()) return false;
            if (destination != destinations.end()) {
                destination->second.rawBytes = encoded;
                destination->second.blockSize = encoded.size();
            }
            copyAt(output.decodedBytes, static_cast<std::size_t>(layout->decodedOffset), encoded);
        }
        return true;
    };

    if (!populateOpaqueOrEncoded(document.objects, objectAddresses, output.objectResources,
        [&](const MldObjectPayload& payload, std::vector<std::uint8_t>& encoded) {
            const auto* decoded = std::get_if<std::shared_ptr<const modeling::ModelDocument>>(&payload);
            if (decoded == nullptr || !*decoded) return false;
            const auto written = modeling::ModelDocumentCodec::encode(**decoded);
            if (!written.ok()) return false;
            encoded = written.bytes;
            return true;
        })) {
        result.diagnostics.push_back({ MldDiagnosticSeverity::Error, "Failed to encode an MLD object resource." });
        return result;
    }
    if (!populateOpaqueOrEncoded(document.motions, motionAddresses, output.motionResources,
        [&](const MldMotionPayload& payload, std::vector<std::uint8_t>& encoded) {
            const auto* decoded = std::get_if<MldDecodedMotion>(&payload);
            if (decoded == nullptr || decoded->variants.empty() || !decoded->variants.front().document) return false;
            const auto written = modeling::MotionDocumentCodec::encode(*decoded->variants.front().document);
            if (!written.ok()) return false;
            encoded = written.bytes;
            return true;
        })) {
        result.diagnostics.push_back({ MldDiagnosticSeverity::Error, "Failed to encode an MLD motion resource." });
        return result;
    }

    std::size_t groundIndex = 0U;
    for (auto& [address, destination] : output.groundResources) {
        const auto& source = document.grounds[groundIndex++];
        if (const auto* grnd = std::get_if<MldGrndDocument>(&source.payload)) destination.grnd = detail::fromDocument(*grnd);
        if (const auto* gobj = std::get_if<MldGobjDocument>(&source.payload)) destination.gobj = detail::fromDocument(*gobj);
        if (const auto* opaque = std::get_if<MldOpaquePayload>(&source.payload)) {
            destination.rawBytes = opaque->bytes;
            destination.blockSize = opaque->bytes.size();
            copyAt(output.decodedBytes, address, opaque->bytes);
        }
    }
    std::size_t textureListIndex = 0U;
    for (auto& [address, destination] : output.textureListResources) {
        (void)address;
        const auto& source = document.textureLists[textureListIndex++];
        if (source.names.size() != destination.entries.size()) {
            result.diagnostics.push_back({ MldDiagnosticSeverity::Error,
                "This release cannot change the number of names in an encoded MLD texture list." });
            return result;
        }
        for (std::size_t index = 0U; index < source.names.size(); ++index)
            destination.entries[index].name = source.names[index];
    }
    if (!document.textureArchives.empty() && output.textureArchive.has_value()) {
        const auto& sourceTextures = document.textureArchives.front().textures;
        if (sourceTextures.size() != output.textureArchive->entries.size()) {
            result.diagnostics.push_back({ MldDiagnosticSeverity::Error,
                "This release cannot add or remove encoded MLD textures during output." });
            return result;
        }
        for (std::size_t index = 0U; index < sourceTextures.size(); ++index) {
            const auto& source = sourceTextures[index];
            auto& destination = output.textureArchive->entries[index];
            destination.encoding = source.encoding;
            destination.textureName = source.name;
            destination.encodedData = source.encodedBytes;
            destination.encodedDataSize = source.encodedBytes.size();
            destination.hasGlobalIndex = source.hasGlobalIndex;
            destination.globalIndex = source.globalIndex;
            destination.pixelFormat = source.pixelFormat;
            destination.dataFormat = source.dataFormat;
            destination.sourceFormat = source.format;
            destination.sourcePaletteFormat = source.paletteFormat;
            destination.hasMipmaps = source.hasMipmaps;
            destination.hasInternalPalette = source.hasInternalPalette;
            destination.width = source.width;
            destination.height = source.height;
            destination.decoded = source.decoded;
            destination.rgba8 = source.rgba8;
        }
    }

    const auto platform = target.platform == MldPlatform::Dreamcast
        ? model::TargetPlatform::Dreamcast : model::TargetPlatform::GameCube;
    const auto written = exporting::MldFileWriter{}.write(output, exporting::MldWriteOptions{
        .platform = platform,
        .compressAklz = target.wrapper == MldWrapper::Aklz,
    });
    for (const auto& diagnostic : written.diagnostics) result.diagnostics.push_back(convertDiagnostic(diagnostic));
    if (written.ok()) result.bytes = written.bytes;
    return result;
}

} // namespace spice::mld
