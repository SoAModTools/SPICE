#include "../Compression/Aklz.h"
#include "../SpiceEct/SpiceEct.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kTableSize = 0x84U;
constexpr std::size_t kIndexHeaderSize = 0x08U;
constexpr std::size_t kIndexRecordSize = 0x20U;
constexpr std::size_t kIndexTitleSize = 0x14U;
constexpr std::size_t kIndexedPayloadSize = 0x420U;

void writeU16(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint16_t value,
    bool bigEndian) {
    const auto high = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    const auto low = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset] = bigEndian ? high : low;
    bytes[offset + 1U] = bigEndian ? low : high;
}
void writeU32(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint32_t value,
    bool bigEndian) {
    for (std::size_t i = 0; i < 4U; ++i) {
        const auto shift = bigEndian ? (3U - i) * 8U : i * 8U;
        bytes[offset + i] = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
    }
}

std::uint16_t readU16(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    bool bigEndian) {
    if (bigEndian) {
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bytes[offset]) << 8U) | bytes[offset + 1U]);
    }
    return static_cast<std::uint16_t>(
        bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

std::uint32_t readU32(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    bool bigEndian) {
    std::uint32_t value = 0U;
    for (std::size_t i = 0; i < 4U; ++i) {
        const auto shift = bigEndian ? (3U - i) * 8U : i * 8U;
        value |= static_cast<std::uint32_t>(bytes[offset + i]) << shift;
    }
    return value;
}

spice::ect::EctEncounterTable makeTable(
    std::uint16_t stage,
    std::uint16_t overallRate,
    std::uint16_t encounterBase) {
    spice::ect::EctEncounterTable table{};
    table.stage = stage;
    table.overallEncounterRate = overallRate;
    for (std::size_t i = 0; i < table.encounters.size(); ++i) {
        table.encounters[i].encounterId = static_cast<std::uint16_t>(encounterBase + i);
        table.encounters[i].encounterRate = static_cast<std::uint16_t>(i * 3U);
    }
    return table;
}

spice::ect::EctFile makeFlatFile(std::size_t tableCount) {
    spice::ect::EctFlatContent flat{};
    flat.tables.reserve(tableCount);
    for (std::size_t i = 0; i < tableCount; ++i) {
        flat.tables.push_back(makeTable(
            static_cast<std::uint16_t>(0x20U + i),
            static_cast<std::uint16_t>(0x10U + i),
            static_cast<std::uint16_t>(0x40U + i * 0x40U)));
    }
    return spice::ect::EctFile{ spice::ect::EctContent{ std::move(flat) } };
}

spice::ect::EctFile makeOverworldFile() {
    spice::ect::EctOverworldContent overworld{};
    for (std::size_t entryIndex = 0; entryIndex < 2U; ++entryIndex) {
        spice::ect::EctOverworldEntry entry{};
        entry.title = entryIndex == 0U ? "a099b_01.ect" : "dam01.ect";
        for (std::size_t tableIndex = 0; tableIndex < entry.tables.size(); ++tableIndex) {
            entry.tables[tableIndex] = makeTable(
                static_cast<std::uint16_t>(0x40U + entryIndex * 0x10U + tableIndex),
                static_cast<std::uint16_t>(0x18U + tableIndex),
                static_cast<std::uint16_t>(0x80U + entryIndex * 0x100U + tableIndex * 0x20U));
        }
        overworld.entries.push_back(std::move(entry));
    }
    return spice::ect::EctFile{ spice::ect::EctContent{ std::move(overworld) } };
}

void writeTableBytes(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    const spice::ect::EctEncounterTable& table,
    bool bigEndian) {
    writeU16(bytes, offset, table.stage, bigEndian);
    writeU16(bytes, offset + 0x02U, table.overallEncounterRate, bigEndian);
    for (std::size_t i = 0; i < table.encounters.size(); ++i) {
        const auto entryOffset = offset + 0x04U + i * 4U;
        writeU16(bytes, entryOffset, table.encounters[i].encounterId, bigEndian);
        writeU16(bytes, entryOffset + 0x02U, table.encounters[i].encounterRate, bigEndian);
    }
}

std::vector<std::uint8_t> serializeFlatFixture(
    const spice::ect::EctFile& file,
    bool bigEndian) {
    const auto& flat = std::get<spice::ect::EctFlatContent>(file.content);
    std::vector<std::uint8_t> bytes(flat.tables.size() * kTableSize, 0U);
    for (std::size_t i = 0; i < flat.tables.size(); ++i) {
        writeTableBytes(bytes, i * kTableSize, flat.tables[i], bigEndian);
    }
    return bytes;
}

std::vector<std::uint8_t> serializeOverworldFixture(
    const spice::ect::EctFile& file,
    bool bigEndian) {
    const auto& overworld = std::get<spice::ect::EctOverworldContent>(file.content);
    const auto payloadStart = kIndexHeaderSize + overworld.entries.size() * kIndexRecordSize;
    std::vector<std::uint8_t> bytes(
        payloadStart + overworld.entries.size() * kIndexedPayloadSize,
        0U);

    writeU16(bytes, 0x00U, 0U, bigEndian);
    writeU16(bytes, 0x02U, 0xFFFFU, bigEndian);
    writeU16(bytes, 0x04U, static_cast<std::uint16_t>(overworld.entries.size()), bigEndian);
    writeU16(bytes, 0x06U, 0xFFFFU, bigEndian);

    for (std::size_t entryIndex = 0; entryIndex < overworld.entries.size(); ++entryIndex) {
        const auto& entry = overworld.entries[entryIndex];
        const auto recordOffset = kIndexHeaderSize + entryIndex * kIndexRecordSize;
        const auto payloadOffset = payloadStart + entryIndex * kIndexedPayloadSize;
        std::copy(entry.title.begin(), entry.title.end(), bytes.begin() + recordOffset);
        writeU32(bytes, recordOffset + 0x14U, static_cast<std::uint32_t>(payloadOffset), bigEndian);
        writeU32(bytes, recordOffset + 0x18U, 0x420U, bigEndian);
        writeU32(bytes, recordOffset + 0x1CU, 0xFFFFFFFFU, bigEndian);
        for (std::size_t tableIndex = 0; tableIndex < entry.tables.size(); ++tableIndex) {
            writeTableBytes(
                bytes,
                payloadOffset + tableIndex * kTableSize,
                entry.tables[tableIndex],
                bigEndian);
        }
    }
    return bytes;
}

std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> decodeAklz(const std::vector<std::uint8_t>& bytes) {
    const auto decoded = spice::compression::aklz::decompress(bytes);
    if (!decoded.ok()) {
        return {};
    }
    return decoded.bytes;
}

} // namespace

TEST(SpiceEctParser, ParsesEquivalentDreamcastAndGameCubeFlatFiles) {
    const auto expected = makeFlatFile(2U);
    const auto dreamcastBytes = serializeFlatFixture(expected, false);
    const auto gameCubeDecoded = serializeFlatFixture(expected, true);
    const auto gameCubeBytes = spice::compression::aklz::compress(gameCubeDecoded);
    ASSERT_TRUE(gameCubeBytes.ok());

    const auto dreamcast = spice::ect::EctParser::parse(
        dreamcastBytes,
        spice::ect::EctLayout::Flat);
    const auto gameCube = spice::ect::EctParser::parse(
        gameCubeBytes.bytes,
        spice::ect::EctLayout::Flat);

    ASSERT_TRUE(dreamcast.ok());
    ASSERT_TRUE(gameCube.ok());
    EXPECT_EQ(*dreamcast.file, expected);
    EXPECT_EQ(*gameCube.file, expected);
    EXPECT_EQ(*dreamcast.file, *gameCube.file);
}

TEST(SpiceEctParser, ParsesAllOverworldEntriesIncludingDamPayloads) {
    const auto expected = makeOverworldFile();
    const auto dreamcastBytes = serializeOverworldFixture(expected, false);
    const auto gameCubeDecoded = serializeOverworldFixture(expected, true);
    const auto gameCubeBytes = spice::compression::aklz::compress(gameCubeDecoded);
    ASSERT_TRUE(gameCubeBytes.ok());

    const auto dreamcast = spice::ect::EctParser::parse(
        dreamcastBytes,
        spice::ect::EctLayout::OverworldIndexed);
    const auto gameCube = spice::ect::EctParser::parse(
        gameCubeBytes.bytes,
        spice::ect::EctLayout::OverworldIndexed);

    ASSERT_TRUE(dreamcast.ok());
    ASSERT_TRUE(gameCube.ok());
    EXPECT_EQ(*dreamcast.file, expected);
    EXPECT_EQ(*gameCube.file, expected);
    const auto& entries = std::get<spice::ect::EctOverworldContent>(dreamcast.file->content).entries;
    ASSERT_EQ(entries.size(), 2U);
    EXPECT_EQ(entries[1].title, "dam01.ect");
    EXPECT_EQ(entries[1].tables[7].stage, 0x57U);
}

TEST(SpiceEctWriter, EditedFlatIrExportsForBothPlatforms) {
    auto file = makeFlatFile(1U);
    auto& table = std::get<spice::ect::EctFlatContent>(file.content).tables[0];
    table.stage = 0x1234U;
    table.encounters[31].encounterId = 0x4567U;

    const spice::ect::EctFileWriter writer{};
    const auto dreamcast = writer.write(file, spice::ect::EctTargetPlatform::Dreamcast);
    const auto gameCube = writer.write(file, spice::ect::EctTargetPlatform::GameCube);
    ASSERT_TRUE(dreamcast.ok());
    ASSERT_TRUE(gameCube.ok());
    EXPECT_FALSE(spice::compression::aklz::isAklz(dreamcast.bytes));
    EXPECT_TRUE(spice::compression::aklz::isAklz(gameCube.bytes));
    EXPECT_EQ(readU16(dreamcast.bytes, 0U, false), 0x1234U);

    const auto gameCubeDecoded = decodeAklz(gameCube.bytes);
    ASSERT_FALSE(gameCubeDecoded.empty());
    EXPECT_EQ(readU16(gameCubeDecoded, 0U, true), 0x1234U);

    const auto reparsedDreamcast = spice::ect::EctParser::parse(
        dreamcast.bytes,
        spice::ect::EctLayout::Flat);
    const auto reparsedGameCube = spice::ect::EctParser::parse(
        gameCube.bytes,
        spice::ect::EctLayout::Flat);
    ASSERT_TRUE(reparsedDreamcast.ok());
    ASSERT_TRUE(reparsedGameCube.ok());
    EXPECT_EQ(*reparsedDreamcast.file, file);
    EXPECT_EQ(*reparsedGameCube.file, file);
}

TEST(SpiceEctWriter, RebuildsCanonicalOverworldOffsetsAndTitles) {
    auto file = makeOverworldFile();
    auto& overworld = std::get<spice::ect::EctOverworldContent>(file.content);
    overworld.entries[0].title = "12345678901234567890";
    overworld.entries[1].tables[0].stage = 0x4321U;

    const spice::ect::EctFileWriter writer{};
    const auto dreamcast = writer.write(file, spice::ect::EctTargetPlatform::Dreamcast);
    const auto gameCube = writer.write(file, spice::ect::EctTargetPlatform::GameCube);
    ASSERT_TRUE(dreamcast.ok());
    ASSERT_TRUE(gameCube.ok());

    EXPECT_EQ(readU16(dreamcast.bytes, 0x00U, false), 0U);
    EXPECT_EQ(readU16(dreamcast.bytes, 0x02U, false), 0xFFFFU);
    EXPECT_EQ(readU16(dreamcast.bytes, 0x04U, false), 2U);
    EXPECT_EQ(readU32(dreamcast.bytes, 0x08U + 0x14U, false), 0x48U);
    EXPECT_EQ(readU32(dreamcast.bytes, 0x08U + 0x18U, false), 0x420U);
    EXPECT_EQ(readU32(dreamcast.bytes, 0x08U + 0x1CU, false), 0xFFFFFFFFU);
    EXPECT_EQ(readU32(dreamcast.bytes, 0x28U + 0x14U, false), 0x468U);

    const auto reparsedDreamcast = spice::ect::EctParser::parse(
        dreamcast.bytes,
        spice::ect::EctLayout::OverworldIndexed);
    const auto reparsedGameCube = spice::ect::EctParser::parse(
        gameCube.bytes,
        spice::ect::EctLayout::OverworldIndexed);
    ASSERT_TRUE(reparsedDreamcast.ok());
    ASSERT_TRUE(reparsedGameCube.ok());
    EXPECT_EQ(*reparsedDreamcast.file, file);
    EXPECT_EQ(*reparsedGameCube.file, file);
}

TEST(SpiceEctParser, RejectsMalformedFlatAndOverworldInputs) {
    const std::vector<std::uint8_t> badFlat(0x83U, 0U);
    EXPECT_FALSE(spice::ect::EctParser::parse(badFlat, spice::ect::EctLayout::Flat).ok());

    const auto expected = makeOverworldFile();
    auto badHeader = serializeOverworldFixture(expected, false);
    writeU16(badHeader, 0x02U, 0U, false);
    EXPECT_FALSE(spice::ect::EctParser::parse(
        badHeader,
        spice::ect::EctLayout::OverworldIndexed).ok());

    auto badSize = serializeOverworldFixture(expected, false);
    writeU32(badSize, 0x08U + 0x18U, 0x41FU, false);
    EXPECT_FALSE(spice::ect::EctParser::parse(
        badSize,
        spice::ect::EctLayout::OverworldIndexed).ok());

    auto badTail = serializeOverworldFixture(expected, false);
    writeU32(badTail, 0x08U + 0x1CU, 0U, false);
    EXPECT_FALSE(spice::ect::EctParser::parse(
        badTail,
        spice::ect::EctLayout::OverworldIndexed).ok());

    auto badTitle = serializeOverworldFixture(expected, false);
    badTitle[0x08U] = 0x1FU;
    EXPECT_FALSE(spice::ect::EctParser::parse(
        badTitle,
        spice::ect::EctLayout::OverworldIndexed).ok());

    auto outOfBounds = serializeOverworldFixture(expected, false);
    writeU32(outOfBounds, 0x08U + 0x14U, 0xFFFFFF00U, false);
    EXPECT_FALSE(spice::ect::EctParser::parse(
        outOfBounds,
        spice::ect::EctLayout::OverworldIndexed).ok());

    auto overlapping = serializeOverworldFixture(expected, false);
    writeU32(overlapping, 0x28U + 0x14U, 0x48U, false);
    EXPECT_FALSE(spice::ect::EctParser::parse(
        overlapping,
        spice::ect::EctLayout::OverworldIndexed).ok());
}

TEST(SpiceEctWriter, RejectsUnrepresentableIr) {
    const spice::ect::EctFileWriter writer{};
    const spice::ect::EctFile emptyFlat{
        spice::ect::EctContent{ spice::ect::EctFlatContent{} }
    };
    EXPECT_FALSE(writer.write(emptyFlat, spice::ect::EctTargetPlatform::Dreamcast).ok());

    const spice::ect::EctFile emptyOverworld{
        spice::ect::EctContent{ spice::ect::EctOverworldContent{} }
    };
    EXPECT_FALSE(writer.write(emptyOverworld, spice::ect::EctTargetPlatform::GameCube).ok());

    auto invalidTitle = makeOverworldFile();
    std::get<spice::ect::EctOverworldContent>(invalidTitle.content).entries[0].title =
        "123456789012345678901";
    EXPECT_FALSE(writer.write(invalidTitle, spice::ect::EctTargetPlatform::Dreamcast).ok());
}
