#include "../SpiceCore/SpiceCore.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>

namespace {

using spice::core::Endian;
using spice::core::EndianReader;
using spice::core::EndianWriter;
using spice::core::FourCC;

} // namespace

TEST(SpiceCoreBinary, ReadsAndWritesBigEndianPrimitives) {
    EndianWriter writer(Endian::Big);
    writer.write_u16(0x1234U);
    writer.write_i16(-2);
    writer.write_u32(0x89ABCDEFU);
    writer.write_i32(-3);
    writer.write_f32(1.5F);

    const auto bytes = writer.data();
    ASSERT_EQ(bytes.size(), 16U);
    EXPECT_EQ(bytes[0], 0x12U);
    EXPECT_EQ(bytes[1], 0x34U);
    EXPECT_EQ(bytes[4], 0x89U);
    EXPECT_EQ(bytes[5], 0xABU);

    EndianReader reader(bytes, Endian::Big);
    EXPECT_EQ(reader.read_u16(0U), 0x1234U);
    EXPECT_EQ(reader.read_i16(2U), -2);
    EXPECT_EQ(reader.read_u32(4U), 0x89ABCDEFU);
    EXPECT_EQ(reader.read_i32(8U), -3);
    EXPECT_FLOAT_EQ(reader.read_f32(12U), 1.5F);
}

TEST(SpiceCoreBinary, ReadsAndWritesLittleEndianPrimitives) {
    EndianWriter writer(Endian::Little);
    writer.write_u16(0x1234U);
    writer.write_u32(0x89ABCDEFU);
    writer.write_f32(2.25F);

    const auto bytes = writer.data();
    ASSERT_EQ(bytes.size(), 10U);
    EXPECT_EQ(bytes[0], 0x34U);
    EXPECT_EQ(bytes[1], 0x12U);
    EXPECT_EQ(bytes[2], 0xEFU);
    EXPECT_EQ(bytes[3], 0xCDU);

    EndianReader reader(bytes, Endian::Little);
    EXPECT_EQ(reader.read_u16(0U), 0x1234U);
    EXPECT_EQ(reader.read_u32(2U), 0x89ABCDEFU);
    EXPECT_FLOAT_EQ(reader.read_f32(6U), 2.25F);
}

TEST(SpiceCoreBinary, PreservesFourCcAsByteTag) {
    const auto tag = FourCC::from_string("GRND");
    EXPECT_EQ(tag.string(), "GRND");
    EXPECT_EQ(tag.as_big_endian_u32(), 0x47524E44U);

    std::array<std::uint8_t, 4> bytes{ 'P', 'V', 'R', 'T' };
    EXPECT_EQ(FourCC::from_bytes(std::span<const std::uint8_t, 4>(bytes)).string(), "PVRT");
}

TEST(SpiceCoreBinary, BoundsAndAlignmentHelpers) {
    EXPECT_TRUE(spice::core::bounds_contains(16U, 12U, 4U));
    EXPECT_FALSE(spice::core::bounds_contains(16U, 13U, 4U));
    EXPECT_EQ(spice::core::align_up(17U, 8U), 24U);
    EXPECT_TRUE(spice::core::is_aligned(24U, 8U));
    EXPECT_FALSE(spice::core::is_aligned(20U, 8U));
}

