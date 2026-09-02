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

std::vector<std::uint8_t> makeOpcode9RawSequenceFixture(FixtureEndian endian)
{
    std::vector<std::uint8_t> section{};
    appendU32(section, 9u, endian);
    appendU32(section, 0x04000000u, endian);
    appendU32(section, 0x3f800000u, endian);
    appendU32(section, 0x1du, endian);
    appendU32(section, 12u, endian);

    std::vector<std::uint8_t> result(kHeaderSize + kIndexEntrySize, 0u);
    writeU32(result, 8u, 1u, endian);
    writeName(result, kHeaderSize + kIndexNameOffset, "M00001");
    result.insert(result.end(), section.begin(), section.end());
    return result;
}

void expectOpcode9RawSequence(FixtureEndian endian)
{
    const auto parsed = spice::sct::SctParser{}.parse(makeOpcode9RawSequenceFixture(endian), "opcode9.sct");
    ASSERT_TRUE(parsed.parseOk);
    ASSERT_EQ(1u, parsed.file.sections.size());
    ASSERT_EQ(spice::sct::SctSectionKind::Script, parsed.file.sections.front().kind);
    const auto& instructions = parsed.file.sections.front().instructions;
    ASSERT_EQ(2u, instructions.size());
    ASSERT_EQ(9u, instructions[0].opcode);
    EXPECT_EQ(16u, instructions[0].sizeBytes);
    ASSERT_EQ(1u, instructions[0].parameters.size());
    EXPECT_EQ(spice::sct::SctParameterValueKind::Raw, instructions[0].parameters[0].valueKind);
    EXPECT_FALSE(instructions[0].parameters[0].expression.has_value());
    EXPECT_TRUE(instructions[0].scptParameterValueRecords.empty());
    EXPECT_TRUE(instructions[0].scptAnalyzeOperandIndexes.empty());
    EXPECT_EQ((std::vector<std::uint32_t>{0x04000000u, 0x3f800000u, 0x1du}),
        instructions[0].parameters[0].rawWords);
    EXPECT_EQ(12u, instructions[1].opcode);
    EXPECT_EQ(16u, instructions[1].offset);
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
        ASSERT_LE(schema.textReferenceCount, schema.textReferences.size());
        for (std::size_t textIndex = 0; textIndex < schema.textReferenceCount; ++textIndex) {
            const auto& reference = schema.textReferences[textIndex];
            EXPECT_LT(reference.parameterIndex, parameters.paramCount);
            EXPECT_NE(0u, reference.targetAlignment);
            EXPECT_EQ(0u, reference.targetAlignment & (reference.targetAlignment - 1u));
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
    const auto text = spice::sct::sctOpcodeTextReference(*opcode265, 1u);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(spice::sct::SctTextKind::SctString, text->kind);
    EXPECT_EQ(spice::sct::SctTextStorage::IndexedSection, text->storage);
    EXPECT_TRUE(text->signedRelative);
    EXPECT_EQ(4u, text->targetAlignment);
    EXPECT_EQ(0xfffffffcu, text->encodedValueMask);
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
        ExpectedShape{9u, 1u, 0x0u},
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

TEST(SctOpcodeSchema, SeparatesTextSemanticsFromPhysicalStorage)
{
    struct ExpectedReference {
        std::uint16_t opcode;
        std::uint32_t parameter;
        spice::sct::SctTextKind kind;
        spice::sct::SctTextStorage storage;
    };
    constexpr std::array expected{
        ExpectedReference{23u, 0u, spice::sct::SctTextKind::PlainString, spice::sct::SctTextStorage::Footer},
        ExpectedReference{24u, 0u, spice::sct::SctTextKind::SctString, spice::sct::SctTextStorage::Footer},
        ExpectedReference{25u, 1u, spice::sct::SctTextKind::SctString, spice::sct::SctTextStorage::Footer},
        ExpectedReference{43u, 0u, spice::sct::SctTextKind::PlainString, spice::sct::SctTextStorage::Footer},
        ExpectedReference{54u, 1u, spice::sct::SctTextKind::PlainString, spice::sct::SctTextStorage::Footer},
        ExpectedReference{69u, 0u, spice::sct::SctTextKind::PlainString, spice::sct::SctTextStorage::Footer},
        ExpectedReference{110u, 0u, spice::sct::SctTextKind::PlainString, spice::sct::SctTextStorage::Footer},
        ExpectedReference{113u, 0u, spice::sct::SctTextKind::PlainString, spice::sct::SctTextStorage::Footer},
        ExpectedReference{144u, 0u, spice::sct::SctTextKind::SctString, spice::sct::SctTextStorage::IndexedSection},
        ExpectedReference{155u, 1u, spice::sct::SctTextKind::SctString, spice::sct::SctTextStorage::IndexedSection},
        ExpectedReference{210u, 0u, spice::sct::SctTextKind::PlainString, spice::sct::SctTextStorage::Footer},
        ExpectedReference{214u, 0u, spice::sct::SctTextKind::PlainString, spice::sct::SctTextStorage::Footer},
        ExpectedReference{215u, 1u, spice::sct::SctTextKind::PlainString, spice::sct::SctTextStorage::Footer},
        ExpectedReference{248u, 0u, spice::sct::SctTextKind::PlainString, spice::sct::SctTextStorage::Footer},
        ExpectedReference{250u, 0u, spice::sct::SctTextKind::PlainString, spice::sct::SctTextStorage::Footer},
        ExpectedReference{257u, 0u, spice::sct::SctTextKind::PlainString, spice::sct::SctTextStorage::Footer},
        ExpectedReference{265u, 1u, spice::sct::SctTextKind::SctString, spice::sct::SctTextStorage::IndexedSection},
    };

    std::size_t actualCount = 0;
    for (const auto& schema : spice::sct::sctOpcodeSchemas()) {
        actualCount += schema.textReferenceCount;
    }
    ASSERT_EQ(expected.size(), actualCount);
    for (const auto& item : expected) {
        const auto* schema = spice::sct::findSctOpcodeSchema(item.opcode);
        ASSERT_NE(nullptr, schema);
        const auto rule = spice::sct::sctOpcodeTextReference(*schema, item.parameter);
        ASSERT_TRUE(rule.has_value()) << item.opcode;
        EXPECT_EQ(item.kind, rule->kind) << item.opcode;
        EXPECT_EQ(item.storage, rule->storage) << item.opcode;
    }
}

TEST(SctOpcodeSchema, RecordsCorrectedRawAndProvisionalContracts)
{
    const auto* opcode9 = spice::sct::findSctOpcodeSchema(9u);
    ASSERT_NE(nullptr, opcode9);
    ASSERT_EQ(1u, opcode9->parameterCatalogCount);
    EXPECT_EQ(spice::sct::SctOpcodeParameterEncoding::RawWordsUntilSentinel,
        opcode9->parameterCatalog[0].encoding);
    ASSERT_TRUE(opcode9->parameterCatalog[0].terminator.has_value());
    EXPECT_EQ(0x1du, opcode9->parameterCatalog[0].terminator->encodedWord);

    const auto* opcode3 = spice::sct::findSctOpcodeSchema(3u);
    ASSERT_NE(nullptr, opcode3);
    ASSERT_GT(opcode3->parameterCatalogCount, 2u);
    EXPECT_EQ(spice::sct::SctOpcodeScalarType::SignedInteger, opcode3->parameterCatalog[2].scalarType);
    EXPECT_FALSE(opcode3->parameterCatalog[2].terminator.has_value());
    EXPECT_EQ("caseValue", opcode3->parameterCatalog[2].role);
    EXPECT_EQ("caseOffset", opcode3->parameterCatalog[3].role);

    for (const auto opcode : {131u, 132u}) {
        const auto* schema = spice::sct::findSctOpcodeSchema(opcode);
        ASSERT_NE(nullptr, schema);
        const auto countIndex = static_cast<std::size_t>(schema->parameters.iterationCountParam);
        ASSERT_LT(countIndex, schema->parameterCatalogCount);
        EXPECT_EQ(0xffffu, schema->parameterCatalog[countIndex].allowedBitMask);
        EXPECT_EQ(spice::sct::SctOpcodeContractConfidence::Provisional,
            schema->parameterCatalog[countIndex].bitContractConfidence);
    }

    for (const auto& schema : spice::sct::sctOpcodeSchemas()) {
        EXPECT_NE(spice::sct::SctOpcodeNaturalRefreshBehavior::Unknown, schema.naturalRefreshBehavior);
        EXPECT_EQ(spice::sct::SctOpcodeContractConfidence::Provisional, schema.naturalRefreshConfidence);
    }

    for (const auto opcode : {0u, 3u, 10u, 11u}) {
        const auto* schema = spice::sct::findSctOpcodeSchema(opcode);
        ASSERT_NE(nullptr, schema);
        const auto parameterIndex = opcode == 3u
            ? static_cast<std::uint32_t>(schema->parameters.switchJumpParam)
            : opcode == 11u ? 0u : static_cast<std::uint32_t>(schema->parameters.jumpParam);
        const auto* parameter = spice::sct::sctOpcodeParameterSchema(*schema, parameterIndex);
        ASSERT_NE(nullptr, parameter);
        EXPECT_EQ(spice::sct::SctOpcodeReferenceKind::Instruction, parameter->referenceKind);
        EXPECT_TRUE(parameter->relativeReferenceSigned);
        EXPECT_EQ(4u, parameter->referenceTargetAlignment);
        EXPECT_EQ(0xfffffffcu, parameter->referenceEncodedValueMask);
    }
}

TEST(SctOpcodeSchema, Opcode253ConsumesThreeScptParametersBigEndian)
{
    expectOpcode253RoundTrip(FixtureEndian::Big, "big");
}

TEST(SctOpcodeSchema, Opcode253ConsumesThreeScptParametersLittleEndian)
{
    expectOpcode253RoundTrip(FixtureEndian::Little, "little");
}

TEST(SctOpcodeSchema, Opcode9ConsumesRawWordsThroughExactSentinelBigEndian)
{
    expectOpcode9RawSequence(FixtureEndian::Big);
}

TEST(SctOpcodeSchema, Opcode9ConsumesRawWordsThroughExactSentinelLittleEndian)
{
    expectOpcode9RawSequence(FixtureEndian::Little);
}
