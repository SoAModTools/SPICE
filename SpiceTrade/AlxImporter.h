#pragma once

#include "AlxDerivedView.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace spice::trade::alx {

struct AlxImportOptions {
    std::optional<AlxLocale> localeHint{};
};

struct AlxImportMetadata {
    AlxTableKind table{};
    AlxLocale locale{};
    std::filesystem::path path{};
    std::uint64_t rawSourceSize{};
    std::array<std::uint8_t, 32U> rawSourceSha256{};
    std::size_t sourceRows{};
    std::size_t publishedRows{};
    std::size_t excludedRows{};
    bool operator==(const AlxImportMetadata&) const = default;
};

template <typename Table>
struct AlxTableImportResult {
    std::optional<Table> table{};
    std::optional<AlxImportMetadata> metadata{};
    AlxDerivedContext derivedContext{};
    std::vector<AlxDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept
    {
        return table.has_value() && metadata.has_value() && !hasErrors(diagnostics);
    }
};

using EnemyImportResult = AlxTableImportResult<EnemyTable>;
using EnemyEncounterImportResult = AlxTableImportResult<EnemyEncounterTable>;
using EnemyEventImportResult = AlxTableImportResult<EnemyEventTable>;
using EnemyTaskImportResult = AlxTableImportResult<EnemyTaskTable>;
using EnemyMagicImportResult = AlxTableImportResult<EnemyMagicTable>;
using AccessoryImportResult = AlxTableImportResult<AccessoryTable>;
using ArmorImportResult = AlxTableImportResult<ArmorTable>;
using UsableItemImportResult = AlxTableImportResult<UsableItemTable>;
using WeaponImportResult = AlxTableImportResult<WeaponTable>;
using WeaponEffectImportResult = AlxTableImportResult<WeaponEffectTable>;
using ExpCurveImportResult = AlxTableImportResult<ExpCurveTable>;
using CharacterImportResult = AlxTableImportResult<CharacterTable>;
using CharacterMagicImportResult = AlxTableImportResult<CharacterMagicTable>;
using CharacterSuperMoveImportResult = AlxTableImportResult<CharacterSuperMoveTable>;
using MagicExpCurveImportResult = AlxTableImportResult<MagicExpCurveTable>;

#define DECLARE_IMPORTER(Name, Result) \
class Name##CsvImporter { public: \
    [[nodiscard]] Result importBytes(std::span<const std::uint8_t> bytes, \
        const AlxImportOptions& options = {}, const std::filesystem::path& path = {}) const; \
    [[nodiscard]] Result importFile(const std::filesystem::path& path, \
        const AlxImportOptions& options = {}) const; \
}

DECLARE_IMPORTER(Enemy, EnemyImportResult);
DECLARE_IMPORTER(EnemyEncounter, EnemyEncounterImportResult);
DECLARE_IMPORTER(EnemyEvent, EnemyEventImportResult);
DECLARE_IMPORTER(EnemyTask, EnemyTaskImportResult);
DECLARE_IMPORTER(EnemyMagic, EnemyMagicImportResult);
DECLARE_IMPORTER(Accessory, AccessoryImportResult);
DECLARE_IMPORTER(Armor, ArmorImportResult);
DECLARE_IMPORTER(UsableItem, UsableItemImportResult);
DECLARE_IMPORTER(Weapon, WeaponImportResult);
DECLARE_IMPORTER(WeaponEffect, WeaponEffectImportResult);
DECLARE_IMPORTER(ExpCurve, ExpCurveImportResult);
DECLARE_IMPORTER(Character, CharacterImportResult);
DECLARE_IMPORTER(CharacterMagic, CharacterMagicImportResult);
DECLARE_IMPORTER(CharacterSuperMove, CharacterSuperMoveImportResult);
DECLARE_IMPORTER(MagicExpCurve, MagicExpCurveImportResult);
#undef DECLARE_IMPORTER

struct AlxDatasetImportResult {
    std::optional<AlxDataset> dataset{};
    std::optional<AlxLocale> locale{};
    std::vector<AlxImportMetadata> metadata{};
    AlxDerivedContext derivedContext{};
    std::vector<AlxDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const noexcept
    {
        return dataset.has_value() && locale.has_value() && !hasErrors(diagnostics);
    }
};

class AlxDatasetImporter {
public:
    [[nodiscard]] AlxDatasetImportResult importDirectory(
        const std::filesystem::path& directory,
        std::span<const AlxTableKind> requested,
        const AlxImportOptions& options = {}) const;

    [[nodiscard]] AlxDatasetImportResult importWhitelistedDirectory(
        const std::filesystem::path& directory,
        const AlxImportOptions& options = {}) const;
};

} // namespace spice::trade::alx
