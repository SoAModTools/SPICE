#include "../SpiceSCT/SpiceSCT.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

enum class FixtureEndian {
    Big,
    Little,
};

constexpr std::size_t kHeaderSize = 12;
constexpr std::size_t kIndexEntrySize = 0x14;
constexpr std::size_t kIndexNameOffset = 4;

void writeU32(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint32_t value,
    FixtureEndian endian)
{
    if (endian == FixtureEndian::Big) {
        bytes[offset + 0u] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
        bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
        bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
        bytes[offset + 3u] = static_cast<std::uint8_t>(value & 0xffu);
        return;
    }
    bytes[offset + 0u] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

void appendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value, FixtureEndian endian)
{
    const auto offset = bytes.size();
    bytes.resize(offset + 4u);
    writeU32(bytes, offset, value, endian);
}

void writeName(std::vector<std::uint8_t>& bytes, std::size_t offset, std::string_view name)
{
    for (std::size_t i = 0; i < name.size() && i < 16u; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>(name[i]);
    }
}

std::vector<std::uint8_t> makeOpcode253Fixture(FixtureEndian endian)
{
    std::vector<std::uint8_t> section{};
    appendU32(section, 253u, endian);
    for (std::uint32_t value = 1u; value <= 3u; ++value) {
        appendU32(section, 0x50000000u | value, endian);
        appendU32(section, 0x1du, endian);
    }
    appendU32(section, 12u, endian);

    std::vector<std::uint8_t> result(kHeaderSize + kIndexEntrySize, 0u);
    writeU32(result, 8u, 1u, endian);
    writeName(result, kHeaderSize + kIndexNameOffset, "M00001");
    result.insert(result.end(), section.begin(), section.end());
    return result;
}

void expectOpcode253RoundTrip(FixtureEndian endian, std::string_view expectedEndian)
{
    const auto original = spice::sct::SctParser{}.parse(makeOpcode253Fixture(endian), "opcode253.sct");
    ASSERT_TRUE(original.parseOk);
    EXPECT_EQ(expectedEndian, original.file.detectedEndian);
    ASSERT_EQ(1u, original.file.sections.size());
    const auto& instructions = original.file.sections.front().instructions;
    ASSERT_EQ(2u, instructions.size());
    EXPECT_EQ(253u, instructions[0].opcode);
    EXPECT_EQ(28u, instructions[0].sizeBytes);
    ASSERT_EQ(3u, instructions[0].parameters.size());
    ASSERT_EQ(3u, instructions[0].scptAnalyzeOperandIndexes.size());
    for (std::size_t i = 0; i < instructions[0].parameters.size(); ++i) {
        EXPECT_EQ(spice::sct::SctParameterValueKind::Expression, instructions[0].parameters[i].valueKind);
        ASSERT_TRUE(instructions[0].parameters[i].expression.has_value());
        EXPECT_TRUE(instructions[0].parameters[i].expression->hitStopCode);
        EXPECT_EQ(i, instructions[0].scptAnalyzeOperandIndexes[i]);
    }
    EXPECT_EQ(12u, instructions[1].opcode);
    EXPECT_EQ(28u, instructions[1].offset);

    const auto exported = spice::sct::SctBinaryExporter{}.exportFile(original);
    const auto reparsed = spice::sct::SctParser{}.parse(exported, "opcode253.exported.sct");
    ASSERT_TRUE(reparsed.parseOk);
    ASSERT_EQ(1u, reparsed.file.sections.size());
    ASSERT_EQ(2u, reparsed.file.sections.front().instructions.size());
    EXPECT_EQ(3u, reparsed.file.sections.front().instructions.front().parameters.size());
    const auto comparison = spice::sct::SctSemanticComparer{}.compare(original, reparsed);
    EXPECT_TRUE(comparison.equivalent)
        << (comparison.differences.empty() ? std::string{} : comparison.differences.front());
}

bool isSharedInvalidOpcode(std::uint16_t opcode)
{
    constexpr std::array<std::uint16_t, 9> invalid{1u, 2u, 4u, 8u, 13u, 14u, 182u, 189u, 200u};
    return std::find(invalid.begin(), invalid.end(), opcode) != invalid.end();
}

} // namespace

TEST(SctOpcodeSchema, CoversContiguousOpcodeRangeWithValidContracts)
{
    const auto schemas = spice::sct::sctOpcodeSchemas();
    ASSERT_EQ(266u, schemas.size());
    for (std::size_t index = 0; index < schemas.size(); ++index) {
        const auto& schema = schemas[index];
        EXPECT_EQ(index, schema.opcode);
        EXPECT_EQ(spice::sct::SctBinaryShapeConfidence::Confirmed, schema.binaryShapeConfidence);
        EXPECT_NE(spice::sct::SctOpcodeAvailability::Unknown, schema.gameCubeAvailability);
        EXPECT_NE(spice::sct::SctOpcodeAvailability::Unknown, schema.dreamcastAvailability);

        const auto& parameters = schema.parameters;
        std::uint32_t highestPatternIndex = parameters.paramCount == 0u ? 0u : parameters.paramCount - 1u;
        if (const auto repeated = spice::sct::sctOpcodeRepeatedGroup(schema); repeated.has_value()) {
            EXPECT_LE(repeated->firstParameter, repeated->lastParameter);
            EXPECT_LT(repeated->iterationCountParameter, parameters.paramCount);
            EXPECT_LE(repeated->lastParameter, parameters.paramCount);
            highestPatternIndex = std::max(highestPatternIndex, repeated->lastParameter);
        } else {
            EXPECT_EQ(-1, parameters.loopStartParam);
            EXPECT_EQ(-1, parameters.loopEndParam);
        }
        if (parameters.iterationCountParam >= 0) {
            EXPECT_LT(parameters.iterationCountParam, parameters.paramCount);
        }

        const auto allowedMask = highestPatternIndex >= 63u
            ? ~std::uint64_t{0}
            : ((std::uint64_t{1} << (highestPatternIndex + 1u)) - 1u);
        EXPECT_EQ(0u, parameters.scptAnalyzeMask & ~allowedMask) << "opcode " << schema.opcode;
        if (parameters.jumpParam >= 0) {
            EXPECT_LT(parameters.jumpParam, parameters.paramCount);
        }
        if (parameters.switchJumpParam >= 0) {
            EXPECT_GE(parameters.switchJumpParam, parameters.loopStartParam);
            EXPECT_LE(parameters.switchJumpParam, parameters.loopEndParam);
        }
        if (parameters.internalLoopBreakParam >= 0) {
            EXPECT_LE(parameters.internalLoopBreakParam, static_cast<std::int8_t>(highestPatternIndex));
        }
        ASSERT_LE(schema.footerReferenceCount, schema.footerReferences.size());
        for (std::size_t footerIndex = 0; footerIndex < schema.footerReferenceCount; ++footerIndex) {
            EXPECT_LT(schema.footerReferences[footerIndex].parameterIndex, parameters.paramCount);
            EXPECT_NE(spice::sct::SctFooterParamKind::None, schema.footerReferences[footerIndex].kind);
        }
    }
    EXPECT_EQ(nullptr, spice::sct::findSctOpcodeSchema(266u));
}

TEST(SctOpcodeSchema, RecordsPlatformAvailabilityWithoutChangingBinaryContracts)
{
    for (const auto& schema : spice::sct::sctOpcodeSchemas()) {
        const auto expectedGameCube = isSharedInvalidOpcode(schema.opcode)
            ? spice::sct::SctOpcodeAvailability::UnavailableInvalidStub
            : spice::sct::SctOpcodeAvailability::Available;
        const auto expectedDreamcast = isSharedInvalidOpcode(schema.opcode) || schema.opcode == 265u
            ? spice::sct::SctOpcodeAvailability::UnavailableInvalidStub
            : spice::sct::SctOpcodeAvailability::Available;
        EXPECT_EQ(expectedGameCube, spice::sct::sctOpcodeAvailability(schema, spice::sct::SctPlatform::GameCube));
        EXPECT_EQ(expectedDreamcast, spice::sct::sctOpcodeAvailability(schema, spice::sct::SctPlatform::Dreamcast));
    }

    const auto* opcode265 = spice::sct::findSctOpcodeSchema(265u);
    ASSERT_NE(nullptr, opcode265);
    EXPECT_EQ(2u, opcode265->parameters.paramCount);
    EXPECT_EQ(0x1u, opcode265->parameters.scptAnalyzeMask);
    EXPECT_EQ(spice::sct::SctOpcodeParameterEncoding::ScptExpression,
        spice::sct::sctOpcodeParameterEncoding(*opcode265, 0u));
    EXPECT_EQ(spice::sct::SctOpcodeParameterEncoding::RawWord,
        spice::sct::sctOpcodeParameterEncoding(*opcode265, 1u));
    const auto footer = spice::sct::sctOpcodeFooterReference(*opcode265, 1u);
    EXPECT_EQ(spice::sct::SctFooterParamKind::SctString, footer.kind);
    EXPECT_TRUE(footer.signedRelative);
    EXPECT_EQ("GeneratedReputationListDialog", opcode265->semantic.mnemonic);
    EXPECT_EQ(spice::sct::SctSemanticConfidence::Partial, opcode265->semantic.confidence);
}

TEST(SctOpcodeSchema, ProtectsResearchBackedParameterShapes)
{
    struct ExpectedShape {
        std::uint16_t opcode;
        std::uint16_t parameterCount;
        std::uint64_t scptMask;
    };
    constexpr std::array expected{
        ExpectedShape{9u, 1u, 0x1u},
        ExpectedShape{100u, 2u, 0x3u},
        ExpectedShape{114u, 2u, 0x3u},
        ExpectedShape{119u, 2u, 0x5u},
        ExpectedShape{153u, 4u, 0xeu},
        ExpectedShape{213u, 17u, 0x1ffffu},
        ExpectedShape{220u, 9u, 0x1ffu},
        ExpectedShape{253u, 3u, 0x7u},
        ExpectedShape{265u, 2u, 0x1u},
    };
    for (const auto& expectedShape : expected) {
        const auto* schema = spice::sct::findSctOpcodeSchema(expectedShape.opcode);
        ASSERT_NE(nullptr, schema);
        EXPECT_EQ(expectedShape.parameterCount, schema->parameters.paramCount);
        EXPECT_EQ(expectedShape.scptMask, schema->parameters.scptAnalyzeMask);
    }

    const auto* opcode253 = spice::sct::findSctOpcodeSchema(253u);
    ASSERT_NE(nullptr, opcode253);
    EXPECT_EQ(spice::sct::SctBinaryShapeConfidence::Confirmed, opcode253->binaryShapeConfidence);
    EXPECT_EQ(spice::sct::SctSemanticConfidence::Unknown, opcode253->semantic.confidence);

    const auto* opcode153 = spice::sct::findSctOpcodeSchema(153u);
    ASSERT_NE(nullptr, opcode153);
    const auto repeated153 = spice::sct::sctOpcodeRepeatedGroup(*opcode153);
    ASSERT_TRUE(repeated153.has_value());
    EXPECT_EQ(1u, repeated153->firstParameter);
    EXPECT_EQ(3u, repeated153->lastParameter);
    EXPECT_EQ(0u, repeated153->iterationCountParameter);
    EXPECT_EQ(3, opcode153->parameters.internalLoopBreakParam);
    EXPECT_EQ(0u, opcode153->parameters.internalLoopBreakValue);
}

TEST(SctOpcodeSchema, Opcode253ConsumesThreeScptParametersBigEndian)
{
    expectOpcode253RoundTrip(FixtureEndian::Big, "big");
}

TEST(SctOpcodeSchema, Opcode253ConsumesThreeScptParametersLittleEndian)
{
    expectOpcode253RoundTrip(FixtureEndian::Little, "little");
}
