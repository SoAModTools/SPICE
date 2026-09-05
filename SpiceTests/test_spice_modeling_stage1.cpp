#include "gtest/gtest.h"

#include "Testing/Slice1TestApi.h"
#include "Testing/PortImplementation.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

using namespace spice::modeling::Testing::Slice1;
namespace S = spice::modeling::Structs;
namespace A = spice::modeling::Animation;
namespace F = spice::modeling::File;

void PutU16(std::vector<std::byte>& data, std::uint32_t offset, std::uint16_t value) {
    if (data.size() < offset + 2u) {
        data.resize(offset + 2u);
    }
    data[offset] = std::byte(value & 0xFFu);
    data[offset + 1u] = std::byte((value >> 8u) & 0xFFu);
}

void PutU32(std::vector<std::byte>& data, std::uint32_t offset, std::uint32_t value) {
    if (data.size() < offset + 4u) {
        data.resize(offset + 4u);
    }
    data[offset] = std::byte(value & 0xFFu);
    data[offset + 1u] = std::byte((value >> 8u) & 0xFFu);
    data[offset + 2u] = std::byte((value >> 16u) & 0xFFu);
    data[offset + 3u] = std::byte((value >> 24u) & 0xFFu);
}

void PutU16Endian(
    std::vector<std::byte>& data,
    std::uint32_t offset,
    std::uint16_t value,
    S::Endian endian) {
    PutU16(data, offset, endian == S::Endian::Little
        ? value
        : static_cast<std::uint16_t>((value << 8u) | (value >> 8u)));
}

void PutU32Endian(
    std::vector<std::byte>& data,
    std::uint32_t offset,
    std::uint32_t value,
    S::Endian endian) {
    const auto encoded = endian == S::Endian::Little ? value
        : ((value & 0x000000FFU) << 24U) |
            ((value & 0x0000FF00U) << 8U) |
            ((value & 0x00FF0000U) >> 8U) |
            ((value & 0xFF000000U) >> 24U);
    PutU32(data, offset, encoded);
}

void PutF32(std::vector<std::byte>& data, std::uint32_t offset, float value) {
    PutU32(data, offset, std::bit_cast<std::uint32_t>(value));
}

void PutVec3(std::vector<std::byte>& data, std::uint32_t offset, S::Vector3 value) {
    PutF32(data, offset, value.x);
    PutF32(data, offset + 4u, value.y);
    PutF32(data, offset + 8u, value.z);
}

void PutQuaternionWxyz(std::vector<std::byte>& data, std::uint32_t offset, S::Quaternion value) {
    PutF32(data, offset, value.w);
    PutF32(data, offset + 4u, value.x);
    PutF32(data, offset + 8u, value.y);
    PutF32(data, offset + 12u, value.z);
}

std::uint32_t MotionPointer(std::uint32_t targetOffset) {
    constexpr std::uint32_t kMotionPayloadOffset = 8u;
    return targetOffset - kMotionPayloadOffset;
}

std::vector<std::byte> MakeAnimationBlock(std::uint32_t size = 0x120u) {
    std::vector<std::byte> data(size, std::byte {0});
    PutU32(data, 0, F::FileHeaders::NMDM);
    PutU32(data, 4, size - 8u);
    return data;
}

std::vector<std::byte> MakeStructuralMotionBlock(
    std::uint32_t tag,
    S::Endian endian,
    bool includePof0 = true) {
    constexpr std::uint32_t payloadSize = 16U;
    constexpr std::uint32_t motionEnd = 8U + payloadSize;
    constexpr std::uint32_t pofSize = 8U;
    std::vector<std::byte> data(includePof0 ? motionEnd + 8U + pofSize : motionEnd, std::byte{0});
    PutU32(data, 0U, tag);
    PutU32Endian(data, 4U, payloadSize, endian);
    PutU32Endian(data, 8U, 0U, endian);
    PutU32Endian(data, 12U, 17U, endian);
    PutU16Endian(data, 16U, 0U, endian);
    PutU16Endian(data, 18U, 0x0080U, endian);
    PutU32Endian(data, 20U, 0x12345678U, endian);
    if (includePof0) {
        PutU32(data, motionEnd, F::FileHeaders::POF0);
        PutU32Endian(data, motionEnd + 4U, pofSize, endian);
        data[motionEnd + 8U] = std::byte{0x40};
        data[motionEnd + 9U] = std::byte{0x80};
        data[motionEnd + 10U] = std::byte{0x00};
        data[motionEnd + 11U] = std::byte{0xC0};
    }
    return data;
}

TEST(SpiceModelingStage1, FileHeadersRecognizeNjcmMagic) {
    constexpr std::array<char, 4> candidate {'N', 'J', 'C', 'M'};
    EXPECT_TRUE(MatchesMagic(candidate, kNjcmMagic));
    EXPECT_FALSE(MatchesMagic(candidate, kNjtlMagic));
}

TEST(SpiceModelingStage1, EndianReaderReadsLittleEndianPrimitives) {
    constexpr std::array<std::byte, 8> bytes {
        std::byte {0x78}, std::byte {0x56}, std::byte {0x34}, std::byte {0x12},
        std::byte {0xFC}, std::byte {0xFF}, std::byte {0xFF}, std::byte {0xFF},
    };

    auto reader = MakeReader(bytes, Endianness::Little);
    EXPECT_EQ(reader.ReadU32(), 0x12345678u);
    EXPECT_EQ(reader.ReadI32(), -4);
}

TEST(SpiceModelingStage1, EndianReaderAppliesImageBaseForPointerOffsets) {
    constexpr std::array<std::byte, 4> bytes {
        std::byte {0x20}, std::byte {0x10}, std::byte {0x00}, std::byte {0x00},
    };

    auto reader = MakeReader(bytes, Endianness::Little, 0x1000);
    EXPECT_EQ(reader.ReadPointerOffset(), 0x20u);
}

TEST(SpiceModelingStage1, PointerLutMemoizesByAddress) {
    PointerLUT<int> lut;
    auto first = std::make_shared<int>(7);
    auto second = std::make_shared<int>(9);

    auto stored = lut.GetOrAdd(0x1010, first);
    auto duplicate = lut.GetOrAdd(0x1010, second);

    EXPECT_EQ(lut.Size(), 1u);
    EXPECT_EQ(stored.get(), duplicate.get());
    ASSERT_NE(lut.TryGet(0x1010), nullptr);
    EXPECT_EQ(*lut.TryGet(0x1010), 7);
}

TEST(SpiceModelingStage1, BamsConversionsRoundTripDegreesAndRadians) {
    constexpr float degrees = 90.0f;
    const auto bams = DegreesToBams(degrees);

    EXPECT_EQ(bams, 16384);
    EXPECT_NEAR(BamsToDegrees(bams), degrees, 0.001f);

    constexpr float radians = kPi * 0.5f;
    EXPECT_NEAR(RadiansToBams(radians), 16384.0f, 0.01f);
    EXPECT_NEAR(BamsToRadians(16384), radians, 0.001f);
}

TEST(SpiceModelingStructs, RoundToEvenMatchesDotNetMidpoints) {
    EXPECT_EQ(S::MathCompat::round_to_even(0.5f), 0.0f);
    EXPECT_EQ(S::MathCompat::round_to_even(1.5f), 2.0f);
    EXPECT_EQ(S::MathCompat::round_to_even(2.5f), 2.0f);
    EXPECT_EQ(S::MathCompat::round_to_even(-0.5f), -0.0f);
    EXPECT_EQ(S::MathCompat::round_to_even(-1.5f), -2.0f);
}

TEST(SpiceModelingStructs, VectorAndMatrixBasicsWork) {
    const S::Vector3 a {1.0f, 2.0f, 3.0f};
    const S::Vector3 b {4.0f, 5.0f, 6.0f};
    EXPECT_EQ(a + b, S::Vector3(5.0f, 7.0f, 9.0f));
    EXPECT_FLOAT_EQ(S::dot(a, b), 32.0f);

    const auto scaleTranslate = S::create_scale(2.0f) * S::create_translation({3.0f, 4.0f, 5.0f});
    EXPECT_FLOAT_EQ(scaleTranslate.m11, 2.0f);
    EXPECT_FLOAT_EQ(scaleTranslate.m22, 2.0f);
    EXPECT_FLOAT_EQ(scaleTranslate.m33, 2.0f);
    EXPECT_FLOAT_EQ(scaleTranslate.m41, 3.0f);
    EXPECT_FLOAT_EQ(scaleTranslate.m42, 4.0f);
    EXPECT_FLOAT_EQ(scaleTranslate.m43, 5.0f);

    S::Matrix4x4 inverse;
    ASSERT_TRUE(S::invert(S::identity(), inverse));
    EXPECT_EQ(inverse, S::identity());
}

TEST(SpiceModelingStructs, EndianStackWriterAndReaderRoundTripBigEndian) {
    S::EndianStackWriter writer(S::Endian::Big);
    writer.write_u16(0x1234u);
    writer.write_i32(-4);
    writer.write_float(1.5f);

    const auto& bytes = writer.data();
    ASSERT_EQ(bytes.size(), 10u);
    EXPECT_EQ(bytes[0], std::byte {0x12});
    EXPECT_EQ(bytes[1], std::byte {0x34});

    S::EndianStackReader reader(bytes, S::Endian::Big);
    EXPECT_EQ(reader.read_u16(0), 0x1234u);
    EXPECT_EQ(reader.read_i32(2), -4);
    EXPECT_FLOAT_EQ(reader.read_float(6), 1.5f);
}

TEST(SpiceModelingStructs, FloatIOTypeWritesAndReadsExpectedFormats) {
    S::EndianStackWriter writer(S::Endian::Little);
    S::write_float_as(writer, 2.5f, S::FloatIOType::Short);
    S::write_float_as(writer, S::MathHelper::HalfPi, S::FloatIOType::BAMS16);

    S::EndianStackReader reader(writer.data(), S::Endian::Little);
    EXPECT_FLOAT_EQ(S::read_float_as(reader, 0, S::FloatIOType::Short), 2.0f);
    EXPECT_NEAR(S::read_float_as(reader, 2, S::FloatIOType::BAMS16), S::MathHelper::HalfPi, 0.001f);
    EXPECT_EQ(S::print_float_as(1.25f, S::FloatIOType::Float), "1.25000f");
    EXPECT_EQ(S::BAMSFHelper::RadToBAMSF(S::MathHelper::HalfPi), 16384);
    EXPECT_EQ(S::BAMSFHelper::DegToBAMSF(180.0f), 32768);
}

TEST(SpiceModelingStructs, ColorPackedFormatsAndHexRoundTrip) {
    S::Color color {0x12, 0x34, 0x56, 0x78};
    EXPECT_EQ(color.rgba(), 0x12345678u);
    EXPECT_EQ(color.argb(), 0x78123456u);

    S::Color parsed;
    parsed.set_hex("#12345678");
    EXPECT_EQ(parsed, color);
    EXPECT_EQ(parsed.hex(), "#12345678");

    EXPECT_EQ(S::Color(0xFF, 0x00, 0xFF).rgb565(), 0x001Fu);
    EXPECT_EQ(S::Color(0xF0, 0xA5, 0x5A, 0xCC).argb4(), 0x0005u);

    S::Color vectorColor;
    vectorColor.set_float_vector({1.0f, 0.25f, 0.5f, 1.0f});
    EXPECT_EQ(vectorColor.red, 0xFF);
    EXPECT_EQ(vectorColor.blue, 63);
    EXPECT_EQ(vectorColor.green, 127);
}

TEST(SpiceModelingStructs, DebugStringsMatchCSharpFormatting) {
    EXPECT_EQ(S::DebugStringExtensions::debug_string(1.25f), " 1.250");
    EXPECT_EQ(S::DebugStringExtensions::debug_string(-1.25f), "-1.250");
    EXPECT_EQ(S::DebugStringExtensions::debug_string(S::Vector2(1.0f, -2.0f)), "( 1.000, -2.000)");
}

TEST(SpiceModelingStructs, EndianIOExtensionsHandleColorsVectorsAndQuaternions) {
    S::EndianStackWriter writer(S::Endian::Little);
    S::EndianIOExtensions::write_color(writer, S::Color(0x12, 0x34, 0x56, 0x78), S::ColorIOType::ARGB8_16);
    S::EndianIOExtensions::write_vector3(writer, {1.0f, 2.0f, 3.0f});
    S::EndianIOExtensions::write_quaternion(writer, {1.0f, 2.0f, 3.0f, 4.0f});

    S::EndianStackReader reader(writer.data(), S::Endian::Little);
    std::uint32_t address = 0;
    const auto color = S::EndianIOExtensions::read_color(reader, address, S::ColorIOType::ARGB8_16);
    EXPECT_EQ(color.argb(), 0x78123456u);
    EXPECT_EQ(address, 4u);

    const auto vector = S::EndianIOExtensions::read_vector3(reader, address);
    EXPECT_EQ(vector, S::Vector3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(address, 16u);

    const auto quaternion = S::EndianIOExtensions::read_quaternion(reader, address);
    EXPECT_EQ(quaternion, S::Quaternion(1.0f, 2.0f, 3.0f, 4.0f));
    EXPECT_EQ(address, 32u);
}

TEST(SpiceModelingStructs, BaseLutCachesReadValuesAndWriteAddresses) {
    S::BaseLUT lut;
    int factoryCalls = 0;
    const auto first = lut.get_add_value<int>(0x20, [&]() {
        ++factoryCalls;
        return std::vector<int> {1, 2, 3};
    });
    const auto second = lut.get_add_value<int>(0x20, [&]() {
        ++factoryCalls;
        return std::vector<int> {9};
    });

    EXPECT_EQ(factoryCalls, 1);
    EXPECT_EQ(first, second);

    std::vector<int> values {1, 2};
    const auto firstAddress = lut.get_add_address(&values, []() { return 0x100u; });
    const auto secondAddress = lut.get_add_address(&values, []() { return 0x200u; });
    EXPECT_EQ(firstAddress, 0x100u);
    EXPECT_EQ(secondAddress, 0x100u);

    S::BaseLUT labeled({{0x40u, "known_label"}});
    const auto labeledArray = labeled.get_add_labeled_value<int>(0x40u, "generated", []() {
        return std::vector<int> {4, 5};
    });
    EXPECT_EQ(labeledArray.label, "known_label");
}

TEST(SpiceModelingAnimation, ReadsTransformKeyframeValuesFromNjAnimationBlock) {
    auto data = MakeAnimationBlock();
    constexpr std::uint32_t motionOffset = 8u;
    constexpr std::uint32_t keyframeTableOffset = 0x40u;
    constexpr std::uint32_t positionOffset = 0x60u;
    constexpr std::uint32_t rotationOffset = 0x90u;
    constexpr std::uint32_t scaleOffset = 0xA0u;
    constexpr std::uint32_t quaternionOffset = 0xB4u;

    const auto type = A::KeyframeAttributes::Position |
        A::KeyframeAttributes::EulerRotation |
        A::KeyframeAttributes::Scale |
        A::KeyframeAttributes::QuaternionRotation;

    PutU32(data, motionOffset, MotionPointer(keyframeTableOffset));
    PutU32(data, motionOffset + 4u, 11u);
    PutU16(data, motionOffset + 8u, static_cast<std::uint16_t>(type));
    PutU16(data, motionOffset + 10u, static_cast<std::uint16_t>(A::channel_count(type)));

    PutU32(data, keyframeTableOffset, MotionPointer(positionOffset));
    PutU32(data, keyframeTableOffset + 4u, MotionPointer(rotationOffset));
    PutU32(data, keyframeTableOffset + 8u, MotionPointer(scaleOffset));
    PutU32(data, keyframeTableOffset + 12u, MotionPointer(quaternionOffset));
    PutU32(data, keyframeTableOffset + 16u, 2u);
    PutU32(data, keyframeTableOffset + 20u, 1u);
    PutU32(data, keyframeTableOffset + 24u, 1u);
    PutU32(data, keyframeTableOffset + 28u, 1u);

    PutU32(data, positionOffset, 0u);
    PutVec3(data, positionOffset + 4u, {1.0f, 2.0f, 3.0f});
    PutU32(data, positionOffset + 16u, 10u);
    PutVec3(data, positionOffset + 20u, {4.0f, 5.0f, 6.0f});

    PutU32(data, rotationOffset, 0u);
    PutU32(data, rotationOffset + 4u, static_cast<std::uint32_t>(S::BAMSFHelper::RadToBAMSF(S::MathHelper::HalfPi)));
    PutU32(data, rotationOffset + 8u, 0u);
    PutU32(data, rotationOffset + 12u, 0u);

    PutU32(data, scaleOffset, 0u);
    PutVec3(data, scaleOffset + 4u, {2.0f, 3.0f, 4.0f});

    PutU32(data, quaternionOffset, 0u);
    PutQuaternionWxyz(data, quaternionOffset + 4u, {0.1f, 0.2f, 0.3f, 0.9f});

    const auto animationFile = F::AnimationFile::read_from_bytes(data, 1u);
    const auto& motion = animationFile.animation;
    ASSERT_EQ(motion.keyframes.size(), 1u);
    const auto& keyframes = motion.keyframes.at(0);

    EXPECT_EQ(motion.frame_count(), 11u);
    EXPECT_EQ(keyframes.position.size(), 2u);
    EXPECT_EQ(keyframes.position.at(0u), S::Vector3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(keyframes.position.at(10u), S::Vector3(4.0f, 5.0f, 6.0f));
    ASSERT_EQ(keyframes.euler_rotation.size(), 1u);
    EXPECT_NEAR(keyframes.euler_rotation.at(0u).x, S::MathHelper::HalfPi, 0.001f);
    EXPECT_EQ(keyframes.scale.at(0u), S::Vector3(2.0f, 3.0f, 4.0f));
    EXPECT_EQ(keyframes.quaternion_rotation.at(0u), S::Quaternion(0.1f, 0.2f, 0.3f, 0.9f));
}

TEST(SpiceModelingAnimation, StructurallyParsesMotionKindsEndiansAndPof0DeltaWidths) {
    const std::array kinds{
        std::pair{F::FileHeaders::NMDM, F::NinjaMotionKind::Node},
        std::pair{F::FileHeaders::NSSM, F::NinjaMotionKind::Shape},
        std::pair{F::FileHeaders::NCAM, F::NinjaMotionKind::Camera},
    };
    for (const auto endian : {S::Endian::Little, S::Endian::Big}) {
        for (const auto& [tag, kind] : kinds) {
            const auto data = MakeStructuralMotionBlock(tag, endian);
            const auto block = F::AnimationFile::parse_structure(data);
            EXPECT_EQ(block.status, F::NinjaMotionParseStatus::Complete);
            EXPECT_EQ(block.header.raw_tag, tag);
            EXPECT_EQ(block.header.kind, kind);
            EXPECT_EQ(block.header.payload_size, 16U);
            EXPECT_EQ(block.header.frame_count, 17U);
            EXPECT_EQ(block.header.raw_style, 0x0080U);
            EXPECT_EQ(block.header.interpolation_mode, A::InterpolationMode::User);
            EXPECT_EQ(block.header.reserved, 0x12345678U);
            EXPECT_EQ(block.chunk_header_range.offset, 0U);
            EXPECT_EQ(block.motion_header_range.offset, 8U);
            ASSERT_TRUE(block.pof0_range.has_value());
            EXPECT_EQ(block.pof0_range->offset, 24U);
            ASSERT_EQ(block.relocations.size(), 3U);
            EXPECT_EQ(block.relocations[0].encoded_range.size, 1U);
            EXPECT_EQ(block.relocations[1].encoded_range.size, 2U);
            EXPECT_EQ(block.relocations[2].encoded_range.size, 4U);
            for (const auto& relocation : block.relocations) {
                EXPECT_EQ(relocation.pointer_field_offset, 0U);
                EXPECT_EQ(relocation.raw_pointer, 0U);
                EXPECT_FALSE(relocation.resolved_payload_offset.has_value());
            }
        }
    }
}

TEST(SpiceModelingAnimation, StructuralParsePreservesUnknownTagAndReportsMissingOrMalformedPof0) {
    constexpr std::uint32_t unknownTag = 0x214E4A58U;
    const auto unknown = F::AnimationFile::parse_structure(
        MakeStructuralMotionBlock(unknownTag, S::Endian::Little));
    EXPECT_EQ(unknown.header.raw_tag, unknownTag);
    EXPECT_EQ(unknown.header.kind, F::NinjaMotionKind::Unknown);

    const auto missing = F::AnimationFile::parse_structure(
        MakeStructuralMotionBlock(F::FileHeaders::NMDM, S::Endian::Little, false));
    EXPECT_EQ(missing.status, F::NinjaMotionParseStatus::Partial);
    EXPECT_FALSE(missing.diagnostics.empty());

    auto malformed = MakeStructuralMotionBlock(F::FileHeaders::NMDM, S::Endian::Little);
    malformed[32U] = std::byte{0x01};
    const auto malformedBlock = F::AnimationFile::parse_structure(malformed);
    EXPECT_EQ(malformedBlock.status, F::NinjaMotionParseStatus::Partial);

    auto styleMismatch = MakeStructuralMotionBlock(F::FileHeaders::NMDM, S::Endian::Little);
    PutU16(styleMismatch, 16U, static_cast<std::uint16_t>(A::KeyframeAttributes::Position));
    const auto styleMismatchBlock = F::AnimationFile::parse_structure(styleMismatch);
    EXPECT_EQ(styleMismatchBlock.status, F::NinjaMotionParseStatus::Partial);

    auto truncated = MakeStructuralMotionBlock(F::FileHeaders::NMDM, S::Endian::Little);
    PutU32(truncated, 28U, 1U);
    truncated.resize(33U);
    truncated[32U] = std::byte{0xC0};
    const auto truncatedBlock = F::AnimationFile::parse_structure(truncated);
    EXPECT_EQ(truncatedBlock.status, F::NinjaMotionParseStatus::Partial);

    auto nonNull = MakeStructuralMotionBlock(F::FileHeaders::NMDM, S::Endian::Big);
    PutU32Endian(nonNull, 8U, 8U, S::Endian::Big);
    const auto nonNullBlock = F::AnimationFile::parse_structure(nonNull);
    ASSERT_FALSE(nonNullBlock.relocations.empty());
    EXPECT_EQ(nonNullBlock.relocations[0].raw_pointer, 8U);
    ASSERT_TRUE(nonNullBlock.relocations[0].resolved_payload_offset.has_value());
    EXPECT_EQ(*nonNullBlock.relocations[0].resolved_payload_offset, 16U);
}

TEST(SpiceModelingAnimation, Pof0DeltaFormsAdvanceCumulativelyInFourByteWords) {
    auto data = MakeStructuralMotionBlock(F::FileHeaders::NMDM, S::Endian::Little);
    PutU32(data, 12U, 0U);
    PutU32(data, 16U, 0U);
    PutU32(data, 20U, 0U);
    data[32U] = std::byte{0x41};
    data[33U] = std::byte{0x80};
    data[34U] = std::byte{0x01};
    data[35U] = std::byte{0xC0};
    data[36U] = std::byte{0x00};
    data[37U] = std::byte{0x00};
    data[38U] = std::byte{0x01};
    data[39U] = std::byte{0x00};

    const auto block = F::AnimationFile::parse_structure(data);
    EXPECT_EQ(block.status, F::NinjaMotionParseStatus::Complete);
    ASSERT_EQ(block.relocations.size(), 3U);
    EXPECT_EQ(block.relocations[0].pointer_field_offset, 4U);
    EXPECT_EQ(block.relocations[1].pointer_field_offset, 8U);
    EXPECT_EQ(block.relocations[2].pointer_field_offset, 12U);
    EXPECT_EQ(block.relocations[0].encoded_range.size, 1U);
    EXPECT_EQ(block.relocations[1].encoded_range.size, 2U);
    EXPECT_EQ(block.relocations[2].encoded_range.size, 4U);
}

TEST(SpiceModelingAnimation, TargetLayoutMapsAnimatedLanesToFullTreeNodeIndicesAndUsesFullEuler) {
    constexpr std::uint32_t motionOffset = 8U;
    constexpr std::uint32_t keyframeTableOffset = 0x30U;
    constexpr std::uint32_t rotationOffset = 0x50U;
    constexpr auto type = A::KeyframeAttributes::EulerRotation;
    for (const auto endian : {S::Endian::Little, S::Endian::Big}) {
        std::vector<std::byte> data(0x70U, std::byte{0});
        PutU32(data, 0U, F::FileHeaders::NMDM);
        PutU32Endian(data, 4U, static_cast<std::uint32_t>(data.size() - 8U), endian);
        PutU32Endian(data, motionOffset, MotionPointer(keyframeTableOffset), endian);
        PutU32Endian(data, motionOffset + 4U, 1U, endian);
        PutU16Endian(data, motionOffset + 8U, static_cast<std::uint16_t>(type), endian);
        PutU16Endian(data, motionOffset + 10U, static_cast<std::uint16_t>(A::channel_count(type)), endian);
        PutU32Endian(data, keyframeTableOffset, MotionPointer(rotationOffset), endian);
        PutU32Endian(data, keyframeTableOffset + 4U, 1U, endian);
        PutU32Endian(data, rotationOffset, 0U, endian);
        PutU32Endian(data, rotationOffset + 4U, 0x4000U, endian);
        PutU32Endian(data, rotationOffset + 8U, 0U, endian);
        PutU32Endian(data, rotationOffset + 12U, 0U, endian);

        A::MotionTargetLayout layout{};
        layout.lanes.push_back(A::MotionTargetLane{.node_index = 3U});
        const auto probe = F::AnimationFile::probe_from_bytes(
            data, layout, A::EulerRecordWidth::Full32);
        ASSERT_TRUE(probe.valid) << probe.failure_reason;
        EXPECT_FALSE(probe.short_rot);
        const auto parsed = F::AnimationFile::read_from_bytes(
            data, layout, A::EulerRecordWidth::Full32);
        EXPECT_EQ(parsed.animation.keyframes.count(0), 0U);
        ASSERT_EQ(parsed.animation.keyframes.count(3), 1U);
        EXPECT_FALSE(parsed.animation.short_rot);
        EXPECT_NEAR(parsed.animation.keyframes.at(3).euler_rotation.at(0U).x,
            S::MathHelper::HalfPi, 0.001F);
    }
}

TEST(SpiceModelingAnimation, DecodesOneLaneCameraPositionAndTargetChannels) {
    auto data = MakeAnimationBlock(0x80U);
    PutU32(data, 0U, F::FileHeaders::NCAM);
    constexpr std::uint32_t motionOffset = 8U;
    constexpr std::uint32_t keyframeTableOffset = 0x30U;
    constexpr std::uint32_t positionOffset = 0x50U;
    constexpr std::uint32_t targetOffset = 0x60U;
    constexpr auto type = A::KeyframeAttributes::Position | A::KeyframeAttributes::Target;

    PutU32(data, motionOffset, MotionPointer(keyframeTableOffset));
    PutU32(data, motionOffset + 4U, 12U);
    PutU16(data, motionOffset + 8U, static_cast<std::uint16_t>(type));
    PutU16(data, motionOffset + 10U, static_cast<std::uint16_t>(A::channel_count(type)));
    PutU32(data, keyframeTableOffset, MotionPointer(positionOffset));
    PutU32(data, keyframeTableOffset + 4U, MotionPointer(targetOffset));
    PutU32(data, keyframeTableOffset + 8U, 1U);
    PutU32(data, keyframeTableOffset + 12U, 1U);
    PutU32(data, positionOffset, 4U);
    PutVec3(data, positionOffset + 4U, {1.0F, 2.0F, 3.0F});
    PutU32(data, targetOffset, 4U);
    PutVec3(data, targetOffset + 4U, {4.0F, 5.0F, 6.0F});

    A::MotionTargetLayout camera{};
    camera.lanes.push_back(A::MotionTargetLane{.node_index = 0U});
    const auto probe = F::AnimationFile::probe_from_bytes(
        data, camera, A::EulerRecordWidth::Full32);
    ASSERT_TRUE(probe.valid) << probe.failure_reason;
    const auto motion = F::AnimationFile::read_from_bytes(
        data, camera, A::EulerRecordWidth::Full32).animation;
    ASSERT_EQ(motion.keyframes.size(), 1U);
    EXPECT_EQ(motion.keyframes.at(0).position.at(4U), S::Vector3(1.0F, 2.0F, 3.0F));
    EXPECT_EQ(motion.keyframes.at(0).target.at(4U), S::Vector3(4.0F, 5.0F, 6.0F));
}

TEST(SpiceModelingAnimation, ProbesMotionNodeCountAndDeclaredFrameCount) {
    auto data = MakeAnimationBlock(0x60u);
    constexpr std::uint32_t motionOffset = 8u;
    constexpr std::uint32_t keyframeTableOffset = 0x30u;
    constexpr std::uint32_t positionOffset = 0x40u;
    constexpr auto type = A::KeyframeAttributes::Position;

    PutU32(data, motionOffset, MotionPointer(keyframeTableOffset));
    PutU32(data, motionOffset + 4u, 7u);
    PutU16(data, motionOffset + 8u, static_cast<std::uint16_t>(type));
    PutU16(data, motionOffset + 10u, static_cast<std::uint16_t>(A::channel_count(type)));
    PutU32(data, keyframeTableOffset, MotionPointer(positionOffset));
    PutU32(data, keyframeTableOffset + 4u, 1u);
    PutU32(data, keyframeTableOffset + 8u, MotionPointer(0x58u));
    PutU32(data, keyframeTableOffset + 12u, 1u);
    PutU32(data, positionOffset, 6u);
    PutVec3(data, positionOffset + 4u, {1.0f, 0.0f, 0.0f});

    const auto valid = F::AnimationFile::probe_from_bytes(data, 1u);
    EXPECT_TRUE(valid.valid);
    EXPECT_EQ(valid.declared_frame_count, 7u);
    EXPECT_EQ(valid.node_count, 1u);
    EXPECT_FALSE(valid.short_rot);

    const auto invalid = F::AnimationFile::probe_from_bytes(data, 2u);
    EXPECT_FALSE(invalid.valid);
    EXPECT_FALSE(invalid.failure_reason.empty());

    auto nullDescriptor = data;
    PutU32(nullDescriptor, keyframeTableOffset, 0U);
    const auto nullDescriptorProbe = F::AnimationFile::probe_from_bytes(nullDescriptor, 1U);
    EXPECT_FALSE(nullDescriptorProbe.valid);
    EXPECT_NE(nullDescriptorProbe.failure_reason.find("null set pointer"), std::string::npos);

    const auto animationFile = F::AnimationFile::read_from_bytes(data, 1u);
    EXPECT_EQ(animationFile.animation.declared_frame_count, 7u);
}

TEST(SpiceModelingAnimation, ProbesDefaultMotionDeclaredFrameCount) {
    auto data = MakeAnimationBlock(0x30u);
    constexpr std::uint32_t motionOffset = 8u;

    PutU32(data, motionOffset, 0u);
    PutU32(data, motionOffset + 4u, 23u);
    PutU16(data, motionOffset + 8u, static_cast<std::uint16_t>(A::KeyframeAttributes::None));
    PutU16(data, motionOffset + 10u, 0u);

    const auto probe = F::AnimationFile::probe_from_bytes(data, 4u);
    EXPECT_TRUE(probe.valid);
    EXPECT_EQ(probe.declared_frame_count, 23u);
    EXPECT_EQ(probe.consumed_end, motionOffset + A::Motion::StructSize);

    const auto animationFile = F::AnimationFile::read_from_bytes(data, 4u);
    EXPECT_EQ(animationFile.animation.declared_frame_count, 23u);
    EXPECT_EQ(animationFile.animation.frame_count(), 0u);
}

TEST(SpiceModelingAnimation, ProbesShortRotationMotion) {
    auto data = MakeAnimationBlock(0x50u);
    constexpr std::uint32_t motionOffset = 8u;
    constexpr std::uint32_t keyframeTableOffset = 0x30u;
    constexpr std::uint32_t rotationOffset = 0x48u;
    constexpr auto type = A::KeyframeAttributes::EulerRotation;

    PutU32(data, motionOffset, MotionPointer(keyframeTableOffset));
    PutU32(data, motionOffset + 4u, 3u);
    PutU16(data, motionOffset + 8u, static_cast<std::uint16_t>(type));
    PutU16(data, motionOffset + 10u, static_cast<std::uint16_t>(A::channel_count(type)));
    PutU32(data, keyframeTableOffset, MotionPointer(rotationOffset));
    PutU32(data, keyframeTableOffset + 4u, 1u);
    PutU16(data, rotationOffset, 0u);
    PutU16(data, rotationOffset + 2u, static_cast<std::uint16_t>(S::BAMSFHelper::RadToBAMSF(S::MathHelper::HalfPi)));
    PutU16(data, rotationOffset + 4u, 0u);
    PutU16(data, rotationOffset + 6u, 0u);

    const auto normal = F::AnimationFile::probe_from_bytes(data, 1u, false);
    EXPECT_FALSE(normal.valid);

    const auto shortRot = F::AnimationFile::probe_from_bytes(data, 1u, true);
    EXPECT_TRUE(shortRot.valid);
    EXPECT_TRUE(shortRot.short_rot);

    const auto animationFile = F::AnimationFile::read_from_bytes(data, 1u, true);
    ASSERT_EQ(animationFile.animation.keyframes.at(0).euler_rotation.size(), 1u);
    EXPECT_NEAR(animationFile.animation.keyframes.at(0).euler_rotation.at(0u).x, S::MathHelper::HalfPi, 0.001f);
}

TEST(SpiceModelingAnimation, ReadsVertexArrayKeyframesThroughLut) {
    auto data = MakeAnimationBlock();
    constexpr std::uint32_t motionOffset = 8u;
    constexpr std::uint32_t vectorArrayOffset = 0x30u;
    constexpr std::uint32_t keyframeTableOffset = 0x80u;
    constexpr std::uint32_t vertexSetOffset = 0x90u;
    constexpr auto type = A::KeyframeAttributes::Vertex;

    PutVec3(data, vectorArrayOffset, {1.0f, 0.0f, 0.0f});
    PutVec3(data, vectorArrayOffset + 12u, {0.0f, 1.0f, 0.0f});
    PutVec3(data, vectorArrayOffset + 24u, {0.0f, 0.0f, 1.0f});
    PutVec3(data, vectorArrayOffset + 36u, {2.0f, 2.0f, 2.0f});

    PutU32(data, motionOffset, MotionPointer(keyframeTableOffset));
    PutU32(data, motionOffset + 4u, 1u);
    PutU16(data, motionOffset + 8u, static_cast<std::uint16_t>(type));
    PutU16(data, motionOffset + 10u, static_cast<std::uint16_t>(A::channel_count(type)));

    PutU32(data, keyframeTableOffset, MotionPointer(vertexSetOffset));
    PutU32(data, keyframeTableOffset + 4u, 1u);
    PutU32(data, vertexSetOffset, 0u);
    PutU32(data, vertexSetOffset + 4u, MotionPointer(vectorArrayOffset));

    const auto motion = A::Motion::read(S::EndianStackReader(data, S::Endian::Little), motionOffset, 1u, 0u - motionOffset);
    const auto& vertex = motion.keyframes.at(0).vertex;
    ASSERT_EQ(vertex.size(), 1u);
    EXPECT_EQ(vertex.at(0u).values.size(), 8u);
    EXPECT_EQ(vertex.at(0u).values[0], S::Vector3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(vertex.at(0u).label, "vertex__0x30");

    A::MotionTargetLayout targetLayout{};
    targetLayout.lanes.push_back(A::MotionTargetLane{
        .node_index = 7U,
        .vertex_count = 4U,
        .normal_count = 4U,
    });
    const auto targetProbe = F::AnimationFile::probe_from_bytes(
        data, targetLayout, A::EulerRecordWidth::Full32);
    ASSERT_TRUE(targetProbe.valid) << targetProbe.failure_reason;
    const auto targetMotion = F::AnimationFile::read_from_bytes(
        data, targetLayout, A::EulerRecordWidth::Full32).animation;
    ASSERT_EQ(targetMotion.keyframes.count(7), 1U);
    const auto& targetValues = targetMotion.keyframes.at(7).vertex.at(0U).values;
    ASSERT_EQ(targetValues.size(), 4U);
    EXPECT_EQ(targetValues[0], S::Vector3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(targetValues[1], S::Vector3(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(targetValues[2], S::Vector3(0.0f, 0.0f, 1.0f));
    EXPECT_EQ(targetValues[3], S::Vector3(2.0f, 2.0f, 2.0f));

    targetLayout.lanes[0].vertex_count = 64U;
    const auto oversized = F::AnimationFile::probe_from_bytes(
        data, targetLayout, A::EulerRecordWidth::Full32);
    EXPECT_FALSE(oversized.valid);
    EXPECT_FALSE(oversized.failure_reason.empty());
}

TEST(SpiceModelingStructs, BoundsRecalculatesMatrixAndPositionNormalHashesExactBits) {
    S::Bounds bounds({1.0f, 2.0f, 3.0f}, 4.0f);
    EXPECT_FLOAT_EQ(bounds.matrix().m11, 4.0f);
    EXPECT_FLOAT_EQ(bounds.matrix().m41, 1.0f);

    bounds.set_position({5.0f, 6.0f, 7.0f});
    bounds.set_radius(2.0f);
    EXPECT_FLOAT_EQ(bounds.matrix().m11, 2.0f);
    EXPECT_FLOAT_EQ(bounds.matrix().m41, 5.0f);

    const S::PositionNormal a {{1.0f, 2.0f, 3.0f}, {0.0f, 1.0f, 0.0f}};
    const S::PositionNormal b {{1.0f, 2.0f, 3.0f}, {0.0f, 1.0f, 0.0f}};
    EXPECT_EQ(a, b);
    EXPECT_EQ(S::PositionNormalHash{}(a), S::PositionNormalHash{}(b));
}

} // namespace
