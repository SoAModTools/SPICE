#pragma once

#include "AlxModel.h"

#include <map>

namespace spice::trade::alx {

namespace detail { struct AlxDerivedContextAccess; }

using AlxDerivedCells = std::map<std::string, std::string, std::less<>>;

class AlxDerivedContext {
public:
    [[nodiscard]] const AlxDerivedCells* imported(std::string_view identity) const noexcept;
    [[nodiscard]] std::optional<std::string_view> enemyName(
        EnemyEntryId id, AlxLocale locale = AlxLocale::Japanese) const noexcept;
    [[nodiscard]] std::optional<std::string_view> characterName(CharacterEntryId id) const noexcept;
    bool operator==(const AlxDerivedContext&) const = default;

private:
    friend struct detail::AlxDerivedContextAccess;
    std::map<std::string, AlxDerivedCells, std::less<>> imported_{};
    std::map<std::pair<EnemyEntryId, AlxLocale>, std::string> enemyNames_{};
    std::map<CharacterEntryId, std::string> characterNames_{};
};

namespace detail {
struct AlxDerivedContextAccess {
    static void addImported(AlxDerivedContext& context, std::string identity, AlxDerivedCells cells);
    static void addEnemyName(AlxDerivedContext& context, EnemyEntryId id, AlxLocale locale, std::string name);
    static void addCharacterName(AlxDerivedContext& context, CharacterEntryId id, std::string name);
    static void merge(AlxDerivedContext& destination, AlxDerivedContext source);
};
}

struct AlxDerivedRow {
    std::string identity{};
    AlxDerivedCells cells{};
    bool operator==(const AlxDerivedRow&) const = default;
};

struct AlxDerivedView {
    std::vector<AlxDerivedRow> rows{};
    std::vector<AlxDiagnostic> diagnostics{};
};

class AlxDerivedViewBuilder {
public:
    [[nodiscard]] AlxDerivedView build(
        const AlxDataset& dataset,
        const AlxDerivedContext& context,
        AlxTableKind table) const;
};

} // namespace spice::trade::alx
