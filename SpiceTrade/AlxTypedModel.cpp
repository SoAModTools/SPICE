#include "AlxTypedModel.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace spice::trade::alx {
namespace {

constexpr std::array<AlxTableDescriptor, 3U> kWhitelist{
    AlxTableDescriptor{ AlxTableKind::Enemy, "enemy.csv" },
    AlxTableDescriptor{ AlxTableKind::EnemyEncounter, "enemyencounter.csv" },
    AlxTableDescriptor{ AlxTableKind::EnemyEvent, "enemyevent.csv" },
};

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string localizedHeader(const std::string_view prefix, const AlxLocale locale)
{
    if (locale == AlxLocale::Japanese) {
        return {};
    }
    return "[" + std::string(prefix)
        + (locale == AlxLocale::UnitedStates ? " US Name]" : " EU Name]");
}

void appendEnemyReferenceHeaders(
    std::vector<std::string>& headers,
    const std::size_t count,
    const AlxLocale locale,
    const bool placements)
{
    for (std::size_t index = 1U; index <= count; ++index) {
        const auto prefix = "EC" + std::to_string(index);
        headers.push_back(prefix + " ID");
        headers.push_back("[" + prefix + " JP Name]");
        if (locale != AlxLocale::Japanese) {
            headers.push_back(localizedHeader(prefix, locale));
        }
        if (placements) {
            headers.push_back(prefix + " X");
            headers.push_back(prefix + " Z");
        }
    }
}

} // namespace

std::span<const AlxTableDescriptor> whitelistedTables() noexcept
{
    return kWhitelist;
}

std::optional<AlxTableKind> whitelistedTableKind(const std::filesystem::path& path) noexcept
{
    const auto basename = lowerAscii(path.filename().string());
    const auto found = std::find_if(kWhitelist.begin(), kWhitelist.end(), [&](const auto& descriptor) {
        return basename == descriptor.filename;
    });
    if (found == kWhitelist.end()) {
        return std::nullopt;
    }
    return found->kind;
}

std::string_view canonicalFilename(const AlxTableKind kind) noexcept
{
    const auto found = std::find_if(kWhitelist.begin(), kWhitelist.end(), [&](const auto& descriptor) {
        return descriptor.kind == kind;
    });
    return found == kWhitelist.end() ? std::string_view{} : found->filename;
}

std::vector<std::string> canonicalHeaders(const AlxTableKind kind, const AlxLocale locale)
{
    std::vector<std::string> headers{};
    if (kind == AlxTableKind::Enemy) {
        headers = {
            "Entry ID", "[Filter]", "Entry JP Name",
        };
        if (locale != AlxLocale::Japanese) {
            headers.push_back(localizedHeader("Entry", locale));
        }
        const std::array<std::string_view, 82U> remaining{
            "Width", "Depth", "Element ID", "[Element Name]", "Pad 1", "Pad 2",
            "Movement Flags", "[May Dodge]", "[Unk Damage]", "[Unk Ranged]", "[Unk Melee]",
            "[Ranged Atk]", "[Melee Atk]", "[Ranged Only]", "[Take Cover]", "[In Air]",
            "[On Ground]", "[Reserved]", "[May Move]", "Counter%", "EXP", "Gold", "Pad 3",
            "Pad 4", "MAXHP", "Unk 1", "Green", "Red", "Purple", "Blue", "Yellow", "Silver",
            "Poison", "Unconscious", "Stone", "Sleep", "Confusion", "Silence", "Fatigue",
            "Revival", "Weak", "State 10", "State 11", "State 12", "State 13", "State 14",
            "State 15", "Danger", "Effect ID", "[Effect Name]", "State ID", "[State Name]",
            "State Miss%", "Pad 5", "Level", "Will", "Vigor", "Agile", "Quick", "Attack",
            "Defense", "MagDef", "Hit%", "Dodge%", "Pad 6", "Pad 7", "Item 1 Prob",
            "Item 1 Amount", "Item 1 ID", "[Item 1 Name]", "Item 2 Prob", "Item 2 Amount",
            "Item 2 ID", "[Item 2 Name]", "Item 3 Prob", "Item 3 Amount", "Item 3 ID",
            "[Item 3 Name]", "Item 4 Prob", "Item 4 Amount", "Item 4 ID", "[Item 4 Name]",
        };
        headers.insert(headers.end(), remaining.begin(), remaining.end());
        return headers;
    }

    if (kind == AlxTableKind::EnemyEncounter) {
        headers = { "Entry ID", "[Filter]", "Initiative", "Magic EXP" };
        appendEnemyReferenceHeaders(headers, 8U, locale, false);
        return headers;
    }

    headers = { "Entry ID", "Magic EXP" };
    for (std::size_t index = 1U; index <= 4U; ++index) {
        const auto prefix = "PC" + std::to_string(index);
        headers.push_back(prefix + " ID");
        headers.push_back("[" + prefix + " Name]");
        headers.push_back(prefix + " X");
        headers.push_back(prefix + " Z");
    }
    appendEnemyReferenceHeaders(headers, 7U, locale, true);
    headers.insert(headers.end(), {
        "Initiative", "Defeat Cond ID", "[Defeat Cond Name]",
        "Escape Cond ID", "[Escape Cond Name]", "BGM ID",
    });
    return headers;
}

const char* toString(const AlxTableKind kind) noexcept
{
    switch (kind) {
    case AlxTableKind::Enemy:
        return "enemy";
    case AlxTableKind::EnemyEncounter:
        return "enemy encounter";
    case AlxTableKind::EnemyEvent:
        return "enemy event";
    }
    return "unknown";
}

const char* toString(const AlxLocale locale) noexcept
{
    switch (locale) {
    case AlxLocale::Japanese:
        return "jp";
    case AlxLocale::UnitedStates:
        return "us";
    case AlxLocale::Europe:
        return "eu";
    }
    return "unknown";
}

} // namespace spice::trade::alx
