#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace spice::trade::alx {

enum class AlxTableKind {
    Enemy,
    EnemyEncounter,
    EnemyEvent,
};

enum class AlxLocale {
    Japanese,
    UnitedStates,
    Europe,
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

struct LocalizedName {
    std::string japanese{};
    std::optional<std::string> localized{};

    bool operator==(const LocalizedName&) const = default;
};

struct EnemyMovementIndicators {
    std::string mayDodge{};
    std::string unknownDamage{};
    std::string unknownRanged{};
    std::string unknownMelee{};
    std::string rangedAttack{};
    std::string meleeAttack{};
    std::string rangedOnly{};
    std::string takeCover{};
    std::string inAir{};
    std::string onGround{};
    std::string reserved{};
    std::string mayMove{};

    bool operator==(const EnemyMovementIndicators&) const = default;
};

struct EnemyItemDrop {
    std::int16_t probability{ -1 };
    std::int16_t amount{ -1 };
    std::int16_t itemId{ -1 };
    std::string itemName{};

    bool operator==(const EnemyItemDrop&) const = default;
};

struct EnemyRecord {
    std::uint32_t entryId{};
    std::vector<std::string> filters{};
    LocalizedName name{};
    std::int8_t width{};
    std::int8_t depth{};
    std::int8_t elementId{};
    std::string elementName{};
    std::array<std::int8_t, 7U> padding{};
    std::int16_t movementFlags{};
    EnemyMovementIndicators movementIndicators{};
    std::int16_t counterPercent{};
    std::uint16_t experience{};
    std::uint16_t gold{};
    std::int32_t maxHp{};
    float unknown1{};
    // Green, Red, Purple, Blue, Yellow, Silver.
    std::array<std::int16_t, 6U> elementValues{};
    // Poison, Unconscious, Stone, Sleep, Confusion, Silence, Fatigue,
    // Revival, Weak, and State 10 through State 15.
    std::array<std::int16_t, 15U> stateValues{};
    std::int16_t danger{};
    std::int8_t effectId{ -1 };
    std::string effectName{};
    std::int8_t stateId{};
    std::string stateName{};
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
    std::array<EnemyItemDrop, 4U> itemDrops{};

    bool operator==(const EnemyRecord&) const = default;
};

struct EnemyTable {
    std::vector<EnemyRecord> records{};

    bool operator==(const EnemyTable&) const = default;
};

struct EnemyReference {
    std::uint8_t enemyId{ 255U };
    LocalizedName name{};

    bool operator==(const EnemyReference&) const = default;
};

struct EnemyEncounterRecord {
    std::uint32_t entryId{};
    std::string filter{};
    std::uint8_t initiative{};
    std::uint8_t magicExperience{};
    std::array<EnemyReference, 8U> enemies{};

    bool operator==(const EnemyEncounterRecord&) const = default;
};

struct EnemyEncounterTable {
    std::vector<EnemyEncounterRecord> records{};

    bool operator==(const EnemyEncounterTable&) const = default;
};

struct PlayerPlacement {
    std::int8_t characterId{ -1 };
    std::string characterName{};
    std::int8_t x{ -1 };
    std::int8_t z{ -1 };

    bool operator==(const PlayerPlacement&) const = default;
};

struct EnemyPlacement {
    EnemyReference enemy{};
    std::int8_t x{ -1 };
    std::int8_t z{ -1 };

    bool operator==(const EnemyPlacement&) const = default;
};

struct ConditionReference {
    std::int8_t conditionId{};
    std::string conditionName{};

    bool operator==(const ConditionReference&) const = default;
};

struct EnemyEventRecord {
    std::uint32_t entryId{};
    std::uint8_t magicExperience{};
    std::array<PlayerPlacement, 4U> players{};
    std::array<EnemyPlacement, 7U> enemies{};
    std::uint8_t initiative{};
    ConditionReference defeatCondition{};
    ConditionReference escapeCondition{};
    // ALX writes -1 for no BGM even though its property is declared :u32.
    std::optional<std::uint32_t> bgmId{};

    bool operator==(const EnemyEventRecord&) const = default;
};

struct EnemyEventTable {
    std::vector<EnemyEventRecord> records{};

    bool operator==(const EnemyEventTable&) const = default;
};

} // namespace spice::trade::alx
