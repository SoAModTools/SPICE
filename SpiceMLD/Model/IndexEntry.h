#pragma once

#include "Types.h"
#include "U32List.h"
#include "../../SpiceCore/Binary/EndianReader.h"

#include <bit>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <algorithm>
#include <utility>

namespace spice::mld::model {

[[nodiscard]] inline Quat eulerRadiansToQuaternionXYZ(const Vec3& euler) {
    const float halfX = euler.x * 0.5F;
    const float halfY = euler.y * 0.5F;
    const float halfZ = euler.z * 0.5F;

    const float sx = std::sin(halfX);
    const float cx = std::cos(halfX);
    const float sy = std::sin(halfY);
    const float cy = std::cos(halfY);
    const float sz = std::sin(halfZ);
    const float cz = std::cos(halfZ);

    return Quat{
        .x = (sx * cy * cz) - (cx * sy * sz),
        .y = (cx * sy * cz) + (sx * cy * sz),
        .z = (cx * cy * sz) - (sx * sy * cz),
        .w = (cx * cy * cz) + (sx * sy * sz),
    };
}

[[nodiscard]] inline Vec3 degreesToRadians(const Vec3& euler) {
    constexpr float pi = 3.14159265358979323846F;
    constexpr float toRadians = pi / 180.0F;
    return Vec3{
        .x = euler.x * toRadians,
        .y = euler.y * toRadians,
        .z = euler.z * toRadians,
    };
}

struct IndexEntry {
    IndexEntry() = default;

    IndexEntry(const IndexEntry& other)
        : tableIndex(other.tableIndex),
          entryId(other.entryId),
          tblId(other.tblId),
          fxnName(other.fxnName),
          transform(other.transform),
          groundLinks(other.groundLinks ? std::make_unique<U32List>(*other.groundLinks) : nullptr),
          paramList2(other.paramList2 ? std::make_unique<U32List>(*other.paramList2) : nullptr),
          functionParameters(other.functionParameters ? std::make_unique<U32List>(*other.functionParameters) : nullptr),
          objectAddresses(other.objectAddresses ? std::make_unique<U32List>(*other.objectAddresses) : nullptr),
          groundAddresses(other.groundAddresses ? std::make_unique<U32List>(*other.groundAddresses) : nullptr),
          motionAddresses(other.motionAddresses ? std::make_unique<U32List>(*other.motionAddresses) : nullptr),
          objectCount(other.objectCount),
          groundCount(other.groundCount),
          motionCount(other.motionCount),
          texturesPointer(other.texturesPointer) {}

    IndexEntry& operator=(const IndexEntry& other) {
        if (this == &other) {
            return *this;
        }
        tableIndex = other.tableIndex;
        entryId = other.entryId;
        tblId = other.tblId;
        fxnName = other.fxnName;
        transform = other.transform;
        groundLinks = other.groundLinks ? std::make_unique<U32List>(*other.groundLinks) : nullptr;
        paramList2 = other.paramList2 ? std::make_unique<U32List>(*other.paramList2) : nullptr;
        functionParameters = other.functionParameters ? std::make_unique<U32List>(*other.functionParameters) : nullptr;
        objectAddresses = other.objectAddresses ? std::make_unique<U32List>(*other.objectAddresses) : nullptr;
        groundAddresses = other.groundAddresses ? std::make_unique<U32List>(*other.groundAddresses) : nullptr;
        motionAddresses = other.motionAddresses ? std::make_unique<U32List>(*other.motionAddresses) : nullptr;
        objectCount = other.objectCount;
        groundCount = other.groundCount;
        motionCount = other.motionCount;
        texturesPointer = other.texturesPointer;
        return *this;
    }

    IndexEntry(IndexEntry&&) noexcept = default;
    IndexEntry& operator=(IndexEntry&&) noexcept = default;

    std::size_t tableIndex = 0;
    std::uint32_t entryId = 0;
    std::int32_t tblId = 0;
    std::string fxnName{};
    Transform transform{};

    std::unique_ptr<U32List> groundLinks{};
    std::unique_ptr<U32List> paramList2{};
    std::unique_ptr<U32List> functionParameters{};
    std::unique_ptr<U32List> objectAddresses{};
    std::unique_ptr<U32List> groundAddresses{};
    std::unique_ptr<U32List> motionAddresses{};
    std::size_t objectCount = 0;
    std::size_t groundCount = 0;
    std::size_t motionCount = 0;
    std::uint32_t texturesPointer = 0;
};

using IndexEntryCoordinateMapper = std::function<Vec3(const Vec3&)>;
using IndexEntryWarningSink = std::function<void(const std::string&)>;

inline void countNotZero(U32List& list, std::size_t& count) {
    count = 0;
    for (auto item : list.values) {
        if (item == 0u) continue;
        count++;
    }
}

[[nodiscard]] inline std::string readFxnString(std::span<const std::uint8_t> bytes, const std::size_t offset) {
    constexpr std::size_t fxnLen = 0x14;
    std::string out{};
    if (offset + fxnLen > bytes.size()) {
        return out;
    }

    out.reserve(fxnLen);
    for (std::size_t i = 0; i < fxnLen; ++i) {
        const char c = static_cast<char>(bytes[offset + i]);
        if (c == '\0') {
            break;
        }
        const unsigned char uc = static_cast<unsigned char>(c);
        out.push_back((uc >= 32U && uc <= 126U) ? c : '?');
    }

    return out;
}

[[nodiscard]] inline std::optional<IndexEntry> parseIndexEntry(std::span<const std::uint8_t> bytes,
    const std::size_t tableIndex,
    const std::size_t entryOffset,
    const spice::core::Endian endian,
    const IndexEntryCoordinateMapper& coordinateMapper,
    const IndexEntryWarningSink& warningSink) {
    const spice::core::EndianReader reader(bytes, endian);
    const auto entryId = reader.try_read_u32(entryOffset + 0x00);
    const auto tblId = reader.try_read_u32(entryOffset + 0x04);
    const auto ptrGroundLinks = reader.try_read_u32(entryOffset + 0x08);
    const auto ptrParamList2 = reader.try_read_u32(entryOffset + 0x0C);
    const auto ptrFunctionParameters = reader.try_read_u32(entryOffset + 0x10);
    const auto ptrObjects = reader.try_read_u32(entryOffset + 0x14);
    const auto ptrGrounds = reader.try_read_u32(entryOffset + 0x18);
    const auto ptrMotions = reader.try_read_u32(entryOffset + 0x1C);
    const auto ptrTextures = reader.try_read_u32(entryOffset + 0x20);
    if (!entryId.has_value() || !tblId.has_value() || !ptrGroundLinks.has_value() || !ptrParamList2.has_value() ||
        !ptrFunctionParameters.has_value() || !ptrObjects.has_value() || !ptrGrounds.has_value() ||
        !ptrMotions.has_value() || !ptrTextures.has_value()) {
        return std::nullopt;
    }

    IndexEntry entry{};
    entry.tableIndex = tableIndex;
    entry.entryId = *entryId;
    entry.tblId = std::bit_cast<std::int32_t>(*tblId);
    entry.texturesPointer = *ptrTextures;

    Transform transform{};
    const auto posX = reader.try_read_f32(entryOffset + 0x44);
    const auto posY = reader.try_read_f32(entryOffset + 0x48);
    const auto posZ = reader.try_read_f32(entryOffset + 0x4C);
    if (posX.has_value() && posY.has_value() && posZ.has_value()) {
        transform.position = coordinateMapper(Vec3{ *posX, *posY, *posZ });
    }

    const auto rotX = reader.try_read_f32(entryOffset + 0x50);
    const auto rotY = reader.try_read_f32(entryOffset + 0x54);
    const auto rotZ = reader.try_read_f32(entryOffset + 0x58);
    if (rotX.has_value() && rotY.has_value() && rotZ.has_value()) {
        transform.rotationRaw = Vec3{ *rotX, *rotY, *rotZ };
        transform.rotation = eulerRadiansToQuaternionXYZ(degreesToRadians(transform.rotationRaw));
    }

    const auto sclX = reader.try_read_f32(entryOffset + 0x5C);
    const auto sclY = reader.try_read_f32(entryOffset + 0x60);
    const auto sclZ = reader.try_read_f32(entryOffset + 0x64);
    if (sclX.has_value() && sclY.has_value() && sclZ.has_value()) {
        transform.scale = Vec3{ *sclX, *sclY, *sclZ };
    }

    entry.transform = transform;

    entry.fxnName = readFxnString(bytes, entryOffset + 0x24);

    const std::string indexPrefix = "entry[" + std::to_string(tableIndex) + "]";
    entry.groundLinks = makeU32List(bytes, *ptrGroundLinks, endian, indexPrefix + ".groundLinks", warningSink);
    entry.paramList2 = makeU32List(bytes, *ptrParamList2, endian, indexPrefix + ".paramList2", warningSink);
    entry.functionParameters = makeU32List(bytes, *ptrFunctionParameters, endian, indexPrefix + ".functionParameters", warningSink);
    entry.objectAddresses = makeU32List(bytes, *ptrObjects, endian, indexPrefix + ".objects", warningSink);
    entry.groundAddresses = makeU32List(bytes, *ptrGrounds, endian, indexPrefix + ".grounds", warningSink);
    entry.motionAddresses = makeU32List(bytes, *ptrMotions, endian, indexPrefix + ".motions", warningSink);
    countNotZero(*entry.objectAddresses.get(), entry.objectCount);
    countNotZero(*entry.groundAddresses.get(), entry.groundCount);
    countNotZero(*entry.motionAddresses.get(), entry.motionCount);

    return std::move(entry);
}


} // namespace spice::mld::model
