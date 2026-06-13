#pragma once

#include "SstSmlModel.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace spice::sstsml {

struct SmlEmbeddedMldExportOptions {
    std::filesystem::path stageOutputDir{};
    std::string stem{};
    bool writeEmbeddedMldPayloads{ true };
    bool writeCommandMap{ true };
    std::map<std::size_t, std::filesystem::path> blenderIrPathsByRecordIndex{};
};

struct SmlEmbeddedMldExportedEntry {
    std::size_t recordIndex{ 0U };
    bool embeddedMldInBounds{ false };
    bool wroteEmbeddedMld{ false };
    std::filesystem::path embeddedMldPath{};
    std::optional<std::filesystem::path> blenderIrPath{};
    std::vector<std::string> diagnostics{};
};

struct SmlSstCommandMapExportResult {
    std::filesystem::path manifestPath{};
    std::optional<std::filesystem::path> commandMapPath{};
    bool wroteManifest{ false };
    bool wroteCommandMap{ false };
    std::vector<SmlEmbeddedMldExportedEntry> entries{};
    std::vector<std::string> diagnostics{};
};

SmlSstCommandMapExportResult exportSmlEmbeddedMldsAndCommandMap(
    const SmlParseResult& sml,
    const SstParseResult* sst,
    const SmlEmbeddedMldExportOptions& options);

} // namespace spice::sstsml
