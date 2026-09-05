#include "AlxImporter.h"

#include "CsvReader.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <type_traits>

namespace spice::trade::alx {
namespace {

using detail::CsvDocument;
using detail::CsvRow;

template <typename Result>
void diagnostic(Result& result, AlxDiagnosticCode code, std::string message,
    const std::filesystem::path& path, std::optional<std::size_t> row = {},
    std::optional<std::size_t> column = {})
{
    result.diagnostics.push_back({ code, AlxDiagnosticSeverity::Error, std::move(message), path, row, column });
}

template <typename Result>
std::optional<CsvDocument> readCsv(std::span<const std::uint8_t> bytes,
    const std::filesystem::path& path, Result& result)
{
    const auto csv = detail::CsvReader{}.parse(bytes, path);
    for (const auto& item : csv.diagnostics) {
        AlxDiagnosticCode code = AlxDiagnosticCode::MalformedCsv;
        if (item.message.find("UTF-8") != std::string::npos) code = AlxDiagnosticCode::InvalidUtf8;
        if (item.message.find("empty") != std::string::npos) code = AlxDiagnosticCode::EmptyInput;
        if (item.message.find("duplicated") != std::string::npos) code = AlxDiagnosticCode::DuplicateHeader;
        result.diagnostics.push_back({
            code,
            item.severity == detail::DiagnosticSeverity::Error ? AlxDiagnosticSeverity::Error
                : item.severity == detail::DiagnosticSeverity::Warning ? AlxDiagnosticSeverity::Warning
                : AlxDiagnosticSeverity::Info,
            item.message, path, item.row, item.column,
        });
    }
    return csv.document;
}

template <typename Result>
std::optional<AlxLocale> detectLocale(const CsvDocument& csv, AlxTableKind kind,
    const AlxImportOptions& options, const std::filesystem::path& path, Result& result)
{
    constexpr std::array locales{ AlxLocale::Japanese, AlxLocale::UnitedStates, AlxLocale::Europe };
    std::vector<AlxLocale> matches;
    for (const auto locale : locales) if (csv.headers == canonicalHeaders(kind, locale)) matches.push_back(locale);
    if (matches.empty()) {
        diagnostic(result, AlxDiagnosticCode::InvalidHeader,
            "Header does not exactly match an ALX 5.0.0 " + std::string(toString(kind)) + " schema", path, 1);
        return std::nullopt;
    }
    if (options.localeHint) {
        if (std::find(matches.begin(), matches.end(), *options.localeHint) == matches.end()) {
            diagnostic(result, AlxDiagnosticCode::ConflictingLocale,
                "Authoritative locale conflicts with the CSV header", path, 1);
            return std::nullopt;
        }
        return *options.localeHint;
    }
    if (matches.size() != 1) {
        diagnostic(result, AlxDiagnosticCode::AmbiguousLocale,
            "This locale-neutral ALX schema requires an authoritative locale hint", path, 1);
        return std::nullopt;
    }
    return matches.front();
}

template <typename Integer>
bool parseInteger(std::string_view text, Integer& output)
{
    static_assert(std::is_integral_v<Integer>);
    if (text.empty()) return false;
    bool negative = false;
    if (text.front() == '-') { negative = true; text.remove_prefix(1); }
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) { base = 2; text.remove_prefix(2); }
    else if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) { base = 16; text.remove_prefix(2); }
    if (text.empty()) return false;
    unsigned long long magnitude{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), magnitude, base);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return false;
    if constexpr (std::is_signed_v<Integer>) {
        const auto value = negative ? -static_cast<long long>(magnitude) : static_cast<long long>(magnitude);
        if (value < std::numeric_limits<Integer>::min() || value > std::numeric_limits<Integer>::max()) return false;
        output = static_cast<Integer>(value);
    } else {
        if (negative || magnitude > std::numeric_limits<Integer>::max()) return false;
        output = static_cast<Integer>(magnitude);
    }
    return true;
}

class Row {
public:
    Row(const CsvDocument& csv, const CsvRow& values, std::size_t row,
        const std::filesystem::path& path, std::vector<AlxDiagnostic>& diagnostics)
        : csv_(csv), values_(values), row_(row), path_(path), diagnostics_(diagnostics) {}

    const std::string& text(std::string_view name) const
    {
        const auto found = std::find(csv_.headers.begin(), csv_.headers.end(), name);
        return values_[static_cast<std::size_t>(found - csv_.headers.begin())];
    }

    template <typename Integer>
    Integer integer(std::string_view name)
    {
        Integer value{};
        const auto found = std::find(csv_.headers.begin(), csv_.headers.end(), name);
        const auto column = static_cast<std::size_t>(found - csv_.headers.begin());
        if (found == csv_.headers.end() || !parseInteger(values_[column], value)) {
            diagnostics_.push_back({ AlxDiagnosticCode::InvalidValue, AlxDiagnosticSeverity::Error,
                "Invalid value for " + std::string(name), path_, row_, column + 1 });
        }
        return value;
    }

    float floating(std::string_view name)
    {
        const auto& value = text(name);
        float output{};
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), output);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || !std::isfinite(output)) {
            diagnostics_.push_back({ AlxDiagnosticCode::InvalidValue, AlxDiagnosticSeverity::Error,
                "Invalid finite float for " + std::string(name), path_, row_, {} });
        }
        return output;
    }

    [[nodiscard]] AlxDerivedCells derived(std::span<const std::string_view> semanticBrackets = {}) const
    {
        AlxDerivedCells cells;
        for (std::size_t i = 0; i < csv_.headers.size(); ++i) {
            const auto& header = csv_.headers[i];
            const bool bracketed = header.size() >= 2 && header.front() == '[' && header.back() == ']';
            const bool semantic = std::find(semanticBrackets.begin(), semanticBrackets.end(), header) != semanticBrackets.end();
            const bool derivedName = header == "Effect Param Name" || header == "State Inflict Name"
                || header == "State Resist Name" || header == "EU SOT Pos";
            if ((bracketed && !semantic) || derivedName) cells.emplace(header, values_[i]);
        }
        return cells;
    }

private:
    const CsvDocument& csv_;
    const CsvRow& values_;
    std::size_t row_;
    const std::filesystem::path& path_;
    std::vector<AlxDiagnostic>& diagnostics_;
};

template <typename Result>
void finish(Result& result, AlxTableKind kind, AlxLocale locale,
    const std::filesystem::path& path, std::size_t source, std::size_t published)
{
    if (published == 0) diagnostic(result, AlxDiagnosticCode::EmptyCanonicalTable,
        "Canonical table contains no published records", path);
    if (!hasErrors(result.diagnostics)) {
        result.metadata = AlxImportMetadata{ kind, locale, path, source, published, source - published };
    } else {
        result.table.reset();
    }
}

template <typename Result, typename Parse>
Result importTable(std::span<const std::uint8_t> bytes, const AlxImportOptions& options,
    const std::filesystem::path& path, AlxTableKind kind, Parse parse)
{
    Result result{};
    const auto csv = readCsv(bytes, path, result);
    if (!csv) return result;
    const auto locale = detectLocale(*csv, kind, options, path, result);
    if (!locale) return result;
    const std::size_t published = parse(*csv, *locale, result);
    finish(result, kind, *locale, path, csv->rows.size(), published);
    return result;
}

template <typename Result>
std::vector<std::uint8_t> fileBytes(const std::filesystem::path& path, Result& result)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        diagnostic(result, AlxDiagnosticCode::IoError, "Could not open CSV file", path);
        return {};
    }
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

template <typename Result, typename Importer>
Result importFileImpl(const Importer& importer, const std::filesystem::path& path, const AlxImportOptions& options)
{
    Result result{};
    auto bytes = fileBytes(path, result);
    if (hasErrors(result.diagnostics)) return result;
    return importer.importBytes(bytes, options, path);
}

template <typename Result>
void rememberDerived(Result& result, std::string identity, AlxDerivedCells cells)
{
    detail::AlxDerivedContextAccess::addImported(result.derivedContext, std::move(identity), std::move(cells));
}

std::size_t taskCount(const EnemyTaskTable& table)
{
    std::size_t count = 0; for (const auto& group : table.groups()) count += group.records().size(); return count;
}

std::size_t encounterCount(const EnemyEncounterTable& table)
{
    std::size_t count = 0; for (const auto& group : table.groups()) count += group.records().size(); return count;
}

template <typename Table>
std::size_t recordCount(const Table& table) { return table.records().size(); }

} // namespace

EnemyImportResult EnemyCsvImporter::importBytes(std::span<const std::uint8_t> bytes,
    const AlxImportOptions& options, const std::filesystem::path& path) const
{
    return importTable<EnemyImportResult>(bytes, options, path, AlxTableKind::Enemy,
        [&](const CsvDocument& csv, AlxLocale locale, EnemyImportResult& result) {
        EnemyTable table; std::set<EnemyEntryId> ids;
        constexpr std::array colors{ "Green", "Red", "Purple", "Blue", "Yellow", "Silver" };
        constexpr std::array states{ "Poison", "Unconscious", "Stone", "Sleep", "Confusion", "Silence",
            "Fatigue", "Revival", "Weak", "State 10", "State 11", "State 12", "State 13", "State 14", "State 15" };
        for (std::size_t i = 0; i < csv.rows.size(); ++i) {
            Row row(csv, csv.rows[i], i + 2, path, result.diagnostics);
            if (row.text("[Filter]") != "*") continue;
            const EnemyEntryId id{ row.integer<std::uint32_t>("Entry ID") };
            if (!ids.insert(id).second) diagnostic(result, AlxDiagnosticCode::DuplicateIdentity,
                "Duplicate canonical enemy identity", path, i + 2);
            EnemyFields f{};
            f.japaneseName = row.text("Entry JP Name");
            f.width = row.integer<std::int8_t>("Width"); f.depth = row.integer<std::int8_t>("Depth");
            f.elementId = row.integer<std::int8_t>("Element ID");
            constexpr std::array pads{ "Pad 1", "Pad 2", "Pad 3", "Pad 4", "Pad 5", "Pad 6", "Pad 7" };
            for (std::size_t n = 0; n < pads.size(); ++n) f.padding[n] = row.integer<std::int8_t>(pads[n]);
            f.movementFlags = row.integer<std::int16_t>("Movement Flags");
            f.counterPercent = row.integer<std::int16_t>("Counter%");
            f.experience = row.integer<std::uint16_t>("EXP"); f.gold = row.integer<std::uint16_t>("Gold");
            f.maxHp = row.integer<std::int32_t>("MAXHP"); f.unknown1 = row.floating("Unk 1");
            for (std::size_t n = 0; n < colors.size(); ++n) f.elements[n] = row.integer<std::int16_t>(colors[n]);
            for (std::size_t n = 0; n < states.size(); ++n) f.states[n] = row.integer<std::int16_t>(states[n]);
            f.danger = row.integer<std::int16_t>("Danger"); f.effectId = row.integer<std::int8_t>("Effect ID");
            f.stateId = row.integer<std::int8_t>("State ID"); f.stateMissPercent = row.integer<std::int8_t>("State Miss%");
            f.level = row.integer<std::int16_t>("Level"); f.will = row.integer<std::int16_t>("Will");
            f.vigor = row.integer<std::int16_t>("Vigor"); f.agile = row.integer<std::int16_t>("Agile");
            f.quick = row.integer<std::int16_t>("Quick"); f.attack = row.integer<std::int16_t>("Attack");
            f.defense = row.integer<std::int16_t>("Defense"); f.magicDefense = row.integer<std::int16_t>("MagDef");
            f.hitPercent = row.integer<std::int16_t>("Hit%"); f.dodgePercent = row.integer<std::int16_t>("Dodge%");
            for (std::size_t n = 0; n < f.itemDrops.size(); ++n) {
                const auto p = "Item " + std::to_string(n + 1);
                f.itemDrops[n] = { row.integer<std::int16_t>(p + " Prob"),
                    row.integer<std::int16_t>(p + " Amount"), row.integer<std::int16_t>(p + " ID") };
            }
            detail::AlxModelAccess::append(table, id, std::move(f));
            const auto identity = canonicalIdentity(id);
            auto derived = row.derived();
            detail::AlxDerivedContextAccess::addEnemyName(
                result.derivedContext, id, AlxLocale::Japanese, table.records().back().fields().japaneseName);
            if (locale != AlxLocale::Japanese) {
                const auto key = std::string("[Entry ") + (locale == AlxLocale::UnitedStates ? "US" : "EU") + " Name]";
                detail::AlxDerivedContextAccess::addEnemyName(result.derivedContext, id, locale, row.text(key));
            }
            rememberDerived(result, identity, std::move(derived));
        }
        const auto count = table.records().size(); result.table = std::move(table); return count;
    });
}

EnemyEncounterImportResult EnemyEncounterCsvImporter::importBytes(std::span<const std::uint8_t> bytes,
    const AlxImportOptions& options, const std::filesystem::path& path) const
{
    return importTable<EnemyEncounterImportResult>(bytes, options, path, AlxTableKind::EnemyEncounter,
        [&](const CsvDocument& csv, AlxLocale, EnemyEncounterImportResult& result) {
        EnemyEncounterTable table; std::set<EnemyEncounterEntryId> ids; std::set<std::string> closedOwners;
        constexpr std::array<std::string_view, 1> semantic{ "[Filter]" };
        EnemyEncounterGroup* group = nullptr; std::string active;
        for (std::size_t i = 0; i < csv.rows.size(); ++i) {
            Row row(csv, csv.rows[i], i + 2, path, result.diagnostics);
            std::string owner = row.text("[Filter]");
            std::transform(owner.begin(), owner.end(), owner.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const auto entry = row.integer<std::uint32_t>("Entry ID");
            if (owner != active) {
                if (!active.empty()) closedOwners.insert(active);
                if (closedOwners.contains(owner)) diagnostic(result, AlxDiagnosticCode::InvalidGrouping,
                    "Enemy encounter owner rows are not contiguous", path, i + 2);
                active = owner; group = &detail::AlxModelAccess::appendEncounterGroup(table, owner);
            }
            const auto expected = group->records().size() + 1;
            if (entry == 0) {
                if (!group->records().empty()) diagnostic(result, AlxDiagnosticCode::InvalidGrouping,
                    "Enemy encounter placeholder must be the first owner row", path, i + 2);
                continue;
            }
            if (entry != expected) diagnostic(result, AlxDiagnosticCode::InvalidGrouping,
                "Enemy encounter entry IDs must be contiguous after placeholder 0", path, i + 2);
            EnemyEncounterEntryId id{ owner, entry };
            if (!ids.insert(id).second) diagnostic(result, AlxDiagnosticCode::DuplicateIdentity,
                "Duplicate canonical enemy encounter identity", path, i + 2);
            EnemyEncounterFields f{};
            f.initiative = row.integer<std::uint8_t>("Initiative");
            f.magicExperience = row.integer<std::uint8_t>("Magic EXP");
            for (std::size_t n = 0; n < f.enemies.size(); ++n) {
                const auto value = row.integer<std::uint8_t>("EC" + std::to_string(n + 1) + " ID");
                if (value != 255) f.enemies[n].enemy = EnemyEntryId{ value };
            }
            detail::AlxModelAccess::appendEncounter(*group, id, std::move(f));
            rememberDerived(result, canonicalIdentity(id), row.derived(semantic));
        }
        const auto count = encounterCount(table); result.table = std::move(table); return count;
    });
}

EnemyEventImportResult EnemyEventCsvImporter::importBytes(std::span<const std::uint8_t> bytes,
    const AlxImportOptions& options, const std::filesystem::path& path) const
{
    return importTable<EnemyEventImportResult>(bytes, options, path, AlxTableKind::EnemyEvent,
        [&](const CsvDocument& csv, AlxLocale, EnemyEventImportResult& result) {
        EnemyEventTable table; std::set<EnemyEventEntryId> ids;
        for (std::size_t i = 0; i < csv.rows.size(); ++i) {
            Row row(csv, csv.rows[i], i + 2, path, result.diagnostics);
            const EnemyEventEntryId id{ row.integer<std::uint32_t>("Entry ID") };
            if (!ids.insert(id).second) diagnostic(result, AlxDiagnosticCode::DuplicateIdentity,
                "Duplicate canonical enemy event identity", path, i + 2);
            EnemyEventFields f{}; f.magicExperience = row.integer<std::uint8_t>("Magic EXP");
            for (std::size_t n = 0; n < f.players.size(); ++n) {
                const auto p = "PC" + std::to_string(n + 1);
                const auto value = row.integer<std::int16_t>(p + " ID");
                if (value >= 0) f.players[n].character = CharacterEntryId{ static_cast<std::uint32_t>(value) };
                f.players[n].x = row.integer<std::int8_t>(p + " X"); f.players[n].z = row.integer<std::int8_t>(p + " Z");
            }
            for (std::size_t n = 0; n < f.enemies.size(); ++n) {
                const auto p = "EC" + std::to_string(n + 1);
                const auto value = row.integer<std::uint8_t>(p + " ID");
                if (value != 255) f.enemies[n].enemy = EnemyEntryId{ value };
                f.enemies[n].x = row.integer<std::int8_t>(p + " X"); f.enemies[n].z = row.integer<std::int8_t>(p + " Z");
            }
            f.initiative = row.integer<std::uint8_t>("Initiative");
            f.defeatConditionId = row.integer<std::int8_t>("Defeat Cond ID");
            f.escapeConditionId = row.integer<std::int8_t>("Escape Cond ID");
            std::int64_t bgm{}; if (!parseInteger(row.text("BGM ID"), bgm)) {
                result.diagnostics.push_back({ AlxDiagnosticCode::InvalidValue, AlxDiagnosticSeverity::Error,
                    "Invalid value for BGM ID", path, i + 2, {} });
            } else if (bgm >= 0 && bgm <= std::numeric_limits<std::uint32_t>::max()) f.bgmId = static_cast<std::uint32_t>(bgm);
            else if (bgm != -1) result.diagnostics.push_back({ AlxDiagnosticCode::InvalidValue, AlxDiagnosticSeverity::Error,
                "BGM ID must be -1 or an unsigned 32-bit value", path, i + 2, {} });
            detail::AlxModelAccess::append(table, id, std::move(f));
            rememberDerived(result, canonicalIdentity(id), row.derived());
        }
        const auto count = table.records().size(); result.table = std::move(table); return count;
    });
}

EnemyTaskImportResult EnemyTaskCsvImporter::importBytes(std::span<const std::uint8_t> bytes,
    const AlxImportOptions& options, const std::filesystem::path& path) const
{
    return importTable<EnemyTaskImportResult>(bytes, options, path, AlxTableKind::EnemyTask,
        [&](const CsvDocument& csv, AlxLocale, EnemyTaskImportResult& result) {
        EnemyTaskTable table; std::set<EnemyTaskEntryId> ids; std::set<EnemyEntryId> closed;
        constexpr std::array<std::string_view, 1> semantic{ "[EC ID]" };
        EnemyTaskGroup* group = nullptr; std::optional<EnemyEntryId> active;
        for (std::size_t i = 0; i < csv.rows.size(); ++i) {
            Row row(csv, csv.rows[i], i + 2, path, result.diagnostics);
            if (row.text("[Filter]") != "*") continue;
            const EnemyEntryId enemy{ row.integer<std::uint32_t>("[EC ID]") };
            const auto entry = row.integer<std::uint32_t>("Entry ID");
            if (!active || *active != enemy) {
                if (active) closed.insert(*active);
                if (closed.contains(enemy)) diagnostic(result, AlxDiagnosticCode::InvalidGrouping,
                    "Enemy task owner rows are not contiguous", path, i + 2);
                active = enemy; group = &detail::AlxModelAccess::appendTaskGroup(table, enemy);
            }
            if (entry != group->records().size() + 1) diagnostic(result, AlxDiagnosticCode::InvalidGrouping,
                "Enemy task entry IDs must be contiguous and one-based within each enemy", path, i + 2);
            const EnemyTaskEntryId id{ enemy, entry };
            if (!ids.insert(id).second) diagnostic(result, AlxDiagnosticCode::DuplicateIdentity,
                "Duplicate canonical enemy task identity", path, i + 2);
            EnemyTaskFields f{ row.integer<std::int16_t>("Type ID"), row.integer<std::int16_t>("Task ID"),
                row.integer<std::int16_t>("Param ID") };
            detail::AlxModelAccess::appendTask(*group, id, std::move(f));
            rememberDerived(result, canonicalIdentity(id), row.derived(semantic));
        }
        const auto count = taskCount(table); result.table = std::move(table); return count;
    });
}

namespace {

std::vector<std::int8_t> padding(Row& row, const CsvDocument& csv)
{
    std::vector<std::int8_t> values;
    for (const auto& header : csv.headers) if (header.starts_with("Pad ")) values.push_back(row.integer<std::int8_t>(header));
    return values;
}

AlxEditableText editableName(Row& row, AlxLocale locale)
{
    AlxEditableText value{};
    if (locale == AlxLocale::Europe) {
        value.text = row.text("[Entry GB Name]");
        value.messageId = row.integer<std::uint32_t>("EU SOT Pos");
    } else {
        value.text = row.text(locale == AlxLocale::Japanese ? "Entry JP Name" : "Entry US Name");
    }
    return value;
}

std::string description(Row& row, AlxLocale locale)
{
    if (locale == AlxLocale::Europe) return row.text("[GB Descr Str]");
    return row.text(locale == AlxLocale::Japanese ? "JP Descr Str" : "US Descr Str");
}

template <typename Result, typename Table, typename Id>
std::size_t parseEquipment(const CsvDocument& csv, AlxLocale locale, Result& result,
    const std::filesystem::path& path)
{
    Table table; std::set<Id> ids;
    constexpr std::array<std::string_view, 2> euSemantic{ "[Entry GB Name]", "[GB Descr Str]" };
    for (std::size_t i = 0; i < csv.rows.size(); ++i) {
        Row row(csv, csv.rows[i], i + 2, path, result.diagnostics);
        const Id id{ row.integer<std::uint32_t>("Entry ID") };
        if (!ids.insert(id).second) diagnostic(result, AlxDiagnosticCode::DuplicateIdentity,
            "Duplicate canonical equipment identity", path, i + 2);
        EquipmentFields f{}; f.name = editableName(row, locale);
        f.characterFlags = row.integer<std::uint8_t>("PC Flags");
        f.sellPercent = row.integer<std::int8_t>("Sell%");
        const auto code = locale == AlxLocale::Japanese ? "JP" : locale == AlxLocale::UnitedStates ? "US" : "EU";
        f.order1 = row.integer<std::int8_t>(std::string(code) + " Order 1");
        f.order2 = row.integer<std::int8_t>(std::string(code) + " Order 2");
        f.padding = padding(row, csv); f.buyPrice = row.integer<std::uint16_t>("Buy");
        for (std::size_t n = 0; n < f.traits.size(); ++n) {
            const auto prefix = "Trait " + std::to_string(n + 1);
            f.traits[n] = { row.integer<std::int8_t>(prefix + " ID"),
                row.integer<std::int8_t>("Pad " + std::to_string(n + (locale == AlxLocale::Europe ? 1 : 2))),
                row.integer<std::int16_t>(prefix + " Value") };
        }
        f.description = description(row, locale);
        detail::AlxModelAccess::append(table, id, std::move(f));
        rememberDerived(result, canonicalIdentity(id), row.derived(locale == AlxLocale::Europe ? euSemantic : std::span<const std::string_view>{}));
    }
    const auto count = table.records().size(); result.table = std::move(table); return count;
}

template <typename Result, typename Table, typename Id>
std::size_t parseCharacterSkill(const CsvDocument& csv, AlxLocale locale, Result& result,
    const std::filesystem::path& path, bool magic)
{
    Table table; std::set<Id> ids;
    constexpr std::array<std::string_view, 3> euMagicSemantic{
        "[Entry GB Name]", "[GB Descr Str]", "[Ship GB Descr Str]" };
    constexpr std::array<std::string_view, 2> euSuperSemantic{ "[Entry GB Name]", "[GB Descr Str]" };
    for (std::size_t i = 0; i < csv.rows.size(); ++i) {
        Row row(csv, csv.rows[i], i + 2, path, result.diagnostics);
        const Id id{ row.integer<std::uint32_t>("Entry ID") };
        if (!ids.insert(id).second) diagnostic(result, AlxDiagnosticCode::DuplicateIdentity,
            "Duplicate canonical character skill identity", path, i + 2);
        CharacterSkillFields f{}; f.name = editableName(row, locale);
        f.elementId = row.integer<std::int8_t>("Element ID"); f.order = row.integer<std::int16_t>("Order");
        f.occasionFlags = row.integer<std::uint8_t>("Occasion Flags"); f.effectId = row.integer<std::int8_t>("Effect ID");
        f.scopeId = row.integer<std::uint8_t>("Scope ID"); f.categoryId = row.integer<std::int8_t>("Category ID");
        f.effectSpeed = row.integer<std::int8_t>("Effect Speed"); f.effectSpirit = row.integer<std::int8_t>("Effect SP");
        f.padding = padding(row, csv); f.effectBase = row.integer<std::int16_t>("Effect Base");
        f.typeId = row.integer<std::int8_t>("Type ID"); f.stateId = row.integer<std::int8_t>("State ID");
        f.stateMissPercent = row.integer<std::int8_t>("State Miss%"); f.shipOccasionId = row.integer<std::int8_t>("Ship Occ ID");
        f.shipEffectId = row.integer<std::int16_t>("Ship Eff ID"); f.shipEffectSpirit = row.integer<std::int8_t>("Ship Eff SP");
        f.shipEffectTurns = row.integer<std::int8_t>("Ship Eff Turns"); f.shipEffectBase = row.integer<std::int16_t>("Ship Eff Base");
        f.unknown = row.integer<std::int8_t>("Unk"); f.description = description(row, locale);
        if (magic) {
            f.shipDescription = locale == AlxLocale::Europe ? row.text("[Ship GB Descr Str]")
                : row.text(locale == AlxLocale::Japanese ? "Ship JP Descr Str" : "Ship US Descr Str");
        }
        detail::AlxModelAccess::append(table, id, std::move(f));
        const std::span<const std::string_view> semantic = locale != AlxLocale::Europe ? std::span<const std::string_view>{}
            : magic ? std::span<const std::string_view>(euMagicSemantic) : std::span<const std::string_view>(euSuperSemantic);
        rememberDerived(result, canonicalIdentity(id), row.derived(semantic));
    }
    const auto count = table.records().size(); result.table = std::move(table); return count;
}

} // namespace

EnemyMagicImportResult EnemyMagicCsvImporter::importBytes(std::span<const std::uint8_t> bytes,
    const AlxImportOptions& options, const std::filesystem::path& path) const
{
    return importTable<EnemyMagicImportResult>(bytes, options, path, AlxTableKind::EnemyMagic,
        [&](const CsvDocument& csv, AlxLocale locale, EnemyMagicImportResult& result) {
        EnemyMagicTable table; std::set<EnemyMagicEntryId> ids;
        constexpr std::array<std::string_view, 1> euSemantic{ "[Entry GB Name]" };
        for (std::size_t i = 0; i < csv.rows.size(); ++i) {
            Row row(csv, csv.rows[i], i + 2, path, result.diagnostics);
            const EnemyMagicEntryId id{ row.integer<std::uint32_t>("Entry ID") };
            if (!ids.insert(id).second) diagnostic(result, AlxDiagnosticCode::DuplicateIdentity,
                "Duplicate canonical enemy magic identity", path, i + 2);
            EnemyMagicFields f{}; f.name = editableName(row, locale); f.padding = padding(row, csv);
            f.categoryId = row.integer<std::int8_t>("Category ID"); f.effectId = row.integer<std::int8_t>("Effect ID");
            f.scopeId = row.integer<std::uint8_t>("Scope ID"); f.effectParameterId = row.integer<std::uint16_t>("Effect Param ID");
            f.effectBase = row.integer<std::uint16_t>("Effect Base"); f.elementId = row.integer<std::int8_t>("Element ID");
            f.typeId = row.integer<std::int8_t>("Type ID"); f.stateInflictionId = row.integer<std::int8_t>("State Inflict ID");
            f.stateResistanceId = row.integer<std::int8_t>("State Resist ID"); f.stateId = row.integer<std::int8_t>("State ID");
            f.stateMissPercent = row.integer<std::int8_t>("State Miss%");
            detail::AlxModelAccess::append(table, id, std::move(f));
            rememberDerived(result, canonicalIdentity(id), row.derived(locale == AlxLocale::Europe ? euSemantic : std::span<const std::string_view>{}));
        }
        const auto count = table.records().size(); result.table = std::move(table); return count;
    });
}

AccessoryImportResult AccessoryCsvImporter::importBytes(std::span<const std::uint8_t> bytes,
    const AlxImportOptions& options, const std::filesystem::path& path) const
{
    return importTable<AccessoryImportResult>(bytes, options, path, AlxTableKind::Accessory,
        [&](const CsvDocument& csv, AlxLocale locale, AccessoryImportResult& result) {
            return parseEquipment<AccessoryImportResult, AccessoryTable, AccessoryEntryId>(csv, locale, result, path);
        });
}

ArmorImportResult ArmorCsvImporter::importBytes(std::span<const std::uint8_t> bytes,
    const AlxImportOptions& options, const std::filesystem::path& path) const
{
    return importTable<ArmorImportResult>(bytes, options, path, AlxTableKind::Armor,
        [&](const CsvDocument& csv, AlxLocale locale, ArmorImportResult& result) {
            return parseEquipment<ArmorImportResult, ArmorTable, ArmorEntryId>(csv, locale, result, path);
        });
}

UsableItemImportResult UsableItemCsvImporter::importBytes(std::span<const std::uint8_t> bytes,
    const AlxImportOptions& options, const std::filesystem::path& path) const
{
    return importTable<UsableItemImportResult>(bytes, options, path, AlxTableKind::UsableItem,
        [&](const CsvDocument& csv, AlxLocale locale, UsableItemImportResult& result) {
        UsableItemTable table; std::set<UsableItemEntryId> ids;
        constexpr std::array<std::string_view, 2> euSemantic{ "[Entry GB Name]", "[GB Descr Str]" };
        for (std::size_t i = 0; i < csv.rows.size(); ++i) {
            Row row(csv, csv.rows[i], i + 2, path, result.diagnostics);
            const UsableItemEntryId id{ row.integer<std::uint32_t>("Entry ID") };
            if (!ids.insert(id).second) diagnostic(result, AlxDiagnosticCode::DuplicateIdentity,
                "Duplicate canonical usable-item identity", path, i + 2);
            UsableItemFields f{}; f.name = editableName(row, locale); f.padding = padding(row, csv);
            f.occasionFlags = row.integer<std::uint8_t>("Occasion Flags"); f.effectId = row.integer<std::int8_t>("Effect ID");
            f.scopeId = row.integer<std::uint8_t>("Scope ID"); f.consumePercent = row.integer<std::int8_t>("Consume%");
            f.sellPercent = row.integer<std::int8_t>("Sell%");
            const auto code = locale == AlxLocale::Japanese ? "JP" : locale == AlxLocale::UnitedStates ? "US" : "EU";
            f.order1 = row.integer<std::int8_t>(std::string(code) + " Order 1"); f.order2 = row.integer<std::int8_t>(std::string(code) + " Order 2");
            f.buyPrice = row.integer<std::uint16_t>("Buy"); f.effectBase = row.integer<std::int16_t>("Effect Base");
            f.elementId = row.integer<std::int8_t>("Element ID"); f.typeId = row.integer<std::int8_t>("Type ID");
            f.stateId = row.integer<std::int16_t>("State ID"); f.stateMissPercent = row.integer<std::int16_t>("State Miss%");
            f.description = description(row, locale);
            detail::AlxModelAccess::append(table, id, std::move(f));
            rememberDerived(result, canonicalIdentity(id), row.derived(locale == AlxLocale::Europe ? euSemantic : std::span<const std::string_view>{}));
        }
        const auto count = table.records().size(); result.table = std::move(table); return count;
    });
}

WeaponImportResult WeaponCsvImporter::importBytes(std::span<const std::uint8_t> bytes,
    const AlxImportOptions& options, const std::filesystem::path& path) const
{
    return importTable<WeaponImportResult>(bytes, options, path, AlxTableKind::Weapon,
        [&](const CsvDocument& csv, AlxLocale locale, WeaponImportResult& result) {
        WeaponTable table; std::set<WeaponEntryId> ids;
        constexpr std::array<std::string_view, 2> euSemantic{ "[Entry GB Name]", "[GB Descr Str]" };
        for (std::size_t i = 0; i < csv.rows.size(); ++i) {
            Row row(csv, csv.rows[i], i + 2, path, result.diagnostics);
            const WeaponEntryId id{ row.integer<std::uint32_t>("Entry ID") };
            if (!ids.insert(id).second) diagnostic(result, AlxDiagnosticCode::DuplicateIdentity,
                "Duplicate canonical weapon identity", path, i + 2);
            WeaponFields f{}; f.name = editableName(row, locale); f.padding = padding(row, csv);
            f.characterId = row.integer<std::int8_t>("PC ID"); f.sellPercent = row.integer<std::int8_t>("Sell%");
            const auto code = locale == AlxLocale::Japanese ? "JP" : locale == AlxLocale::UnitedStates ? "US" : "EU";
            f.order1 = row.integer<std::int8_t>(std::string(code) + " Order 1"); f.order2 = row.integer<std::int8_t>(std::string(code) + " Order 2");
            f.effectId = row.integer<std::int8_t>("Effect ID"); f.buyPrice = row.integer<std::uint16_t>("Buy");
            f.attack = row.integer<std::int16_t>("Attack"); f.hitPercent = row.integer<std::int16_t>("Hit%");
            f.traitId = row.integer<std::int8_t>("Trait ID"); f.traitValue = row.integer<std::int16_t>("Trait Value");
            f.description = description(row, locale);
            detail::AlxModelAccess::append(table, id, std::move(f));
            rememberDerived(result, canonicalIdentity(id), row.derived(locale == AlxLocale::Europe ? euSemantic : std::span<const std::string_view>{}));
        }
        const auto count = table.records().size(); result.table = std::move(table); return count;
    });
}

WeaponEffectImportResult WeaponEffectCsvImporter::importBytes(std::span<const std::uint8_t> bytes,
    const AlxImportOptions& options, const std::filesystem::path& path) const
{
    return importTable<WeaponEffectImportResult>(bytes, options, path, AlxTableKind::WeaponEffect,
        [&](const CsvDocument& csv, AlxLocale, WeaponEffectImportResult& result) {
        WeaponEffectTable table; std::set<WeaponEffectEntryId> ids;
        for (std::size_t i = 0; i < csv.rows.size(); ++i) {
            Row row(csv, csv.rows[i], i + 2, path, result.diagnostics);
            const WeaponEffectEntryId id{ row.integer<std::uint32_t>("Entry ID") };
            if (!ids.insert(id).second) diagnostic(result, AlxDiagnosticCode::DuplicateIdentity,
                "Duplicate canonical weapon-effect identity", path, i + 2);
            WeaponEffectFields f{ row.text("Entry JP Name"), row.integer<std::int8_t>("Effect ID"),
                row.integer<std::int8_t>("State ID"), row.integer<std::int8_t>("State Miss%") };
            detail::AlxModelAccess::append(table, id, std::move(f));
            rememberDerived(result, canonicalIdentity(id), row.derived());
        }
        const auto count = table.records().size(); result.table = std::move(table); return count;
    });
}

ExpCurveImportResult ExpCurveCsvImporter::importBytes(std::span<const std::uint8_t> bytes,
    const AlxImportOptions& options, const std::filesystem::path& path) const
{
    return importTable<ExpCurveImportResult>(bytes, options, path, AlxTableKind::ExpCurve,
        [&](const CsvDocument& csv, AlxLocale, ExpCurveImportResult& result) {
        ExpCurveTable table; std::set<ExpCurveEntryId> ids;
        for (std::size_t i = 0; i < csv.rows.size(); ++i) {
            Row row(csv, csv.rows[i], i + 2, path, result.diagnostics);
            const ExpCurveEntryId id{ row.integer<std::uint32_t>("Entry ID") };
            if (!ids.insert(id).second) diagnostic(result, AlxDiagnosticCode::DuplicateIdentity,
                "Duplicate canonical experience-curve identity", path, i + 2);
            ExpCurveFields f{}; for (std::size_t n = 0; n < f.experience.size(); ++n)
                f.experience[n] = row.integer<std::int32_t>("EXP " + std::to_string(n + 1));
            detail::AlxModelAccess::append(table, id, std::move(f));
            rememberDerived(result, canonicalIdentity(id), row.derived());
        }
        const auto count = table.records().size(); result.table = std::move(table); return count;
    });
}

CharacterImportResult CharacterCsvImporter::importBytes(std::span<const std::uint8_t> bytes,
    const AlxImportOptions& options, const std::filesystem::path& path) const
{
    return importTable<CharacterImportResult>(bytes, options, path, AlxTableKind::Character,
        [&](const CsvDocument& csv, AlxLocale locale, CharacterImportResult& result) {
        CharacterTable table; std::set<CharacterEntryId> ids;
        constexpr std::array colors{ "Green", "Red", "Purple", "Blue", "Yellow", "Silver" };
        constexpr std::array states{ "Poison", "Unconscious", "Stone", "Sleep", "Confusion", "Silence",
            "Fatigue", "Revival", "Weak", "State 10", "State 11", "State 12", "State 13", "State 14", "State 15" };
        constexpr std::array growth{ "Power Growth", "Will Growth", "Vigor Growth", "Agile Growth", "Quick Growth" };
        for (std::size_t i = 0; i < csv.rows.size(); ++i) {
            Row row(csv, csv.rows[i], i + 2, path, result.diagnostics);
            const CharacterEntryId id{ row.integer<std::uint32_t>("Entry ID") };
            if (!ids.insert(id).second) diagnostic(result, AlxDiagnosticCode::DuplicateIdentity,
                "Duplicate canonical character identity", path, i + 2);
            const auto code = locale == AlxLocale::Japanese ? "JP" : locale == AlxLocale::UnitedStates ? "US" : "EU";
            CharacterFields f{}; f.name = row.text(std::string("Entry ") + code + " Name");
            f.age = row.integer<std::int8_t>("Age"); f.genderId = row.integer<std::int8_t>("Gender ID");
            f.width = row.integer<std::int8_t>("Width"); f.depth = row.integer<std::int8_t>("Depth");
            f.maxMp = row.integer<std::int8_t>("MAXMP"); f.elementId = row.integer<std::int8_t>("Element ID");
            f.padding1 = row.integer<std::int8_t>("Pad 1"); f.weaponId = row.integer<std::uint16_t>("Weapon ID");
            f.armorId = row.integer<std::uint16_t>("Armor ID"); f.accessoryId = row.integer<std::uint16_t>("Accessory ID");
            f.movementFlags = row.integer<std::int16_t>("Movement Flags"); f.hp = row.integer<std::int16_t>("HP");
            f.maxHp = row.integer<std::int16_t>("MAXHP"); f.maxHpGrowth = row.integer<std::int16_t>("MAXHP Growth");
            f.spirit = row.integer<std::int16_t>("SP"); f.maxSpirit = row.integer<std::int16_t>("MAXSP");
            f.counterPercent = row.integer<std::int16_t>("Counter%"); f.padding2 = row.integer<std::int16_t>("Pad 2");
            f.experience = row.integer<std::uint32_t>("EXP"); f.maxMpGrowth = row.floating("MAXMP Growth"); f.unknown1 = row.floating("Unk 1");
            for (std::size_t n = 0; n < colors.size(); ++n) f.elements[n] = row.integer<std::int16_t>(colors[n]);
            for (std::size_t n = 0; n < states.size(); ++n) f.states[n] = row.integer<std::int16_t>(states[n]);
            f.danger = row.integer<std::int16_t>("Danger"); f.power = row.integer<std::int16_t>("Power");
            f.will = row.integer<std::int16_t>("Will"); f.vigor = row.integer<std::int16_t>("Vigor");
            f.agile = row.integer<std::int16_t>("Agile"); f.quick = row.integer<std::int16_t>("Quick");
            f.padding3 = row.integer<std::int16_t>("Pad 3");
            for (std::size_t n = 0; n < growth.size(); ++n) f.growth[n] = row.floating(growth[n]);
            for (std::size_t n = 0; n < colors.size(); ++n) f.magicExperience[n] = row.integer<std::int32_t>(std::string(colors[n]) + " EXP");
            detail::AlxModelAccess::append(table, id, std::move(f));
            rememberDerived(result, canonicalIdentity(id), row.derived());
            detail::AlxDerivedContextAccess::addCharacterName(result.derivedContext, id, table.records().back().fields().name);
        }
        const auto count = table.records().size(); result.table = std::move(table); return count;
    });
}

CharacterMagicImportResult CharacterMagicCsvImporter::importBytes(std::span<const std::uint8_t> bytes,
    const AlxImportOptions& options, const std::filesystem::path& path) const
{
    return importTable<CharacterMagicImportResult>(bytes, options, path, AlxTableKind::CharacterMagic,
        [&](const CsvDocument& csv, AlxLocale locale, CharacterMagicImportResult& result) {
            return parseCharacterSkill<CharacterMagicImportResult, CharacterMagicTable, CharacterMagicEntryId>(csv, locale, result, path, true);
        });
}

CharacterSuperMoveImportResult CharacterSuperMoveCsvImporter::importBytes(std::span<const std::uint8_t> bytes,
    const AlxImportOptions& options, const std::filesystem::path& path) const
{
    return importTable<CharacterSuperMoveImportResult>(bytes, options, path, AlxTableKind::CharacterSuperMove,
        [&](const CsvDocument& csv, AlxLocale locale, CharacterSuperMoveImportResult& result) {
            return parseCharacterSkill<CharacterSuperMoveImportResult, CharacterSuperMoveTable, CharacterSuperMoveEntryId>(csv, locale, result, path, false);
        });
}

MagicExpCurveImportResult MagicExpCurveCsvImporter::importBytes(std::span<const std::uint8_t> bytes,
    const AlxImportOptions& options, const std::filesystem::path& path) const
{
    return importTable<MagicExpCurveImportResult>(bytes, options, path, AlxTableKind::MagicExpCurve,
        [&](const CsvDocument& csv, AlxLocale, MagicExpCurveImportResult& result) {
        MagicExpCurveTable table; std::set<MagicExpCurveEntryId> ids;
        constexpr std::array colors{ "Green", "Red", "Purple", "Blue", "Yellow", "Silver" };
        for (std::size_t i = 0; i < csv.rows.size(); ++i) {
            Row row(csv, csv.rows[i], i + 2, path, result.diagnostics);
            const MagicExpCurveEntryId id{ row.integer<std::uint32_t>("Entry ID") };
            if (!ids.insert(id).second) diagnostic(result, AlxDiagnosticCode::DuplicateIdentity,
                "Duplicate canonical magic-experience identity", path, i + 2);
            MagicExpCurveFields f{};
            for (std::size_t color = 0; color < colors.size(); ++color) for (std::size_t n = 0; n < 6; ++n)
                f.experience[color][n] = row.integer<std::uint16_t>(std::string(colors[color]) + " EXP " + std::to_string(n + 1));
            detail::AlxModelAccess::append(table, id, std::move(f));
            rememberDerived(result, canonicalIdentity(id), row.derived());
        }
        const auto count = table.records().size(); result.table = std::move(table); return count;
    });
}

#define DEFINE_IMPORT_FILE(Name, Result) \
Result Name##CsvImporter::importFile(const std::filesystem::path& path, const AlxImportOptions& options) const \
{ return importFileImpl<Result>(*this, path, options); }
DEFINE_IMPORT_FILE(Enemy, EnemyImportResult)
DEFINE_IMPORT_FILE(EnemyEncounter, EnemyEncounterImportResult)
DEFINE_IMPORT_FILE(EnemyEvent, EnemyEventImportResult)
DEFINE_IMPORT_FILE(EnemyTask, EnemyTaskImportResult)
DEFINE_IMPORT_FILE(EnemyMagic, EnemyMagicImportResult)
DEFINE_IMPORT_FILE(Accessory, AccessoryImportResult)
DEFINE_IMPORT_FILE(Armor, ArmorImportResult)
DEFINE_IMPORT_FILE(UsableItem, UsableItemImportResult)
DEFINE_IMPORT_FILE(Weapon, WeaponImportResult)
DEFINE_IMPORT_FILE(WeaponEffect, WeaponEffectImportResult)
DEFINE_IMPORT_FILE(ExpCurve, ExpCurveImportResult)
DEFINE_IMPORT_FILE(Character, CharacterImportResult)
DEFINE_IMPORT_FILE(CharacterMagic, CharacterMagicImportResult)
DEFINE_IMPORT_FILE(CharacterSuperMove, CharacterSuperMoveImportResult)
DEFINE_IMPORT_FILE(MagicExpCurve, MagicExpCurveImportResult)
#undef DEFINE_IMPORT_FILE

namespace {

template <typename Result, typename Assign>
void accept(AlxDatasetImportResult& output, Result input, Assign assign)
{
    output.diagnostics.insert(output.diagnostics.end(),
        std::make_move_iterator(input.diagnostics.begin()), std::make_move_iterator(input.diagnostics.end()));
    if (!input.ok()) return;
    output.metadata.push_back(*input.metadata);
    detail::AlxDerivedContextAccess::merge(output.derivedContext, std::move(input.derivedContext));
    assign(std::move(*input.table));
}

} // namespace

AlxDatasetImportResult AlxDatasetImporter::importDirectory(
    const std::filesystem::path& directory,
    std::span<const AlxTableKind> requested,
    const AlxImportOptions& options) const
{
    AlxDatasetImportResult result{};
    if (requested.empty()) {
        diagnostic(result, AlxDiagnosticCode::MissingRequestedTable,
            "At least one whitelisted table must be requested", directory);
        return result;
    }
    std::set<AlxTableKind> unique;
    for (const auto kind : requested) if (!unique.insert(kind).second) {
        diagnostic(result, AlxDiagnosticCode::DuplicateRequestedTable,
            "A whitelisted table was requested more than once", directory / canonicalFilename(kind));
    }
    if (hasErrors(result.diagnostics)) return result;

    for (const auto kind : requested) if (!std::filesystem::is_regular_file(directory / canonicalFilename(kind))) {
        diagnostic(result, AlxDiagnosticCode::MissingRequestedTable,
            "Requested ALX 5.0.0 table is missing", directory / canonicalFilename(kind));
    }
    if (hasErrors(result.diagnostics)) return result;

    std::optional<AlxLocale> locale = options.localeHint;
    if (!locale) {
        constexpr std::array locales{ AlxLocale::Japanese, AlxLocale::UnitedStates, AlxLocale::Europe };
        for (const auto kind : requested) {
            const auto path = directory / canonicalFilename(kind);
            const auto csv = detail::CsvReader{}.readFile(path);
            if (!csv.ok()) continue;
            std::vector<AlxLocale> matches;
            for (const auto candidate : locales) if (csv.document->headers == canonicalHeaders(kind, candidate)) matches.push_back(candidate);
            if (matches.size() == 1) {
                if (locale && *locale != matches.front()) {
                    diagnostic(result, AlxDiagnosticCode::ConflictingLocale,
                        "Requested files contain conflicting locale-specific schemas", path, 1);
                    return result;
                }
                locale = matches.front();
            }
        }
    }
    if (!locale) {
        diagnostic(result, AlxDiagnosticCode::AmbiguousLocale,
            "Requested files are locale-neutral; provide an authoritative locale hint", directory);
        return result;
    }

    AlxDataset dataset{};
    const AlxImportOptions resolved{ .localeHint = locale };
    for (const auto kind : requested) {
        const auto path = directory / canonicalFilename(kind);
        switch (kind) {
        case AlxTableKind::Enemy: accept(result, EnemyCsvImporter{}.importFile(path, resolved), [&](auto value) { dataset.enemies = std::move(value); }); break;
        case AlxTableKind::EnemyEncounter: accept(result, EnemyEncounterCsvImporter{}.importFile(path, resolved), [&](auto value) { dataset.enemyEncounters = std::move(value); }); break;
        case AlxTableKind::EnemyEvent: accept(result, EnemyEventCsvImporter{}.importFile(path, resolved), [&](auto value) { dataset.enemyEvents = std::move(value); }); break;
        case AlxTableKind::EnemyTask: accept(result, EnemyTaskCsvImporter{}.importFile(path, resolved), [&](auto value) { dataset.enemyTasks = std::move(value); }); break;
        case AlxTableKind::EnemyMagic: accept(result, EnemyMagicCsvImporter{}.importFile(path, resolved), [&](auto value) { dataset.enemyMagic = std::move(value); }); break;
        case AlxTableKind::Accessory: accept(result, AccessoryCsvImporter{}.importFile(path, resolved), [&](auto value) { dataset.accessories = std::move(value); }); break;
        case AlxTableKind::Armor: accept(result, ArmorCsvImporter{}.importFile(path, resolved), [&](auto value) { dataset.armor = std::move(value); }); break;
        case AlxTableKind::UsableItem: accept(result, UsableItemCsvImporter{}.importFile(path, resolved), [&](auto value) { dataset.usableItems = std::move(value); }); break;
        case AlxTableKind::Weapon: accept(result, WeaponCsvImporter{}.importFile(path, resolved), [&](auto value) { dataset.weapons = std::move(value); }); break;
        case AlxTableKind::WeaponEffect: accept(result, WeaponEffectCsvImporter{}.importFile(path, resolved), [&](auto value) { dataset.weaponEffects = std::move(value); }); break;
        case AlxTableKind::ExpCurve: accept(result, ExpCurveCsvImporter{}.importFile(path, resolved), [&](auto value) { dataset.experienceCurves = std::move(value); }); break;
        case AlxTableKind::Character: accept(result, CharacterCsvImporter{}.importFile(path, resolved), [&](auto value) { dataset.characters = std::move(value); }); break;
        case AlxTableKind::CharacterMagic: accept(result, CharacterMagicCsvImporter{}.importFile(path, resolved), [&](auto value) { dataset.characterMagic = std::move(value); }); break;
        case AlxTableKind::CharacterSuperMove: accept(result, CharacterSuperMoveCsvImporter{}.importFile(path, resolved), [&](auto value) { dataset.characterSuperMoves = std::move(value); }); break;
        case AlxTableKind::MagicExpCurve: accept(result, MagicExpCurveCsvImporter{}.importFile(path, resolved), [&](auto value) { dataset.magicExperienceCurves = std::move(value); }); break;
        }
    }
    if (!hasErrors(result.diagnostics)) {
        for (const auto kind : requested) {
            auto view = AlxDerivedViewBuilder{}.build(dataset, result.derivedContext, kind);
            result.diagnostics.insert(result.diagnostics.end(),
                std::make_move_iterator(view.diagnostics.begin()),
                std::make_move_iterator(view.diagnostics.end()));
        }
    }
    if (!hasErrors(result.diagnostics)) {
        result.dataset = std::move(dataset);
        result.locale = locale;
    } else {
        result.metadata.clear();
        result.derivedContext = {};
    }
    return result;
}

AlxDatasetImportResult AlxDatasetImporter::importWhitelistedDirectory(
    const std::filesystem::path& directory, const AlxImportOptions& options) const
{
    std::vector<AlxTableKind> kinds;
    for (const auto& table : whitelistedTables()) kinds.push_back(table.kind);
    return importDirectory(directory, kinds, options);
}

} // namespace spice::trade::alx
