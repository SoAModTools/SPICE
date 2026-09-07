#include "../SpiceTrade/SpiceTrade.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace spice::trade::alx;

using CsvOverrides = std::map<std::string, std::string>;

void appendCsvCell(std::string& csv, std::string_view value)
{
    const bool quote = value.find_first_of(",\"\r\n") != std::string_view::npos;
    if (!quote) {
        csv.append(value);
        return;
    }
    csv.push_back('"');
    for (const char c : value) {
        csv.push_back(c);
        if (c == '"') csv.push_back('"');
    }
    csv.push_back('"');
}

std::vector<std::uint8_t> csvBytes(
    AlxTableKind kind, AlxLocale locale, const std::vector<CsvOverrides>& rows)
{
    const auto headers = canonicalHeaders(kind, locale);
    std::string csv{};
    for (std::size_t i = 0; i < headers.size(); ++i) {
        if (i != 0) csv.push_back(',');
        appendCsvCell(csv, headers[i]);
    }
    csv.push_back('\n');
    for (const auto& row : rows) {
        for (std::size_t i = 0; i < headers.size(); ++i) {
            if (i != 0) csv.push_back(',');
            const auto override = row.find(headers[i]);
            if (override != row.end()) {
                appendCsvCell(csv, override->second);
            } else if (headers[i].starts_with('[')) {
                appendCsvCell(csv, "");
            } else {
                appendCsvCell(csv, "0");
            }
        }
        csv.push_back('\n');
    }
    return { csv.begin(), csv.end() };
}

CsvOverrides encounterRow(
    std::uint32_t id, std::string owner, std::uint8_t initiative,
    std::uint8_t magicExperience, std::vector<std::uint8_t> enemies)
{
    CsvOverrides row{
        { "Entry ID", std::to_string(id) },
        { "[Filter]", std::move(owner) },
        { "Initiative", std::to_string(initiative) },
        { "Magic EXP", std::to_string(magicExperience) },
    };
    for (std::size_t i = 0; i < 8; ++i) {
        row["EC" + std::to_string(i + 1) + " ID"] =
            std::to_string(i < enemies.size() ? enemies[i] : 255U);
    }
    return row;
}

bool hasDiagnostic(const std::vector<AlxDiagnostic>& diagnostics, AlxDiagnosticCode code)
{
    return std::ranges::any_of(diagnostics, [code](const auto& diagnostic) {
        return diagnostic.code == code;
    });
}

TEST(SpiceTradeHardCut, WhitelistAndStableIdentitiesAreCanonical)
{
    ASSERT_EQ(whitelistedTables().size(), 15U);
    EXPECT_TRUE(whitelistedTableKind("ENEMYTASK.CSV").has_value());
    EXPECT_EQ(canonicalIdentity(EnemyEntryId{42}), "enemy.42");
    EXPECT_EQ(canonicalIdentity(EnemyTaskEntryId{EnemyEntryId{42}, 3}), "enemytask.42.3");
    EXPECT_EQ(canonicalIdentity(EnemyEncounterEntryId{"MA000.ENP", 7}), "enemyencounter.ma000.enp.7");
}

TEST(SpiceTradeHardCut, EnemyEncountersPublishZeroBasedEntriesForEveryOwner)
{
    const auto bytes = csvBytes(AlxTableKind::EnemyEncounter, AlxLocale::UnitedStates, {
        encounterRow(0, "MA000.ENP", 22, 1, { 15, 15 }),
        encounterRow(1, "MA000.ENP", 44, 2, { 18 }),
        encounterRow(0, "MA001.ENP", 63, 3, { 13, 13, 13, 13 }),
    });
    const auto imported = EnemyEncounterCsvImporter{}.importBytes(bytes);

    ASSERT_TRUE(imported.ok());
    ASSERT_TRUE(imported.metadata.has_value());
    EXPECT_EQ(imported.metadata->sourceRows, 3U);
    EXPECT_EQ(imported.metadata->publishedRows, 3U);
    EXPECT_EQ(imported.metadata->excludedRows, 0U);
    ASSERT_TRUE(imported.table.has_value());
    ASSERT_EQ(imported.table->groups().size(), 2U);
    ASSERT_EQ(imported.table->groups()[0].records().size(), 2U);
    ASSERT_EQ(imported.table->groups()[1].records().size(), 1U);
    EXPECT_EQ(imported.table->groups()[0].records()[0].id().entry, 0U);
    EXPECT_EQ(imported.table->groups()[0].records()[1].id().entry, 1U);
    EXPECT_EQ(imported.table->groups()[1].records()[0].id().entry, 0U);

    const auto* first = imported.table->find(EnemyEncounterEntryId{ "ma000.enp", 0U });
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->initiative, 22U);
    EXPECT_EQ(first->magicExperience, 1U);
    EXPECT_EQ(first->enemies[0].enemy, std::optional{ EnemyEntryId{ 15U } });
    EXPECT_EQ(first->enemies[1].enemy, std::optional{ EnemyEntryId{ 15U } });
    EXPECT_FALSE(first->enemies[2].enemy.has_value());
    EXPECT_EQ(canonicalIdentity(EnemyEncounterEntryId{ "MA000.ENP", 0U }),
        "enemyencounter.ma000.enp.0");
}

TEST(SpiceTradeHardCut, EnemyEncounterGroupsMustBeContiguousAndZeroBased)
{
    const std::array invalidRows{
        std::vector<CsvOverrides>{ encounterRow(1, "MA000.ENP", 22, 1, { 15 }) },
        std::vector<CsvOverrides>{
            encounterRow(0, "MA000.ENP", 22, 1, { 15 }),
            encounterRow(2, "MA000.ENP", 44, 2, { 18 }),
        },
    };
    for (const auto& rows : invalidRows) {
        const auto imported = EnemyEncounterCsvImporter{}.importBytes(
            csvBytes(AlxTableKind::EnemyEncounter, AlxLocale::UnitedStates, rows));
        EXPECT_FALSE(imported.ok());
        EXPECT_FALSE(imported.table.has_value());
        EXPECT_TRUE(hasDiagnostic(imported.diagnostics, AlxDiagnosticCode::InvalidGrouping));
    }
}

TEST(SpiceTradeHardCut, EnemyImportPublishesOnlyExactWildcardFilterRows)
{
    const auto bytes = csvBytes(AlxTableKind::Enemy, AlxLocale::UnitedStates, {
        CsvOverrides{
            { "Entry ID", "0" }, { "[Filter]", "epevent.evp" },
            { "Entry JP Name", "Contextual soldier" }, { "MAXHP", "999" },
        },
        CsvOverrides{
            { "Entry ID", "0" }, { "[Filter]", "*" },
            { "Entry JP Name", "Canonical soldier" }, { "MAXHP", "58" },
        },
        CsvOverrides{
            { "Entry ID", "1" }, { "[Filter]", "* " },
            { "Entry JP Name", "Whitespace is not canonical" }, { "MAXHP", "777" },
        },
        CsvOverrides{
            { "Entry ID", "1" }, { "[Filter]", "*" },
            { "Entry JP Name", "Canonical second enemy" }, { "MAXHP", "100" },
        },
    });
    const auto imported = EnemyCsvImporter{}.importBytes(bytes);

    ASSERT_TRUE(imported.ok());
    ASSERT_TRUE(imported.metadata.has_value());
    EXPECT_EQ(imported.metadata->sourceRows, 4U);
    EXPECT_EQ(imported.metadata->publishedRows, 2U);
    EXPECT_EQ(imported.metadata->excludedRows, 2U);
    ASSERT_TRUE(imported.table.has_value());
    ASSERT_EQ(imported.table->records().size(), 2U);
    const auto* first = imported.table->find(EnemyEntryId{ 0U });
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->japaneseName, "Canonical soldier");
    EXPECT_EQ(first->maxHp, 58);
    const auto* second = imported.table->find(EnemyEntryId{ 1U });
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->japaneseName, "Canonical second enemy");
    EXPECT_EQ(second->maxHp, 100);
}

} // namespace
