#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace spice::mld::model {

struct MldTextureEntry {
    std::uint32_t archiveTextureIndex = 0;
    std::size_t archiveOffset = 0;
    std::size_t gvrDataOffset = 0;
    std::size_t gvrDataSize = 0;
    bool hasGlobalIndex = false;
    std::uint32_t globalIndex = 0;
    std::string textureName{};
    std::uint8_t pixelFormat = 0;
    std::uint8_t dataFormat = 0;
    std::string sourceFormat{};
    std::string sourcePaletteFormat{};
    bool hasMipmaps = false;
    bool hasInternalPalette = false;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::size_t imageDataOffset = 0;
    std::size_t imageDataSize = 0;
    std::size_t paletteDataSize = 0;
    std::vector<std::uint8_t> gvrData{};
    bool decoded = false;
    std::vector<std::uint8_t> rgba8{};
    std::vector<std::string> diagnostics{};
};

struct MldTextureArchive {
    std::size_t tableOffset = 0;
    std::vector<MldTextureEntry> entries{};
    std::vector<std::string> diagnostics{};
};

} // namespace spice::mld::model
