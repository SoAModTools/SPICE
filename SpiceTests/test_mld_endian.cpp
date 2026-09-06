#include "../SpiceMLD/SpiceMLD.h"
#include "../SpiceMLD/Export/MldFileExporter.h"
#include "../SpiceMLD/Export/MldFileWriter.h"
#include "../SpiceMLD/Export/MldEntryListJsonExporter.h"
#include "../SpiceMLD/Model/MldGroundEditing.h"
#include "../SpiceMLD/Model/TriangleMetadata.h"
#include "../SpiceMLD/Patching/TriangleMetadataPatcher.h"
#include "../SpiceMLD/Parsing/EntryHandlers.h"
#include "../SpiceMLD/Parsing/MldParser.h"
#include "../SpiceMLD/Parsing/Sa3dBlenderIrBuilder.h"
#include "../SpiceMLD/Parsing/GobjParser.h"
#include "../SpiceMLD/Parsing/GrndParser.h"
#include "../SpiceGvm/SpiceGvm.h"
#include "../SpicePvm/SpicePvm.h"
#include "../Compression/Aklz.h"
#include "../SpiceModeling/SpiceModeling.h"

#include <gtest/gtest.h>

#include <bit>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <array>

namespace {

using spice::root::Endian;
using spice::mld::exporting::MldExportOptions;
using spice::mld::exporting::MldFileExporter;
using spice::mld::exporting::MldFileWriter;
using spice::mld::model::TargetPlatform;
using spice::mld::parsing::MldParser;

constexpr std::size_t kHeaderSize = 0x14U;
constexpr std::size_t kEntryOffset = 0x20U;
constexpr std::size_t kEntrySize = 0x68U;
constexpr std::size_t kListGroundLinks = 0x100U;
constexpr std::size_t kListParam2 = 0x108U;
constexpr std::size_t kListFunctionParams = 0x110U;
constexpr std::size_t kListObjects = 0x11CU;
constexpr std::size_t kListGrounds = 0x124U;
constexpr std::size_t kListMotions = 0x12CU;
constexpr std::size_t kGrndOffset = 0x140U;
constexpr std::size_t kTextureTable = 0x170U;

void writeU16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value, Endian endian) {
    if (endian == Endian::Big) {
        bytes[offset + 0U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        bytes[offset + 1U] = static_cast<std::uint8_t>(value & 0xFFU);
    } else {
        bytes[offset + 0U] = static_cast<std::uint8_t>(value & 0xFFU);
        bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    }
}

void writeU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value, Endian endian) {
    if (endian == Endian::Big) {
        bytes[offset + 0U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
        bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
        bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        bytes[offset + 3U] = static_cast<std::uint8_t>(value & 0xFFU);
    } else {
        bytes[offset + 0U] = static_cast<std::uint8_t>(value & 0xFFU);
        bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
        bytes[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    }
}

void writeF32(std::vector<std::uint8_t>& bytes, std::size_t offset, float value, Endian endian) {
    writeU32(bytes, offset, std::bit_cast<std::uint32_t>(value), endian);
}

void writeTag(std::vector<std::uint8_t>& bytes, std::size_t offset, const char* tag) {
    bytes[offset + 0U] = static_cast<std::uint8_t>(tag[0]);
    bytes[offset + 1U] = static_cast<std::uint8_t>(tag[1]);
    bytes[offset + 2U] = static_cast<std::uint8_t>(tag[2]);
    bytes[offset + 3U] = static_cast<std::uint8_t>(tag[3]);
}

std::vector<std::uint8_t> makeSyntheticGobj(
    const std::uint8_t chunkType,
    const std::uint8_t recordWords,
    const bool hasNormal,
    const bool hasUserAttributes,
    const Endian endian = Endian::Big,
    const bool hasDiffuseColor = false) {
    constexpr std::size_t nodeOffset = 0x10U;
    constexpr std::size_t attachOffset = 0x50U;
    constexpr std::size_t payloadOffset = attachOffset + 0x10U;
    constexpr std::size_t polyOffset = payloadOffset + 76U;
    constexpr std::size_t vertexOffset = 0xBCU;
    constexpr std::size_t vertexCount = 3U;
    const std::size_t declaredSize = vertexOffset + 8U + (vertexCount * recordWords * 4U);

    std::vector<std::uint8_t> bytes(declaredSize, 0U);
    writeTag(bytes, 0U, "GOBJ");
    writeU32(bytes, 4U, static_cast<std::uint32_t>(declaredSize), endian);
    writeU32(bytes, nodeOffset, static_cast<std::uint32_t>(attachOffset - nodeOffset), endian);
    writeF32(bytes, nodeOffset + 0x20U, 1.0F, endian);
    writeF32(bytes, nodeOffset + 0x24U, 1.0F, endian);
    writeF32(bytes, nodeOffset + 0x28U, 1.0F, endian);

    writeU32(bytes, payloadOffset, static_cast<std::uint32_t>(vertexOffset - payloadOffset), endian);
    constexpr std::array<std::uint16_t, 3> flags{ 0x0001U, 0x0002U, 0x8003U };
    for (std::size_t i = 0; i < vertexCount; ++i) {
        writeU16(bytes, polyOffset + (i * 4U), static_cast<std::uint16_t>(2U + (i * recordWords)), endian);
        writeU16(bytes, polyOffset + (i * 4U) + 2U, flags[i], endian);
    }
    writeU16(bytes, polyOffset + 12U, 0xFFFFU, endian);
    writeU16(bytes, polyOffset + 14U, 0xFFFFU, endian);

    writeU32(bytes, vertexOffset, chunkType, endian);
    writeU32(bytes, vertexOffset + 4U, static_cast<std::uint32_t>(vertexCount << 16U), endian);
    constexpr std::array<std::uint32_t, 3> userAttributes{ 0xFFFFFFFFU, 0x12345678U, 0x00000000U };
    for (std::size_t i = 0; i < vertexCount; ++i) {
        const std::size_t recordOffset = vertexOffset + 8U + (i * recordWords * 4U);
        writeF32(bytes, recordOffset + 0U, static_cast<float>(i), endian);
        writeF32(bytes, recordOffset + 4U, static_cast<float>(i + 1U), endian);
        writeF32(bytes, recordOffset + 8U, static_cast<float>(i + 2U), endian);
        if (hasNormal) {
            writeF32(bytes, recordOffset + 12U, 0.0F, endian);
            writeF32(bytes, recordOffset + 16U, 1.0F, endian);
            writeF32(bytes, recordOffset + 20U, 0.0F, endian);
        }
        if (hasUserAttributes || hasDiffuseColor) {
            writeU32(bytes, recordOffset + 24U, userAttributes[i], endian);
        }
    }
    return bytes;
}

std::vector<std::uint8_t> makeSyntheticGrnd(
    const Endian endian = Endian::Big,
    const float translationX = 0.0F,
    const float translationY = 0.0F,
    const float translationZ = 0.0F,
    const float gridOriginX = 0.0F,
    const float gridOriginZ = 0.0F) {
    constexpr std::size_t innerHeader = 0x10U;
    constexpr std::size_t triangleSetsOffset = 0x40U;
    constexpr std::size_t streamOffset = 0x60U;
    constexpr std::size_t vertexOffset = 0x80U;
    constexpr std::size_t quadRegistryOffset = 0xC8U;
    constexpr std::size_t quadTableOffset = quadRegistryOffset + 4U;
    constexpr std::size_t refListOffset = 0xDCU;
    constexpr std::size_t declaredSize = 0xE0U;

    std::vector<std::uint8_t> bytes(declaredSize, 0U);
    writeTag(bytes, 0U, "GRND");
    writeU32(bytes, 4U, static_cast<std::uint32_t>(declaredSize), endian);
    writeU32(bytes, innerHeader, static_cast<std::uint32_t>(triangleSetsOffset - innerHeader), endian);
    writeU32(bytes, innerHeader + 4U, static_cast<std::uint32_t>(quadRegistryOffset - innerHeader), endian);
    writeF32(bytes, innerHeader + 8U, gridOriginX, endian);
    writeF32(bytes, innerHeader + 0x0CU, gridOriginZ, endian);
    writeU16(bytes, innerHeader + 0x10U, 1U, endian);
    writeU16(bytes, innerHeader + 0x12U, 1U, endian);
    writeU16(bytes, innerHeader + 0x14U, 1U, endian);
    writeU16(bytes, innerHeader + 0x16U, 1U, endian);
    writeU16(bytes, innerHeader + 0x18U, 1U, endian);
    writeU16(bytes, innerHeader + 0x1AU, 1U, endian);

    writeF32(bytes, triangleSetsOffset, translationX, endian);
    writeF32(bytes, triangleSetsOffset + 4U, translationY, endian);
    writeF32(bytes, triangleSetsOffset + 8U, translationZ, endian);
    writeU32(bytes, triangleSetsOffset + 0x0CU,
        static_cast<std::uint32_t>(vertexOffset - (triangleSetsOffset + 0x0CU)), endian);
    writeU32(bytes, triangleSetsOffset + 0x10U,
        static_cast<std::uint32_t>(streamOffset - (triangleSetsOffset + 0x10U)), endian);
    writeU32(bytes, triangleSetsOffset + 0x14U, 1U, endian);

    constexpr std::array<std::uint16_t, 3> floatIndices{ 0U, 6U, 12U };
    constexpr std::array<std::uint16_t, 3> flags{ 0x0001U, 0x7FFFU, 0x800AU };
    for (std::size_t i = 0; i < 3U; ++i) {
        writeU16(bytes, streamOffset + (i * 4U), floatIndices[i], endian);
        writeU16(bytes, streamOffset + (i * 4U) + 2U, flags[i], endian);
        const std::size_t recordOffset = vertexOffset + (i * 24U);
        writeF32(bytes, recordOffset + 0U, static_cast<float>(i), endian);
        writeF32(bytes, recordOffset + 4U, static_cast<float>(i == 1U), endian);
        writeF32(bytes, recordOffset + 8U, static_cast<float>(i == 2U), endian);
        writeF32(bytes, recordOffset + 12U, 0.0F, endian);
        writeF32(bytes, recordOffset + 16U, 1.0F, endian);
        writeF32(bytes, recordOffset + 20U, 0.0F, endian);
    }

    writeU32(bytes, quadTableOffset, 1U, endian);
    writeU32(bytes, quadTableOffset + 4U,
        static_cast<std::uint32_t>(refListOffset - (quadTableOffset + 4U)), endian);
    writeU16(bytes, refListOffset, 0U, endian);
    writeU16(bytes, refListOffset + 2U, 0U, endian);
    return bytes;
}

void appendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value, Endian endian) {
    const auto offset = bytes.size();
    bytes.resize(offset + 4U);
    writeU32(bytes, offset, value, endian);
}

void appendNameRecord(std::vector<std::uint8_t>& bytes, const std::string& name) {
    const auto offset = bytes.size();
    bytes.resize(offset + 44U, 0U);
    const auto count = std::min<std::size_t>(name.size(), 31U);
    std::copy_n(name.begin(), count, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

void writeList(std::vector<std::uint8_t>& bytes, std::size_t offset, std::span<const std::uint32_t> values, Endian endian) {
    writeU32(bytes, offset, static_cast<std::uint32_t>(values.size()), endian);
    for (std::size_t i = 0; i < values.size(); ++i) {
        writeU32(bytes, offset + 4U + (i * 4U), values[i], endian);
    }
}

std::vector<std::uint8_t> makeMinimalMld(Endian endian) {
    std::vector<std::uint8_t> bytes(0x180U, 0U);
    writeU32(bytes, 0x00U, 1U, endian);
    writeU32(bytes, 0x04U, static_cast<std::uint32_t>(kEntryOffset), endian);
    writeU32(bytes, 0x08U, static_cast<std::uint32_t>(kListFunctionParams), endian);
    writeU32(bytes, 0x0CU, static_cast<std::uint32_t>(kGrndOffset), endian);
    writeU32(bytes, 0x10U, static_cast<std::uint32_t>(kTextureTable), endian);

    writeU32(bytes, kEntryOffset + 0x00U, 0x101U, endian);
    writeU32(bytes, kEntryOffset + 0x04U, 0x202U, endian);
    writeU32(bytes, kEntryOffset + 0x08U, static_cast<std::uint32_t>(kListGroundLinks), endian);
    writeU32(bytes, kEntryOffset + 0x0CU, static_cast<std::uint32_t>(kListParam2), endian);
    writeU32(bytes, kEntryOffset + 0x10U, static_cast<std::uint32_t>(kListFunctionParams), endian);
    writeU32(bytes, kEntryOffset + 0x14U, static_cast<std::uint32_t>(kListObjects), endian);
    writeU32(bytes, kEntryOffset + 0x18U, static_cast<std::uint32_t>(kListGrounds), endian);
    writeU32(bytes, kEntryOffset + 0x1CU, static_cast<std::uint32_t>(kListMotions), endian);
    writeU32(bytes, kEntryOffset + 0x20U, 0U, endian);
    const std::string fxn = "wall";
    std::copy(fxn.begin(), fxn.end(), bytes.begin() + static_cast<std::ptrdiff_t>(kEntryOffset + 0x24U));
    writeF32(bytes, kEntryOffset + 0x44U, 1.0F, endian);
    writeF32(bytes, kEntryOffset + 0x48U, 2.0F, endian);
    writeF32(bytes, kEntryOffset + 0x4CU, 3.0F, endian);
    writeF32(bytes, kEntryOffset + 0x50U, 4.0F, endian);
    writeF32(bytes, kEntryOffset + 0x54U, 5.0F, endian);
    writeF32(bytes, kEntryOffset + 0x58U, 6.0F, endian);
    writeF32(bytes, kEntryOffset + 0x5CU, 1.0F, endian);
    writeF32(bytes, kEntryOffset + 0x60U, 1.0F, endian);
    writeF32(bytes, kEntryOffset + 0x64U, 1.0F, endian);

    const std::uint32_t groundLinks[] = { 7U };
    const std::uint32_t functionParams[] = { 0x333U, 0x444U };
    const std::uint32_t grounds[] = { static_cast<std::uint32_t>(kGrndOffset) };
    const std::array<std::uint32_t, 0> empty{};
    writeList(bytes, kListGroundLinks, groundLinks, endian);
    writeList(bytes, kListParam2, empty, endian);
    writeList(bytes, kListFunctionParams, functionParams, endian);
    writeList(bytes, kListObjects, empty, endian);
    writeList(bytes, kListGrounds, grounds, endian);
    writeList(bytes, kListMotions, empty, endian);

    writeTag(bytes, kGrndOffset, "GRND");
    writeU32(bytes, kGrndOffset + 4U, 0x2CU, endian);
    writeU32(bytes, kGrndOffset + 0x10U, 0, endian);
    writeU32(bytes, kGrndOffset + 0x14U, 0, endian);
    writeU16(bytes, kGrndOffset + 0x20U, 2U, endian);
    writeU16(bytes, kGrndOffset + 0x22U, 3U, endian);
    writeU16(bytes, kGrndOffset + 0x24U, 4U, endian);
    writeU16(bytes, kGrndOffset + 0x26U, 5U, endian);
    writeU16(bytes, kGrndOffset + 0x28U, 0U, endian);
    writeU16(bytes, kGrndOffset + 0x2AU, 0U, endian);

    writeU32(bytes, kTextureTable, 0U, endian);
    return bytes;
}

void clearSyntheticGround(std::vector<std::uint8_t>& bytes, const Endian endian) {
    const std::array<std::uint32_t, 0> empty{};
    writeList(bytes, kListGrounds, empty, endian);
}

void writeDirectTextureList(std::vector<std::uint8_t>& bytes,
    const std::size_t offset,
    const char* tag,
    const Endian endian,
    const bool invalidNamePointer = false) {
    writeTag(bytes, offset, tag);
    writeU32(bytes, offset + 4U, 0x30U, endian);
    writeU32(bytes, offset + 8U, 0xAABBCCDDU, endian);
    writeU32(bytes, offset + 12U, 1U, endian);
    writeU32(bytes, offset + 16U, invalidNamePointer ? 0x100U : 0x20U, endian);
    writeU32(bytes, offset + 20U, 0x11223344U, endian);
    writeU32(bytes, offset + 24U, 0x55667788U, endian);
    const std::string name = "deck_a";
    std::copy(name.begin(), name.end(),
        bytes.begin() + static_cast<std::ptrdiff_t>(offset + 0x28U));
}

std::vector<std::uint8_t> makeDirectTextureListMld(
    const Endian endian,
    const char* tag,
    const bool invalidNamePointer = false,
    const bool sharedByTwoEntries = false) {
    constexpr std::uint32_t textureList = 0x180U;
    auto bytes = makeMinimalMld(endian);
    bytes.resize(0x200U, 0U);
    clearSyntheticGround(bytes, endian);
    writeU32(bytes, 0x0CU, textureList, endian);
    writeU32(bytes, kEntryOffset + 0x20U, textureList, endian);
    writeDirectTextureList(bytes, textureList, tag, endian, invalidNamePointer);
    if (sharedByTwoEntries) {
        constexpr std::size_t secondEntry = kEntryOffset + kEntrySize;
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(kEntryOffset), kEntrySize,
            bytes.begin() + static_cast<std::ptrdiff_t>(secondEntry));
        writeU32(bytes, 0x00U, 2U, endian);
        writeU32(bytes, secondEntry, 0x102U, endian);
    }
    return bytes;
}

std::vector<std::uint8_t> makeWrappedTextureListMld(const Endian endian) {
    constexpr std::uint32_t wrapper = 0x180U;
    constexpr std::uint32_t textureList = 0x190U;
    auto bytes = makeMinimalMld(endian);
    bytes.resize(0x210U, 0U);
    clearSyntheticGround(bytes, endian);
    writeU32(bytes, 0x0CU, wrapper, endian);
    writeU32(bytes, kEntryOffset + 0x20U, wrapper, endian);
    writeU32(bytes, wrapper, 0x12345678U, endian);
    writeU32(bytes, wrapper + 4U, 0U, endian);
    writeU32(bytes, wrapper + 8U, textureList, endian);
    writeDirectTextureList(bytes, textureList, "NJTL", endian);
    return bytes;
}

std::vector<std::uint8_t> makeCountedTextureListMld(const Endian endian) {
    constexpr std::uint32_t textureList = 0x180U;
    constexpr std::uint32_t nameOffset = 0x1A0U;
    auto bytes = makeMinimalMld(endian);
    bytes.resize(0x1C0U, 0U);
    clearSyntheticGround(bytes, endian);
    writeU32(bytes, 0x0CU, textureList, endian);
    writeU32(bytes, kEntryOffset + 0x20U, textureList, endian);
    writeU32(bytes, textureList, 0xCAFEBABEU, endian);
    writeU32(bytes, textureList + 4U, 1U, endian);
    writeU32(bytes, textureList + 8U, nameOffset, endian);
    writeU32(bytes, textureList + 12U, 0x11111111U, endian);
    writeU32(bytes, textureList + 16U, 0x22222222U, endian);
    const std::string name = "counted";
    std::copy(name.begin(), name.end(),
        bytes.begin() + static_cast<std::ptrdiff_t>(nameOffset));
    return bytes;
}

void writeSa3dPointer(std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint32_t modelBlockRelativeOffset) {
    writeU32(bytes, offset, modelBlockRelativeOffset - 0x08U, Endian::Little);
}

std::vector<std::uint8_t> makeWrappedObjectMldWithTriangleGeometry() {
    constexpr std::uint32_t kObjectWrapperOffset = 0x180U;
    constexpr std::uint32_t kModelRelativeOffset = 0x40U;
    constexpr std::uint32_t kModelBlockOffset = kObjectWrapperOffset + kModelRelativeOffset;
    constexpr std::uint32_t kNodeRelativeOffset = 0x08U;
    constexpr std::uint32_t kAttachRelativeOffset = 0x48U;
    constexpr std::uint32_t kVertexChunkRelativeOffset = 0x78U;
    constexpr std::uint32_t kPolyChunkRelativeOffset = 0xC8U;
    constexpr std::uint32_t kNodeOffset = kModelBlockOffset + kNodeRelativeOffset;
    constexpr std::uint32_t kAttachOffset = kModelBlockOffset + kAttachRelativeOffset;
    constexpr std::uint32_t kVertexChunkOffset = kModelBlockOffset + kVertexChunkRelativeOffset;
    constexpr std::uint32_t kPolyChunkOffset = kModelBlockOffset + kPolyChunkRelativeOffset;

    auto bytes = makeMinimalMld(Endian::Big);
    bytes.resize(0x300U, 0U);

    const std::uint32_t objects[] = { kObjectWrapperOffset };
    writeList(bytes, kListObjects, objects, Endian::Big);

    writeU32(bytes, kObjectWrapperOffset, kModelRelativeOffset, Endian::Big);
    writeTag(bytes, kModelBlockOffset, "NJCM");
    writeU32(bytes, kModelBlockOffset + 0x04U, 0xF0U, Endian::Little);

    writeU32(bytes, kNodeOffset + 0x00U, 0U, Endian::Little);
    writeSa3dPointer(bytes, kNodeOffset + 0x04U, kAttachRelativeOffset);
    writeF32(bytes, kNodeOffset + 0x08U, 1.0F, Endian::Little);
    writeF32(bytes, kNodeOffset + 0x0CU, 2.0F, Endian::Little);
    writeF32(bytes, kNodeOffset + 0x10U, 3.0F, Endian::Little);
    writeU32(bytes, kNodeOffset + 0x14U, 0U, Endian::Little);
    writeU32(bytes, kNodeOffset + 0x18U, 0U, Endian::Little);
    writeU32(bytes, kNodeOffset + 0x1CU, 0U, Endian::Little);
    writeF32(bytes, kNodeOffset + 0x20U, 1.0F, Endian::Little);
    writeF32(bytes, kNodeOffset + 0x24U, 1.0F, Endian::Little);
    writeF32(bytes, kNodeOffset + 0x28U, 1.0F, Endian::Little);
    writeU32(bytes, kNodeOffset + 0x2CU, 0U, Endian::Little);
    writeU32(bytes, kNodeOffset + 0x30U, 0U, Endian::Little);

    writeSa3dPointer(bytes, kAttachOffset + 0x00U, kVertexChunkRelativeOffset);
    writeSa3dPointer(bytes, kAttachOffset + 0x04U, kPolyChunkRelativeOffset);
    writeF32(bytes, kAttachOffset + 0x08U, 0.0F, Endian::Little);
    writeF32(bytes, kAttachOffset + 0x0CU, 0.0F, Endian::Little);
    writeF32(bytes, kAttachOffset + 0x10U, 0.0F, Endian::Little);
    writeF32(bytes, kAttachOffset + 0x14U, 8.0F, Endian::Little);

    writeU32(bytes, kVertexChunkOffset + 0x00U, 0x00000022U, Endian::Little);
    writeU32(bytes, kVertexChunkOffset + 0x04U, 0x00030000U, Endian::Little);
    writeF32(bytes, kVertexChunkOffset + 0x08U, 0.0F, Endian::Little);
    writeF32(bytes, kVertexChunkOffset + 0x0CU, 0.0F, Endian::Little);
    writeF32(bytes, kVertexChunkOffset + 0x10U, 0.0F, Endian::Little);
    writeF32(bytes, kVertexChunkOffset + 0x14U, 1.0F, Endian::Little);
    writeF32(bytes, kVertexChunkOffset + 0x18U, 0.0F, Endian::Little);
    writeF32(bytes, kVertexChunkOffset + 0x1CU, 0.0F, Endian::Little);
    writeF32(bytes, kVertexChunkOffset + 0x20U, 0.0F, Endian::Little);
    writeF32(bytes, kVertexChunkOffset + 0x24U, 1.0F, Endian::Little);
    writeF32(bytes, kVertexChunkOffset + 0x28U, 0.0F, Endian::Little);
    writeU32(bytes, kVertexChunkOffset + 0x2CU, 0x000000FFU, Endian::Little);
    writeU32(bytes, kVertexChunkOffset + 0x30U, 0U, Endian::Little);

    writeU16(bytes, kPolyChunkOffset + 0x00U, 0x0040U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x02U, 5U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x04U, 1U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x06U, 3U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x08U, 0U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x0AU, 1U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x0CU, 2U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x0EU, 0x00FFU, Endian::Little);

    return bytes;
}

void writeEmptyNjMotion(
    std::vector<std::uint8_t>& bytes,
    const std::size_t offset,
    const char* tag) {
    if (bytes.size() < offset + 36U) {
        bytes.resize(offset + 36U, 0U);
    }
    writeTag(bytes, offset, tag);
    writeU32(bytes, offset + 4U, 16U, Endian::Little);
    writeU32(bytes, offset + 8U, 0U, Endian::Little);
    writeU32(bytes, offset + 12U, 23U, Endian::Little);
    writeU16(bytes, offset + 16U, 0U, Endian::Little);
    writeU16(bytes, offset + 18U, 0U, Endian::Little);
    writeU32(bytes, offset + 20U, 0U, Endian::Little);
    writeTag(bytes, offset + 24U, "POF0");
    writeU32(bytes, offset + 28U, 4U, Endian::Little);
    writeU32(bytes, offset + 32U, 0U, Endian::Little);
}

std::vector<std::uint8_t> makeWrappedObjectMldWithMotions(
    const std::span<const std::uint32_t> objectAddresses,
    const std::span<const std::pair<std::uint32_t, const char*>> motions) {
    auto bytes = makeWrappedObjectMldWithTriangleGeometry();
    std::vector<std::uint32_t> motionAddresses{};
    for (const auto& [address, tag] : motions) {
        motionAddresses.push_back(address);
        writeEmptyNjMotion(bytes, address, tag);
    }
    writeList(bytes, kListObjects, objectAddresses, Endian::Big);
    writeList(bytes, kListMotions, motionAddresses, Endian::Big);
    return bytes;
}

spice::gvm::model::RgbaImage makeImage(std::uint32_t width, std::uint32_t height, std::uint8_t bias = 0U) {
    spice::gvm::model::RgbaImage image{};
    image.width = width;
    image.height = height;
    image.rgba8.resize(static_cast<std::size_t>(width) * height * 4U);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto offset = (static_cast<std::size_t>(y) * width + x) * 4U;
            image.rgba8[offset + 0U] = static_cast<std::uint8_t>((x * 17U + bias) & 0xFFU);
            image.rgba8[offset + 1U] = static_cast<std::uint8_t>((y * 19U + bias) & 0xFFU);
            image.rgba8[offset + 2U] = static_cast<std::uint8_t>(((x + y) * 13U + bias) & 0xFFU);
            image.rgba8[offset + 3U] = 0xFFU;
        }
    }
    return image;
}

std::filesystem::path testOutDir(const char* name) {
    auto dir = std::filesystem::temp_directory_path() / "spice_mld_endian_tests" / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

std::vector<std::uint8_t> encodeTexture(
    const spice::gvm::model::RgbaImage& image,
    spice::gvm::model::TextureFormat format,
    spice::gvm::model::PaletteFormat palette = spice::gvm::model::PaletteFormat::None,
    bool hasGlobalIndex = false,
    std::uint32_t globalIndex = 0U) {
    spice::gvm::encoding::EncodeOptions options{};
    options.textureFormat = format;
    options.paletteFormat = palette;
    options.hasGlobalIndex = hasGlobalIndex;
    options.globalIndex = globalIndex;
    return spice::gvm::encoding::encodeGvr(image, options);
}

std::vector<std::uint8_t> makeTexturedMld(
    const std::vector<std::uint8_t>& firstTexture,
    const std::vector<std::uint8_t>& secondTexture,
    bool addSuffix = false) {
    auto bytes = makeMinimalMld(Endian::Big);
    bytes.resize(kTextureTable);
    appendU32(bytes, 2U, Endian::Big);
    appendNameRecord(bytes, "tex_a");
    appendNameRecord(bytes, "tex_b");
    bytes.insert(bytes.end(), firstTexture.begin(), firstTexture.end());
    bytes.insert(bytes.end(), secondTexture.begin(), secondTexture.end());
    if (addSuffix) {
        bytes.push_back('T');
        bytes.push_back('A');
        bytes.push_back('I');
        bytes.push_back('L');
    }
    return bytes;
}

spice::pvm::model::RgbaImage makePvrImage(
    const std::uint32_t width, const std::uint32_t height, const std::uint8_t bias = 0U) {
    spice::pvm::model::RgbaImage image{};
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(width) * height * 4U);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto offset = (static_cast<std::size_t>(y) * width + x) * 4U;
            image.pixels[offset + 0U] = static_cast<std::uint8_t>(x * 17U + bias);
            image.pixels[offset + 1U] = static_cast<std::uint8_t>(y * 19U + bias);
            image.pixels[offset + 2U] = static_cast<std::uint8_t>((x + y) * 13U + bias);
            image.pixels[offset + 3U] = 0xFFU;
        }
    }
    return image;
}

std::vector<std::uint8_t> encodePvrTexture(
    const std::uint32_t size, const std::uint32_t globalIndex, const std::uint8_t bias = 0U) {
    spice::pvm::encoding::PvrEncodeOptions options{};
    options.pixelFormat = spice::pvm::model::PixelFormat::Rgb565;
    options.dataLayout = spice::pvm::model::DataLayout::Twiddled;
    options.includeGlobalIndex = true;
    options.globalIndex = globalIndex;
    const auto result = spice::pvm::encoding::encodePvrTexture(makePvrImage(size, size, bias), options);
    EXPECT_TRUE(result.ok());
    return result.bytes;
}

std::vector<std::uint8_t> makeDreamcastTexturedMld(
    const std::vector<std::vector<std::uint8_t>>& textures,
    const bool alignTextures) {
    auto bytes = makeMinimalMld(Endian::Little);
    bytes.resize(kTextureTable);
    appendU32(bytes, static_cast<std::uint32_t>(textures.size()), Endian::Little);
    for (std::size_t i = 0; i < textures.size(); ++i)
        appendNameRecord(bytes, "dc_tex_" + std::to_string(i));

    for (std::size_t i = 0; i < textures.size(); ++i) {
        const auto record = kTextureTable + 4U + i * 44U;
        const auto blockStart = bytes.size();
        const auto prefixSize = alignTextures ? ((32U - (blockStart & 31U)) & 31U) : 0U;
        if (alignTextures) {
            writeU32(bytes, record + 36U, 0x80000000U, Endian::Little);
            bytes.resize(bytes.size() + prefixSize, 0xCCU);
        }
        bytes.insert(bytes.end(), textures[i].begin(), textures[i].end());
        writeU32(bytes, record + 40U,
            static_cast<std::uint32_t>(textures[i].size()), Endian::Little);
    }
    return bytes;
}

} // namespace

TEST(MldEndian, ParsesBigAndLittleEndianFixturesToEquivalentIr) {
    MldParser parser;
    const auto be = parser.parseFile(makeMinimalMld(Endian::Big));
    const auto le = parser.parseFile(makeMinimalMld(Endian::Little));

    ASSERT_EQ(be.entries.size(), 1U);
    ASSERT_EQ(le.entries.size(), 1U);
    EXPECT_EQ(be.endian, Endian::Big);
    EXPECT_EQ(le.endian, Endian::Little);
    EXPECT_EQ(be.header.entryCount, le.header.entryCount);
    EXPECT_EQ(be.entries[0].entry.entryId, le.entries[0].entry.entryId);
    EXPECT_EQ(be.entries[0].entry.fxnName, le.entries[0].entry.fxnName);
    EXPECT_FLOAT_EQ(be.entries[0].entry.transform.position.y, le.entries[0].entry.transform.position.y);
    ASSERT_TRUE(be.entries[0].entry.groundAddresses);
    ASSERT_TRUE(le.entries[0].entry.groundAddresses);
    EXPECT_EQ(be.entries[0].entry.groundAddresses->values, le.entries[0].entry.groundAddresses->values);
}

TEST(MldResourceHealth, ParsesDirectTextureListsInBothEndianModesWithoutObjectWarnings) {
    for (const auto endian : {Endian::Big, Endian::Little}) {
        for (const auto tag : {"NJTL", "GJTL"}) {
            const auto source = makeDirectTextureListMld(endian, tag);
            const auto file = MldParser{}.parseFile(source);
            EXPECT_EQ(file.parseStatus, spice::mld::model::MldParseStatus::Complete);
            EXPECT_EQ(file.assetStatus, spice::mld::model::MldResourceStatus::Complete);
            EXPECT_TRUE(file.objectResources.empty());
            ASSERT_EQ(file.textureListResources.size(), 1U);
            const auto& resource = file.textureListResources.at(0x180U);
            EXPECT_EQ(resource.layout, tag[0] == 'N'
                ? spice::mld::model::MldTextureListLayout::Njtl
                : spice::mld::model::MldTextureListLayout::Gjtl);
            EXPECT_EQ(resource.status, spice::mld::model::MldResourceStatus::Complete);
            EXPECT_EQ(resource.listRange.offset, 0x180U);
            EXPECT_EQ(resource.listRange.size, 0x38U);
            EXPECT_EQ(resource.sourceBytes.size(), resource.listRange.size);
            ASSERT_EQ(resource.entries.size(), 1U);
            EXPECT_EQ(resource.entries[0].name, "deck_a");
            EXPECT_EQ(resource.entries[0].rawNamePointer, 0x20U);
            EXPECT_EQ(resource.entries[0].recordRange.offset, 0x190U);
            EXPECT_EQ(resource.entries[0].rawRecordBytes.size(), 12U);
            const auto projected = MldParser{}.project(file);
            ASSERT_EQ(projected.entryList.size(), 1U);
            EXPECT_EQ(projected.entryList[0].textureNames,
                (std::vector<std::string>{"deck_a"}));
            const auto written = MldFileWriter{}.write(file);
            ASSERT_TRUE(written.ok());
            EXPECT_EQ(written.bytes, source);
        }
    }
}

TEST(MldResourceHealth, ParsesWrappersCountedRecordsAndSharedTexturePointers) {
    for (const auto endian : {Endian::Big, Endian::Little}) {
        const auto wrapped = MldParser{}.parseFile(makeWrappedTextureListMld(endian));
        ASSERT_EQ(wrapped.textureListResources.size(), 1U);
        const auto& wrapper = wrapped.textureListResources.at(0x180U);
        EXPECT_EQ(wrapper.layout, spice::mld::model::MldTextureListLayout::Wrapper);
        ASSERT_TRUE(wrapper.wrapperRange.has_value());
        EXPECT_EQ(wrapper.wrapperRange->offset, 0x180U);
        EXPECT_EQ(wrapper.resolvedListOffset, 0x190U);
        EXPECT_EQ(wrapper.sourceBytes.size(), 0x48U);
        ASSERT_EQ(wrapper.entries.size(), 1U);
        EXPECT_EQ(wrapper.entries[0].name, "deck_a");
        EXPECT_TRUE(wrapped.objectResources.empty());

        const auto counted = MldParser{}.parseFile(makeCountedTextureListMld(endian));
        ASSERT_EQ(counted.textureListResources.size(), 1U);
        const auto& table = counted.textureListResources.at(0x180U);
        EXPECT_EQ(table.layout, spice::mld::model::MldTextureListLayout::CountedRecords);
        EXPECT_EQ(table.status, spice::mld::model::MldResourceStatus::Complete);
        ASSERT_EQ(table.entries.size(), 1U);
        EXPECT_EQ(table.entries[0].name, "counted");
        EXPECT_EQ(table.entries[0].rawNamePointer, 0x1A0U);

        const auto shared = MldParser{}.parseFile(
            makeDirectTextureListMld(endian, "NJTL", false, true));
        ASSERT_EQ(shared.entries.size(), 2U);
        EXPECT_EQ(shared.textureListResources.size(), 1U);
        const auto projection = MldParser{}.project(shared);
        ASSERT_EQ(projection.entryList.size(), 2U);
        EXPECT_EQ(projection.entryList[0].textureNames,
            (std::vector<std::string>{"deck_a"}));
        EXPECT_EQ(projection.entryList[1].textureNames,
            projection.entryList[0].textureNames);
    }
}

TEST(MldResourceHealth, ResourceFailureDoesNotChangeStructuralParseStatus) {
    const auto source = makeDirectTextureListMld(Endian::Big, "NJTL", true);
    const auto file = MldParser{}.parseFile(source);
    EXPECT_EQ(file.parseStatus, spice::mld::model::MldParseStatus::Complete);
    EXPECT_TRUE(std::none_of(file.parseDiagnostics.begin(), file.parseDiagnostics.end(),
        [](const auto& diagnostic) {
            return diagnostic.severity != spice::mld::model::MldDiagnostic::Severity::Info;
        }));
    EXPECT_EQ(file.assetStatus, spice::mld::model::MldResourceStatus::Partial);
    const auto& resource = file.textureListResources.at(0x180U);
    EXPECT_EQ(resource.status, spice::mld::model::MldResourceStatus::Partial);
    ASSERT_EQ(resource.diagnostics.size(), 1U);
    EXPECT_EQ(resource.diagnostics[0].scope,
        spice::mld::model::MldDiagnosticScope::Resource);
    EXPECT_EQ(resource.diagnostics[0].resourceKind,
        spice::mld::model::MldResourceKind::TextureList);
    EXPECT_EQ(resource.diagnostics[0].sourceOffset, 0x190U);
    EXPECT_FALSE(MldParser{}.project(file).diagnostics.empty());
    const auto written = MldFileWriter{}.write(file);
    ASSERT_TRUE(written.ok());
    EXPECT_EQ(written.bytes, source);
}

TEST(MldEndian, PreservesFullMotionAddressSlotListWithZeroEntries) {
    auto bytes = makeMinimalMld(Endian::Big);
    bytes.resize(0x220U, 0U);
    const std::vector<std::uint32_t> motions{ 0U, 0x180U, 0U, 0x1C0U };
    writeList(bytes, kListMotions, motions, Endian::Big);
    writeTag(bytes, 0x180U, "NJCM");
    writeTag(bytes, 0x1C0U, "NJCM");

    MldParser parser;
    const auto parsedFile = parser.parseFile(bytes);
    ASSERT_EQ(parsedFile.entries.size(), 1U);
    ASSERT_TRUE(parsedFile.entries[0].entry.motionAddresses);
    EXPECT_EQ(parsedFile.entries[0].entry.motionAddresses->values, motions);
    EXPECT_EQ(parsedFile.entries[0].entry.motionCount, 2U);

    spice::mld::parsing::ParseOptions options{};
    options.buildBlenderIntermediateIr = false;
    const auto parsed = parser.parse(bytes, options);
    ASSERT_EQ(parsed.entryList.size(), 1U);
    EXPECT_EQ(parsed.entryList[0].motionAddresses, motions);
    EXPECT_EQ(parsed.entryList[0].motionCount, 2U);
    ASSERT_EQ(parsed.rawEntries.size(), 1U);
    EXPECT_EQ(parsed.rawEntries[0].motionAddresses, motions);
}

TEST(MldParser, ResolvesWrappedObjectModelOffsetBeforeFallbackScan) {
    auto bytes = makeMinimalMld(Endian::Big);
    bytes.resize(0x300U, 0U);
    const std::uint32_t objects[] = { 0x180U };
    writeList(bytes, kListObjects, objects, Endian::Big);

    writeU32(bytes, 0x180U, 0x40U, Endian::Big);
    writeU32(bytes, 0x184U, 0x80U, Endian::Big);
    writeU32(bytes, 0x188U, 0x10U, Endian::Big);
    writeU32(bytes, kEntryOffset + 0x20U, 0x190U, Endian::Big);
    writeTag(bytes, 0x190U, "NJTL");
    writeU32(bytes, 0x194U, 0x20U, Endian::Big);
    writeU32(bytes, 0x19CU, 0U, Endian::Big);
    writeTag(bytes, 0x1A0U, "NJCM");
    writeTag(bytes, 0x1C0U, "NJCM");

    MldParser parser;
    spice::mld::parsing::ParseOptions options{};
    options.buildBlenderIntermediateIr = false;
    const auto blocks = parser.extractNjBlocks(bytes, options);

    const auto found = std::find_if(blocks.begin(), blocks.end(), [](const auto& block) {
        return block.sourceObjectAddress == 0x180U;
    });
    ASSERT_NE(found, blocks.end());
    ASSERT_TRUE(found->modelBlockOffset.has_value());
    ASSERT_TRUE(found->modelReadOffset.has_value());
    ASSERT_TRUE(found->textureListOffset.has_value());
    EXPECT_EQ(*found->modelBlockOffset, 0x1C0U);
    EXPECT_EQ(*found->modelReadOffset, 0x40U);
    EXPECT_EQ(*found->textureListOffset, 0x190U);
    EXPECT_EQ(found->wrapperLayout, "mld-object-wrapper");

    const auto file = parser.parseFile(bytes);
    EXPECT_TRUE(file.textureListResources.contains(0x190U));
    EXPECT_FALSE(file.objectResources.contains(0x190U));
    EXPECT_TRUE(file.objectResources.contains(0x180U));
}

TEST(MldParser, ParsesSyntheticWrappedMldObjectIntoBlenderIrGeometry) {
    const auto bytes = makeWrappedObjectMldWithTriangleGeometry();

    MldParser parser;
    const auto parsed = parser.parse(bytes);

    ASSERT_EQ(parsed.entryList.size(), 1U);
    ASSERT_EQ(parsed.entryList[0].objectAddresses.size(), 1U);
    EXPECT_EQ(parsed.entryList[0].objectAddresses[0], 0x180U);

    const auto found = std::find_if(parsed.extractedNjBlocks.begin(), parsed.extractedNjBlocks.end(), [](const auto& block) {
        return block.sourceObjectAddress == 0x180U;
    });
    ASSERT_NE(found, parsed.extractedNjBlocks.end());
    ASSERT_TRUE(found->modelBlockOffset.has_value());
    ASSERT_TRUE(found->modelReadOffset.has_value());
    EXPECT_EQ(*found->modelBlockOffset, 0x1C0U);
    EXPECT_EQ(*found->modelReadOffset, 0x40U);
    EXPECT_EQ(found->wrapperLayout, "mld-object-wrapper");

    ASSERT_TRUE(parsed.blenderIrScene.has_value());
    EXPECT_GT(parsed.blenderIrScene->objectTrees.size(), 0U);
    EXPECT_GT(parsed.blenderIrScene->meshes.size(), 0U);
    ASSERT_EQ(parsed.blenderIrScene->indexEntries.size(), 1U);
    EXPECT_FALSE(parsed.blenderIrScene->indexEntries[0].objectTreeIndices.empty());
    EXPECT_FALSE(parsed.blenderIrScene->indexEntries[0].meshIndices.empty());
}

TEST(MldParser, BuildsUniqueNodeAndCameraRelationsWithoutSelectingCameraBinding) {
    constexpr std::array<std::uint32_t, 1> objects{0x180U};
    constexpr std::array motions{
        std::pair<std::uint32_t, const char*>{0x300U, "NMDM"},
        std::pair<std::uint32_t, const char*>{0x340U, "NCAM"},
        std::pair<std::uint32_t, const char*>{0x380U, "NSSM"},
    };
    const auto bytes = makeWrappedObjectMldWithMotions(objects, motions);
    MldParser parser{};
    const auto file = parser.parseFile(bytes);

    ASSERT_EQ(file.motionRelations.size(), 3U);
    EXPECT_EQ(file.motionRelations[0].motionSlot, 0U);
    EXPECT_EQ(file.motionRelations[0].motionKind, spice::modeling::File::NinjaMotionKind::Node);
    EXPECT_EQ(file.motionRelations[0].scope,
        spice::mld::model::MldMotionRelationScope::SameEntryObjectList);
    EXPECT_EQ(file.motionRelations[0].status, spice::mld::model::MldMotionRelationStatus::Unique);
    ASSERT_EQ(file.motionRelations[0].targetCandidates.size(), 1U);
    EXPECT_TRUE(file.motionRelations[0].targetCandidates[0].compatible);
    EXPECT_EQ(file.motionRelations[1].motionKind, spice::modeling::File::NinjaMotionKind::Camera);
    EXPECT_EQ(file.motionRelations[1].status, spice::mld::model::MldMotionRelationStatus::Camera);
    EXPECT_EQ(file.motionRelations[1].scope,
        spice::mld::model::MldMotionRelationScope::NoObjectTarget);
    EXPECT_TRUE(file.motionRelations[1].targetCandidates.empty());
    EXPECT_EQ(file.motionRelations[2].motionKind, spice::modeling::File::NinjaMotionKind::Shape);
    EXPECT_EQ(file.motionRelations[2].status, spice::mld::model::MldMotionRelationStatus::Unique);
    ASSERT_EQ(file.animationBindings.size(), 2U);
    EXPECT_EQ(file.animationBindings[0].motionAddress, 0x300U);
    EXPECT_EQ(file.animationBindings[0].scope,
        spice::mld::model::MldMotionRelationScope::SameEntryObjectList);
    EXPECT_FALSE(file.animationBindings[0].shortRot);

    const auto nodeMotion = file.motionResources.find(0x300U);
    const auto cameraMotion = file.motionResources.find(0x340U);
    const auto shapeMotion = file.motionResources.find(0x380U);
    ASSERT_NE(nodeMotion, file.motionResources.end());
    ASSERT_NE(cameraMotion, file.motionResources.end());
    ASSERT_NE(shapeMotion, file.motionResources.end());
    ASSERT_EQ(nodeMotion->second.variants.size(), 1U);
    ASSERT_EQ(cameraMotion->second.variants.size(), 1U);
    ASSERT_EQ(shapeMotion->second.variants.size(), 1U);
    ASSERT_EQ(shapeMotion->second.variants[0].targetLayout.lanes.size(), 1U);
    ASSERT_TRUE(shapeMotion->second.variants[0].targetLayout.lanes[0].vertex_count.has_value());
    ASSERT_TRUE(shapeMotion->second.variants[0].targetLayout.lanes[0].normal_count.has_value());
    EXPECT_EQ(*shapeMotion->second.variants[0].targetLayout.lanes[0].vertex_count, 3U);
    EXPECT_EQ(*shapeMotion->second.variants[0].targetLayout.lanes[0].normal_count, 3U);
    EXPECT_FALSE(nodeMotion->second.variants[0].shortRot);
    EXPECT_EQ(nodeMotion->second.structure.status, spice::modeling::File::NinjaMotionParseStatus::Complete);
    EXPECT_TRUE(nodeMotion->second.structure.pof0_range.has_value());

    const auto projected = parser.parse(bytes);
    ASSERT_EQ(projected.animations.size(), 2U);
    EXPECT_EQ(projected.animations[0].sourceMotionAddress, 0x300U);
    ASSERT_TRUE(projected.blenderIrScene.has_value());
    ASSERT_EQ(projected.blenderIrScene->animations.size(), 2U);
    EXPECT_EQ(projected.blenderIrScene->animations[0].sourceMotionAddress, 0x300U);
}

TEST(MldParser, RetainsDuplicateCompatibleObjectSlotsAsAmbiguousCandidates) {
    constexpr std::array<std::uint32_t, 2> objects{0x180U, 0x180U};
    constexpr std::array motions{
        std::pair<std::uint32_t, const char*>{0x300U, "NMDM"},
    };
    const auto file = MldParser{}.parseFile(makeWrappedObjectMldWithMotions(objects, motions));

    ASSERT_EQ(file.motionRelations.size(), 1U);
    const auto& relation = file.motionRelations[0];
    EXPECT_EQ(relation.status, spice::mld::model::MldMotionRelationStatus::Ambiguous);
    EXPECT_EQ(relation.scope, spice::mld::model::MldMotionRelationScope::SameEntryObjectList);
    ASSERT_EQ(relation.targetCandidates.size(), 2U);
    EXPECT_EQ(relation.targetCandidates[0].objectSlot, 0U);
    EXPECT_EQ(relation.targetCandidates[1].objectSlot, 1U);
    EXPECT_EQ(relation.targetCandidates[0].objectAddress, relation.targetCandidates[1].objectAddress);
    ASSERT_TRUE(relation.targetCandidates[0].motionVariantIndex.has_value());
    ASSERT_TRUE(relation.targetCandidates[1].motionVariantIndex.has_value());
    EXPECT_EQ(relation.targetCandidates[0].motionVariantIndex, relation.targetCandidates[1].motionVariantIndex);
    EXPECT_TRUE(file.animationBindings.empty());
    ASSERT_EQ(file.motionResources.at(0x300U).variants.size(), 1U);
}

TEST(MldParser, DoesNotUseCompatibleObjectsFromAnotherEntry) {
    constexpr std::array<std::uint32_t, 1> objects{0x180U};
    constexpr std::array motions{
        std::pair<std::uint32_t, const char*>{0x300U, "NMDM"},
    };
    auto bytes = makeWrappedObjectMldWithMotions(objects, motions);
    bytes.resize(0x470U, 0U);
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(kEntryOffset), kEntrySize,
        bytes.begin() + static_cast<std::ptrdiff_t>(kEntryOffset + kEntrySize));
    writeU32(bytes, 0x00U, 2U, Endian::Big);

    constexpr std::size_t kFirstObjects = 0x440U;
    constexpr std::size_t kFirstMotions = 0x448U;
    constexpr std::size_t kSecondObjects = 0x454U;
    constexpr std::size_t kSecondMotions = 0x460U;
    const std::array<std::uint32_t, 0> empty{};
    writeList(bytes, kFirstObjects, empty, Endian::Big);
    writeList(bytes, kFirstMotions, std::array<std::uint32_t, 1>{0x300U}, Endian::Big);
    writeList(bytes, kSecondObjects, objects, Endian::Big);
    writeList(bytes, kSecondMotions, empty, Endian::Big);
    writeU32(bytes, kEntryOffset + 0x14U, static_cast<std::uint32_t>(kFirstObjects), Endian::Big);
    writeU32(bytes, kEntryOffset + 0x1CU, static_cast<std::uint32_t>(kFirstMotions), Endian::Big);
    const auto secondEntry = kEntryOffset + kEntrySize;
    writeU32(bytes, secondEntry + 0x00U, 0x303U, Endian::Big);
    writeU32(bytes, secondEntry + 0x14U, static_cast<std::uint32_t>(kSecondObjects), Endian::Big);
    writeU32(bytes, secondEntry + 0x1CU, static_cast<std::uint32_t>(kSecondMotions), Endian::Big);

    const auto file = MldParser{}.parseFile(bytes);
    ASSERT_EQ(file.entries.size(), 2U);
    ASSERT_EQ(file.motionRelations.size(), 1U);
    const auto& relation = file.motionRelations[0];
    EXPECT_EQ(relation.tableIndex, 0U);
    EXPECT_EQ(relation.scope, spice::mld::model::MldMotionRelationScope::SameEntryObjectList);
    EXPECT_EQ(relation.status, spice::mld::model::MldMotionRelationStatus::TargetUnavailable);
    EXPECT_TRUE(relation.targetCandidates.empty());
    EXPECT_TRUE(file.animationBindings.empty());
    EXPECT_NE(file.objectResources.find(0x180U), file.objectResources.end());
}

TEST(MldParser, UsesStructuralOnlyScopeForUnknownMotionKinds) {
    constexpr std::array<std::uint32_t, 1> objects{0x180U};
    constexpr std::array motions{
        std::pair<std::uint32_t, const char*>{0x300U, "ABCD"},
    };
    const auto file = MldParser{}.parseFile(makeWrappedObjectMldWithMotions(objects, motions));

    ASSERT_EQ(file.motionRelations.size(), 1U);
    EXPECT_EQ(file.motionRelations[0].motionKind, spice::modeling::File::NinjaMotionKind::Unknown);
    EXPECT_EQ(file.motionRelations[0].scope, spice::mld::model::MldMotionRelationScope::StructuralOnly);
    EXPECT_EQ(file.motionRelations[0].status,
        spice::mld::model::MldMotionRelationStatus::NoCompatibleTarget);
    EXPECT_TRUE(file.motionRelations[0].targetCandidates.empty());
    EXPECT_TRUE(file.animationBindings.empty());
}

TEST(Sa3dBlenderIrBuilder, ProjectsNcamPositionAndTargetUnderOwningEntry) {
    spice::mld::parsing::ParseResult parsed{};
    parsed.coordinatePolicy.swapYZ = true;
    parsed.coordinatePolicy.negateX = true;
    parsed.coordinatePolicy.uniformScale = 2.0F;
    spice::mld::parsing::ParsedRawEntry entry{};
    entry.sourceEntryId = 12U;
    entry.tableIndex = 7U;
    parsed.rawEntries.push_back(entry);

    auto motion = std::make_shared<spice::modeling::Animation::Motion>();
    motion->declared_frame_count = 20U;
    motion->interpolation_mode = spice::modeling::Animation::InterpolationMode::Linear;
    motion->keyframes[0U].position.emplace(3U, spice::modeling::Structs::Vector3{1.0F, 2.0F, 3.0F});
    motion->keyframes[0U].target.emplace(5U, spice::modeling::Structs::Vector3{4.0F, 5.0F, 6.0F});
    motion->keyframes[0U].roll.emplace(5U, 0.25F);
    parsed.cameraMotions.push_back(spice::mld::parsing::ParsedMldCameraMotion{
        .sourceEntryId = 12U,
        .tableIndex = 7U,
        .sourceMotionAddress = 0x340U,
        .motionSlot = 2U,
        .motion = motion,
    });

    const auto scene = spice::mld::parsing::Sa3dBlenderIrBuilder{}.build(parsed);
    ASSERT_EQ(scene.indexEntries.size(), 1U);
    ASSERT_EQ(scene.indexEntries[0].cameraMotions.size(), 1U);
    const auto& camera = scene.indexEntries[0].cameraMotions[0];
    EXPECT_EQ(camera.sourceMotionAddress, 0x340U);
    EXPECT_EQ(camera.motionSlot, 2U);
    EXPECT_EQ(camera.frameCount, 20U);
    EXPECT_EQ(camera.interpolationMode, "linear");
    ASSERT_EQ(camera.position.size(), 1U);
    EXPECT_EQ(camera.position[0].frame, 3U);
    EXPECT_FLOAT_EQ(camera.position[0].value.x, -2.0F);
    EXPECT_FLOAT_EQ(camera.position[0].value.y, 6.0F);
    EXPECT_FLOAT_EQ(camera.position[0].value.z, 4.0F);
    ASSERT_EQ(camera.target.size(), 1U);
    EXPECT_FLOAT_EQ(camera.target[0].value.x, -8.0F);
    EXPECT_FLOAT_EQ(camera.target[0].value.y, 12.0F);
    EXPECT_FLOAT_EQ(camera.target[0].value.z, 10.0F);
    ASSERT_EQ(camera.unsupportedChannels.size(), 1U);
    EXPECT_EQ(camera.unsupportedChannels[0].channel, "roll");
    EXPECT_TRUE(scene.animations.empty());

    const auto json = spice::mld::exporting::BlenderIrJsonExporter{}.toJson(scene);
    EXPECT_NE(json.find("\"cameraMotions\":[{\"sourceMotionAddress\":832,\"motionSlot\":2"), std::string::npos);
    EXPECT_NE(json.find("\"position\":[{\"frame\":3,\"value\":[-2,6,4]}]"), std::string::npos);
    EXPECT_NE(json.find("\"target\":[{\"frame\":5,\"value\":[-8,12,10]}]"), std::string::npos);
}

TEST(Sa3dBlenderIrBuilder, OmitsNcamWithoutBothSpatialChannels) {
    spice::mld::parsing::ParseResult parsed{};
    spice::mld::parsing::ParsedRawEntry entry{};
    entry.sourceEntryId = 12U;
    entry.tableIndex = 7U;
    parsed.rawEntries.push_back(entry);

    auto motion = std::make_shared<spice::modeling::Animation::Motion>();
    motion->keyframes[0U].position.emplace(0U, spice::modeling::Structs::Vector3{1.0F, 2.0F, 3.0F});
    parsed.cameraMotions.push_back(spice::mld::parsing::ParsedMldCameraMotion{
        .sourceEntryId = 12U,
        .tableIndex = 7U,
        .sourceMotionAddress = 0x340U,
        .motionSlot = 2U,
        .motion = motion,
    });

    const auto scene = spice::mld::parsing::Sa3dBlenderIrBuilder{}.build(parsed);
    ASSERT_EQ(scene.indexEntries.size(), 1U);
    EXPECT_TRUE(scene.indexEntries[0].cameraMotions.empty());
    EXPECT_TRUE(std::any_of(scene.diagnostics.begin(), scene.diagnostics.end(), [](const auto& message) {
        return message.find("NCAM at 0x00000340") != std::string::npos &&
            message.find("position or target") != std::string::npos;
    }));
}

TEST(MldParser, ProjectsType56AsNodeAuxiliaryGeometryOnly) {
    constexpr std::size_t kPolyChunkOffset = 0x288U;
    auto bytes = makeWrappedObjectMldWithTriangleGeometry();
    writeU16(bytes, kPolyChunkOffset + 0x00U, 0x0038U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x02U, 5U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x04U, 0x4001U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x06U, 0U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x08U, 1U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x0AU, 2U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x0CU, 0xBEEFU, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x0EU, 0x00FFU, Endian::Little);

    const auto parsed = MldParser{}.parse(bytes);
    ASSERT_TRUE(parsed.blenderIrScene.has_value());
    const auto& scene = *parsed.blenderIrScene;
    ASSERT_EQ(scene.objectTrees.size(), 1U);
    ASSERT_EQ(scene.objectTrees[0].nodes.size(), 1U);
    ASSERT_EQ(scene.objectTrees[0].nodes[0].auxiliaryGeometry.size(), 1U);
    const auto& auxiliary = scene.objectTrees[0].nodes[0].auxiliaryGeometry[0];
    EXPECT_EQ(auxiliary.role, spice::mld::model::BlenderIrAuxiliaryGeometryRole::Type56ShadowVolume);
    EXPECT_EQ(auxiliary.sourceChunkType, 56U);
    EXPECT_FALSE(auxiliary.fromCacheReplay);
    ASSERT_EQ(auxiliary.vertices.size(), 3U);
    ASSERT_EQ(auxiliary.triangles.size(), 1U);
    EXPECT_EQ(auxiliary.triangles[0].sourceIndices,
        (std::array<std::uint16_t, 3>{0U, 1U, 2U}));
    EXPECT_EQ(auxiliary.triangles[0].userWords, (std::vector<std::uint16_t>{0xBEEFU}));
    EXPECT_TRUE(scene.meshes.empty());

    const auto json = spice::mld::exporting::BlenderIrJsonExporter{}.toJson(scene);
    EXPECT_NE(json.find("\"role\":\"type56ShadowVolume\""), std::string::npos);
    EXPECT_NE(json.find("\"sourceIndices\":[0,1,2]"), std::string::npos);
}

TEST(MldParser, MarksType56DrawListProjectionAsCacheReplay) {
    constexpr std::size_t kPolyChunkOffset = 0x288U;
    auto bytes = makeWrappedObjectMldWithTriangleGeometry();
    writeU16(bytes, kPolyChunkOffset + 0x00U, 0x0004U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x02U, 0x0038U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x04U, 4U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x06U, 1U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x08U, 0U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x0AU, 1U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x0CU, 2U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x0EU, 0x0005U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x10U, 0x00FFU, Endian::Little);

    const auto parsed = MldParser{}.parse(bytes);
    ASSERT_TRUE(parsed.blenderIrScene.has_value());
    const auto& scene = *parsed.blenderIrScene;
    ASSERT_EQ(scene.objectTrees.size(), 1U);
    ASSERT_EQ(scene.objectTrees[0].nodes[0].auxiliaryGeometry.size(), 1U);
    const auto& auxiliary = scene.objectTrees[0].nodes[0].auxiliaryGeometry[0];
    EXPECT_TRUE(auxiliary.fromCacheReplay);
    EXPECT_EQ(auxiliary.sourceChunkOffset, kPolyChunkOffset + 2U);
    EXPECT_EQ(auxiliary.triangles.size(), 1U);
}

TEST(MldParser, KeepsType57AndType58OutOfAuxiliaryBlenderGeometry) {
    constexpr std::size_t kPolyChunkOffset = 0x288U;
    for (const auto type : {0x0039U, 0x003AU}) {
        auto bytes = makeWrappedObjectMldWithTriangleGeometry();
        writeU16(bytes, kPolyChunkOffset + 0x00U, static_cast<std::uint16_t>(type), Endian::Little);
        writeU16(bytes, kPolyChunkOffset + 0x02U, 5U, Endian::Little);
        writeU16(bytes, kPolyChunkOffset + 0x04U, 1U, Endian::Little);
        if (type == 0x0039U) {
            writeU16(bytes, kPolyChunkOffset + 0x06U, 0U, Endian::Little);
            writeU16(bytes, kPolyChunkOffset + 0x08U, 1U, Endian::Little);
            writeU16(bytes, kPolyChunkOffset + 0x0AU, 2U, Endian::Little);
            writeU16(bytes, kPolyChunkOffset + 0x0CU, 3U, Endian::Little);
        } else {
            writeU16(bytes, kPolyChunkOffset + 0x06U, 3U, Endian::Little);
            writeU16(bytes, kPolyChunkOffset + 0x08U, 0U, Endian::Little);
            writeU16(bytes, kPolyChunkOffset + 0x0AU, 1U, Endian::Little);
            writeU16(bytes, kPolyChunkOffset + 0x0CU, 2U, Endian::Little);
        }
        writeU16(bytes, kPolyChunkOffset + 0x0EU, 0x00FFU, Endian::Little);

        const auto parsed = MldParser{}.parse(bytes);
        ASSERT_TRUE(parsed.blenderIrScene.has_value());
        ASSERT_EQ(parsed.blenderIrScene->objectTrees.size(), 1U);
        EXPECT_TRUE(parsed.blenderIrScene->objectTrees[0].nodes[0].auxiliaryGeometry.empty());
    }
}

TEST(MldParser, RejectsOnlyUnresolvedType56PrimitivesWithoutZeroSubstitution) {
    constexpr std::size_t kPolyChunkOffset = 0x288U;
    auto bytes = makeWrappedObjectMldWithTriangleGeometry();
    writeU16(bytes, kPolyChunkOffset + 0x00U, 0x0038U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x02U, 4U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x04U, 1U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x06U, 0U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x08U, 1U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x0AU, 9U, Endian::Little);
    writeU16(bytes, kPolyChunkOffset + 0x0CU, 0x00FFU, Endian::Little);

    const auto parsed = MldParser{}.parse(bytes);
    ASSERT_TRUE(parsed.blenderIrScene.has_value());
    const auto& scene = *parsed.blenderIrScene;
    ASSERT_EQ(scene.objectTrees.size(), 1U);
    ASSERT_EQ(scene.objectTrees[0].nodes.size(), 1U);
    EXPECT_TRUE(scene.objectTrees[0].nodes[0].auxiliaryGeometry.empty());
    EXPECT_TRUE(std::any_of(scene.diagnostics.begin(), scene.diagnostics.end(), [](const auto& message) {
        return message.find("Type-56 projection omitted unresolved primitive") != std::string::npos &&
            message.find("chunk 0x00000288") != std::string::npos &&
            message.find("cache indices=9") != std::string::npos;
    }));
}

TEST(MldParser, PreservesDuplicateMotionSlotsAndSharesTheirDecodedVariant) {
    constexpr std::array<std::uint32_t, 1> objects{0x180U};
    constexpr std::array motions{
        std::pair<std::uint32_t, const char*>{0x300U, "NMDM"},
        std::pair<std::uint32_t, const char*>{0x300U, "NMDM"},
    };
    const auto file = MldParser{}.parseFile(makeWrappedObjectMldWithMotions(objects, motions));

    ASSERT_EQ(file.motionRelations.size(), 2U);
    EXPECT_EQ(file.motionRelations[0].motionSlot, 0U);
    EXPECT_EQ(file.motionRelations[1].motionSlot, 1U);
    EXPECT_EQ(file.motionRelations[0].motionAddress, file.motionRelations[1].motionAddress);
    EXPECT_EQ(file.motionRelations[0].status, spice::mld::model::MldMotionRelationStatus::Unique);
    EXPECT_EQ(file.motionRelations[1].status, spice::mld::model::MldMotionRelationStatus::Unique);
    ASSERT_EQ(file.motionRelations[0].targetCandidates.size(), 1U);
    ASSERT_EQ(file.motionRelations[1].targetCandidates.size(), 1U);
    EXPECT_EQ(
        file.motionRelations[0].targetCandidates[0].motionVariantIndex,
        file.motionRelations[1].targetCandidates[0].motionVariantIndex);
    ASSERT_EQ(file.animationBindings.size(), 2U);
    EXPECT_EQ(file.animationBindings[0].motionSlot, 0U);
    EXPECT_EQ(file.animationBindings[1].motionSlot, 1U);
    ASSERT_EQ(file.motionResources.at(0x300U).variants.size(), 1U);
}

TEST(MldParser, FiltersNoAnimateNodesButRetainsFullTreeIndicesInMotionLanes) {
    constexpr std::uint32_t modelBlockOffset = 0x1C0U;
    constexpr std::uint32_t rootNodeOffset = modelBlockOffset + 0x08U;
    constexpr std::uint32_t childRelativeOffset = 0x110U;
    constexpr std::uint32_t childNodeOffset = modelBlockOffset + childRelativeOffset;
    constexpr std::array<std::uint32_t, 1> objects{0x180U};
    constexpr std::array motions{
        std::pair<std::uint32_t, const char*>{0x400U, "NMDM"},
    };
    auto bytes = makeWrappedObjectMldWithMotions(objects, motions);
    writeU32(bytes, modelBlockOffset + 4U, 0x160U, Endian::Little);
    writeU32(bytes, rootNodeOffset, 1U << 6U, Endian::Little);
    writeSa3dPointer(bytes, rootNodeOffset + 0x2CU, childRelativeOffset);
    writeU32(bytes, childNodeOffset, 0U, Endian::Little);
    writeU32(bytes, childNodeOffset + 4U, 0U, Endian::Little);
    writeF32(bytes, childNodeOffset + 0x20U, 1.0F, Endian::Little);
    writeF32(bytes, childNodeOffset + 0x24U, 1.0F, Endian::Little);
    writeF32(bytes, childNodeOffset + 0x28U, 1.0F, Endian::Little);
    writeU32(bytes, childNodeOffset + 0x2CU, 0U, Endian::Little);
    writeU32(bytes, childNodeOffset + 0x30U, 0U, Endian::Little);

    const auto file = MldParser{}.parseFile(bytes);
    ASSERT_EQ(file.motionRelations.size(), 1U);
    ASSERT_EQ(file.motionRelations[0].targetCandidates.size(), 1U);
    ASSERT_TRUE(file.motionRelations[0].targetCandidates[0].motionVariantIndex.has_value());
    const auto& variant = file.motionResources.at(0x400U).variants.at(
        *file.motionRelations[0].targetCandidates[0].motionVariantIndex);
    ASSERT_EQ(variant.targetLayout.lanes.size(), 1U);
    EXPECT_EQ(variant.targetLayout.lanes[0].node_index, 1U);
    EXPECT_EQ(variant.nodeCount, 1U);
}

TEST(MldParser, RecordsUnavailableSameEntryObjectWithoutGuessingTarget) {
    constexpr std::array<std::uint32_t, 1> objects{0x2F0U};
    constexpr std::array motions{
        std::pair<std::uint32_t, const char*>{0x300U, "NMDM"},
    };
    const auto file = MldParser{}.parseFile(makeWrappedObjectMldWithMotions(objects, motions));

    ASSERT_EQ(file.motionRelations.size(), 1U);
    const auto& relation = file.motionRelations[0];
    EXPECT_EQ(relation.status, spice::mld::model::MldMotionRelationStatus::TargetUnavailable);
    ASSERT_EQ(relation.targetCandidates.size(), 1U);
    EXPECT_EQ(relation.targetCandidates[0].objectSlot, 0U);
    EXPECT_FALSE(relation.targetCandidates[0].targetAvailable);
    EXPECT_FALSE(relation.targetCandidates[0].diagnostic.empty());
    EXPECT_TRUE(file.animationBindings.empty());
}

TEST(MldParser, MarksShapeTargetUnavailableWhenItsAttachRepresentationIsUnsupported) {
    constexpr std::array<std::uint32_t, 1> objects{0x180U};
    constexpr std::array motions{
        std::pair<std::uint32_t, const char*>{0x300U, "NSSM"},
    };
    auto bytes = makeWrappedObjectMldWithMotions(objects, motions);
    writeTag(bytes, 0x1C0U, "NJBM");
    const auto file = MldParser{}.parseFile(bytes);

    ASSERT_EQ(file.motionRelations.size(), 1U);
    const auto& relation = file.motionRelations[0];
    EXPECT_EQ(relation.motionKind, spice::modeling::File::NinjaMotionKind::Shape);
    EXPECT_EQ(relation.status, spice::mld::model::MldMotionRelationStatus::TargetUnavailable);
    ASSERT_EQ(relation.targetCandidates.size(), 1U);
    EXPECT_FALSE(relation.targetCandidates[0].targetAvailable);
    EXPECT_NE(relation.targetCandidates[0].diagnostic.find("supported representation"),
        std::string::npos);
    EXPECT_TRUE(file.animationBindings.empty());
}

TEST(BlenderIrJsonExporter, EmitsWeightedMeshBinding) {
    spice::mld::model::BlenderIrScene scene{};
    spice::mld::model::BlenderIrMesh mesh{};
    mesh.label = "weighted";
    mesh.sourceObjectAddress = 0x1000;
    mesh.sourceChunkOffset = 0x2000;
    mesh.sourceAttachOffset = 0x3000;
    mesh.weightedBinding = spice::mld::model::BlenderIrWeightedBinding{
        .rootNodeIndex = 4,
        .sourceNodeIndex = 7,
        .nodeIndices = {4, 7},
    };

    spice::mld::model::BlenderIrVertex vertex{};
    vertex.hasPosition = true;
    vertex.hasNormal = true;
    vertex.weights.push_back({.boneOrNodeIndex = 4, .weight = 0.25f});
    vertex.weights.push_back({.boneOrNodeIndex = 7, .weight = 0.75f});
    mesh.vertices.push_back(std::move(vertex));

    spice::mld::model::BlenderIrMaterial material{};
    material.materialHash = 1;
    mesh.materials.push_back(material);

    spice::mld::model::BlenderIrTriangleSet triangles{};
    triangles.corners.resize(3);
    mesh.triangleSets.push_back(std::move(triangles));
    scene.meshes.push_back(std::move(mesh));

    const auto json = spice::mld::exporting::BlenderIrJsonExporter{}.toJson(scene);
    EXPECT_NE(json.find("\"weightedBinding\":{\"rootNodeIndex\":4,\"sourceNodeIndex\":7,\"nodeIndices\":[4,7]}"), std::string::npos);
    EXPECT_NE(json.find("\"weights\":[{\"boneOrNodeIndex\":4,\"weight\":0.25},{\"boneOrNodeIndex\":7,\"weight\":0.75}]"), std::string::npos);
}

TEST(BlenderIrJsonExporter, EmitsTblIdAsSignedDecimalNumber) {
    spice::mld::model::BlenderIrScene scene{};
    spice::mld::model::BlenderIrInstance instance{};
    instance.sourceEntryId = 1;
    instance.tableIndex = 0;
    instance.tblId = -42;
    instance.fxnName = "wall";
    scene.indexEntries.push_back(std::move(instance));

    const auto json = spice::mld::exporting::BlenderIrJsonExporter{}.toJson(scene);
    EXPECT_NE(json.find("\"tblId\":-42"), std::string::npos);
    EXPECT_EQ(json.find("\"tblId\":\"-42\""), std::string::npos);
    EXPECT_EQ(json.find("\"tblId\":0x"), std::string::npos);
}

TEST(BlenderIrJsonExporter, EmitsFunctionParametersAsUnsignedDecimalNumbers) {
    spice::mld::model::BlenderIrScene scene{};
    spice::mld::model::BlenderIrInstance instance{};
    instance.functionParameters = {0U, 0xFFFFFFFFU, 0x12345678U};
    scene.indexEntries.push_back(std::move(instance));

    const auto json = spice::mld::exporting::BlenderIrJsonExporter{}.toJson(scene);
    EXPECT_NE(
        json.find("\"functionParameters\":[0,4294967295,305419896]"),
        std::string::npos);
    EXPECT_EQ(json.find("\"functionParameters\":[0x"), std::string::npos);
    EXPECT_EQ(json.find("\"functionParameters\":[-1"), std::string::npos);
}

TEST(Sa3dBlenderIrBuilder, PreservesParsedFunctionParameters) {
    spice::mld::parsing::ParseResult parsed{};
    spice::mld::parsing::ParsedRawEntry entry{};
    entry.sourceEntryId = 9U;
    entry.functionParameters = {0U, 0xFFFFFFFFU, 0x12345678U};
    parsed.rawEntries.push_back(std::move(entry));

    const auto scene = spice::mld::parsing::Sa3dBlenderIrBuilder{}.build(parsed);
    ASSERT_EQ(scene.indexEntries.size(), 1U);
    EXPECT_EQ(
        scene.indexEntries[0].functionParameters,
        (std::vector<std::uint32_t>{0U, 0xFFFFFFFFU, 0x12345678U}));
}

TEST(BlenderIrJsonExporter, EmitsValueBearingAnimationChannels) {
    spice::mld::model::BlenderIrScene scene{};
    spice::mld::model::BlenderIrAnimation animation{};
    animation.sourceEntryId = 4;
    animation.tableIndex = 0;
    animation.sourceObjectAddress = 0x1000;
    animation.sourceMotionAddress = 0x2000;
    animation.motionSlot = 3;
    animation.objectTreeIndex = 1;
    animation.nodeCount = 2;
    animation.frameCount = 23;
    animation.interpolationMode = "linear";

    spice::mld::model::BlenderIrAnimationChannel roll{};
    roll.nodeIndex = 1;
    roll.channel = "roll";
    roll.valueType = "float";
    roll.floatValues.push_back({.frame = 7, .value = 1.25f});
    animation.channels.push_back(std::move(roll));

    spice::mld::model::BlenderIrAnimationChannel vertex{};
    vertex.nodeIndex = 1;
    vertex.channel = "vertex";
    vertex.valueType = "vec3Array";
    vertex.vectorArrayValues.push_back({
        .frame = 9,
        .label = "shape-a",
        .values = {{.x = 1.0f, .y = 2.0f, .z = 3.0f}},
    });
    animation.channels.push_back(std::move(vertex));
    scene.animations.push_back(std::move(animation));

    const auto json = spice::mld::exporting::BlenderIrJsonExporter{}.toJson(scene);
    EXPECT_NE(json.find("\"channels\":[{\"nodeIndex\":1,\"channel\":\"roll\",\"valueType\":\"float\",\"keyframes\":[{\"frame\":7,\"value\":1.25}]"), std::string::npos);
    EXPECT_NE(json.find("\"channel\":\"vertex\",\"valueType\":\"vec3Array\""), std::string::npos);
    EXPECT_NE(json.find("\"frame\":9,\"label\":\"shape-a\",\"values\":[[1,2,3]]"), std::string::npos);
}

TEST(MldEndian, ParsesTblIdAsSignedAndExportsOriginalBits) {
    auto bytes = makeMinimalMld(Endian::Big);
    writeU32(bytes, kEntryOffset + 0x04U, 0xFFFFFFFEU, Endian::Big);

    MldParser parser;
    const auto parsed = parser.parseFile(bytes);
    ASSERT_EQ(parsed.entries.size(), 1U);
    EXPECT_EQ(parsed.entries[0].entry.tblId, -2);

    const auto exported = MldFileExporter{}.exportFile(parsed, MldExportOptions{ .platform = TargetPlatform::GameCube });
    ASSERT_GT(exported.size(), kEntryOffset + 0x07U);
    EXPECT_EQ(exported[kEntryOffset + 0x04U], 0xFFU);
    EXPECT_EQ(exported[kEntryOffset + 0x05U], 0xFFU);
    EXPECT_EQ(exported[kEntryOffset + 0x06U], 0xFFU);
    EXPECT_EQ(exported[kEntryOffset + 0x07U], 0xFEU);
}

TEST(MldEndian, ExportGameCubeToDreamcastPreservesSemanticIrAndFourCcBytes) {
    MldParser parser;
    const auto be = parser.parseFile(makeMinimalMld(Endian::Big));
    const auto out = MldFileExporter{}.exportFile(be, MldExportOptions{ .platform = TargetPlatform::Dreamcast });
    ASSERT_EQ(out[kGrndOffset + 0U], 'G');
    ASSERT_EQ(out[kGrndOffset + 1U], 'R');
    ASSERT_EQ(out[kGrndOffset + 2U], 'N');
    ASSERT_EQ(out[kGrndOffset + 3U], 'D');
    EXPECT_EQ(out[0], 0x01U);
    EXPECT_EQ(out[1], 0x00U);
    EXPECT_EQ(out[kGrndOffset + 4U], 0x2CU);
    EXPECT_EQ(out[kGrndOffset + 5U], 0x00U);

    const auto le = parser.parseFile(out);
    ASSERT_EQ(le.entries.size(), 1U);
    EXPECT_EQ(le.endian, Endian::Little);
    EXPECT_EQ(le.entries[0].entry.entryId, be.entries[0].entry.entryId);
    EXPECT_EQ(le.entries[0].entry.tblId, be.entries[0].entry.tblId);
    EXPECT_EQ(le.entries[0].entry.functionParameters->values, be.entries[0].entry.functionParameters->values);
    EXPECT_FLOAT_EQ(le.entries[0].entry.transform.position.x, be.entries[0].entry.transform.position.x);
}

TEST(MldEndian, AutoDetectChoosesSmallerPlausibleEntryCount) {
    std::vector<std::uint8_t> bytes(7U * 1024U * 1024U, 0U);
    bytes[0] = 0x00U;
    bytes[1] = 0x01U;
    bytes[2] = 0x00U;
    bytes[3] = 0x00U;
    bytes[4] = 0x00U;
    bytes[5] = 0x00U;
    bytes[6] = 0x20U;
    bytes[7] = 0x00U;
    bytes[0x10] = 0xFFU;
    bytes[0x11] = 0xFFU;
    bytes[0x12] = 0xFFU;
    bytes[0x13] = 0xFFU;

    MldParser parser;
    const auto parsed = parser.parseFile(bytes);
    EXPECT_EQ(parsed.endian, Endian::Little);
    EXPECT_EQ(parsed.header.entryCount, 256U);
    EXPECT_EQ(parsed.header.indexTableOffset, 0x200000U);
}

TEST(MldEndian, GameCubeExportCanBeAklzCompressed) {
    MldParser parser;
    const auto file = parser.parseFile(makeMinimalMld(Endian::Big));
    const MldFileExporter exporter;
    const auto uncompressed = exporter.exportFile(file, MldExportOptions{ .platform = TargetPlatform::GameCube });
    const auto compressed = exporter.exportFile(file, MldExportOptions{ .platform = TargetPlatform::GameCube, .compressAklz = true });

    ASSERT_TRUE(spice::compression::aklz::isAklz(compressed));
    const auto decoded = spice::compression::aklz::decompress(compressed);
    ASSERT_TRUE(decoded.ok()) << spice::compression::aklz::errorToString(decoded.error);
    EXPECT_EQ(decoded.bytes, uncompressed);
}

TEST(MldEndian, DreamcastExportRejectsAklzCompression) {
    MldParser parser;
    const auto file = parser.parseFile(makeMinimalMld(Endian::Big));
    const MldFileExporter exporter;

    try {
        (void)exporter.exportFile(file, MldExportOptions{ .platform = TargetPlatform::Dreamcast, .compressAklz = true });
        FAIL() << "Expected AKLZ compression to reject Dreamcast export";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "AKLZ compression is GameCube-only");
    }
}

TEST(MldTextureArchiveRebuild, ReplacesWithLargerTextureAndPreservesNames) {
    const auto small = encodeTexture(makeImage(8U, 8U, 3U), spice::gvm::model::TextureFormat::I4);
    const auto second = encodeTexture(makeImage(8U, 8U, 7U), spice::gvm::model::TextureFormat::RGB565);
    const auto replacement = encodeTexture(makeImage(8U, 8U, 11U), spice::gvm::model::TextureFormat::RGBA8);
    ASSERT_GT(replacement.size(), small.size());

    MldParser parser;
    const auto originalBytes = makeTexturedMld(small, second);
    const auto parsed = parser.parseFile(originalBytes);
    ASSERT_TRUE(parsed.textureArchive.has_value());
    ASSERT_EQ(parsed.textureArchive->entries.size(), 2U);
    EXPECT_EQ(parsed.textureArchive->entries[0].textureName, "tex_a");
    EXPECT_EQ(parsed.textureArchive->entries[1].textureName, "tex_b");
    ASSERT_FALSE(parsed.textureArchive->archivePrefixBytes.empty());

    MldExportOptions options{};
    options.platform = TargetPlatform::GameCube;
    options.textureReplacement = spice::mld::exporting::MldTextureReplacement{
        .textureIndex = 0U,
        .encodedData = replacement,
    };

    const auto rebuilt = MldFileExporter{}.exportFile(parsed, options);
    EXPECT_EQ(rebuilt.size(), originalBytes.size() + replacement.size() - small.size());

    const auto reparsed = parser.parseFile(rebuilt);
    ASSERT_TRUE(reparsed.textureArchive.has_value());
    ASSERT_EQ(reparsed.textureArchive->entries.size(), 2U);
    EXPECT_EQ(reparsed.textureArchive->entries[0].textureName, "tex_a");
    EXPECT_EQ(reparsed.textureArchive->entries[1].textureName, "tex_b");
    EXPECT_EQ(reparsed.textureArchive->entries[0].encodedDataSize, replacement.size());
    EXPECT_EQ(reparsed.textureArchive->entries[1].encodedDataSize, second.size());
    EXPECT_EQ(reparsed.textureArchive->entries[0].sourceFormat, "RGBA8");
    EXPECT_EQ(reparsed.textureArchive->archivePrefixBytes, parsed.textureArchive->archivePrefixBytes);
}

TEST(MldCanonicalTextureWriter, RebuildsEditedTextureArchiveFromCanonicalFile) {
    const auto small = encodeTexture(makeImage(8U, 8U, 3U), spice::gvm::model::TextureFormat::I4);
    const auto second = encodeTexture(makeImage(8U, 8U, 7U), spice::gvm::model::TextureFormat::RGB565);
    const auto replacement = encodeTexture(makeImage(8U, 8U, 11U), spice::gvm::model::TextureFormat::RGBA8);
    ASSERT_GT(replacement.size(), small.size());

    auto file = MldParser{}.parseBytes(makeTexturedMld(small, second));
    ASSERT_TRUE(file.textureArchive.has_value());
    ASSERT_EQ(file.textureArchive->entries.size(), 2U);
    file.textureArchive->entries[0].encodedData = replacement;

    const auto written = MldFileWriter{}.write(file);
    ASSERT_TRUE(written.ok());
    const auto reparsed = MldParser{}.parseBytes(written.bytes);
    ASSERT_TRUE(reparsed.textureArchive.has_value());
    ASSERT_EQ(reparsed.textureArchive->entries.size(), 2U);
    EXPECT_EQ(reparsed.textureArchive->entries[0].textureName, "tex_a");
    EXPECT_EQ(reparsed.textureArchive->entries[0].encodedData, replacement);
    EXPECT_EQ(reparsed.textureArchive->entries[1].textureName, "tex_b");
    EXPECT_EQ(reparsed.textureArchive->entries[1].encodedData, second);
}

TEST(MldCanonicalTextureWriter, RebuildsNameTableWhenAddingTextureEntry) {
    const auto first = encodeTexture(makeImage(8U, 8U, 3U), spice::gvm::model::TextureFormat::I4);
    const auto second = encodeTexture(makeImage(8U, 8U, 7U), spice::gvm::model::TextureFormat::RGB565);
    const auto added = encodeTexture(makeImage(8U, 8U, 13U), spice::gvm::model::TextureFormat::RGB5A3);

    auto file = MldParser{}.parseBytes(makeTexturedMld(first, second));
    ASSERT_TRUE(file.textureArchive.has_value());
    auto addedEntry = file.textureArchive->entries.front();
    addedEntry.archiveTextureIndex = 2U;
    addedEntry.textureName = "tex_c";
    addedEntry.encodedData = added;
    file.textureArchive->entries.push_back(std::move(addedEntry));

    const auto written = MldFileWriter{}.write(file);
    ASSERT_TRUE(written.ok());
    const auto reparsed = MldParser{}.parseBytes(written.bytes);
    ASSERT_TRUE(reparsed.textureArchive.has_value());
    ASSERT_EQ(reparsed.textureArchive->entries.size(), 3U);
    EXPECT_EQ(reparsed.textureArchive->entries[2].textureName, "tex_c");
    EXPECT_EQ(reparsed.textureArchive->entries[2].encodedData, added);
}

TEST(MldDreamcastTextureArchive, ParsesPvrRecordsAndPreservesNoEditBytes) {
    const auto first = encodePvrTexture(8U, 41U, 3U);
    const auto second = encodePvrTexture(8U, 42U, 9U);
    const auto original = makeDreamcastTexturedMld({first, second}, true);

    const auto file = MldParser{}.parseBytes(original);
    ASSERT_EQ(file.sourcePlatform, TargetPlatform::Dreamcast);
    ASSERT_TRUE(file.textureArchive.has_value());
    ASSERT_EQ(file.textureArchive->entries.size(), 2U);
    const auto& firstEntry = file.textureArchive->entries[0];
    EXPECT_EQ(firstEntry.encoding, spice::mld::model::MldTextureEncoding::Pvr);
    EXPECT_EQ(firstEntry.textureName, "dc_tex_0");
    EXPECT_EQ(firstEntry.encodedData, first);
    EXPECT_EQ(firstEntry.rawRecordWord1, 0x80000000U);
    EXPECT_EQ(firstEntry.declaredBlockSize, firstEntry.encodedData.size());
    EXPECT_TRUE(firstEntry.decoded);
    EXPECT_EQ(firstEntry.globalIndex, 41U);
    EXPECT_EQ(firstEntry.encodedDataOffset & 31U, 0U);

    const auto written = MldFileWriter{}.write(file);
    ASSERT_TRUE(written.ok());
    EXPECT_EQ(written.bytes, original);
}

TEST(MldDreamcastTextureArchive, ReplacesRelocatesAndRewritesDeclaredBlockSizes) {
    const auto first = encodePvrTexture(8U, 51U, 3U);
    const auto second = encodePvrTexture(8U, 52U, 9U);
    auto file = MldParser{}.parseBytes(makeDreamcastTexturedMld({first, second}, true));
    ASSERT_TRUE(file.textureArchive.has_value());

    const auto replacement = encodePvrTexture(16U, 151U, 17U);
    file.textureArchive->entries[0].encodedData = replacement;
    const auto written = MldFileWriter{}.write(file);
    ASSERT_TRUE(written.ok()) << (written.diagnostics.empty() ? "" : written.diagnostics.front().message);
    const auto reparsed = MldParser{}.parseBytes(written.bytes);
    ASSERT_TRUE(reparsed.textureArchive.has_value());
    ASSERT_EQ(reparsed.textureArchive->entries.size(), 2U);
    const auto& replaced = reparsed.textureArchive->entries[0];
    EXPECT_EQ(replaced.encodedData, replacement);
    EXPECT_EQ(replaced.globalIndex, 151U);
    EXPECT_EQ(replaced.encodedDataOffset & 31U, 0U);
    EXPECT_EQ(replaced.declaredBlockSize,
        replaced.encodedData.size() + replaced.trailingBlockBytes.size());
    EXPECT_EQ(reparsed.textureArchive->entries[1].encodedData, second);
    EXPECT_EQ(reparsed.header.textureTableOffset, reparsed.textureArchive->tableOffset);
}

TEST(MldDreamcastTextureArchive, AddsAndRemovesPvrRecordsCanonically) {
    const auto first = encodePvrTexture(8U, 61U, 3U);
    const auto second = encodePvrTexture(8U, 62U, 9U);
    auto file = MldParser{}.parseBytes(makeDreamcastTexturedMld({first, second}, false));
    ASSERT_TRUE(file.textureArchive.has_value());

    auto added = file.textureArchive->entries.front();
    added.archiveTextureIndex = 2U;
    added.textureName = "dc_tex_2";
    added.encodedData = encodePvrTexture(8U, 63U, 15U);
    added.encodedDataSize = added.encodedData.size();
    added.rawRecordWord1 = 0x80000000U;
    added.alignmentPrefixBytes.clear();
    added.trailingBlockBytes.clear();
    file.textureArchive->entries.push_back(std::move(added));

    auto written = MldFileWriter{}.write(file);
    ASSERT_TRUE(written.ok());
    auto reparsed = MldParser{}.parseBytes(written.bytes);
    ASSERT_TRUE(reparsed.textureArchive.has_value());
    ASSERT_EQ(reparsed.textureArchive->entries.size(), 3U);
    EXPECT_EQ(reparsed.textureArchive->entries[2].textureName, "dc_tex_2");
    EXPECT_EQ(reparsed.textureArchive->entries[2].globalIndex, 63U);
    EXPECT_EQ(reparsed.textureArchive->entries[2].encodedDataOffset & 31U, 0U);

    reparsed.textureArchive->entries.erase(reparsed.textureArchive->entries.begin() + 1);
    written = MldFileWriter{}.write(reparsed);
    ASSERT_TRUE(written.ok());
    const auto removed = MldParser{}.parseBytes(written.bytes);
    ASSERT_TRUE(removed.textureArchive.has_value());
    ASSERT_EQ(removed.textureArchive->entries.size(), 2U);
    EXPECT_EQ(removed.textureArchive->entries[1].textureName, "dc_tex_2");
    EXPECT_EQ(removed.textureArchive->entries[1].globalIndex, 63U);
}

TEST(MldDreamcastTextureArchive, RejectsInvalidOrCrossPlatformEncodedTextureBytes) {
    const auto pvr = encodePvrTexture(8U, 71U, 3U);
    auto file = MldParser{}.parseBytes(makeDreamcastTexturedMld({pvr}, false));
    ASSERT_TRUE(file.textureArchive.has_value());
    file.textureArchive->entries[0].encodedData = {'N', 'O', 'P', 'E'};
    EXPECT_FALSE(MldFileWriter{}.write(file).ok());

    file = MldParser{}.parseBytes(makeDreamcastTexturedMld({pvr}, false));
    spice::mld::exporting::MldWriteOptions options{};
    options.platform = TargetPlatform::GameCube;
    EXPECT_FALSE(MldFileWriter{}.write(file, options).ok());
}

TEST(MldTextureArchiveRebuild, PreservesExactTextureGvrPayloadsForExtraction) {
    const auto first = encodeTexture(makeImage(8U, 8U, 3U), spice::gvm::model::TextureFormat::I4);
    const auto second = encodeTexture(makeImage(8U, 8U, 7U), spice::gvm::model::TextureFormat::RGB565);

    MldParser parser;
    const auto parsed = parser.parseFile(makeTexturedMld(first, second));
    ASSERT_TRUE(parsed.textureArchive.has_value());
    ASSERT_EQ(parsed.textureArchive->entries.size(), 2U);

    const auto& firstEntry = parsed.textureArchive->entries[0];
    const auto& secondEntry = parsed.textureArchive->entries[1];
    EXPECT_EQ(firstEntry.archiveTextureIndex, 0U);
    EXPECT_EQ(firstEntry.textureName, "tex_a");
    EXPECT_EQ(firstEntry.encodedDataSize, first.size());
    EXPECT_EQ(firstEntry.encodedData, first);
    EXPECT_EQ(secondEntry.archiveTextureIndex, 1U);
    EXPECT_EQ(secondEntry.textureName, "tex_b");
    EXPECT_EQ(secondEntry.encodedDataSize, second.size());
    EXPECT_EQ(secondEntry.encodedData, second);
}

TEST(MldTextureArchiveRebuild, ExtractedTextureGvrPayloadDecodesToPng) {
    const auto dir = testOutDir("texture_gvr_to_png");
    const auto image = makeImage(8U, 8U, 17U);
    const auto first = encodeTexture(image, spice::gvm::model::TextureFormat::RGBA8);
    const auto second = encodeTexture(makeImage(8U, 8U, 7U), spice::gvm::model::TextureFormat::RGB565);

    MldParser parser;
    const auto parsed = parser.parseFile(makeTexturedMld(first, second));
    ASSERT_TRUE(parsed.textureArchive.has_value());
    ASSERT_EQ(parsed.textureArchive->entries.size(), 2U);

    const auto& firstEntry = parsed.textureArchive->entries[0];
    const auto pngPath = dir / "tex_a.png";
    const auto exported = spice::gvm::ir::exportGvrPng(
        std::span<const std::uint8_t>(firstEntry.encodedData.data(), firstEntry.encodedData.size()),
        pngPath);
    const auto decoded = spice::gvm::image::readPngRgba8(pngPath);

    EXPECT_EQ(exported.texture.textureFormat, spice::gvm::model::TextureFormat::RGBA8);
    EXPECT_EQ(decoded.width, image.width);
    EXPECT_EQ(decoded.height, image.height);
    EXPECT_EQ(decoded.rgba8, image.rgba8);
}

TEST(MldTextureArchiveRebuild, ReplacesWithSmallerTextureWithoutPadding) {
    const auto large = encodeTexture(makeImage(8U, 8U, 5U), spice::gvm::model::TextureFormat::RGBA8);
    const auto second = encodeTexture(makeImage(8U, 8U, 9U), spice::gvm::model::TextureFormat::RGB5A3);
    const auto replacement = encodeTexture(makeImage(8U, 8U, 13U), spice::gvm::model::TextureFormat::I4);
    ASSERT_LT(replacement.size(), large.size());

    MldParser parser;
    const auto originalBytes = makeTexturedMld(large, second);
    const auto parsed = parser.parseFile(originalBytes);
    ASSERT_TRUE(parsed.textureArchive.has_value());

    MldExportOptions options{};
    options.platform = TargetPlatform::GameCube;
    options.textureReplacement = spice::mld::exporting::MldTextureReplacement{
        .textureIndex = 0U,
        .encodedData = replacement,
    };

    const auto rebuilt = MldFileExporter{}.exportFile(parsed, options);
    EXPECT_EQ(rebuilt.size(), originalBytes.size() - (large.size() - replacement.size()));

    const auto reparsed = parser.parseFile(rebuilt);
    ASSERT_TRUE(reparsed.textureArchive.has_value());
    ASSERT_EQ(reparsed.textureArchive->entries.size(), 2U);
    EXPECT_EQ(reparsed.textureArchive->entries[0].encodedDataSize, replacement.size());
    EXPECT_EQ(reparsed.textureArchive->entries[1].encodedDataSize, second.size());
    EXPECT_EQ(reparsed.textureArchive->entries[0].sourceFormat, "I4");
}

TEST(MldTextureArchiveRebuild, RejectsNonTerminalArchiveSizeShiftByDefault) {
    const auto small = encodeTexture(makeImage(8U, 8U, 3U), spice::gvm::model::TextureFormat::I4);
    const auto second = encodeTexture(makeImage(8U, 8U, 7U), spice::gvm::model::TextureFormat::RGB565);
    const auto replacement = encodeTexture(makeImage(8U, 8U, 11U), spice::gvm::model::TextureFormat::RGBA8);

    MldParser parser;
    const auto parsed = parser.parseFile(makeTexturedMld(small, second, true));
    ASSERT_TRUE(parsed.textureArchive.has_value());

    MldExportOptions options{};
    options.platform = TargetPlatform::GameCube;
    options.textureReplacement = spice::mld::exporting::MldTextureReplacement{
        .textureIndex = 0U,
        .encodedData = replacement,
    };

    EXPECT_THROW((void)MldFileExporter{}.exportFile(parsed, options), std::runtime_error);
    options.textureReplacement->allowPostArchiveShift = true;
    const auto rebuilt = MldFileExporter{}.exportFile(parsed, options);
    ASSERT_GE(rebuilt.size(), 4U);
    EXPECT_EQ(rebuilt[rebuilt.size() - 4U], 'T');
    EXPECT_EQ(rebuilt[rebuilt.size() - 3U], 'A');
    EXPECT_EQ(rebuilt[rebuilt.size() - 2U], 'I');
    EXPECT_EQ(rebuilt[rebuilt.size() - 1U], 'L');
}

TEST(MldTextureArchiveRebuild, PreservesAklzWrappingByDefaultWhenCompressed) {
    const auto small = encodeTexture(makeImage(8U, 8U, 3U), spice::gvm::model::TextureFormat::I4);
    const auto second = encodeTexture(makeImage(8U, 8U, 7U), spice::gvm::model::TextureFormat::RGB565);
    const auto replacement = encodeTexture(makeImage(8U, 8U, 11U), spice::gvm::model::TextureFormat::RGBA8);
    const auto compressed = spice::compression::aklz::compress(makeTexturedMld(small, second));
    ASSERT_TRUE(compressed.ok()) << spice::compression::aklz::errorToString(compressed.error);

    MldParser parser;
    const auto parsed = parser.parseFile(compressed.bytes);
    ASSERT_TRUE(parsed.sourceWasCompressedAklz);
    ASSERT_TRUE(parsed.textureArchive.has_value());

    MldExportOptions options{};
    options.platform = TargetPlatform::GameCube;
    options.compressAklz = parsed.sourceWasCompressedAklz;
    options.textureReplacement = spice::mld::exporting::MldTextureReplacement{
        .textureIndex = 0U,
        .encodedData = replacement,
    };

    const auto rebuilt = MldFileExporter{}.exportFile(parsed, options);
    ASSERT_TRUE(spice::compression::aklz::isAklz(rebuilt));
    const auto decoded = spice::compression::aklz::decompress(rebuilt);
    ASSERT_TRUE(decoded.ok()) << spice::compression::aklz::errorToString(decoded.error);
    const auto reparsed = parser.parseFile(decoded.bytes);
    ASSERT_TRUE(reparsed.textureArchive.has_value());
    EXPECT_EQ(reparsed.textureArchive->entries[0].encodedDataSize, replacement.size());
}

TEST(GobjParser, DecodesPositionOnlyVertexChunk22) {
    const auto bytes = makeSyntheticGobj(0x22U, 3U, false, false);
    const auto decoded = spice::mld::parsing::GobjParser{}.decode(bytes, 0x1000U, Endian::Big);

    ASSERT_TRUE(decoded.decoded);
    ASSERT_EQ(decoded.nodes.size(), 1U);
    const auto& mesh = decoded.nodes[0].streamMesh;
    ASSERT_EQ(mesh.vertices.size(), 3U);
    ASSERT_EQ(mesh.indices.size(), 3U);
    ASSERT_EQ(mesh.triangleMetadata.size(), 1U);
    EXPECT_FALSE(mesh.vertices[0].hasNormal);
    EXPECT_FALSE(mesh.vertices[0].rawUserAttributesU32.has_value());
    EXPECT_FLOAT_EQ(mesh.vertices[0].position.x, 2.0F);
    EXPECT_EQ(mesh.triangleMetadata[0].rawU16, (std::array<std::uint16_t, 3>{ 1U, 2U, 0x8003U }));
}

TEST(GobjParser, PreservesExistingNormalVertexChunk29) {
    const auto bytes = makeSyntheticGobj(0x29U, 6U, true, false);
    const auto decoded = spice::mld::parsing::GobjParser{}.decode(bytes, 0x2000U, Endian::Big);

    ASSERT_TRUE(decoded.decoded);
    ASSERT_EQ(decoded.nodes.size(), 1U);
    const auto& mesh = decoded.nodes[0].streamMesh;
    ASSERT_EQ(mesh.vertices.size(), 3U);
    EXPECT_TRUE(mesh.vertices[0].hasNormal);
    EXPECT_FLOAT_EQ(mesh.vertices[0].normal.y, 1.0F);
    EXPECT_FALSE(mesh.vertices[0].rawUserAttributesU32.has_value());
}

TEST(GobjParser, DecodesChunk2bAndPreservesRawUserAttributes) {
    const auto bytes = makeSyntheticGobj(0x2BU, 7U, true, true);
    const auto decoded = spice::mld::parsing::GobjParser{}.decode(bytes, 0x3000U, Endian::Big);

    ASSERT_TRUE(decoded.decoded);
    ASSERT_EQ(decoded.nodes.size(), 1U);
    const auto& mesh = decoded.nodes[0].streamMesh;
    ASSERT_EQ(mesh.vertices.size(), 3U);
    ASSERT_TRUE(mesh.vertices[0].rawUserAttributesU32.has_value());
    ASSERT_TRUE(mesh.vertices[1].rawUserAttributesU32.has_value());
    ASSERT_TRUE(mesh.vertices[2].rawUserAttributesU32.has_value());
    EXPECT_EQ(*mesh.vertices[0].rawUserAttributesU32, 0U);
    EXPECT_EQ(*mesh.vertices[1].rawUserAttributesU32, 0x12345678U);
    EXPECT_EQ(*mesh.vertices[2].rawUserAttributesU32, 0xFFFFFFFFU);
}

TEST(GobjParser, DecodesNormalDiffuseChunk2aForBothEndians) {
    for (const auto endian : { Endian::Big, Endian::Little }) {
        const auto bytes = makeSyntheticGobj(0x2AU, 7U, true, false, endian, true);
        const auto decoded = spice::mld::parsing::GobjParser{}.decode(bytes, 0x3400U, endian);

        ASSERT_TRUE(decoded.decoded);
        ASSERT_EQ(decoded.nodes.size(), 1U);
        const auto& mesh = decoded.nodes[0].streamMesh;
        ASSERT_EQ(mesh.vertices.size(), 3U);
        ASSERT_TRUE(mesh.vertices[0].diffuseColor.has_value());
        ASSERT_TRUE(mesh.vertices[1].diffuseColor.has_value());
        ASSERT_TRUE(mesh.vertices[2].diffuseColor.has_value());
        EXPECT_EQ(mesh.vertices[0].diffuseColor->r, 0U);
        EXPECT_EQ(mesh.vertices[0].diffuseColor->g, 0U);
        EXPECT_EQ(mesh.vertices[0].diffuseColor->b, 0U);
        EXPECT_EQ(mesh.vertices[0].diffuseColor->a, 0U);
        EXPECT_EQ(mesh.vertices[1].diffuseColor->r, 0x34U);
        EXPECT_EQ(mesh.vertices[1].diffuseColor->g, 0x56U);
        EXPECT_EQ(mesh.vertices[1].diffuseColor->b, 0x78U);
        EXPECT_EQ(mesh.vertices[1].diffuseColor->a, 0x12U);
        EXPECT_EQ(mesh.vertices[2].diffuseColor->r, 0xFFU);
        EXPECT_EQ(mesh.vertices[2].diffuseColor->g, 0xFFU);
        EXPECT_EQ(mesh.vertices[2].diffuseColor->b, 0xFFU);
        EXPECT_EQ(mesh.vertices[2].diffuseColor->a, 0xFFU);
        EXPECT_FALSE(mesh.vertices[0].rawUserAttributesU32.has_value());
        ASSERT_TRUE(decoded.nodes[0].attach.has_value());
        EXPECT_EQ(decoded.nodes[0].attach->vertexChunk.chunkType, 0x2AU);
        EXPECT_EQ(decoded.nodes[0].attach->vertexChunk.recordWords, 7U);

        auto changed = decoded.data;
        const auto originalHash = spice::mld::model::semanticHash(decoded.data);
        changed.nodes[0].streamMesh.vertices[0].diffuseColor->r = 1U;
        EXPECT_NE(spice::mld::model::semanticHash(changed), originalHash);
    }
}

TEST(TriangleMetadataDecoder, ExhaustivelyMatchesRuntimeDecimalArithmetic) {
    constexpr std::array<std::uint16_t, 10> ones{
        0x0000U, 0x6800U, 0x7800U, 0x1800U, 0x1400U,
        0x0100U, 0x1200U, 0x1300U, 0x0000U, 0x0000U,
    };
    constexpr std::array<std::uint16_t, 10> tens{
        0x0000U, 0x0001U, 0x0002U, 0x0003U, 0x0004U,
        0x0005U, 0x0006U, 0x0007U, 0x0008U, 0x0009U,
    };
    constexpr std::array<std::uint16_t, 10> hundreds{
        0x0000U, 0x0010U, 0x0020U, 0x0030U, 0x0040U,
        0x0050U, 0x0060U, 0x0000U, 0x0000U, 0x8000U,
    };
    constexpr std::array<std::uint16_t, 10> thousands{
        0x0000U, 0x8000U, 0x8200U, 0x8400U, 0x8600U,
        0x8800U, 0x8A00U, 0x8C00U, 0x8E00U, 0x9000U,
    };

    for (std::uint32_t selector = 0U; selector <= 0x7FFFU; ++selector) {
        const auto d0 = static_cast<std::uint8_t>(selector % 10U);
        const auto d1 = static_cast<std::uint8_t>((selector / 10U) % 10U);
        const auto d2 = static_cast<std::uint8_t>((selector / 100U) % 10U);
        const auto d3 = static_cast<std::uint8_t>((selector / 1000U) % 10U);
        auto expected = ones[d0];
        if (selector / 10U != 0U) expected = static_cast<std::uint16_t>(expected | tens[d1]);
        if (selector / 100U != 0U) expected = static_cast<std::uint16_t>(expected | hundreds[d2]);
        if (selector / 1000U != 0U) expected = static_cast<std::uint16_t>(expected + thousands[d3]);

        for (const auto winding : { false, true }) {
            const auto raw = static_cast<std::uint16_t>(selector | (winding ? 0x8000U : 0U));
            const auto decoded = spice::mld::model::decodeTriangleMetadataWord(raw);
            ASSERT_EQ(decoded.rawWord, raw);
            ASSERT_EQ(decoded.selectorLow15, selector);
            ASSERT_EQ(decoded.streamWindingHighBit, winding);
            ASSERT_EQ(decoded.onesDigit, d0);
            ASSERT_EQ(decoded.tensDigit, d1);
            ASSERT_EQ(decoded.hundredsDigit, d2);
            ASSERT_EQ(decoded.thousandsDigit, d3);
            ASSERT_EQ(decoded.ignoredTenThousandsDigit, (selector / 10000U) % 10U);
            ASSERT_EQ(decoded.decodedU16, expected);
            ASSERT_EQ(decoded.decodedHighBit, (expected & 0x8000U) != 0U);
            ASSERT_EQ(decoded.decodedClassBits, (expected >> 8U) & 0x7FU);
            ASSERT_EQ(decoded.payloadGroupBits, (expected >> 4U) & 0x0FU);
            ASSERT_EQ(decoded.encounterSelectorBits, expected & 0x0FU);
        }
    }
}

TEST(GrndParser, PreservesRawTriangleMetadataAcrossWindingReversal) {
    const auto bytes = makeSyntheticGrnd();
    const auto decoded = spice::mld::parsing::GrndParser{}.decode(bytes, 0x4000U, Endian::Big);

    ASSERT_TRUE(decoded.decoded);
    ASSERT_EQ(decoded.mesh.indices.size(), 3U);
    EXPECT_EQ(decoded.mesh.indices[0], 2U);
    EXPECT_EQ(decoded.mesh.indices[1], 1U);
    EXPECT_EQ(decoded.mesh.indices[2], 0U);
    ASSERT_EQ(decoded.mesh.triangleMetadata.size(), 1U);
    EXPECT_EQ(decoded.mesh.triangleMetadata[0].rawU16,
        (std::array<std::uint16_t, 3>{ 0x0001U, 0x7FFFU, 0x800AU }));
}

TEST(GrndParser, PreservesPartialSourceModelWhenCellReferencesDoNotResolve) {
    constexpr std::size_t refListOffset = 0xDCU;
    for (const auto endian : { Endian::Big, Endian::Little }) {
        auto bytes = makeSyntheticGrnd(endian);
        writeU16(bytes, refListOffset, 1U, endian); // one set exists, so set index one is unresolved
        const auto decoded = spice::mld::parsing::GrndParser{}.decode(bytes, 0x4100U, endian);

        EXPECT_FALSE(decoded.decoded);
        EXPECT_EQ(decoded.skippedReferenceCount, 1U);
        EXPECT_TRUE(decoded.mesh.indices.empty());
        ASSERT_EQ(decoded.data.triangleSets.size(), 1U);
        ASSERT_EQ(decoded.data.cells.size(), 1U);
        ASSERT_EQ(decoded.data.cells[0].references.size(), 1U);
        EXPECT_EQ(decoded.data.cells[0].references[0].triangleSet, 1U);
        EXPECT_FALSE(decoded.data.cells[0].references[0].meshTriangleIndex.has_value());
        EXPECT_TRUE(std::any_of(decoded.diagnostics.begin(), decoded.diagnostics.end(), [](const auto& diagnostic) {
            return diagnostic.find("skippedRefs=1") != std::string::npos;
        }));
    }
}

TEST(GrndParser, BakesTriangleSetTranslationIntoCanonicalMeshForBothEndians) {
    for (const auto endian : { Endian::Big, Endian::Little }) {
        const auto bytes = makeSyntheticGrnd(endian, 10.0F, 20.0F, -30.0F, -100.5F, 200.25F);
        const auto decoded = spice::mld::parsing::GrndParser{}.decode(bytes, 0x4000U, endian);

        ASSERT_TRUE(decoded.decoded);
        ASSERT_EQ(decoded.data.triangleSets.size(), 1U);
        const auto& sourceSet = decoded.data.triangleSets.front();
        EXPECT_FLOAT_EQ(sourceSet.localToResourceTranslation.x, 10.0F);
        EXPECT_FLOAT_EQ(sourceSet.localToResourceTranslation.y, 20.0F);
        EXPECT_FLOAT_EQ(sourceSet.localToResourceTranslation.z, -30.0F);
        EXPECT_FLOAT_EQ(decoded.data.gridOriginX, -100.5F);
        EXPECT_FLOAT_EQ(decoded.data.gridOriginZ, 200.25F);

        ASSERT_EQ(sourceSet.verticesByFloatIndex.size(), 3U);
        const auto& localVertex = sourceSet.verticesByFloatIndex.at(0U);
        EXPECT_FLOAT_EQ(localVertex.position.x, 0.0F);
        EXPECT_FLOAT_EQ(localVertex.position.y, 0.0F);
        EXPECT_FLOAT_EQ(localVertex.position.z, 0.0F);

        ASSERT_EQ(decoded.mesh.vertices.size(), 3U);
        EXPECT_FLOAT_EQ(decoded.mesh.vertices[0].position.x, 10.0F);
        EXPECT_FLOAT_EQ(decoded.mesh.vertices[0].position.y, 20.0F);
        EXPECT_FLOAT_EQ(decoded.mesh.vertices[0].position.z, -30.0F);
        EXPECT_FLOAT_EQ(decoded.mesh.vertices[1].position.x, 11.0F);
        EXPECT_FLOAT_EQ(decoded.mesh.vertices[1].position.y, 21.0F);
        EXPECT_FLOAT_EQ(decoded.mesh.vertices[1].position.z, -30.0F);
        EXPECT_FLOAT_EQ(decoded.mesh.vertices[2].position.x, 12.0F);
        EXPECT_FLOAT_EQ(decoded.mesh.vertices[2].position.y, 20.0F);
        EXPECT_FLOAT_EQ(decoded.mesh.vertices[2].position.z, -29.0F);
        for (const auto& vertex : decoded.mesh.vertices) {
            EXPECT_FLOAT_EQ(vertex.normal.x, 0.0F);
            EXPECT_FLOAT_EQ(vertex.normal.y, 1.0F);
            EXPECT_FLOAT_EQ(vertex.normal.z, 0.0F);
        }
    }
}

TEST(BlenderIrJsonExporter, EmitsRawTriangleMetadataAndVertexUserAttributesWithoutSemantics) {
    spice::mld::model::BlenderIrScene scene{};
    spice::mld::model::BlenderIrMesh mesh{};
    spice::mld::model::BlenderIrVertex vertex{};
    vertex.rawUserAttributesU32 = 0xFFFFFFFFU;
    mesh.vertices.push_back(vertex);

    spice::mld::model::BlenderIrTriangleSet triangleSet{};
    triangleSet.triangleMetadata.push_back(spice::mld::model::TriangleMetadata{
        .rawU16 = { 0U, 0xFFFFU, 0x8000U },
    });
    mesh.triangleSets.push_back(std::move(triangleSet));
    scene.meshes.push_back(std::move(mesh));

    const auto json = spice::mld::exporting::BlenderIrJsonExporter{}.toJson(scene);
    EXPECT_NE(json.find("\"rawUserAttributesU32\":4294967295"), std::string::npos);
    EXPECT_NE(json.find("\"triangleMetadata\":[{\"rawU16\":[0,65535,32768]}]"), std::string::npos);
    EXPECT_EQ(json.find("collisionTriangles"), std::string::npos);
    EXPECT_EQ(json.find("selectorLow15"), std::string::npos);
    EXPECT_EQ(json.find("encounterTableId"), std::string::npos);
}

TEST(BlenderIrJsonExporter, SanitizesNonFiniteFloatsWithBoundedExactPaths) {
    spice::mld::model::BlenderIrScene scene{};
    spice::mld::model::BlenderIrMesh mesh{};
    for (std::size_t i = 0; i < 40U; ++i) {
        spice::mld::model::BlenderIrVertex vertex{};
        vertex.position.x = std::numeric_limits<float>::quiet_NaN();
        if (i == 0U) {
            vertex.weights.push_back(spice::mld::model::BlenderIrWeight{
                .boneOrNodeIndex = 0U,
                .weight = std::numeric_limits<float>::quiet_NaN(),
            });
        }
        mesh.vertices.push_back(vertex);
    }
    spice::mld::model::BlenderIrMaterial material{};
    material.mipmapDistanceMultiplier = std::numeric_limits<float>::infinity();
    mesh.materials.push_back(material);
    spice::mld::model::BlenderIrTriangleSet triangleSet{};
    spice::mld::model::BlenderIrCorner corner{};
    corner.u = -std::numeric_limits<float>::infinity();
    corner.colorA = std::numeric_limits<float>::infinity();
    triangleSet.corners.push_back(corner);
    mesh.triangleSets.push_back(triangleSet);
    scene.meshes.push_back(mesh);
    spice::mld::model::BlenderIrInstance instance{};
    instance.transform.position.y = std::numeric_limits<float>::infinity();
    scene.indexEntries.push_back(instance);
    spice::mld::model::BlenderIrAnimation animation{};
    spice::mld::model::BlenderIrAnimationChannel channel{};
    channel.floatValues.push_back(spice::mld::model::BlenderIrFloatKeyframe{
        .frame = 0U,
        .value = std::numeric_limits<float>::quiet_NaN(),
    });
    animation.channels.push_back(channel);
    scene.animations.push_back(animation);

    const auto json = spice::mld::exporting::BlenderIrJsonExporter{}.toJson(scene);
    EXPECT_EQ(json.find("nan"), std::string::npos);
    EXPECT_EQ(json.find("inf"), std::string::npos);
    EXPECT_NE(json.find("sanitized 46 non-finite"), std::string::npos);
    EXPECT_NE(json.find("meshes[0].vertices[0].position.x"), std::string::npos);
    EXPECT_NE(json.find("omitted 14 additional path(s)"), std::string::npos);
}

TEST(MldEntryListJsonExporter, PreservesDetailedSchemaFieldsAndEscapesStrings) {
    spice::mld::parsing::ParsedEntryListItem entry{};
    entry.tableIndex = 4U;
    entry.entryId = 17U;
    entry.tblId = -3;
    entry.fxnName = "function\"\\line\n\tend";
    entry.objectCount = 1U;
    entry.groundCount = 2U;
    entry.motionCount = 3U;
    entry.textureCount = 2U;
    entry.texturesPointer = 0x1234ABCDU;
    entry.groundLinks = { 1U, 2U };
    entry.paramList2 = { 3U };
    entry.functionParameters = { 4U, 5U };
    entry.objectAddresses = { 0x100U };
    entry.groundAddresses = { 0x200U, 0x204U };
    entry.motionAddresses = { 0x300U };
    entry.textureNames = { "plain", "quoted\"\\name" };

    const std::array entries{ entry };
    const auto json = spice::mld::exporting::MldEntryListJsonExporter{}.toJson(
        std::filesystem::path("C:\\fixtures\\a101b_DC.mld"), entries);

    EXPECT_NE(json.find("\"schema\": \"spice_mld_entry_list_v1\""), std::string::npos);
    EXPECT_NE(json.find("\"source\": \"C:\\\\fixtures\\\\a101b_DC.mld\""), std::string::npos);
    EXPECT_NE(json.find("\"entry_count\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"table_index\": 4"), std::string::npos);
    EXPECT_NE(json.find("\"entryID\": 17"), std::string::npos);
    EXPECT_NE(json.find("\"tableID\": -3"), std::string::npos);
    EXPECT_NE(json.find("\"function\": \"function\\\"\\\\line\\n\\tend\""), std::string::npos);
    EXPECT_NE(json.find("\"object_count\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"ground_count\": 2"), std::string::npos);
    EXPECT_NE(json.find("\"motion_count\": 3"), std::string::npos);
    EXPECT_NE(json.find("\"texture_count\": 2"), std::string::npos);
    EXPECT_NE(json.find("\"textures_pointer\": 305441741"), std::string::npos);
    EXPECT_NE(json.find("\"textures_pointer_hex\": \"0x1234abcd\""), std::string::npos);
    EXPECT_NE(json.find("\"ground_links\": [1, 2]"), std::string::npos);
    EXPECT_NE(json.find("\"param_list2\": [3]"), std::string::npos);
    EXPECT_NE(json.find("\"function_parameters\": [4, 5]"), std::string::npos);
    EXPECT_NE(json.find("\"object_addresses\": [256]"), std::string::npos);
    EXPECT_NE(json.find("\"ground_addresses\": [512, 516]"), std::string::npos);
    EXPECT_NE(json.find("\"motion_addresses\": [768]"), std::string::npos);
    EXPECT_NE(json.find("\"texture_names\": [\"plain\", \"quoted\\\"\\\\name\"]"), std::string::npos);
}
