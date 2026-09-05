#include "AlxEnemyEventExport.h"

#include "../SpiceTrade/SpiceTrade.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace spice::alx {
namespace {

std::string jsonEscape(std::string_view value)
{
    std::ostringstream out;
    for (const unsigned char c : value) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20U) {
                constexpr char kHex[] = "0123456789ABCDEF";
                out << "\\u00" << kHex[(c >> 4U) & 0x0fU] << kHex[c & 0x0fU];
            } else out << static_cast<char>(c);
        }
    }
    return out.str();
}

std::string importError(const spice::trade::alx::EnemyEventImportResult& result)
{
    std::string message = "failed to import ALX enemy-event CSV";
    for (const auto& item : result.diagnostics) {
        if (item.severity == spice::trade::alx::AlxDiagnosticSeverity::Error) {
            message += ": " + item.message;
            break;
        }
    }
    return message;
}

std::string derived(const spice::trade::alx::AlxDerivedContext& context,
    std::string_view identity, const std::string& column)
{
    const auto* cells = context.imported(identity);
    if (!cells) return {};
    const auto found = cells->find(column);
    return found == cells->end() ? std::string{} : found->second;
}

} // namespace

void exportEnemyEventsCsvToJson(
    const std::filesystem::path& inputCsv,
    const std::filesystem::path& outputJson)
{
    const auto imported = spice::trade::alx::EnemyEventCsvImporter{}.importFile(inputCsv);
    if (!imported.ok()) throw std::runtime_error(importError(imported));

    if (outputJson.has_parent_path()) std::filesystem::create_directories(outputJson.parent_path());
    std::ofstream output(outputJson, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("failed to open ALX JSON output: " + outputJson.string());

    output << "{\n  \"schema\": \"spice_alx_enemy_events_v1\",\n"
           << "  \"sourceFile\": \"" << jsonEscape(inputCsv.generic_string()) << "\",\n"
           << "  \"events\": [\n";
    bool firstEvent = true;
    const auto locale = imported.metadata->locale;
    for (const auto& event : imported.table->records()) {
        if (!firstEvent) output << ",\n";
        firstEvent = false;
        const auto identity = spice::trade::alx::canonicalIdentity(event.id());
        const auto& fields = event.fields();
        output << "    {\n"
               << "      \"entryId\": " << event.id().value << ",\n"
               << "      \"magicExp\": " << static_cast<int>(fields.magicExperience) << ",\n"
               << "      \"initiative\": " << static_cast<int>(fields.initiative) << ",\n"
               << "      \"defeatConditionId\": " << static_cast<int>(fields.defeatConditionId) << ",\n"
               << "      \"escapeConditionId\": " << static_cast<int>(fields.escapeConditionId) << ",\n"
               << "      \"bgmId\": " << (fields.bgmId ? std::to_string(*fields.bgmId) : "-1") << ",\n"
               << "      \"combatants\": [\n";
        for (std::size_t i = 0; i < fields.players.size(); ++i) {
            const auto& player = fields.players[i];
            const auto id = player.character ? static_cast<int>(player.character->value) : -1;
            output << "        {\"slot\": " << i << ", \"side\": \"pc\", \"id\": " << id
                   << ", \"name\": \"" << jsonEscape(derived(imported.derivedContext, identity,
                        "[PC" + std::to_string(i + 1) + " Name]"))
                   << "\", \"gridX\": " << static_cast<int>(player.x)
                   << ", \"gridZ\": " << static_cast<int>(player.z)
                   << ", \"present\": " << (player.character ? "true" : "false") << "},\n";
        }
        for (std::size_t i = 0; i < fields.enemies.size(); ++i) {
            const auto& enemy = fields.enemies[i];
            const auto id = enemy.enemy ? static_cast<int>(enemy.enemy->value) : 255;
            std::string column = "[EC" + std::to_string(i + 1) + " JP Name]";
            if (locale != spice::trade::alx::AlxLocale::Japanese) column = "[EC" + std::to_string(i + 1)
                + (locale == spice::trade::alx::AlxLocale::UnitedStates ? " US Name]" : " EU Name]");
            output << "        {\"slot\": " << i + 4 << ", \"side\": \"enemy\", \"id\": " << id
                   << ", \"name\": \"" << jsonEscape(derived(imported.derivedContext, identity, column))
                   << "\", \"gridX\": " << static_cast<int>(enemy.x)
                   << ", \"gridZ\": " << static_cast<int>(enemy.z)
                   << ", \"present\": " << (enemy.enemy ? "true" : "false") << "}";
            if (i + 1 < fields.enemies.size()) output << ',';
            output << '\n';
        }
        output << "      ]\n    }";
    }
    output << "\n  ]\n}\n";
    if (!output.good()) throw std::runtime_error("failed while writing ALX JSON output: " + outputJson.string());
}

} // namespace spice::alx
