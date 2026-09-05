#include "AlxDerivedView.h"

#include <algorithm>

namespace spice::trade::alx {

const AlxDerivedCells* AlxDerivedContext::imported(std::string_view identity) const noexcept
{
    const auto found = imported_.find(identity);
    return found == imported_.end() ? nullptr : &found->second;
}

std::optional<std::string_view> AlxDerivedContext::enemyName(EnemyEntryId id, AlxLocale locale) const noexcept
{
    const auto found = enemyNames_.find({ id, locale });
    if (found == enemyNames_.end()) return std::nullopt;
    return found->second;
}

std::optional<std::string_view> AlxDerivedContext::characterName(CharacterEntryId id) const noexcept
{
    const auto found = characterNames_.find(id);
    if (found == characterNames_.end()) return std::nullopt;
    return found->second;
}

void detail::AlxDerivedContextAccess::addImported(
    AlxDerivedContext& context, std::string identity, AlxDerivedCells cells)
{
    context.imported_.emplace(std::move(identity), std::move(cells));
}

void detail::AlxDerivedContextAccess::addEnemyName(
    AlxDerivedContext& context, EnemyEntryId id, AlxLocale locale, std::string name)
{
    context.enemyNames_.insert_or_assign({ id, locale }, std::move(name));
}

void detail::AlxDerivedContextAccess::addCharacterName(
    AlxDerivedContext& context, CharacterEntryId id, std::string name)
{
    context.characterNames_.insert_or_assign(id, std::move(name));
}

void detail::AlxDerivedContextAccess::merge(AlxDerivedContext& destination, AlxDerivedContext source)
{
    destination.imported_.merge(source.imported_);
    destination.enemyNames_.merge(source.enemyNames_);
    destination.characterNames_.merge(source.characterNames_);
}

namespace {

void seed(AlxDerivedView& view, const AlxDerivedContext& context, std::string identity)
{
    AlxDerivedRow row{ .identity = std::move(identity) };
    if (const auto* cells = context.imported(row.identity)) row.cells = *cells;
    view.rows.push_back(std::move(row));
}

void setResolved(
    AlxDerivedRow& row,
    std::string column,
    std::optional<std::string_view> value,
    AlxDerivedView& view)
{
    const auto imported = row.cells.find(column);
    if (!value) {
        row.cells.erase(column);
        return;
    }
    if (imported != row.cells.end() && imported->second != *value) {
        view.diagnostics.push_back({
            .code = AlxDiagnosticCode::DerivedValueMismatch,
            .severity = AlxDiagnosticSeverity::Warning,
            .message = "Imported derived value differs from the value resolved from the current dataset",
        });
    }
    row.cells[std::move(column)] = std::string(*value);
}

std::optional<std::string_view> currentEnemyName(const AlxDataset& dataset, EnemyEntryId id)
{
    if (!dataset.enemies) return std::nullopt;
    const auto* fields = dataset.enemies->find(id);
    return fields ? std::optional<std::string_view>{ fields->japaneseName } : std::nullopt;
}

std::optional<std::string_view> currentCharacterName(const AlxDataset& dataset, CharacterEntryId id)
{
    if (!dataset.characters) return std::nullopt;
    const auto* fields = dataset.characters->find(id);
    return fields ? std::optional<std::string_view>{ fields->name } : std::nullopt;
}

void movementFlags(AlxDerivedRow& row, std::int16_t flags, AlxDerivedView& view)
{
    constexpr std::array<std::string_view, 12> columns{
        "[May Dodge]", "[Unk Damage]", "[Unk Ranged]", "[Unk Melee]",
        "[Ranged Atk]", "[Melee Atk]", "[Ranged Only]", "[Take Cover]",
        "[In Air]", "[On Ground]", "[Reserved]", "[May Move]" };
    const auto bits = static_cast<std::uint16_t>(flags);
    for (std::size_t i = 0; i < columns.size(); ++i) {
        setResolved(row, std::string(columns[i]), (bits & (0x800U >> i)) != 0
            ? std::optional<std::string_view>{ "X" } : std::optional<std::string_view>{ "" }, view);
    }
}

void characterFlags(AlxDerivedRow& row, std::uint8_t flags, AlxDerivedView& view)
{
    constexpr std::array<std::string_view, 6> columns{ "[V]", "[A]", "[F]", "[D]", "[E]", "[G]" };
    for (std::size_t i = 0; i < columns.size(); ++i) {
        setResolved(row, std::string(columns[i]), (flags & (0x20U >> i)) != 0
            ? std::optional<std::string_view>{ "X" } : std::optional<std::string_view>{ "" }, view);
    }
}

void occasionFlags(AlxDerivedRow& row, std::uint8_t flags, AlxDerivedView& view)
{
    constexpr std::array<std::string_view, 3> columns{ "[M]", "[B]", "[S]" };
    for (std::size_t i = 0; i < columns.size(); ++i) {
        setResolved(row, std::string(columns[i]), (flags & (0x4U >> i)) != 0
            ? std::optional<std::string_view>{ "X" } : std::optional<std::string_view>{ "" }, view);
    }
}

} // namespace

AlxDerivedView AlxDerivedViewBuilder::build(
    const AlxDataset& dataset,
    const AlxDerivedContext& context,
    AlxTableKind table) const
{
    AlxDerivedView view{};
    switch (table) {
    case AlxTableKind::Enemy:
        if (dataset.enemies) for (const auto& record : dataset.enemies->records()) {
            seed(view, context, canonicalIdentity(record.id()));
            movementFlags(view.rows.back(), record.fields().movementFlags, view);
        }
        break;
    case AlxTableKind::EnemyEncounter:
        if (dataset.enemyEncounters) for (const auto& group : dataset.enemyEncounters->groups()) {
            for (const auto& record : group.records()) {
                seed(view, context, canonicalIdentity(record.id()));
                auto& row = view.rows.back();
                for (std::size_t i = 0; i < record.fields().enemies.size(); ++i) {
                    const auto& ref = record.fields().enemies[i];
                    const auto prefix = "[EC" + std::to_string(i + 1) + " JP Name]";
                    setResolved(row, prefix, ref.enemy ? currentEnemyName(dataset, *ref.enemy) : std::nullopt, view);
                    for (const auto locale : { AlxLocale::UnitedStates, AlxLocale::Europe }) {
                        const auto column = "[EC" + std::to_string(i + 1)
                            + (locale == AlxLocale::UnitedStates ? " US Name]" : " EU Name]");
                        if (row.cells.contains(column)) setResolved(row, column,
                            ref.enemy ? context.enemyName(*ref.enemy, locale) : std::nullopt, view);
                    }
                }
            }
        }
        break;
    case AlxTableKind::EnemyEvent:
        if (dataset.enemyEvents) for (const auto& record : dataset.enemyEvents->records()) {
            seed(view, context, canonicalIdentity(record.id()));
            auto& row = view.rows.back();
            for (std::size_t i = 0; i < record.fields().players.size(); ++i) {
                const auto& placement = record.fields().players[i];
                setResolved(row, "[PC" + std::to_string(i + 1) + " Name]",
                    placement.character ? currentCharacterName(dataset, *placement.character) : std::nullopt, view);
            }
            for (std::size_t i = 0; i < record.fields().enemies.size(); ++i) {
                const auto& placement = record.fields().enemies[i];
                setResolved(row, "[EC" + std::to_string(i + 1) + " JP Name]",
                    placement.enemy ? currentEnemyName(dataset, *placement.enemy) : std::nullopt, view);
                for (const auto locale : { AlxLocale::UnitedStates, AlxLocale::Europe }) {
                    const auto column = "[EC" + std::to_string(i + 1)
                        + (locale == AlxLocale::UnitedStates ? " US Name]" : " EU Name]");
                    if (row.cells.contains(column)) setResolved(row, column,
                        placement.enemy ? context.enemyName(*placement.enemy, locale) : std::nullopt, view);
                }
            }
        }
        break;
    case AlxTableKind::EnemyTask:
        if (dataset.enemyTasks) for (const auto& group : dataset.enemyTasks->groups()) {
            for (const auto& record : group.records()) {
                seed(view, context, canonicalIdentity(record.id()));
                auto& row = view.rows.back();
                setResolved(row, "[EC JP Name]", currentEnemyName(dataset, group.enemyId()), view);
                for (const auto locale : { AlxLocale::UnitedStates, AlxLocale::Europe }) {
                    const auto column = locale == AlxLocale::UnitedStates ? "[EC US Name]" : "[EC EU Name]";
                    if (row.cells.contains(column)) setResolved(row, column, context.enemyName(group.enemyId(), locale), view);
                }
            }
        }
        break;
#define FLAT_CASE(Kind, Member) \
    case AlxTableKind::Kind: if (dataset.Member) for (const auto& record : dataset.Member->records()) \
        seed(view, context, canonicalIdentity(record.id())); break
    FLAT_CASE(EnemyMagic, enemyMagic);
    case AlxTableKind::Accessory:
        if (dataset.accessories) for (const auto& record : dataset.accessories->records()) {
            seed(view, context, canonicalIdentity(record.id())); characterFlags(view.rows.back(), record.fields().characterFlags, view);
        }
        break;
    case AlxTableKind::Armor:
        if (dataset.armor) for (const auto& record : dataset.armor->records()) {
            seed(view, context, canonicalIdentity(record.id())); characterFlags(view.rows.back(), record.fields().characterFlags, view);
        }
        break;
    case AlxTableKind::UsableItem:
        if (dataset.usableItems) for (const auto& record : dataset.usableItems->records()) {
            seed(view, context, canonicalIdentity(record.id())); occasionFlags(view.rows.back(), record.fields().occasionFlags, view);
        }
        break;
    FLAT_CASE(Weapon, weapons);
    FLAT_CASE(WeaponEffect, weaponEffects);
    FLAT_CASE(ExpCurve, experienceCurves);
    case AlxTableKind::Character:
        if (dataset.characters) for (const auto& record : dataset.characters->records()) {
            seed(view, context, canonicalIdentity(record.id())); movementFlags(view.rows.back(), record.fields().movementFlags, view);
        }
        break;
    case AlxTableKind::CharacterMagic:
        if (dataset.characterMagic) for (const auto& record : dataset.characterMagic->records()) {
            seed(view, context, canonicalIdentity(record.id())); occasionFlags(view.rows.back(), record.fields().occasionFlags, view);
        }
        break;
    case AlxTableKind::CharacterSuperMove:
        if (dataset.characterSuperMoves) for (const auto& record : dataset.characterSuperMoves->records()) {
            seed(view, context, canonicalIdentity(record.id())); occasionFlags(view.rows.back(), record.fields().occasionFlags, view);
        }
        break;
    FLAT_CASE(MagicExpCurve, magicExperienceCurves);
#undef FLAT_CASE
    }
    return view;
}

} // namespace spice::trade::alx
