#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace spice::trade::alx {

inline constexpr std::string_view kCompatibilityVersion = "5.0.0";

enum class AlxTableKind {
    Enemy,
    EnemyEncounter,
    EnemyEvent,
    EnemyTask,
    EnemyMagic,
    Accessory,
    Armor,
    UsableItem,
    Weapon,
    WeaponEffect,
    ExpCurve,
    Character,
    CharacterMagic,
    CharacterSuperMove,
    MagicExpCurve,
};

enum class AlxLocale {
    Japanese,
    UnitedStates,
    Europe,
};

enum class AlxDiagnosticSeverity {
    Info,
    Warning,
    Error,
};

enum class AlxDiagnosticCode {
    IoError,
    EmptyInput,
    InvalidUtf8,
    MalformedCsv,
    DuplicateHeader,
    InvalidHeader,
    AmbiguousLocale,
    ConflictingLocale,
    InvalidValue,
    EmptyCanonicalTable,
    DuplicateIdentity,
    InvalidGrouping,
    MissingRequestedTable,
    DuplicateRequestedTable,
    DerivedValueMismatch,
};

struct AlxDiagnostic {
    AlxDiagnosticCode code{ AlxDiagnosticCode::MalformedCsv };
    AlxDiagnosticSeverity severity{ AlxDiagnosticSeverity::Error };
    std::string message{};
    std::filesystem::path path{};
    std::optional<std::size_t> row{};
    std::optional<std::size_t> column{};
};

struct AlxTableDescriptor {
    AlxTableKind kind{};
    std::string_view filename{};
};

[[nodiscard]] std::span<const AlxTableDescriptor> whitelistedTables() noexcept;
[[nodiscard]] std::optional<AlxTableKind> whitelistedTableKind(
    const std::filesystem::path& path) noexcept;
[[nodiscard]] std::string_view canonicalFilename(AlxTableKind kind) noexcept;
[[nodiscard]] std::vector<std::string> canonicalHeaders(AlxTableKind kind, AlxLocale locale);
[[nodiscard]] const char* toString(AlxTableKind kind) noexcept;
[[nodiscard]] const char* toString(AlxLocale locale) noexcept;
[[nodiscard]] const char* toString(AlxDiagnosticCode code) noexcept;
[[nodiscard]] bool hasErrors(std::span<const AlxDiagnostic> diagnostics) noexcept;

template <typename Tag>
struct AlxEntryId {
    std::uint32_t value{};
    auto operator<=>(const AlxEntryId&) const = default;
};

struct EnemyIdTag;
struct EnemyEventIdTag;
struct EnemyMagicIdTag;
struct AccessoryIdTag;
struct ArmorIdTag;
struct UsableItemIdTag;
struct WeaponIdTag;
struct WeaponEffectIdTag;
struct ExpCurveIdTag;
struct CharacterIdTag;
struct CharacterMagicIdTag;
struct CharacterSuperMoveIdTag;
struct MagicExpCurveIdTag;

using EnemyEntryId = AlxEntryId<EnemyIdTag>;
using EnemyEventEntryId = AlxEntryId<EnemyEventIdTag>;
using EnemyMagicEntryId = AlxEntryId<EnemyMagicIdTag>;
using AccessoryEntryId = AlxEntryId<AccessoryIdTag>;
using ArmorEntryId = AlxEntryId<ArmorIdTag>;
using UsableItemEntryId = AlxEntryId<UsableItemIdTag>;
using WeaponEntryId = AlxEntryId<WeaponIdTag>;
using WeaponEffectEntryId = AlxEntryId<WeaponEffectIdTag>;
using ExpCurveEntryId = AlxEntryId<ExpCurveIdTag>;
using CharacterEntryId = AlxEntryId<CharacterIdTag>;
using CharacterMagicEntryId = AlxEntryId<CharacterMagicIdTag>;
using CharacterSuperMoveEntryId = AlxEntryId<CharacterSuperMoveIdTag>;
using MagicExpCurveEntryId = AlxEntryId<MagicExpCurveIdTag>;

struct EnemyTaskEntryId {
    EnemyEntryId enemy{};
    std::uint32_t entry{};
    auto operator<=>(const EnemyTaskEntryId&) const = default;
};

struct EnemyEncounterEntryId {
    std::string owner{};
    std::uint32_t entry{};
    auto operator<=>(const EnemyEncounterEntryId&) const = default;
};

[[nodiscard]] std::string canonicalIdentity(EnemyEntryId id);
[[nodiscard]] std::string canonicalIdentity(EnemyEventEntryId id);
[[nodiscard]] std::string canonicalIdentity(EnemyTaskEntryId id);
[[nodiscard]] std::string canonicalIdentity(EnemyEncounterEntryId id);
[[nodiscard]] std::string canonicalIdentity(EnemyMagicEntryId id);
[[nodiscard]] std::string canonicalIdentity(AccessoryEntryId id);
[[nodiscard]] std::string canonicalIdentity(ArmorEntryId id);
[[nodiscard]] std::string canonicalIdentity(UsableItemEntryId id);
[[nodiscard]] std::string canonicalIdentity(WeaponEntryId id);
[[nodiscard]] std::string canonicalIdentity(WeaponEffectEntryId id);
[[nodiscard]] std::string canonicalIdentity(ExpCurveEntryId id);
[[nodiscard]] std::string canonicalIdentity(CharacterEntryId id);
[[nodiscard]] std::string canonicalIdentity(CharacterMagicEntryId id);
[[nodiscard]] std::string canonicalIdentity(CharacterSuperMoveEntryId id);
[[nodiscard]] std::string canonicalIdentity(MagicExpCurveEntryId id);

struct AlxEditableText {
    std::string text{};
    std::optional<std::uint32_t> messageId{};
    bool operator==(const AlxEditableText&) const = default;
};

struct EnemyItemDrop {
    std::int16_t probability{ -1 };
    std::int16_t amount{ -1 };
    std::int16_t itemId{ -1 };
    bool operator==(const EnemyItemDrop&) const = default;
};

struct EnemyFields {
    std::string japaneseName{};
    std::int8_t width{};
    std::int8_t depth{};
    std::int8_t elementId{};
    std::array<std::int8_t, 7> padding{};
    std::int16_t movementFlags{};
    std::int16_t counterPercent{};
    std::uint16_t experience{};
    std::uint16_t gold{};
    std::int32_t maxHp{};
    float unknown1{};
    std::array<std::int16_t, 6> elements{};
    std::array<std::int16_t, 15> states{};
    std::int16_t danger{};
    std::int8_t effectId{ -1 };
    std::int8_t stateId{};
    std::int8_t stateMissPercent{};
    std::int16_t level{};
    std::int16_t will{};
    std::int16_t vigor{};
    std::int16_t agile{};
    std::int16_t quick{};
    std::int16_t attack{};
    std::int16_t defense{};
    std::int16_t magicDefense{};
    std::int16_t hitPercent{};
    std::int16_t dodgePercent{};
    std::array<EnemyItemDrop, 4> itemDrops{};
    bool operator==(const EnemyFields&) const = default;
};

struct EnemyReference {
    std::optional<EnemyEntryId> enemy{};
    bool operator==(const EnemyReference&) const = default;
};

struct EnemyEncounterFields {
    std::uint8_t initiative{};
    std::uint8_t magicExperience{};
    std::array<EnemyReference, 8> enemies{};
    bool operator==(const EnemyEncounterFields&) const = default;
};

struct PlayerPlacement {
    std::optional<CharacterEntryId> character{};
    std::int8_t x{};
    std::int8_t z{};
    bool operator==(const PlayerPlacement&) const = default;
};

struct EnemyPlacement {
    std::optional<EnemyEntryId> enemy{};
    std::int8_t x{};
    std::int8_t z{};
    bool operator==(const EnemyPlacement&) const = default;
};

struct EnemyEventFields {
    std::uint8_t magicExperience{};
    std::array<PlayerPlacement, 4> players{};
    std::array<EnemyPlacement, 7> enemies{};
    std::uint8_t initiative{};
    std::int8_t defeatConditionId{};
    std::int8_t escapeConditionId{};
    std::optional<std::uint32_t> bgmId{};
    bool operator==(const EnemyEventFields&) const = default;
};

struct EnemyTaskFields {
    std::int16_t typeId{ -1 };
    std::int16_t taskId{ -1 };
    std::int16_t parameterId{ -1 };
    bool operator==(const EnemyTaskFields&) const = default;
};

struct EnemyMagicFields {
    AlxEditableText name{};
    std::vector<std::int8_t> padding{};
    std::int8_t categoryId{};
    std::int8_t effectId{ -1 };
    std::uint8_t scopeId{};
    std::uint16_t effectParameterId{};
    std::uint16_t effectBase{};
    std::int8_t elementId{};
    std::int8_t typeId{};
    std::int8_t stateInflictionId{};
    std::int8_t stateResistanceId{};
    std::int8_t stateId{};
    std::int8_t stateMissPercent{};
    bool operator==(const EnemyMagicFields&) const = default;
};

struct EquipmentTrait {
    std::int8_t id{};
    std::int8_t padding{};
    std::int16_t value{};
    bool operator==(const EquipmentTrait&) const = default;
};

struct EquipmentFields {
    AlxEditableText name{};
    std::uint8_t characterFlags{};
    std::int8_t sellPercent{};
    std::int8_t order1{ -1 };
    std::int8_t order2{ -1 };
    std::vector<std::int8_t> padding{};
    std::uint16_t buyPrice{};
    std::array<EquipmentTrait, 4> traits{};
    std::string description{};
    bool operator==(const EquipmentFields&) const = default;
};

struct UsableItemFields {
    AlxEditableText name{};
    std::uint8_t occasionFlags{};
    std::int8_t effectId{};
    std::uint8_t scopeId{};
    std::int8_t consumePercent{};
    std::int8_t sellPercent{};
    std::int8_t order1{ -1 };
    std::int8_t order2{ -1 };
    std::vector<std::int8_t> padding{};
    std::uint16_t buyPrice{};
    std::int16_t effectBase{};
    std::int8_t elementId{};
    std::int8_t typeId{};
    std::int16_t stateId{};
    std::int16_t stateMissPercent{};
    std::string description{};
    bool operator==(const UsableItemFields&) const = default;
};

struct WeaponFields {
    AlxEditableText name{};
    std::int8_t characterId{};
    std::int8_t sellPercent{};
    std::int8_t order1{ -1 };
    std::int8_t order2{ -1 };
    std::int8_t effectId{ -1 };
    std::vector<std::int8_t> padding{};
    std::uint16_t buyPrice{};
    std::int16_t attack{};
    std::int16_t hitPercent{};
    std::int8_t traitId{};
    std::int16_t traitValue{};
    std::string description{};
    bool operator==(const WeaponFields&) const = default;
};

struct WeaponEffectFields {
    std::string japaneseName{};
    std::int8_t effectId{ -1 };
    std::int8_t stateId{};
    std::int8_t stateMissPercent{};
    bool operator==(const WeaponEffectFields&) const = default;
};

struct ExpCurveFields {
    std::array<std::int32_t, 99> experience{};
    bool operator==(const ExpCurveFields&) const = default;
};

struct CharacterFields {
    std::string name{};
    std::int8_t age{};
    std::int8_t genderId{};
    std::int8_t width{};
    std::int8_t depth{};
    std::int8_t maxMp{};
    std::int8_t elementId{};
    std::int8_t padding1{};
    std::uint16_t weaponId{};
    std::uint16_t armorId{};
    std::uint16_t accessoryId{};
    std::int16_t movementFlags{};
    std::int16_t hp{};
    std::int16_t maxHp{};
    std::int16_t maxHpGrowth{};
    std::int16_t spirit{};
    std::int16_t maxSpirit{};
    std::int16_t counterPercent{};
    std::int16_t padding2{};
    std::uint32_t experience{};
    float maxMpGrowth{};
    float unknown1{};
    std::array<std::int16_t, 6> elements{};
    std::array<std::int16_t, 15> states{};
    std::int16_t danger{};
    std::int16_t power{};
    std::int16_t will{};
    std::int16_t vigor{};
    std::int16_t agile{};
    std::int16_t quick{};
    std::int16_t padding3{};
    std::array<float, 5> growth{};
    std::array<std::int32_t, 6> magicExperience{};
    bool operator==(const CharacterFields&) const = default;
};

struct CharacterSkillFields {
    AlxEditableText name{};
    std::int8_t elementId{};
    std::int16_t order{ -1 };
    std::uint8_t occasionFlags{};
    std::int8_t effectId{ -1 };
    std::uint8_t scopeId{};
    std::int8_t categoryId{};
    std::int8_t effectSpeed{ -1 };
    std::int8_t effectSpirit{ -1 };
    std::vector<std::int8_t> padding{};
    std::int16_t effectBase{};
    std::int8_t typeId{};
    std::int8_t stateId{};
    std::int8_t stateMissPercent{};
    std::int8_t shipOccasionId{};
    std::int16_t shipEffectId{ -1 };
    std::int8_t shipEffectSpirit{ -1 };
    std::int8_t shipEffectTurns{ -1 };
    std::int16_t shipEffectBase{};
    std::int8_t unknown{ -1 };
    std::string description{};
    std::optional<std::string> shipDescription{};
    bool operator==(const CharacterSkillFields&) const = default;
};

struct MagicExpCurveFields {
    std::array<std::array<std::uint16_t, 6>, 6> experience{};
    bool operator==(const MagicExpCurveFields&) const = default;
};

namespace detail { struct AlxModelAccess; }

template <typename Id, typename Fields>
class AlxRecord {
public:
    [[nodiscard]] Id id() const noexcept { return id_; }
    [[nodiscard]] Fields& fields() noexcept { return fields_; }
    [[nodiscard]] const Fields& fields() const noexcept { return fields_; }
    bool operator==(const AlxRecord&) const = default;

private:
    friend struct detail::AlxModelAccess;
    Id id_{};
    Fields fields_{};
};

using EnemyRecord = AlxRecord<EnemyEntryId, EnemyFields>;
using EnemyEncounterRecord = AlxRecord<EnemyEncounterEntryId, EnemyEncounterFields>;
using EnemyEventRecord = AlxRecord<EnemyEventEntryId, EnemyEventFields>;
using EnemyTaskRecord = AlxRecord<EnemyTaskEntryId, EnemyTaskFields>;
using EnemyMagicRecord = AlxRecord<EnemyMagicEntryId, EnemyMagicFields>;
using AccessoryRecord = AlxRecord<AccessoryEntryId, EquipmentFields>;
using ArmorRecord = AlxRecord<ArmorEntryId, EquipmentFields>;
using UsableItemRecord = AlxRecord<UsableItemEntryId, UsableItemFields>;
using WeaponRecord = AlxRecord<WeaponEntryId, WeaponFields>;
using WeaponEffectRecord = AlxRecord<WeaponEffectEntryId, WeaponEffectFields>;
using ExpCurveRecord = AlxRecord<ExpCurveEntryId, ExpCurveFields>;
using CharacterRecord = AlxRecord<CharacterEntryId, CharacterFields>;
using CharacterMagicRecord = AlxRecord<CharacterMagicEntryId, CharacterSkillFields>;
using CharacterSuperMoveRecord = AlxRecord<CharacterSuperMoveEntryId, CharacterSkillFields>;
using MagicExpCurveRecord = AlxRecord<MagicExpCurveEntryId, MagicExpCurveFields>;

template <typename Record, typename Id, typename Fields>
class AlxFlatTable {
public:
    [[nodiscard]] std::span<const Record> records() const noexcept { return records_; }
    [[nodiscard]] Fields* edit(Id id) noexcept
    {
        for (auto& record : records_) {
            if (record.id() == id) return &record.fields();
        }
        return nullptr;
    }
    [[nodiscard]] const Fields* find(Id id) const noexcept
    {
        for (const auto& record : records_) {
            if (record.id() == id) return &record.fields();
        }
        return nullptr;
    }
    bool operator==(const AlxFlatTable&) const = default;

private:
    friend struct detail::AlxModelAccess;
    std::vector<Record> records_{};
};

using EnemyTable = AlxFlatTable<EnemyRecord, EnemyEntryId, EnemyFields>;
using EnemyEventTable = AlxFlatTable<EnemyEventRecord, EnemyEventEntryId, EnemyEventFields>;
using EnemyMagicTable = AlxFlatTable<EnemyMagicRecord, EnemyMagicEntryId, EnemyMagicFields>;
using AccessoryTable = AlxFlatTable<AccessoryRecord, AccessoryEntryId, EquipmentFields>;
using ArmorTable = AlxFlatTable<ArmorRecord, ArmorEntryId, EquipmentFields>;
using UsableItemTable = AlxFlatTable<UsableItemRecord, UsableItemEntryId, UsableItemFields>;
using WeaponTable = AlxFlatTable<WeaponRecord, WeaponEntryId, WeaponFields>;
using WeaponEffectTable = AlxFlatTable<WeaponEffectRecord, WeaponEffectEntryId, WeaponEffectFields>;
using ExpCurveTable = AlxFlatTable<ExpCurveRecord, ExpCurveEntryId, ExpCurveFields>;
using CharacterTable = AlxFlatTable<CharacterRecord, CharacterEntryId, CharacterFields>;
using CharacterMagicTable = AlxFlatTable<CharacterMagicRecord, CharacterMagicEntryId, CharacterSkillFields>;
using CharacterSuperMoveTable = AlxFlatTable<CharacterSuperMoveRecord, CharacterSuperMoveEntryId, CharacterSkillFields>;
using MagicExpCurveTable = AlxFlatTable<MagicExpCurveRecord, MagicExpCurveEntryId, MagicExpCurveFields>;

class EnemyTaskGroup {
public:
    [[nodiscard]] EnemyEntryId enemyId() const noexcept { return enemyId_; }
    [[nodiscard]] std::span<const EnemyTaskRecord> records() const noexcept { return records_; }
    bool operator==(const EnemyTaskGroup&) const = default;
private:
    friend struct detail::AlxModelAccess;
    friend class EnemyTaskTable;
    EnemyEntryId enemyId_{};
    std::vector<EnemyTaskRecord> records_{};
};

class EnemyTaskTable {
public:
    [[nodiscard]] std::span<const EnemyTaskGroup> groups() const noexcept { return groups_; }
    [[nodiscard]] EnemyTaskFields* edit(EnemyTaskEntryId id) noexcept;
    [[nodiscard]] const EnemyTaskFields* find(EnemyTaskEntryId id) const noexcept;
    bool operator==(const EnemyTaskTable&) const = default;
private:
    friend struct detail::AlxModelAccess;
    std::vector<EnemyTaskGroup> groups_{};
};

class EnemyEncounterGroup {
public:
    [[nodiscard]] std::string_view owner() const noexcept { return owner_; }
    [[nodiscard]] std::span<const EnemyEncounterRecord> records() const noexcept { return records_; }
    bool operator==(const EnemyEncounterGroup&) const = default;
private:
    friend struct detail::AlxModelAccess;
    friend class EnemyEncounterTable;
    std::string owner_{};
    std::vector<EnemyEncounterRecord> records_{};
};

class EnemyEncounterTable {
public:
    [[nodiscard]] std::span<const EnemyEncounterGroup> groups() const noexcept { return groups_; }
    [[nodiscard]] EnemyEncounterFields* edit(const EnemyEncounterEntryId& id) noexcept;
    [[nodiscard]] const EnemyEncounterFields* find(const EnemyEncounterEntryId& id) const noexcept;
    bool operator==(const EnemyEncounterTable&) const = default;
private:
    friend struct detail::AlxModelAccess;
    std::vector<EnemyEncounterGroup> groups_{};
};

struct AlxDataset {
    std::optional<EnemyTable> enemies{};
    std::optional<EnemyEncounterTable> enemyEncounters{};
    std::optional<EnemyEventTable> enemyEvents{};
    std::optional<EnemyTaskTable> enemyTasks{};
    std::optional<EnemyMagicTable> enemyMagic{};
    std::optional<AccessoryTable> accessories{};
    std::optional<ArmorTable> armor{};
    std::optional<UsableItemTable> usableItems{};
    std::optional<WeaponTable> weapons{};
    std::optional<WeaponEffectTable> weaponEffects{};
    std::optional<ExpCurveTable> experienceCurves{};
    std::optional<CharacterTable> characters{};
    std::optional<CharacterMagicTable> characterMagic{};
    std::optional<CharacterSuperMoveTable> characterSuperMoves{};
    std::optional<MagicExpCurveTable> magicExperienceCurves{};
    bool operator==(const AlxDataset&) const = default;
};

namespace detail {

struct AlxModelAccess {
    template <typename Record, typename Id, typename Fields>
    static void append(AlxFlatTable<Record, Id, Fields>& table, Id id, Fields fields)
    {
        Record record{};
        record.id_ = std::move(id);
        record.fields_ = std::move(fields);
        table.records_.push_back(std::move(record));
    }
    static EnemyTaskGroup& appendTaskGroup(EnemyTaskTable& table, EnemyEntryId enemy);
    static void appendTask(EnemyTaskGroup& group, EnemyTaskEntryId id, EnemyTaskFields fields);
    static EnemyEncounterGroup& appendEncounterGroup(EnemyEncounterTable& table, std::string owner);
    static void appendEncounter(EnemyEncounterGroup& group, EnemyEncounterEntryId id, EnemyEncounterFields fields);
};

} // namespace detail

} // namespace spice::trade::alx
