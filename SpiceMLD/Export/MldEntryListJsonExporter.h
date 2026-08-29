#pragma once

#include "../Parsing/MldParser.h"

#include <filesystem>
#include <span>
#include <string>

namespace spice::mld::exporting {

class MldEntryListJsonExporter {
public:
    [[nodiscard]] std::string toJson(
        const std::filesystem::path& sourcePath,
        std::span<const parsing::ParsedEntryListItem> entries) const;
};

} // namespace spice::mld::exporting
