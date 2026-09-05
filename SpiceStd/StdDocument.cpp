#include "StdDocument.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

namespace spice::stdfile {
namespace {

template <typename Id, typename Range, typename GetId>
Id allocateId(const Range& values, GetId getId) noexcept {
    std::uint64_t next = 1U;
    for (const auto& value : values) next = std::max(next, getId(value).value + 1U);
    return Id{ next };
}

constexpr std::array kCommandDescriptors{
    StdCommandDescriptor{ StdCommandKind::Sparc, kStdSparcCombinedType, "SPARC", kStdSparcPayloadSize, true },
    StdCommandDescriptor{ StdCommandKind::PutModel, kStdPutModelCombinedType, "PUTMODEL", kStdPutModelPayloadSize, true },
    StdCommandDescriptor{ StdCommandKind::SetCommand, kStdSetCommandCombinedType, "SET COMMAND", kStdSetCommandPayloadSize, true },
    StdCommandDescriptor{ StdCommandKind::MotionPause, kStdMotionPauseCombinedType, "MOTION PAUSE", kStdMotionPausePayloadSize, true },
    StdCommandDescriptor{ StdCommandKind::CollisionBox, kStdCollisionBoxCombinedType, "COLISION BOX", kStdCollisionBoxPayloadSize, true },
    StdCommandDescriptor{ StdCommandKind::MoveModel, kStdMoveModelCombinedType, "MOVE MODEL", kStdMoveModelPayloadSize, true },
    StdCommandDescriptor{ StdCommandKind::HitWeapon, kStdHitWeaponCombinedType, "HIT WEAPON", kStdHitWeaponPayloadSize, true },
    StdCommandDescriptor{ StdCommandKind::PointLight, kStdPointLightCombinedType, "POINT LIGHT", kStdPointLightPayloadSize, true },
    StdCommandDescriptor{ StdCommandKind::SystemCamera, kStdSystemCameraCombinedType, "SYSTEM CAMERA", kStdSystemCameraPayloadSize, true },
    StdCommandDescriptor{ StdCommandKind::EffectWait, kStdEffectWaitCombinedType, "EFFECT WAIT", kStdEffectWaitPayloadSize, true },
    StdCommandDescriptor{ StdCommandKind::SeRequest, kStdSeRequestCombinedType, "SE REQUEST", kStdSeRequestPayloadSize, true },
};

} // namespace

std::optional<std::string> StdSparcPayload::formattedMldFilename() const {
    if (mldFilenameKey == 9999999U) return std::nullopt;

    char buffer[16]{};
    if (mldFilenameKey < 10000000U) {
        std::snprintf(buffer, sizeof(buffer), "E%02u%03u%02u.MLD",
            mldFilenameKey / 100000U,
            (mldFilenameKey / 100U) % 1000U,
            mldFilenameKey % 100U);
        return std::string(buffer);
    }

    const auto modelKey = mldFilenameKey - 10000000U;
    constexpr std::array familyLetters{ 'A', 'B', 'G' };
    const auto familyIndex = modelKey / 1000U;
    if (familyIndex >= familyLetters.size()) return std::nullopt;
    std::snprintf(buffer, sizeof(buffer), "M%c%03u.MLD",
        familyLetters[familyIndex], modelKey % 1000U);
    return std::string(buffer);
}

std::int16_t StdActionRow::rowType() const noexcept {
    if (std::holds_alternative<StdType0ActionRowFields>(fields)) return 0;
    if (std::holds_alternative<StdMotionActionRowFields>(fields)) return 1;
    return std::get<StdUnrecognizedActionRowFields>(fields).rowType;
}

StdActionRowId StdDocument::allocateActionRowId() const noexcept {
    const auto* value = std::get_if<StdActionRowsContent>(&content);
    return value == nullptr ? StdActionRowId{ 1U }
        : allocateId<StdActionRowId>(value->rows, [](const auto& item) { return item.id; });
}

StdEntryRecordId StdDocument::allocateEntryRecordId() const noexcept {
    const auto* value = std::get_if<StdEntryTableContent>(&content);
    return value == nullptr ? StdEntryRecordId{ 1U }
        : allocateId<StdEntryRecordId>(value->records, [](const auto& item) { return item.id; });
}

StdEntryPayloadId StdDocument::allocateEntryPayloadId() const noexcept {
    const auto* value = std::get_if<StdEntryTableContent>(&content);
    return value == nullptr ? StdEntryPayloadId{ 1U }
        : allocateId<StdEntryPayloadId>(value->payloads, [](const auto& item) { return item.id; });
}

StdEntryTerminatorId StdDocument::allocateEntryTerminatorId() const noexcept {
    const auto* value = std::get_if<StdEntryTableContent>(&content);
    return value == nullptr || !value->terminator.id ? StdEntryTerminatorId{ 1U }
        : StdEntryTerminatorId{ value->terminator.id.value + 1U };
}

StdOpaqueFragmentId StdDocument::allocateOpaqueFragmentId() const noexcept {
    const auto* value = std::get_if<StdEntryTableContent>(&content);
    if (value == nullptr) return StdOpaqueFragmentId{ 1U };
    auto result = allocateId<StdOpaqueFragmentId>(value->opaqueFragments, [](const auto& item) { return item.id; });
    if (value->fileTrailer.has_value()) {
        result.value = std::max(result.value, value->fileTrailer->id.value + 1U);
    }
    return result;
}

bool StdDocument::hasOpaqueContent() const noexcept {
    if (std::holds_alternative<StdOpaqueContent>(content)) return true;
    const auto* table = std::get_if<StdEntryTableContent>(&content);
    if (table == nullptr) return false;
    if (!table->opaqueFragments.empty() || table->fileTrailer.has_value()) return true;
    return std::any_of(table->payloads.begin(), table->payloads.end(), [](const auto& payload) {
        return std::holds_alternative<StdOpaquePayload>(payload.content);
    });
}

const StdCommandDescriptor* findStdCommandDescriptor(const std::uint32_t combinedType) noexcept {
    const auto found = std::find_if(kCommandDescriptors.begin(), kCommandDescriptors.end(),
        [&](const auto& value) { return value.combinedType == combinedType; });
    return found == kCommandDescriptors.end() ? nullptr : &*found;
}

const StdEntryPayload* findEntryPayload(const StdEntryTableContent& table, const StdEntryPayloadId id) noexcept {
    const auto found = std::find_if(table.payloads.begin(), table.payloads.end(),
        [&](const auto& item) { return item.id == id; });
    return found == table.payloads.end() ? nullptr : &*found;
}

StdEntryPayload* findEntryPayload(StdEntryTableContent& table, const StdEntryPayloadId id) noexcept {
    return const_cast<StdEntryPayload*>(findEntryPayload(std::as_const(table), id));
}

const StdOpaqueFragment* findOpaqueFragment(const StdEntryTableContent& table, const StdOpaqueFragmentId id) noexcept {
    const auto found = std::find_if(table.opaqueFragments.begin(), table.opaqueFragments.end(),
        [&](const auto& item) { return item.id == id; });
    return found == table.opaqueFragments.end() ? nullptr : &*found;
}

StdOpaqueFragment* findOpaqueFragment(StdEntryTableContent& table, const StdOpaqueFragmentId id) noexcept {
    return const_cast<StdOpaqueFragment*>(findOpaqueFragment(std::as_const(table), id));
}

} // namespace spice::stdfile
