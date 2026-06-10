#pragma once

#include "../../SpiceCore/Binary/Endian.h"
#include "BlenderIrModel.h"
#include "IndexEntry.h"
#include "MldTextureArchiveModel.h"
#include "SearchWorldModel.h"
#include "WorldModel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace soasim::mld::model {

enum class TargetPlatform {
    Unknown,
    Dreamcast,
    GameCube,
};

struct MldHeader {
    std::uint32_t entryCount = 0;
    std::uint32_t indexTableOffset = 0;
    std::uint32_t functionParametersOffset = 0;
    std::uint32_t realDataOffset = 0;
    std::uint32_t textureTableOffset = 0;
};

struct MldUnknownRange {
    std::size_t offset = 0;
    std::size_t size = 0;
    std::string label{};
};

struct MldRawDataBlock {
    enum class Kind {
        Unknown,
        Grnd,
        Gobj,
        TextureArchive,
        Ninja,
    };

    Kind kind = Kind::Unknown;
    std::uint32_t offset = 0;
    std::size_t size = 0;
    std::string tag{};
    std::vector<std::uint8_t> bytes{};
};

struct MldIndexEntryRecord {
    IndexEntry entry{};
    std::uint32_t groundLinksPointer = 0;
    std::uint32_t paramList2Pointer = 0;
    std::uint32_t functionParametersPointer = 0;
    std::uint32_t objectAddressesPointer = 0;
    std::uint32_t groundAddressesPointer = 0;
    std::uint32_t motionAddressesPointer = 0;
    std::vector<std::uint8_t> rawBytes{};
};

struct MldFile {
    TargetPlatform sourcePlatform = TargetPlatform::Unknown;
    spice::core::Endian endian = spice::core::Endian::Big;
    bool sourceWasCompressedAklz = false;
    MldHeader header{};
    std::vector<MldIndexEntryRecord> entries{};
    std::vector<U32List> u32Lists{};
    std::vector<MldRawDataBlock> rawDataBlocks{};
    std::vector<MldUnknownRange> paddingAndUnknownRanges{};
    std::optional<MldTextureArchive> textureArchive{};
    std::vector<std::uint8_t> originalBytes{};

    WorldModel world{};
    SearchWorldModel searchWorld{};
    std::optional<BlenderIrScene> blenderIrScene{};
    std::vector<std::string> diagnostics{};
};

} // namespace soasim::mld::model

