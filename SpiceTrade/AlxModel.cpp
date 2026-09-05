#include "AlxModel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace spice::trade::alx {
namespace {

constexpr std::array<AlxTableDescriptor, 15> kWhitelist{
    AlxTableDescriptor{ AlxTableKind::Enemy, "enemy.csv" },
    AlxTableDescriptor{ AlxTableKind::EnemyEncounter, "enemyencounter.csv" },
    AlxTableDescriptor{ AlxTableKind::EnemyEvent, "enemyevent.csv" },
    AlxTableDescriptor{ AlxTableKind::EnemyTask, "enemytask.csv" },
    AlxTableDescriptor{ AlxTableKind::EnemyMagic, "enemymagic.csv" },
    AlxTableDescriptor{ AlxTableKind::Accessory, "accessory.csv" },
    AlxTableDescriptor{ AlxTableKind::Armor, "armor.csv" },
    AlxTableDescriptor{ AlxTableKind::UsableItem, "usableitem.csv" },
    AlxTableDescriptor{ AlxTableKind::Weapon, "weapon.csv" },
    AlxTableDescriptor{ AlxTableKind::WeaponEffect, "weaponeffect.csv" },
    AlxTableDescriptor{ AlxTableKind::ExpCurve, "expcurve.csv" },
    AlxTableDescriptor{ AlxTableKind::Character, "character.csv" },
    AlxTableDescriptor{ AlxTableKind::CharacterMagic, "charactermagic.csv" },
    AlxTableDescriptor{ AlxTableKind::CharacterSuperMove, "charactersupermove.csv" },
    AlxTableDescriptor{ AlxTableKind::MagicExpCurve, "magicexpcurve.csv" },
};

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

template <typename Id>
std::string simpleIdentity(std::string_view prefix, Id id)
{
    return std::string(prefix) + "." + std::to_string(id.value);
}

std::vector<std::string> headers(std::string_view line)
{
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin <= line.size()) {
        const auto end = line.find(',', begin);
        result.emplace_back(line.substr(begin, end == std::string_view::npos ? end : end - begin));
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return result;
}

std::string regionCode(AlxLocale locale)
{
    switch (locale) {
    case AlxLocale::Japanese: return "JP";
    case AlxLocale::UnitedStates: return "US";
    case AlxLocale::Europe: return "EU";
    }
    return {};
}

void appendEnemyReferences(
    std::vector<std::string>& result, std::size_t count, AlxLocale locale, bool placement)
{
    for (std::size_t i = 1; i <= count; ++i) {
        const auto prefix = "EC" + std::to_string(i);
        result.push_back(prefix + " ID");
        result.push_back("[" + prefix + " JP Name]");
        if (locale != AlxLocale::Japanese) {
            result.push_back("[" + prefix + " " + regionCode(locale) + " Name]");
        }
        if (placement) {
            result.push_back(prefix + " X");
            result.push_back(prefix + " Z");
        }
    }
}

} // namespace

std::span<const AlxTableDescriptor> whitelistedTables() noexcept { return kWhitelist; }

std::optional<AlxTableKind> whitelistedTableKind(const std::filesystem::path& path) noexcept
{
    const auto basename = lowerAscii(path.filename().string());
    const auto found = std::find_if(kWhitelist.begin(), kWhitelist.end(), [&](const auto& item) {
        return item.filename == basename;
    });
    if (found == kWhitelist.end()) return std::nullopt;
    return found->kind;
}

std::string_view canonicalFilename(AlxTableKind kind) noexcept
{
    const auto found = std::find_if(kWhitelist.begin(), kWhitelist.end(), [&](const auto& item) {
        return item.kind == kind;
    });
    return found == kWhitelist.end() ? std::string_view{} : found->filename;
}

std::vector<std::string> canonicalHeaders(AlxTableKind kind, AlxLocale locale)
{
    const auto code = regionCode(locale);
    switch (kind) {
    case AlxTableKind::Enemy: {
        auto result = headers("Entry ID,[Filter],Entry JP Name");
        if (locale != AlxLocale::Japanese) result.push_back("[Entry " + code + " Name]");
        auto tail = headers("Width,Depth,Element ID,[Element Name],Pad 1,Pad 2,Movement Flags,[May Dodge],[Unk Damage],[Unk Ranged],[Unk Melee],[Ranged Atk],[Melee Atk],[Ranged Only],[Take Cover],[In Air],[On Ground],[Reserved],[May Move],Counter%,EXP,Gold,Pad 3,Pad 4,MAXHP,Unk 1,Green,Red,Purple,Blue,Yellow,Silver,Poison,Unconscious,Stone,Sleep,Confusion,Silence,Fatigue,Revival,Weak,State 10,State 11,State 12,State 13,State 14,State 15,Danger,Effect ID,[Effect Name],State ID,[State Name],State Miss%,Pad 5,Level,Will,Vigor,Agile,Quick,Attack,Defense,MagDef,Hit%,Dodge%,Pad 6,Pad 7,Item 1 Prob,Item 1 Amount,Item 1 ID,[Item 1 Name],Item 2 Prob,Item 2 Amount,Item 2 ID,[Item 2 Name],Item 3 Prob,Item 3 Amount,Item 3 ID,[Item 3 Name],Item 4 Prob,Item 4 Amount,Item 4 ID,[Item 4 Name]");
        result.insert(result.end(), tail.begin(), tail.end());
        return result;
    }
    case AlxTableKind::EnemyEncounter: {
        auto result = headers("Entry ID,[Filter],Initiative,Magic EXP");
        appendEnemyReferences(result, 8, locale, false);
        return result;
    }
    case AlxTableKind::EnemyEvent: {
        auto result = headers("Entry ID,Magic EXP");
        for (std::size_t i = 1; i <= 4; ++i) {
            const auto p = "PC" + std::to_string(i);
            result.insert(result.end(), { p + " ID", "[" + p + " Name]", p + " X", p + " Z" });
        }
        appendEnemyReferences(result, 7, locale, true);
        auto tail = headers("Initiative,Defeat Cond ID,[Defeat Cond Name],Escape Cond ID,[Escape Cond Name],BGM ID");
        result.insert(result.end(), tail.begin(), tail.end());
        return result;
    }
    case AlxTableKind::EnemyTask: {
        auto result = headers("Entry ID,[Filter],[EC ID],[EC JP Name]");
        if (locale != AlxLocale::Japanese) result.push_back("[EC " + code + " Name]");
        auto tail = headers("Type ID,[Type Name],Task ID,[Task Name],Param ID,[Param Name]");
        result.insert(result.end(), tail.begin(), tail.end());
        return result;
    }
    case AlxTableKind::EnemyMagic:
        if (locale == AlxLocale::Europe) return headers("Entry ID,EU SOT Pos,[Entry GB Name],Category ID,[Category Name],Effect ID,[Effect Name],Scope ID,[Scope Name],Pad 1,Effect Param ID,Effect Param Name,Effect Base,Element ID,[Element Name],Type ID,[Type Name],State Inflict ID,State Inflict Name,State Resist ID,State Resist Name,State ID,[State Name],State Miss%,Pad 2,Pad 3");
        return headers("Entry ID,Entry " + code + " Name,Pad 1,Pad 2,Pad 3,Pad 4,Category ID,[Category Name],Effect ID,[Effect Name],Scope ID,[Scope Name],Effect Param ID,Effect Param Name,Effect Base,Element ID,[Element Name],Type ID,[Type Name],State Inflict ID,State Inflict Name,State Resist ID,State Resist Name,State ID,[State Name],State Miss%,Pad 5,Pad 6");
    case AlxTableKind::Accessory:
    case AlxTableKind::Armor:
        if (locale == AlxLocale::Europe) return headers("Entry ID,EU SOT Pos,[Entry GB Name],PC Flags,[V],[A],[F],[D],[E],[G],Sell%,EU Order 1,EU Order 2,Buy,Trait 1 ID,[Trait 1 Name],Pad 1,Trait 1 Value,Trait 2 ID,[Trait 2 Name],Pad 2,Trait 2 Value,Trait 3 ID,[Trait 3 Name],Pad 3,Trait 3 Value,Trait 4 ID,[Trait 4 Name],Pad 4,Trait 4 Value,Pad 5,Pad 6,[GB Descr Str]");
        return headers("Entry ID,Entry " + code + " Name,PC Flags,[V],[A],[F],[D],[E],[G],Sell%," + code + " Order 1," + code + " Order 2,Pad 1,Buy,Trait 1 ID,[Trait 1 Name],Pad 2,Trait 1 Value,Trait 2 ID,[Trait 2 Name],Pad 3,Trait 2 Value,Trait 3 ID,[Trait 3 Name],Pad 4,Trait 3 Value,Trait 4 ID,[Trait 4 Name],Pad 5,Trait 4 Value,[" + code + " Descr Pos],[" + code + " Descr Size]," + code + " Descr Str");
    case AlxTableKind::UsableItem:
        if (locale == AlxLocale::Europe) return headers("Entry ID,EU SOT Pos,[Entry GB Name],Occasion Flags,[M],[B],[S],Effect ID,[Effect Name],Scope ID,[Scope Name],Consume%,Sell%,EU Order 1,EU Order 2,Pad 1,Buy,Pad 2,Pad 3,Effect Base,Element ID,[Element Name],Type ID,[Type Name],State ID,[State Name],State Miss%,[GB Descr Str]");
        return headers("Entry ID,Entry " + code + " Name,Occasion Flags,[M],[B],[S],Effect ID,[Effect Name],Scope ID,[Scope Name],Consume%,Sell%," + code + " Order 1," + code + " Order 2,Buy,Pad 1,Pad 2,Effect Base,Element ID,[Element Name],Type ID,[Type Name],State ID,[State Name],State Miss%,[" + code + " Descr Pos],[" + code + " Descr Size]," + code + " Descr Str");
    case AlxTableKind::Weapon:
        if (locale == AlxLocale::Europe) return headers("Entry ID,EU SOT Pos,[Entry GB Name],PC ID,[PC Name],Sell%,EU Order 1,EU Order 2,Effect ID,[Effect Name],Pad 1,Buy,Attack,Hit%,Trait ID,[Trait Name],Pad 2,Trait Value,[GB Descr Str]");
        return headers("Entry ID,Entry " + code + " Name,PC ID,[PC Name],Sell%," + code + " Order 1," + code + " Order 2,Effect ID,[Effect Name],Buy,Attack,Hit%,Trait ID,[Trait Name],Pad 1,Trait Value,[" + code + " Descr Pos],[" + code + " Descr Size]," + code + " Descr Str");
    case AlxTableKind::WeaponEffect:
        return headers("Entry ID,Entry JP Name,[Descr Str],Effect ID,[Effect Name],State ID,[State Name],State Miss%");
    case AlxTableKind::ExpCurve: {
        auto result = headers("Entry ID,[PC Name]");
        for (int i = 1; i <= 99; ++i) result.push_back("EXP " + std::to_string(i));
        return result;
    }
    case AlxTableKind::Character: {
        auto result = headers("Entry ID,Entry " + code + " Name,Age,Gender ID,[Gender Name],Width,Depth,MAXMP,Element ID,[Element Name],Pad 1,Weapon ID,[Weapon Name],Armor ID,[Armor Name],Accessory ID,[Accessory Name],Movement Flags,[May Dodge],[Unk Damage],[Unk Ranged],[Unk Melee],[Ranged Atk],[Melee Atk],[Ranged Only],[Take Cover],[In Air],[On Ground],[Reserved],[May Move],HP,MAXHP,MAXHP Growth,SP,MAXSP,Counter%,Pad 2,EXP,MAXMP Growth,Unk 1,Green,Red,Purple,Blue,Yellow,Silver,Poison,Unconscious,Stone,Sleep,Confusion,Silence,Fatigue,Revival,Weak,State 10,State 11,State 12,State 13,State 14,State 15,Danger,Power,Will,Vigor,Agile,Quick,Pad 3,Power Growth,Will Growth,Vigor Growth,Agile Growth,Quick Growth,Green EXP,Red EXP,Purple EXP,Blue EXP,Yellow EXP,Silver EXP");
        return result;
    }
    case AlxTableKind::CharacterMagic:
    case AlxTableKind::CharacterSuperMove: {
        const bool magic = kind == AlxTableKind::CharacterMagic;
        if (locale == AlxLocale::Europe) {
            auto result = headers("Entry ID,EU SOT Pos,[Entry GB Name],Element ID,[Element Name],Pad 1,Order,Occasion Flags,[M],[B],[S],Effect ID,[Effect Name],Scope ID,[Scope Name],Category ID,[Category Name],Effect Speed,Effect SP,Pad 2,Pad 3,Effect Base,Type ID,[Type Name],State ID,[State Name],State Miss%,Pad 4,Pad 5,Pad 6,Ship Occ ID,[Ship Occ Name],Pad 7,Ship Eff ID,[Ship Eff Name],Ship Eff SP,Ship Eff Turns,Ship Eff Base,Unk,Pad 8,Pad 9,Pad 10,[GB Descr Str]");
            if (magic) result.push_back("[Ship GB Descr Str]");
            return result;
        }
        auto result = headers("Entry ID,Entry " + code + " Name,Element ID,[Element Name],Order,Occasion Flags,[M],[B],[S],Effect ID,[Effect Name],Scope ID,[Scope Name],Category ID,[Category Name],Effect Speed,Effect SP,Pad 1,Pad 2,Effect Base,Type ID,[Type Name],State ID,[State Name],State Miss%,Pad 3,Pad 4,Pad 5,Ship Occ ID,[Ship Occ Name],Pad 6,Ship Eff ID,[Ship Eff Name],Ship Eff SP,Ship Eff Turns,Ship Eff Base,Unk,Pad 7,Pad 8,Pad 9,[" + code + " Descr Pos],[" + code + " Descr Size]," + code + " Descr Str");
        if (magic) {
            result.push_back("[Ship " + code + " Descr Pos]");
            result.push_back("[Ship " + code + " Descr Size]");
            result.push_back("Ship " + code + " Descr Str");
        }
        return result;
    }
    case AlxTableKind::MagicExpCurve: {
        auto result = headers("Entry ID,[PC Name]");
        constexpr std::array<std::string_view, 6> colors{ "Green", "Red", "Purple", "Blue", "Yellow", "Silver" };
        for (auto color : colors) for (int i = 1; i <= 6; ++i) result.push_back(std::string(color) + " EXP " + std::to_string(i));
        return result;
    }
    }
    return {};
}

const char* toString(AlxTableKind kind) noexcept
{
    switch (kind) {
    case AlxTableKind::Enemy: return "enemy";
    case AlxTableKind::EnemyEncounter: return "enemyencounter";
    case AlxTableKind::EnemyEvent: return "enemyevent";
    case AlxTableKind::EnemyTask: return "enemytask";
    case AlxTableKind::EnemyMagic: return "enemymagic";
    case AlxTableKind::Accessory: return "accessory";
    case AlxTableKind::Armor: return "armor";
    case AlxTableKind::UsableItem: return "usableitem";
    case AlxTableKind::Weapon: return "weapon";
    case AlxTableKind::WeaponEffect: return "weaponeffect";
    case AlxTableKind::ExpCurve: return "expcurve";
    case AlxTableKind::Character: return "character";
    case AlxTableKind::CharacterMagic: return "charactermagic";
    case AlxTableKind::CharacterSuperMove: return "charactersupermove";
    case AlxTableKind::MagicExpCurve: return "magicexpcurve";
    }
    return "unknown";
}

const char* toString(AlxLocale locale) noexcept
{
    switch (locale) {
    case AlxLocale::Japanese: return "jp";
    case AlxLocale::UnitedStates: return "us";
    case AlxLocale::Europe: return "eu";
    }
    return "unknown";
}

const char* toString(AlxDiagnosticCode code) noexcept
{
    switch (code) {
    case AlxDiagnosticCode::IoError: return "trade.io_error";
    case AlxDiagnosticCode::EmptyInput: return "trade.empty_input";
    case AlxDiagnosticCode::InvalidUtf8: return "trade.invalid_utf8";
    case AlxDiagnosticCode::MalformedCsv: return "trade.malformed_csv";
    case AlxDiagnosticCode::DuplicateHeader: return "trade.duplicate_header";
    case AlxDiagnosticCode::InvalidHeader: return "trade.invalid_header";
    case AlxDiagnosticCode::AmbiguousLocale: return "trade.ambiguous_locale";
    case AlxDiagnosticCode::ConflictingLocale: return "trade.conflicting_locale";
    case AlxDiagnosticCode::InvalidValue: return "trade.invalid_value";
    case AlxDiagnosticCode::EmptyCanonicalTable: return "trade.empty_canonical_table";
    case AlxDiagnosticCode::DuplicateIdentity: return "trade.duplicate_identity";
    case AlxDiagnosticCode::InvalidGrouping: return "trade.invalid_grouping";
    case AlxDiagnosticCode::MissingRequestedTable: return "trade.missing_requested_table";
    case AlxDiagnosticCode::DuplicateRequestedTable: return "trade.duplicate_requested_table";
    case AlxDiagnosticCode::DerivedValueMismatch: return "trade.derived_value_mismatch";
    }
    return "trade.unknown";
}

bool hasErrors(std::span<const AlxDiagnostic> diagnostics) noexcept
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity == AlxDiagnosticSeverity::Error;
    });
}

#define SIMPLE_IDENTITY(Type, Prefix) \
    std::string canonicalIdentity(Type id) { return simpleIdentity(Prefix, id); }
SIMPLE_IDENTITY(EnemyEntryId, "enemy")
SIMPLE_IDENTITY(EnemyEventEntryId, "enemyevent")
SIMPLE_IDENTITY(EnemyMagicEntryId, "enemymagic")
SIMPLE_IDENTITY(AccessoryEntryId, "accessory")
SIMPLE_IDENTITY(ArmorEntryId, "armor")
SIMPLE_IDENTITY(UsableItemEntryId, "usableitem")
SIMPLE_IDENTITY(WeaponEntryId, "weapon")
SIMPLE_IDENTITY(WeaponEffectEntryId, "weaponeffect")
SIMPLE_IDENTITY(ExpCurveEntryId, "expcurve")
SIMPLE_IDENTITY(CharacterEntryId, "character")
SIMPLE_IDENTITY(CharacterMagicEntryId, "charactermagic")
SIMPLE_IDENTITY(CharacterSuperMoveEntryId, "charactersupermove")
SIMPLE_IDENTITY(MagicExpCurveEntryId, "magicexpcurve")
#undef SIMPLE_IDENTITY

std::string canonicalIdentity(EnemyTaskEntryId id)
{
    return "enemytask." + std::to_string(id.enemy.value) + "." + std::to_string(id.entry);
}

std::string canonicalIdentity(EnemyEncounterEntryId id)
{
    return "enemyencounter." + lowerAscii(std::move(id.owner)) + "." + std::to_string(id.entry);
}

EnemyTaskFields* EnemyTaskTable::edit(EnemyTaskEntryId id) noexcept
{
    for (auto& group : groups_) for (auto& record : group.records_) {
        if (record.id() == id) return &record.fields();
    }
    return nullptr;
}

const EnemyTaskFields* EnemyTaskTable::find(EnemyTaskEntryId id) const noexcept
{
    for (const auto& group : groups_) for (const auto& record : group.records_) {
        if (record.id() == id) return &record.fields();
    }
    return nullptr;
}

EnemyEncounterFields* EnemyEncounterTable::edit(const EnemyEncounterEntryId& id) noexcept
{
    for (auto& group : groups_) for (auto& record : group.records_) {
        if (record.id() == id) return &record.fields();
    }
    return nullptr;
}

const EnemyEncounterFields* EnemyEncounterTable::find(const EnemyEncounterEntryId& id) const noexcept
{
    for (const auto& group : groups_) for (const auto& record : group.records_) {
        if (record.id() == id) return &record.fields();
    }
    return nullptr;
}

namespace detail {

EnemyTaskGroup& AlxModelAccess::appendTaskGroup(EnemyTaskTable& table, EnemyEntryId enemy)
{
    table.groups_.push_back({});
    table.groups_.back().enemyId_ = enemy;
    return table.groups_.back();
}

void AlxModelAccess::appendTask(EnemyTaskGroup& group, EnemyTaskEntryId id, EnemyTaskFields fields)
{
    EnemyTaskRecord record{};
    record.id_ = id;
    record.fields_ = std::move(fields);
    group.records_.push_back(std::move(record));
}

EnemyEncounterGroup& AlxModelAccess::appendEncounterGroup(EnemyEncounterTable& table, std::string owner)
{
    table.groups_.push_back({});
    table.groups_.back().owner_ = lowerAscii(std::move(owner));
    return table.groups_.back();
}

void AlxModelAccess::appendEncounter(
    EnemyEncounterGroup& group, EnemyEncounterEntryId id, EnemyEncounterFields fields)
{
    EnemyEncounterRecord record{};
    record.id_ = std::move(id);
    record.fields_ = std::move(fields);
    group.records_.push_back(std::move(record));
}

} // namespace detail

} // namespace spice::trade::alx
