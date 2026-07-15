#pragma once

#include <filesystem>

namespace spice::alx {

void exportEnemyEventsCsvToJson(
    const std::filesystem::path& inputCsv,
    const std::filesystem::path& outputJson);

} // namespace spice::alx
