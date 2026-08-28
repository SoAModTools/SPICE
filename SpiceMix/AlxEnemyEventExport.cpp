#include "AlxEnemyEventExport.h"

#include "../SpiceTrade/SpiceTrade.h"

#include <charconv>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace spice::alx {
namespace {

std::string jsonEscape(std::string_view value) {
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
            } else {
                out << static_cast<char>(c);
            }
            break;
        }
    }
    return out.str();
}

int parseInt(std::string_view value, std::string_view fieldName, int row) {
    int parsed = 0;
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if (error != std::errc() || end != value.data() + value.size()) {
        throw std::runtime_error(
            "invalid integer in ALX row " + std::to_string(row)
            + " field " + std::string(fieldName));
    }
    return parsed;
}

std::size_t requireColumn(
    const std::map<std::string, std::size_t>& columns,
    std::string_view name) {
    const auto found = columns.find(std::string(name));
    if (found == columns.end()) {
        throw std::runtime_error("missing ALX column: " + std::string(name));
    }
    return found->second;
}

struct CombatantColumns {
    int slot = -1;
    std::string side;
    std::size_t id = 0;
    std::size_t name = 0;
    std::size_t x = 0;
    std::size_t z = 0;
    int absentId = -1;
};

std::string csvReadError(const spice::trade::alx::CsvReadResult& result) {
    std::ostringstream message;
    message << "failed to read ALX enemy-event CSV";
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.severity != spice::trade::alx::DiagnosticSeverity::Error) {
            continue;
        }
        message << ": " << diagnostic.message;
        if (diagnostic.row.has_value()) {
            message << " at row " << *diagnostic.row;
            if (diagnostic.column.has_value()) {
                message << ", column " << *diagnostic.column;
            }
        }
        break;
    }
    return message.str();
}

} // namespace

void exportEnemyEventsCsvToJson(
    const std::filesystem::path& inputCsv,
    const std::filesystem::path& outputJson) {
    const auto read = spice::trade::alx::CsvReader{}.readFile(inputCsv);
    if (!read.ok()) {
        throw std::runtime_error(csvReadError(read));
    }
    const auto& document = *read.document;
    const auto& headers = document.headers;
    std::map<std::string, std::size_t> columns;
    for (std::size_t index = 0; index < headers.size(); ++index) {
        columns.emplace(headers[index], index);
    }

    std::vector<CombatantColumns> combatants;
    for (int pc = 1; pc <= 4; ++pc) {
        const auto prefix = "PC" + std::to_string(pc);
        combatants.push_back(CombatantColumns{
            .slot = pc - 1,
            .side = "pc",
            .id = requireColumn(columns, prefix + " ID"),
            .name = requireColumn(columns, "[" + prefix + " Name]"),
            .x = requireColumn(columns, prefix + " X"),
            .z = requireColumn(columns, prefix + " Z"),
            .absentId = -1,
        });
    }
    for (int enemy = 1; enemy <= 7; ++enemy) {
        const auto prefix = "EC" + std::to_string(enemy);
        combatants.push_back(CombatantColumns{
            .slot = enemy + 3,
            .side = "enemy",
            .id = requireColumn(columns, prefix + " ID"),
            .name = requireColumn(columns, "[" + prefix + " US Name]"),
            .x = requireColumn(columns, prefix + " X"),
            .z = requireColumn(columns, prefix + " Z"),
            .absentId = 255,
        });
    }

    const auto entryIdColumn = requireColumn(columns, "Entry ID");
    const auto magicExpColumn = requireColumn(columns, "Magic EXP");
    const auto initiativeColumn = requireColumn(columns, "Initiative");
    const auto defeatColumn = requireColumn(columns, "Defeat Cond ID");
    const auto escapeColumn = requireColumn(columns, "Escape Cond ID");
    const auto bgmColumn = requireColumn(columns, "BGM ID");

    if (outputJson.has_parent_path()) {
        std::filesystem::create_directories(outputJson.parent_path());
    }
    std::ofstream output(outputJson, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("failed to open ALX JSON output: " + outputJson.string());
    }

    output << "{\n"
           << "  \"schema\": \"spice_alx_enemy_events_v1\",\n"
           << "  \"sourceFile\": \"" << jsonEscape(inputCsv.generic_string()) << "\",\n"
           << "  \"events\": [\n";

    bool firstEvent = true;
    int row = 1;
    for (const auto& fields : document.rows) {
        ++row;
        if (!firstEvent) {
            output << ",\n";
        }
        firstEvent = false;

        output << "    {\n"
               << "      \"entryId\": " << parseInt(fields[entryIdColumn], "Entry ID", row) << ",\n"
               << "      \"magicExp\": " << parseInt(fields[magicExpColumn], "Magic EXP", row) << ",\n"
               << "      \"initiative\": " << parseInt(fields[initiativeColumn], "Initiative", row) << ",\n"
               << "      \"defeatConditionId\": " << parseInt(fields[defeatColumn], "Defeat Cond ID", row) << ",\n"
               << "      \"escapeConditionId\": " << parseInt(fields[escapeColumn], "Escape Cond ID", row) << ",\n"
               << "      \"bgmId\": " << parseInt(fields[bgmColumn], "BGM ID", row) << ",\n"
               << "      \"combatants\": [\n";

        for (std::size_t index = 0; index < combatants.size(); ++index) {
            const auto& columnsForCombatant = combatants[index];
            const int id = parseInt(fields[columnsForCombatant.id], "combatant ID", row);
            const bool present = id != columnsForCombatant.absentId;
            output << "        {\"slot\": " << columnsForCombatant.slot
                   << ", \"side\": \"" << columnsForCombatant.side
                   << "\", \"id\": " << id
                   << ", \"name\": \"" << jsonEscape(fields[columnsForCombatant.name])
                   << "\", \"gridX\": " << parseInt(fields[columnsForCombatant.x], "combatant X", row)
                   << ", \"gridZ\": " << parseInt(fields[columnsForCombatant.z], "combatant Z", row)
                   << ", \"present\": " << (present ? "true" : "false") << "}";
            if (index + 1 < combatants.size()) {
                output << ',';
            }
            output << '\n';
        }
        output << "      ]\n"
               << "    }";
    }
    output << "\n  ]\n}\n";
    if (!output.good()) {
        throw std::runtime_error("failed while writing ALX JSON output: " + outputJson.string());
    }
}

} // namespace spice::alx
