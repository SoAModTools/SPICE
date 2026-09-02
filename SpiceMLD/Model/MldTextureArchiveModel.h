#pragma once

#include "MldDiagnostics.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace spice::mld::model {

enum class MldTextureEncoding {
    Unknown,
    Gvr,
    Pvr,
};

struct MldTextureEntry {
    MldResourceStatus status = MldResourceStatus::Empty;
    std::uint32_t archiveTextureIndex = 0;
    std::size_t archiveOffset = 0;
    std::size_t encodedDataOffset = 0;
    std::size_t encodedDataSize = 0;
    MldTextureEncoding encoding = MldTextureEncoding::Unknown;
    std::uint32_t rawRecordWord0 = 0;
    std::uint32_t rawRecordWord1 = 0;
    std::uint32_t declaredBlockSize = 0;
    std::vector<std::uint8_t> alignmentPrefixBytes{};
    std::vector<std::uint8_t> trailingBlockBytes{};
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
    std::vector<std::uint8_t> encodedData{};
    bool decoded = false;
    std::vector<std::uint8_t> rgba8{};
    std::vector<MldDiagnostic> diagnostics{};
};

struct MldTextureArchive {
    MldResourceStatus status = MldResourceStatus::Empty;
    std::size_t tableOffset = 0;
    std::size_t archiveStartOffset = 0;
    std::size_t archiveEndOffset = 0;
    std::vector<std::uint8_t> archivePrefixBytes{};
    std::vector<MldTextureEntry> entries{};
    std::vector<MldDiagnostic> diagnostics{};
};

} // namespace spice::mld::model
