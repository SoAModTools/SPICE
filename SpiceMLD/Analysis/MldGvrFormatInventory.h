#pragma once

#include "../Model/MldTextureArchiveModel.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace spice::mld::analysis {

struct MldGvrTextureSample {
    std::string sourcePath{};
    std::size_t textureIndex = 0;
    std::uint32_t archiveTextureIndex = 0;
    std::string textureName{};
    bool hasGlobalIndex = false;
    std::uint32_t globalIndex = 0;
    std::uint8_t rawFlags = 0;
    std::uint8_t rawDataFormat = 0;
    std::string sourceFormat{};
    std::string sourcePaletteFormat{};
    bool hasMipmaps = false;
    bool hasInternalPalette = false;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::size_t imageDataSize = 0;
    std::size_t paletteDataSize = 0;
    bool decoded = false;
    std::vector<std::string> diagnosticPreview{};
};

struct MldGvrFormatGroup {
    std::string sourceFormat{};
    std::string sourcePaletteFormat{};
    std::uint8_t rawFlags = 0;
    std::uint8_t rawDataFormat = 0;
    bool hasMipmaps = false;
    bool hasInternalPalette = false;
    bool encoderCovered = false;
    std::size_t textureCount = 0;
    std::size_t decodedCount = 0;
    std::vector<std::string> sourceFiles{};
    std::vector<MldGvrTextureSample> representativeSamples{};
};

struct MldGvrFormatInventory {
    std::size_t filesScanned = 0;
    std::size_t filesParsed = 0;
    std::size_t filesFailed = 0;
    std::size_t textureCount = 0;
    std::size_t decodedTextureCount = 0;
    std::vector<std::string> failures{};
    std::vector<MldGvrTextureSample> samples{};
    std::vector<MldGvrFormatGroup> formatGroups{};
    std::vector<MldGvrFormatGroup> priorityGroups{};
};

class MldGvrFormatInventoryBuilder {
public:
    void noteFileScanned();
    void addParseFailure(const std::string& sourcePath, const std::string& message);
    void addParsedMld(const std::string& sourcePath, const model::MldTextureArchive& archive);

    [[nodiscard]] MldGvrFormatInventory build() const;

private:
    MldGvrFormatInventory inventory_{};
};

[[nodiscard]] bool isGvrEncoderCovered(const MldGvrTextureSample& sample);
[[nodiscard]] std::string formatMldGvrFormatInventoryJson(const MldGvrFormatInventory& inventory);
[[nodiscard]] std::string formatMldGvrFormatInventoryMarkdown(const MldGvrFormatInventory& inventory);

} // namespace spice::mld::analysis
