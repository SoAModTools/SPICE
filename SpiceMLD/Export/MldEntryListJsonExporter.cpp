#include "MldEntryListJsonExporter.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace spice::mld::exporting {
namespace {

std::string jsonEscape(const std::string_view value) {
    std::ostringstream escaped{};
    for (const unsigned char character : value) {
        switch (character) {
        case '\\': escaped << "\\\\"; break;
        case '"': escaped << "\\\""; break;
        case '\b': escaped << "\\b"; break;
        case '\f': escaped << "\\f"; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default:
            if (character < 0x20U) {
                escaped << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<unsigned int>(character) << std::dec;
            } else {
                escaped << static_cast<char>(character);
            }
            break;
        }
    }
    return escaped.str();
}

std::string hexU32(const std::uint32_t value) {
    std::ostringstream out{};
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

void writeU32Array(std::ostream& out, const std::span<const std::uint32_t> values) {
    out << "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) out << ", ";
        out << values[index];
    }
    out << "]";
}

void writeStringArray(std::ostream& out, const std::span<const std::string> values) {
    out << "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) out << ", ";
        out << "\"" << jsonEscape(values[index]) << "\"";
    }
    out << "]";
}

} // namespace

std::string MldEntryListJsonExporter::toJson(
    const std::filesystem::path& sourcePath,
    const std::span<const parsing::ParsedEntryListItem> entries) const {
    std::ostringstream out{};
    out << "{\n";
    out << "  \"schema\": \"spice_mld_entry_list_v1\",\n";
    out << "  \"source\": \"" << jsonEscape(sourcePath.string()) << "\",\n";
    out << "  \"entry_count\": " << entries.size() << ",\n";
    out << "  \"entries\": [\n";
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        out << "    {\n";
        out << "      \"table_index\": " << entry.tableIndex << ",\n";
        out << "      \"entryID\": " << entry.entryId << ",\n";
        out << "      \"tableID\": " << entry.tblId << ",\n";
        out << "      \"function\": \"" << jsonEscape(entry.fxnName) << "\",\n";
        out << "      \"object_count\": " << entry.objectCount << ",\n";
        out << "      \"ground_count\": " << entry.groundCount << ",\n";
        out << "      \"motion_count\": " << entry.motionCount << ",\n";
        out << "      \"texture_count\": " << entry.textureCount << ",\n";
        out << "      \"textures_pointer\": " << entry.texturesPointer << ",\n";
        out << "      \"textures_pointer_hex\": \"" << hexU32(entry.texturesPointer) << "\",\n";
        out << "      \"ground_links\": ";
        writeU32Array(out, entry.groundLinks);
        out << ",\n";
        out << "      \"param_list2\": ";
        writeU32Array(out, entry.paramList2);
        out << ",\n";
        out << "      \"function_parameters\": ";
        writeU32Array(out, entry.functionParameters);
        out << ",\n";
        out << "      \"object_addresses\": ";
        writeU32Array(out, entry.objectAddresses);
        out << ",\n";
        out << "      \"ground_addresses\": ";
        writeU32Array(out, entry.groundAddresses);
        out << ",\n";
        out << "      \"motion_addresses\": ";
        writeU32Array(out, entry.motionAddresses);
        out << ",\n";
        out << "      \"texture_names\": ";
        writeStringArray(out, entry.textureNames);
        out << "\n";
        out << "    }";
        if (index + 1 < entries.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

} // namespace spice::mld::exporting
