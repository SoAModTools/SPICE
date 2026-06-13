#pragma once

#include "SstSmlModel.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace spice::sstsml {

struct SmlBlenderIrEntrySummary {
    std::size_t meshCount{ 0U };
    std::size_t objectTreeCount{ 0U };
    std::size_t indexEntryCount{ 0U };
    std::size_t textureCount{ 0U };
    std::size_t animationCount{ 0U };
    std::size_t animationNodeCount{ 0U };
    std::size_t animationPositionKeyCount{ 0U };
    std::size_t animationRotationKeyCount{ 0U };
    std::size_t animationScaleKeyCount{ 0U };
    std::size_t animationQuaternionKeyCount{ 0U };
    std::size_t varyingAnimationChannelCount{ 0U };
    std::vector<std::string> indexEntryNames{};
    std::vector<std::string> varyingAnimationChannels{};
};

struct SmlEmbeddedMldExportOptions {
    std::filesystem::path stageOutputDir{};
    std::filesystem::path stageAnnotationRepositoryDir{};
    std::string stem{};
    bool writeEmbeddedMldPayloads{ true };
    bool writeCommandMap{ true };
    bool writeStageAnnotationTemplate{ true };
    bool overwriteStageAnnotationTemplate{ false };
    std::optional<std::filesystem::path> combinedBlenderIrPath{};
    std::map<std::size_t, std::filesystem::path> blenderIrPathsByRecordIndex{};
    std::map<std::size_t, SmlBlenderIrEntrySummary> blenderIrSummariesByRecordIndex{};
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
    std::optional<std::filesystem::path> stageAnnotationTemplatePath{};
    std::optional<std::filesystem::path> stageAnnotationMediaDir{};
    std::optional<std::filesystem::path> stageAnnotationCombinedBlenderIrPath{};
    bool wroteManifest{ false };
    bool wroteCommandMap{ false };
    bool wroteStageAnnotationTemplate{ false };
    bool createdStageAnnotationMediaDir{ false };
    bool copiedStageAnnotationCombinedBlenderIr{ false };
    std::vector<SmlEmbeddedMldExportedEntry> entries{};
    std::vector<std::string> diagnostics{};
};

SmlSstCommandMapExportResult exportSmlEmbeddedMldsAndCommandMap(
    const SmlParseResult& sml,
    const SstParseResult* sst,
    const SmlEmbeddedMldExportOptions& options);

} // namespace spice::sstsml
