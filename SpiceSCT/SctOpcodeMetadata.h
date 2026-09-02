#pragma once
#include "SctOpcodeCatalogNames.h"
#include "SctOpcodeParameterRoles.h"
#include "SctModel.h"
#include "SctTextContract.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace spice::sct {
enum class SctOpcodeControlRole {
    None,
    Branch,
    Switch,
    Jump,
    CallSubscript,
    Return,
};

enum class SctOpcodeEffectKind {
    None,
    LoadScript,
    LoadMld,
    SelectGroundVariant,
};

struct SctOpcodeEffectRule {
    SctOpcodeEffectKind kind = SctOpcodeEffectKind::None;
    std::uint8_t firstParameter = 0;
    std::optional<std::uint8_t> secondParameter;
    SctSemanticConfidence confidence = SctSemanticConfidence::Unknown;
};

enum class SctFooterParamKind {
    None,
    String,
    SctString,
};

enum class SctPlatform {
    GameCube,
    Dreamcast,
};

enum class SctOpcodeAvailability {
    Available,
    UnavailableInvalidStub,
    Unknown,
};

enum class SctBinaryShapeConfidence {
    Unknown,
    Confirmed,
};

enum class SctOpcodeParameterEncoding {
    RawWord,
    ScptExpression,
    RawWordsUntilSentinel,
};

enum class SctOpcodeParameterStorage {
    Word32,
    ScptWordSequence,
    RawWordSequence,
};

enum class SctOpcodeScalarType {
    Unknown,
    UnsignedInteger,
    SignedInteger,
    NumericExpression,
    VariableReference,
    RepetitionCount,
    RelativeOffset,
};

enum class SctOpcodeReferenceKind {
    None,
    Instruction,
    Text,
};

enum class SctOpcodeDefaultKind {
    Required,
    DerivedRepeatedGroupCount,
    DerivedInstructionByteLength,
    ProvisionalZero,
    ConfirmedEncodedWord,
};

enum class SctOpcodeDocumentRole {
    Instruction,
    FoldedModifier,
};

enum class SctOpcodeNaturalRefreshBehavior {
    Unknown,
    NoNewFrame,
    MayCreateNewFrame,
};

enum class SctOpcodeContractConfidence {
    Unknown,
    Provisional,
    Confirmed,
};

struct SctOpcodeParameterSchema {
    std::uint32_t schemaIndex = 0;
    std::string_view role = {};
    SctOpcodeParameterEncoding encoding = SctOpcodeParameterEncoding::RawWord;
    SctOpcodeParameterStorage storage = SctOpcodeParameterStorage::Word32;
    SctOpcodeScalarType scalarType = SctOpcodeScalarType::Unknown;
    SctOpcodeReferenceKind referenceKind = SctOpcodeReferenceKind::None;
    std::optional<SctOpcodeTextReferenceRule> textReference;
    bool relativeReferenceSigned = false;
    SctRelativeReferenceBase relativeReferenceBase = SctRelativeReferenceBase::OperandWord;
    std::uint32_t referenceTargetAlignment = 1;
    std::uint32_t referenceEncodedValueMask = 0xffffffffu;
    std::uint32_t allowedBitMask = 0xffffffffu;
    std::uint32_t requiredBitValue = 0u;
    SctOpcodeContractConfidence bitContractConfidence = SctOpcodeContractConfidence::Unknown;
    struct TerminatorRule {
        std::uint32_t encodedWord = 0;
        SctOpcodeContractConfidence confidence = SctOpcodeContractConfidence::Unknown;
        auto operator<=>(const TerminatorRule&) const = default;
    };
    std::optional<TerminatorRule> terminator;
    bool belongsToRepeatedGroup = false;
    SctOpcodeDefaultKind defaultKind = SctOpcodeDefaultKind::Required;
    std::uint32_t defaultEncodedWord = 0;
    SctOpcodeContractConfidence binaryConfidence = SctOpcodeContractConfidence::Unknown;
    SctOpcodeContractConfidence semanticConfidence = SctOpcodeContractConfidence::Unknown;
    SctOpcodeContractConfidence defaultConfidence = SctOpcodeContractConfidence::Unknown;
};

struct SctFooterParamMetadata {
    SctFooterParamKind kind = SctFooterParamKind::None;
    bool signedRelative = false;
};

struct SctOpcodeParamPattern {
    std::uint16_t paramCount;
    std::uint64_t scptAnalyzeMask;
    std::int8_t loopStartParam;
    std::int8_t loopEndParam;
    std::int8_t iterationCountParam;
    std::int8_t jumpParam;
    std::int8_t switchJumpParam;
    std::int8_t internalLoopBreakParam = -1;
    std::uint32_t internalLoopBreakValue = 0;
};

struct SctOpcodeSemanticMetadata {
    std::uint16_t opcode = 0;
    std::string_view mnemonic = {};
    SctSemanticConfidence confidence = SctSemanticConfidence::Unknown;
    SctOpcodeControlRole controlRole = SctOpcodeControlRole::None;
    SctOpcodeEffectRule effect;
    std::array<std::string_view, 32> parameterRoles{};
    std::string_view notes = {};
};

struct SctOpcodeRepeatedGroup {
    std::uint32_t firstParameter = 0;
    std::uint32_t lastParameter = 0;
    std::uint32_t iterationCountParameter = 0;
};

struct SctOpcodeSchema {
    std::uint16_t opcode = 0;
    SctOpcodeParamPattern parameters{};
    SctBinaryShapeConfidence binaryShapeConfidence = SctBinaryShapeConfidence::Unknown;
    SctOpcodeSemanticMetadata semantic{};
    std::array<SctOpcodeTextReferenceRule, 2> textReferences{};
    std::uint8_t textReferenceCount = 0;
    SctOpcodeAvailability gameCubeAvailability = SctOpcodeAvailability::Unknown;
    SctOpcodeAvailability dreamcastAvailability = SctOpcodeAvailability::Unknown;
    SctOpcodeDocumentRole documentRole = SctOpcodeDocumentRole::Instruction;
    SctOpcodeNaturalRefreshBehavior naturalRefreshBehavior = SctOpcodeNaturalRefreshBehavior::Unknown;
    SctOpcodeContractConfidence naturalRefreshConfidence = SctOpcodeContractConfidence::Unknown;
    std::array<SctOpcodeParameterSchema, 32> parameterCatalog{};
    std::uint8_t parameterCatalogCount = 0;
};

namespace detail {
// Provisional handler-behavior hints transcribed from legacy SALSA's
// "Skip Frame Refresh" catalog field. This is not the serialized opcode-13
// modifier represented by SctDocumentInstruction::skipRefresh.
inline constexpr std::array<bool, 266> kNaturalNoNewFrameHints{{
    true, true, true, true, true, true, true, true, true, true, true, true,
    false, true, true, false, true, true, true, true, true, true, true, false,
    false, false, true, true, true, false, false, false, false, false, false, false,
    false, false, false, false, false, true, true, false, true, false, false, false,
    false, false, true, true, true, true, true, true, true, true, true, false,
    false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, true, true, true, true, true,
    true, true, true, true, true, false, false, true, true, true, true, true,
    true, true, true, true, false, true, true, true, true, true, true, true,
    true, true, true, true, true, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, true, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false, false, true, false, false,
    false, true, true, false, false, false, true, true, true, true, true, true,
    true, true, true, true, true, true, true, false, false, false, true, true,
    false, false, false, true, true, true, true, true, true, false, true, true,
    true, true, true, false, true, true, true, true, false, true, true, false,
    true, true, true, true, true, true, true, true, true, true, false, true,
    true, true, true, true, true, true, true, true, true, true, true, true,
    true, true, true, false, true, false, false, true, true, false, false, true,
    true, true, true, false, false, true, true, false, false, true, false, true,
    true, true, true, true, false, true, true, false, false, false, false, false,
    false, false,
}};

inline constexpr std::array<SctOpcodeParamPattern, 266> kOpcodeParamPatternSeeds{{
    SctOpcodeParamPattern{2, 0x1ull, -1, -1, -1, 1, -1}, // opcode 0
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 1
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 2
    SctOpcodeParamPattern{4, 0x1ull, 2, 3, 1, 3, 3}, // opcode 3
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 4
    SctOpcodeParamPattern{2, 0x2ull, -1, -1, -1, -1, -1}, // opcode 5
    SctOpcodeParamPattern{2, 0x2ull, -1, -1, -1, -1, -1}, // opcode 6
    SctOpcodeParamPattern{2, 0x2ull, -1, -1, -1, -1, -1}, // opcode 7
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 8
    SctOpcodeParamPattern{1, 0x0ull, -1, -1, -1, -1, -1}, // opcode 9: raw words through exact 0x1d
    SctOpcodeParamPattern{1, 0x0ull, -1, -1, -1, 0, -1}, // opcode 10
    SctOpcodeParamPattern{1, 0x0ull, -1, -1, -1, -1, -1}, // opcode 11
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 12
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 13
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 14
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 15
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 16
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 17
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 18
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 19
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 20
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 21
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 22
    SctOpcodeParamPattern{1, 0x0ull, -1, -1, -1, -1, -1}, // opcode 23
    SctOpcodeParamPattern{1, 0x0ull, -1, -1, -1, -1, -1}, // opcode 24
    SctOpcodeParamPattern{2, 0x1ull, -1, -1, -1, -1, -1}, // opcode 25
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 26
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 27
    SctOpcodeParamPattern{4, 0xfull, -1, -1, -1, -1, -1}, // opcode 28
    SctOpcodeParamPattern{4, 0xfull, -1, -1, -1, -1, -1}, // opcode 29
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 30
    SctOpcodeParamPattern{8, 0xffull, -1, -1, -1, -1, -1}, // opcode 31
    SctOpcodeParamPattern{8, 0xffull, -1, -1, -1, -1, -1}, // opcode 32
    SctOpcodeParamPattern{8, 0xfeull, 1, 7, 0, -1, -1}, // opcode 33
    SctOpcodeParamPattern{11, 0x7fdull, 2, 10, 1, -1, -1}, // opcode 34
    SctOpcodeParamPattern{11, 0x7fdull, 2, 10, 1, -1, -1}, // opcode 35
    SctOpcodeParamPattern{8, 0xffull, -1, -1, -1, -1, -1}, // opcode 36
    SctOpcodeParamPattern{8, 0xffull, -1, -1, -1, -1, -1}, // opcode 37
    SctOpcodeParamPattern{5, 0x1full, -1, -1, -1, -1, -1}, // opcode 38
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 39
    SctOpcodeParamPattern{3, 0x7ull, -1, -1, -1, -1, -1}, // opcode 40
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 41
    SctOpcodeParamPattern{3, 0x5ull, 2, 2, 1, -1, -1}, // opcode 42
    SctOpcodeParamPattern{1, 0x0ull, -1, -1, -1, -1, -1}, // opcode 43
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 44
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 45
    SctOpcodeParamPattern{4, 0xfull, -1, -1, -1, -1, -1}, // opcode 46
    SctOpcodeParamPattern{4, 0xfull, -1, -1, -1, -1, -1}, // opcode 47
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 48
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 49
    SctOpcodeParamPattern{6, 0x3full, -1, -1, -1, -1, -1}, // opcode 50
    SctOpcodeParamPattern{4, 0xfull, -1, -1, -1, -1, -1}, // opcode 51
    SctOpcodeParamPattern{6, 0x3full, -1, -1, -1, -1, -1}, // opcode 52
    SctOpcodeParamPattern{20, 0xfffffull, -1, -1, -1, -1, -1}, // opcode 53
    SctOpcodeParamPattern{2, 0x1ull, -1, -1, -1, -1, -1}, // opcode 54
    SctOpcodeParamPattern{3, 0x7ull, -1, -1, -1, -1, -1}, // opcode 55
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 56
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 57
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 58
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 59
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 60
    SctOpcodeParamPattern{3, 0x7ull, -1, -1, -1, -1, -1}, // opcode 61
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 62
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 63
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 64
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 65
    SctOpcodeParamPattern{3, 0x7ull, -1, -1, -1, -1, -1}, // opcode 66
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 67
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 68
    SctOpcodeParamPattern{1, 0x0ull, -1, -1, -1, -1, -1}, // opcode 69
    SctOpcodeParamPattern{4, 0xfull, -1, -1, -1, -1, -1}, // opcode 70
    SctOpcodeParamPattern{4, 0xfull, -1, -1, -1, -1, -1}, // opcode 71
    SctOpcodeParamPattern{9, 0x1ffull, -1, -1, -1, -1, -1}, // opcode 72
    SctOpcodeParamPattern{10, 0x3ffull, -1, -1, -1, -1, -1}, // opcode 73
    SctOpcodeParamPattern{7, 0x7full, -1, -1, -1, -1, -1}, // opcode 74
    SctOpcodeParamPattern{12, 0xfffull, -1, -1, -1, -1, -1}, // opcode 75
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 76
    SctOpcodeParamPattern{5, 0x1full, -1, -1, -1, -1, -1}, // opcode 77
    SctOpcodeParamPattern{4, 0xfull, -1, -1, -1, -1, -1}, // opcode 78
    SctOpcodeParamPattern{7, 0x7full, -1, -1, -1, -1, -1}, // opcode 79
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 80
    SctOpcodeParamPattern{9, 0x1ffull, -1, -1, -1, -1, -1}, // opcode 81
    SctOpcodeParamPattern{4, 0xfull, -1, -1, -1, -1, -1}, // opcode 82
    SctOpcodeParamPattern{7, 0x7full, -1, -1, -1, -1, -1}, // opcode 83
    SctOpcodeParamPattern{10, 0x3ffull, -1, -1, -1, -1, -1}, // opcode 84
    SctOpcodeParamPattern{3, 0x5ull, 2, 2, 1, -1, -1}, // opcode 85
    SctOpcodeParamPattern{3, 0x5ull, 2, 2, 1, -1, -1}, // opcode 86
    SctOpcodeParamPattern{3, 0x5ull, 2, 2, 1, -1, -1}, // opcode 87
    SctOpcodeParamPattern{3, 0x5ull, 2, 2, 1, -1, -1}, // opcode 88
    SctOpcodeParamPattern{11, 0x7fdull, 2, 10, 1, -1, -1}, // opcode 89
    SctOpcodeParamPattern{11, 0x7fdull, 2, 10, 1, -1, -1}, // opcode 90
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 91
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 92
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 93
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 94
    SctOpcodeParamPattern{7, 0x7full, -1, -1, -1, -1, -1}, // opcode 95
    SctOpcodeParamPattern{7, 0x7full, -1, -1, -1, -1, -1}, // opcode 96
    SctOpcodeParamPattern{3, 0x7ull, -1, -1, -1, -1, -1}, // opcode 97
    SctOpcodeParamPattern{2, 0x2ull, 1, 1, 0, -1, -1}, // opcode 98
    SctOpcodeParamPattern{2, 0x2ull, 1, 1, 0, -1, -1}, // opcode 99
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 100
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 101
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 102
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 103
    SctOpcodeParamPattern{3, 0x7ull, -1, -1, -1, -1, -1}, // opcode 104
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 105
    SctOpcodeParamPattern{9, 0x1ffull, -1, -1, -1, -1, -1}, // opcode 106
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 107
    SctOpcodeParamPattern{11, 0x7fdull, 2, 10, 1, -1, -1}, // opcode 108
    SctOpcodeParamPattern{9, 0x1ffull, -1, -1, -1, -1, -1}, // opcode 109
    SctOpcodeParamPattern{1, 0x0ull, -1, -1, -1, -1, -1}, // opcode 110
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 111
    SctOpcodeParamPattern{4, 0xfull, -1, -1, -1, -1, -1}, // opcode 112
    SctOpcodeParamPattern{1, 0x0ull, -1, -1, -1, -1, -1}, // opcode 113
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 114
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 115
    SctOpcodeParamPattern{7, 0x7full, -1, -1, -1, -1, -1}, // opcode 116
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 117
    SctOpcodeParamPattern{2, 0x5ull, 2, 2, 1, -1, -1}, // opcode 118
    SctOpcodeParamPattern{2, 0x5ull, 2, 2, 1, -1, -1}, // opcode 119
    SctOpcodeParamPattern{7, 0x7full, -1, -1, -1, -1, -1}, // opcode 120
    SctOpcodeParamPattern{8, 0xfeull, 1, 7, 0, -1, -1}, // opcode 121
    SctOpcodeParamPattern{5, 0x1full, -1, -1, -1, -1, -1}, // opcode 122
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 123
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 124
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 125
    SctOpcodeParamPattern{2, 0x2ull, 1, 1, 0, -1, -1}, // opcode 126
    SctOpcodeParamPattern{2, 0x2ull, 1, 1, 0, -1, -1}, // opcode 127
    SctOpcodeParamPattern{6, 0x3full, -1, -1, -1, -1, -1}, // opcode 128
    SctOpcodeParamPattern{2, 0x1ull, -1, -1, 1, -1, -1}, // opcode 129
    SctOpcodeParamPattern{9, 0x1fdull, 2, 8, 1, -1, -1}, // opcode 130
    SctOpcodeParamPattern{5, 0x1eull, 1, 4, 0, -1, -1}, // opcode 131
    SctOpcodeParamPattern{8, 0xfeull, 1, 7, 0, -1, -1}, // opcode 132
    SctOpcodeParamPattern{11, 0x7feull, 2, 10, 0, -1, -1}, // opcode 133
    SctOpcodeParamPattern{7, 0x7full, -1, -1, -1, -1, -1}, // opcode 134
    SctOpcodeParamPattern{3, 0x5ull, 2, 2, 1, -1, -1}, // opcode 135
    SctOpcodeParamPattern{5, 0x1full, -1, -1, -1, -1, -1}, // opcode 136
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 137
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 138
    SctOpcodeParamPattern{8, 0xfeull, 1, 7, 0, -1, -1}, // opcode 139
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 140
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 141
    SctOpcodeParamPattern{5, 0x1full, -1, -1, -1, -1, -1}, // opcode 142
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 143
    SctOpcodeParamPattern{2, 0x2ull, -1, -1, -1, -1, -1}, // opcode 144
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 145
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 146
    SctOpcodeParamPattern{10, 0x3ffull, -1, -1, -1, -1, -1}, // opcode 147
    SctOpcodeParamPattern{10, 0x3feull, 1, 9, 0, -1, -1}, // opcode 148
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 149
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 150
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 151
    SctOpcodeParamPattern{2, 0x2ull, 1, 1, 0, -1, -1}, // opcode 152
    SctOpcodeParamPattern{4, 0xeull, 1, 3, 0, -1, -1, 3, 0}, // opcode 153
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 154
    SctOpcodeParamPattern{3, 0x5ull, -1, -1, -1, -1, -1}, // opcode 155
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 156
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 157
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 158
    SctOpcodeParamPattern{9, 0x1feull, 1, 8, 0, -1, -1}, // opcode 159
    SctOpcodeParamPattern{7, 0x7full, -1, -1, -1, -1, -1}, // opcode 160
    SctOpcodeParamPattern{11, 0x7fdull, 2, 10, 1, -1, -1}, // opcode 161
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 162
    SctOpcodeParamPattern{9, 0x1ffull, -1, -1, -1, -1, -1}, // opcode 163
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 164
    SctOpcodeParamPattern{19, 0x7ffffull, -1, -1, -1, -1, -1}, // opcode 165
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 166
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 167
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 168
    SctOpcodeParamPattern{4, 0xfull, -1, -1, -1, -1, -1}, // opcode 169
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 170
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 171
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 172
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 173
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 174
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 175
    SctOpcodeParamPattern{6, 0x3full, -1, -1, -1, -1, -1}, // opcode 176
    SctOpcodeParamPattern{13, 0x1fffull, -1, -1, -1, -1, -1}, // opcode 177
    SctOpcodeParamPattern{8, 0xffull, -1, -1, -1, -1, -1}, // opcode 178
    SctOpcodeParamPattern{4, 0xfull, -1, -1, -1, -1, -1}, // opcode 179
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 180
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 181
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 182
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 183
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 184
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 185
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 186
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 187
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 188
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 189
    SctOpcodeParamPattern{3, 0x7ull, -1, -1, -1, -1, -1}, // opcode 190
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 191
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 192
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 193
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 194
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 195
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 196
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 197
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 198
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 199
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 200
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 201
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 202
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 203
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 204
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 205
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 206
    SctOpcodeParamPattern{3, 0x7ull, -1, -1, -1, -1, -1}, // opcode 207
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 208
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 209
    SctOpcodeParamPattern{1, 0x0ull, -1, -1, -1, -1, -1}, // opcode 210
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 211
    SctOpcodeParamPattern{5, 0x1full, -1, -1, -1, -1, -1}, // opcode 212
    SctOpcodeParamPattern{17, 0x1ffffull, -1, -1, -1, -1, -1}, // opcode 213
    SctOpcodeParamPattern{1, 0x0ull, -1, -1, -1, -1, -1}, // opcode 214
    SctOpcodeParamPattern{2, 0x1ull, -1, -1, -1, -1, -1}, // opcode 215
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 216
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 217
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 218
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 219
    SctOpcodeParamPattern{9, 0x1ffull, -1, -1, -1, -1, -1}, // opcode 220
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 221
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 222
    SctOpcodeParamPattern{25, 0x1ffffffull, -1, -1, -1, -1, -1}, // opcode 223
    SctOpcodeParamPattern{25, 0x1ffffffull, -1, -1, -1, -1, -1}, // opcode 224
    SctOpcodeParamPattern{7, 0x7full, -1, -1, -1, -1, -1}, // opcode 225
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 226
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 227
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 228
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 229
    SctOpcodeParamPattern{7, 0x7full, -1, -1, -1, -1, -1}, // opcode 230
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 231
    SctOpcodeParamPattern{14, 0x3fffull, -1, -1, -1, -1, -1}, // opcode 232
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 233
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 234
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 235
    SctOpcodeParamPattern{6, 0x3full, -1, -1, -1, -1, -1}, // opcode 236
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 237
    SctOpcodeParamPattern{3, 0x7ull, -1, -1, -1, -1, -1}, // opcode 238
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 239
    SctOpcodeParamPattern{6, 0x3full, -1, -1, -1, -1, -1}, // opcode 240
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 241
    SctOpcodeParamPattern{3, 0x7ull, -1, -1, -1, -1, -1}, // opcode 242
    SctOpcodeParamPattern{10, 0x3ffull, -1, -1, -1, -1, -1}, // opcode 243
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 244
    SctOpcodeParamPattern{2, 0x2ull, 1, 1, 0, -1, -1}, // opcode 245
    SctOpcodeParamPattern{2, 0x2ull, 1, 1, 0, -1, -1}, // opcode 246
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 247
    SctOpcodeParamPattern{1, 0x0ull, -1, -1, -1, -1, -1}, // opcode 248
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 249
    SctOpcodeParamPattern{1, 0x0ull, -1, -1, -1, -1, -1}, // opcode 250
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 251
    SctOpcodeParamPattern{2, 0x3ull, -1, -1, -1, -1, -1}, // opcode 252
    SctOpcodeParamPattern{3, 0x7ull, -1, -1, -1, -1, -1}, // opcode 253
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 254
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 255
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 256
    SctOpcodeParamPattern{1, 0x0ull, -1, -1, -1, -1, -1}, // opcode 257
    SctOpcodeParamPattern{2, 0x2ull, 1, 1, 0, -1, -1}, // opcode 258
    SctOpcodeParamPattern{1, 0x1ull, -1, -1, -1, -1, -1}, // opcode 259
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 260
    SctOpcodeParamPattern{0, 0x0ull, -1, -1, -1, -1, -1}, // opcode 261
    SctOpcodeParamPattern{5, 0x1full, -1, -1, -1, -1, -1}, // opcode 262
    SctOpcodeParamPattern{2, 0x2ull, 1, 1, 0, -1, -1}, // opcode 263
    SctOpcodeParamPattern{8, 0xffull, -1, -1, -1, -1, -1}, // opcode 264
    SctOpcodeParamPattern{2, 0x1ull, -1, -1, -1, -1, -1}, // opcode 265
}};

[[nodiscard]] constexpr SctOpcodeSemanticMetadata makeOpcodeSemanticMetadata(std::uint16_t opcode) {
    SctOpcodeSemanticMetadata meta{};
    meta.opcode = opcode;
    meta.mnemonic = kOpcodeCatalogNames[opcode];
    meta.confidence = SctSemanticConfidence::Unknown;
    for (const auto& seed : kOpcodeParameterRoleSeeds) {
        if (seed.opcode == opcode && seed.parameterIndex < meta.parameterRoles.size()) {
            meta.parameterRoles[seed.parameterIndex] = seed.role;
        }
    }

    switch (opcode) {
    case 0:
        meta.mnemonic = "If";
        meta.confidence = SctSemanticConfidence::Known;
        meta.controlRole = SctOpcodeControlRole::Branch;
        meta.parameterRoles = {"condition", "falseOffset"};
        break;
    case 3:
        meta.mnemonic = "Switch";
        meta.confidence = SctSemanticConfidence::Known;
        meta.controlRole = SctOpcodeControlRole::Switch;
        meta.parameterRoles = {"choice", "caseCount", "caseValue", "caseOffset"};
        break;
    case 9:
        meta.mnemonic = "LabelOrStringPrefix";
        meta.confidence = SctSemanticConfidence::Partial;
        meta.parameterRoles = {"payload"};
        break;
    case 10:
        meta.mnemonic = "Jump";
        meta.confidence = SctSemanticConfidence::Known;
        meta.controlRole = SctOpcodeControlRole::Jump;
        meta.parameterRoles = {"offset"};
        break;
    case 11:
        meta.mnemonic = "CallSubscript";
        meta.confidence = SctSemanticConfidence::Known;
        meta.controlRole = SctOpcodeControlRole::CallSubscript;
        meta.parameterRoles = {"offset"};
        break;
    case 12:
        meta.mnemonic = "Return";
        meta.confidence = SctSemanticConfidence::Known;
        meta.controlRole = SctOpcodeControlRole::Return;
        break;
    case 23:
        meta.mnemonic = "LoadMldFile";
        meta.confidence = SctSemanticConfidence::Partial;
        meta.effect = {SctOpcodeEffectKind::LoadMld, 0u, std::nullopt,
            SctSemanticConfidence::Partial};
        meta.parameterRoles = {"mldPathOffset"};
        break;
    case 43:
        meta.mnemonic = "LoadScriptByName";
        meta.confidence = SctSemanticConfidence::Partial;
        meta.effect = {SctOpcodeEffectKind::LoadScript, 0u, std::nullopt,
            SctSemanticConfidence::Partial};
        meta.parameterRoles = {"scriptNameOffset"};
        break;
    case 114:
        meta.mnemonic = "ChangeGroundVariant";
        meta.confidence = SctSemanticConfidence::Partial;
        meta.effect = {SctOpcodeEffectKind::SelectGroundVariant, 0u, 1u,
            SctSemanticConfidence::Known};
        meta.parameterRoles = { "tblId", "variant" };
        meta.notes = "Finds a ground entry using tblId and sets the active ground from the ground address list. -1 disables the ground.";
        break;
    case 210:
        meta.mnemonic = "WarpCurrentAreaByString";
        meta.confidence = SctSemanticConfidence::Partial;
        meta.effect = {SctOpcodeEffectKind::LoadScript, 0u, std::nullopt,
            SctSemanticConfidence::Partial};
        meta.parameterRoles = {"footerStringOffset"};
        break;
    case 238:
        meta.mnemonic = "ReturnToOverworldAtPosition";
        meta.confidence = SctSemanticConfidence::Partial;
        meta.parameterRoles = {"overworldXExpr", "overworldYExpr", "overworldZExpr"};
        break;
    case 257:
        meta.mnemonic = "ExitShipBattleToScript";
        meta.confidence = SctSemanticConfidence::Partial;
        meta.effect = {SctOpcodeEffectKind::LoadScript, 0u, std::nullopt,
            SctSemanticConfidence::Partial};
        meta.parameterRoles = {"scriptRef"};
        break;
    case 265:
        meta.mnemonic = "GeneratedReputationListDialog";
        meta.confidence = SctSemanticConfidence::Partial;
        meta.parameterRoles = {"displayedValueExpr", "labelStringOffset"};
        break;
    default:
        break;
    }

    return meta;
}

[[nodiscard]] constexpr std::optional<SctOpcodeTextReferenceRule> makeTextReferenceMetadata(
    std::uint16_t opcode, std::uint32_t parameterIndex) {
    constexpr auto footerPlain = [](std::uint32_t index, bool signedRelative) {
        return SctOpcodeTextReferenceRule{index, SctTextKind::PlainString, SctTextStorage::Footer,
            signedRelative, SctRelativeReferenceBase::OperandWord, 1u, 0xffffffffu};
    };
    constexpr auto footerSct = [](std::uint32_t index, bool signedRelative) {
        return SctOpcodeTextReferenceRule{index, SctTextKind::SctString, SctTextStorage::Footer,
            signedRelative, SctRelativeReferenceBase::OperandWord, 1u, 0xffffffffu};
    };
    constexpr auto indexedSct = [](std::uint32_t index) {
        return SctOpcodeTextReferenceRule{index, SctTextKind::SctString, SctTextStorage::IndexedSection,
            true, SctRelativeReferenceBase::OperandWord, 4u, 0xfffffffcu};
    };
    switch (opcode) {
    case 23:
        if (parameterIndex == 0u) return footerPlain(parameterIndex, true);
        break;
    case 24:
        if (parameterIndex == 0u) return footerSct(parameterIndex, true);
        break;
    case 25:
        if (parameterIndex == 1u) return footerSct(parameterIndex, false);
        break;
    case 43:
        if (parameterIndex == 0u) return footerPlain(parameterIndex, false);
        break;
    case 54:
        if (parameterIndex == 1u) return footerPlain(parameterIndex, true);
        break;
    case 69:
        if (parameterIndex == 0u) return footerPlain(parameterIndex, true);
        break;
    case 110:
    case 113:
    case 210:
    case 214:
    case 248:
    case 250:
    case 257:
        if (parameterIndex == 0u) return footerPlain(parameterIndex, false);
        break;
    case 144:
        if (parameterIndex == 0u) return indexedSct(parameterIndex);
        break;
    case 155:
        if (parameterIndex == 1u) return indexedSct(parameterIndex);
        break;
    case 215:
        if (parameterIndex == 1u) return footerPlain(parameterIndex, false);
        break;
    case 265:
        if (parameterIndex == 1u) return indexedSct(parameterIndex);
        break;
    default:
        break;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr bool isSharedInvalidOpcode(std::uint16_t opcode) noexcept {
    switch (opcode) {
    case 1:
    case 2:
    case 4:
    case 8:
    case 13:
    case 14:
    case 182:
    case 189:
    case 200:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool isControlReferenceParameter(
    const SctOpcodeSchema& schema, std::uint32_t parameterIndex) noexcept {
    if (schema.semantic.controlRole == SctOpcodeControlRole::CallSubscript && parameterIndex == 0u) {
        return true;
    }
    return static_cast<int>(parameterIndex) == schema.parameters.jumpParam
        || static_cast<int>(parameterIndex) == schema.parameters.switchJumpParam;
}

[[nodiscard]] constexpr std::optional<SctOpcodeTextReferenceRule> textReferenceForBaseParameter(
    const SctOpcodeSchema& schema, std::uint32_t parameterIndex) noexcept {
    for (std::size_t i = 0; i < schema.textReferenceCount; ++i) {
        if (schema.textReferences[i].parameterIndex == parameterIndex) {
            return schema.textReferences[i];
        }
    }
    return std::nullopt;
}

[[nodiscard]] constexpr SctOpcodeParameterSchema makeParameterCatalogEntry(
    const SctOpcodeSchema& schema, std::uint32_t parameterIndex) noexcept {
    SctOpcodeParameterSchema parameter{};
    parameter.schemaIndex = parameterIndex;
    if (parameterIndex < schema.semantic.parameterRoles.size()) {
        parameter.role = schema.semantic.parameterRoles[parameterIndex];
    }
    const bool expression = parameterIndex < 64u
        && ((schema.parameters.scptAnalyzeMask >> parameterIndex) & 1ull) != 0ull;
    parameter.encoding = expression ? SctOpcodeParameterEncoding::ScptExpression
                                    : SctOpcodeParameterEncoding::RawWord;
    parameter.storage = expression ? SctOpcodeParameterStorage::ScptWordSequence
                                   : SctOpcodeParameterStorage::Word32;
    parameter.scalarType = expression ? SctOpcodeScalarType::NumericExpression
                                     : SctOpcodeScalarType::Unknown;
    parameter.binaryConfidence = SctOpcodeContractConfidence::Confirmed;
    parameter.semanticConfidence = parameter.role.empty()
        ? SctOpcodeContractConfidence::Unknown : SctOpcodeContractConfidence::Confirmed;

    const bool repeated = schema.parameters.loopStartParam >= 0
        && schema.parameters.loopEndParam >= schema.parameters.loopStartParam;
    parameter.belongsToRepeatedGroup = repeated
        && parameterIndex >= static_cast<std::uint32_t>(schema.parameters.loopStartParam)
        && parameterIndex <= static_cast<std::uint32_t>(schema.parameters.loopEndParam);

    if (schema.opcode == 9u && parameterIndex == 0u) {
        parameter.encoding = SctOpcodeParameterEncoding::RawWordsUntilSentinel;
        parameter.storage = SctOpcodeParameterStorage::RawWordSequence;
        parameter.terminator = SctOpcodeParameterSchema::TerminatorRule{
            0x0000001du, SctOpcodeContractConfidence::Confirmed};
        parameter.defaultKind = SctOpcodeDefaultKind::ConfirmedEncodedWord;
        parameter.defaultEncodedWord = 0x0000001du;
        parameter.defaultConfidence = SctOpcodeContractConfidence::Confirmed;
        return parameter;
    }

    if (parameterIndex == 0u && (schema.opcode == 5u || schema.opcode == 6u || schema.opcode == 7u)) {
        parameter.scalarType = SctOpcodeScalarType::VariableReference;
        parameter.allowedBitMask = schema.opcode == 5u ? 0x1000ffffu
            : schema.opcode == 6u ? 0x5000ffffu : 0x4000ffffu;
        parameter.requiredBitValue = schema.opcode == 5u ? 0x10000000u
            : schema.opcode == 6u ? 0x50000000u : 0x40000000u;
        parameter.bitContractConfidence = SctOpcodeContractConfidence::Confirmed;
        parameter.defaultKind = SctOpcodeDefaultKind::Required;
        parameter.defaultConfidence = SctOpcodeContractConfidence::Confirmed;
        return parameter;
    }

    if (schema.opcode == 129u && parameterIndex == 1u) {
        parameter.scalarType = SctOpcodeScalarType::UnsignedInteger;
        parameter.defaultKind = SctOpcodeDefaultKind::DerivedInstructionByteLength;
        parameter.defaultConfidence = SctOpcodeContractConfidence::Confirmed;
        return parameter;
    }
    if (repeated && schema.parameters.iterationCountParam >= 0
        && parameterIndex == static_cast<std::uint32_t>(schema.parameters.iterationCountParam)) {
        parameter.scalarType = SctOpcodeScalarType::RepetitionCount;
        if (schema.opcode == 131u || schema.opcode == 132u) {
            parameter.allowedBitMask = 0x0000ffffu;
            parameter.bitContractConfidence = SctOpcodeContractConfidence::Provisional;
        }
        parameter.defaultKind = SctOpcodeDefaultKind::DerivedRepeatedGroupCount;
        parameter.defaultConfidence = SctOpcodeContractConfidence::Confirmed;
        return parameter;
    }
    if (isControlReferenceParameter(schema, parameterIndex)) {
        parameter.scalarType = SctOpcodeScalarType::RelativeOffset;
        parameter.referenceKind = SctOpcodeReferenceKind::Instruction;
        parameter.relativeReferenceSigned = true;
        parameter.relativeReferenceBase = schema.semantic.controlRole == SctOpcodeControlRole::Switch
            ? SctRelativeReferenceBase::OperandWord
            : SctRelativeReferenceBase::InstructionEndMinusWord;
        parameter.referenceTargetAlignment = 4u;
        parameter.referenceEncodedValueMask = 0xfffffffcu;
        parameter.defaultKind = SctOpcodeDefaultKind::Required;
        parameter.defaultConfidence = SctOpcodeContractConfidence::Confirmed;
        return parameter;
    }
    const auto text = textReferenceForBaseParameter(schema, parameterIndex);
    if (text.has_value()) {
        parameter.scalarType = SctOpcodeScalarType::RelativeOffset;
        parameter.referenceKind = SctOpcodeReferenceKind::Text;
        parameter.textReference = text;
        parameter.relativeReferenceSigned = text->signedRelative;
        parameter.relativeReferenceBase = text->relativeBase;
        parameter.referenceTargetAlignment = text->targetAlignment;
        parameter.referenceEncodedValueMask = text->encodedValueMask;
        parameter.defaultKind = SctOpcodeDefaultKind::Required;
        parameter.defaultConfidence = SctOpcodeContractConfidence::Confirmed;
        return parameter;
    }

    if (schema.opcode == 3u && parameterIndex == 2u) {
        parameter.scalarType = SctOpcodeScalarType::SignedInteger;
        parameter.semanticConfidence = SctOpcodeContractConfidence::Provisional;
    }

    // Zero is a safe structural seed for the known binary shape, but absent a
    // confirmed handler-specific default it remains authoring evidence rather
    // than a claimed legal-domain fact.
    parameter.defaultKind = SctOpcodeDefaultKind::ProvisionalZero;
    parameter.defaultEncodedWord = 0;
    parameter.defaultConfidence = SctOpcodeContractConfidence::Provisional;
    return parameter;
}

[[nodiscard]] constexpr std::array<SctOpcodeSchema, 266> makeOpcodeSchemas() {
    std::array<SctOpcodeSchema, 266> schemas{};
    for (std::size_t opcode = 0; opcode < schemas.size(); ++opcode) {
        auto& schema = schemas[opcode];
        schema.opcode = static_cast<std::uint16_t>(opcode);
        schema.parameters = kOpcodeParamPatternSeeds[opcode];
        schema.binaryShapeConfidence = SctBinaryShapeConfidence::Confirmed;
        schema.semantic = makeOpcodeSemanticMetadata(schema.opcode);
        schema.naturalRefreshBehavior = kNaturalNoNewFrameHints[opcode]
            ? SctOpcodeNaturalRefreshBehavior::NoNewFrame
            : SctOpcodeNaturalRefreshBehavior::MayCreateNewFrame;
        schema.naturalRefreshConfidence = SctOpcodeContractConfidence::Provisional;
        schema.gameCubeAvailability = isSharedInvalidOpcode(schema.opcode)
            ? SctOpcodeAvailability::UnavailableInvalidStub
            : SctOpcodeAvailability::Available;
        schema.dreamcastAvailability = schema.gameCubeAvailability;
        if (schema.opcode == 13u || schema.opcode == 129u) {
            schema.documentRole = SctOpcodeDocumentRole::FoldedModifier;
        }
        if (schema.opcode == 265u) {
            schema.dreamcastAvailability = SctOpcodeAvailability::UnavailableInvalidStub;
        }

        for (std::uint32_t parameterIndex = 0; parameterIndex < schema.parameters.paramCount; ++parameterIndex) {
            const auto text = makeTextReferenceMetadata(schema.opcode, parameterIndex);
            if (!text.has_value()) {
                continue;
            }
            schema.textReferences[schema.textReferenceCount++] = *text;
        }
        const auto catalogCount = schema.parameters.loopEndParam >= 0
            ? std::max<std::uint32_t>(schema.parameters.paramCount,
                static_cast<std::uint32_t>(schema.parameters.loopEndParam) + 1u)
            : static_cast<std::uint32_t>(schema.parameters.paramCount);
        schema.parameterCatalogCount = static_cast<std::uint8_t>(catalogCount);
        for (std::uint32_t parameterIndex = 0; parameterIndex < catalogCount; ++parameterIndex) {
            schema.parameterCatalog[parameterIndex] = makeParameterCatalogEntry(schema, parameterIndex);
        }
    }
    return schemas;
}

inline constexpr auto kOpcodeSchemas = makeOpcodeSchemas();

[[nodiscard]] constexpr std::array<SctOpcodeParamPattern, 266> makeLegacyParamPatterns() {
    std::array<SctOpcodeParamPattern, 266> patterns{};
    for (std::size_t opcode = 0; opcode < patterns.size(); ++opcode) {
        patterns[opcode] = kOpcodeSchemas[opcode].parameters;
    }
    return patterns;
}
} // namespace detail

[[deprecated("Use sctOpcodeSchemas or findSctOpcodeSchema")]]
inline constexpr auto kSalsaOpcodeParamPatterns = detail::makeLegacyParamPatterns();

[[nodiscard]] constexpr std::span<const SctOpcodeSchema> sctOpcodeSchemas() noexcept {
    return detail::kOpcodeSchemas;
}

[[nodiscard]] constexpr const SctOpcodeSchema* findSctOpcodeSchema(std::uint16_t opcode) noexcept {
    return opcode < detail::kOpcodeSchemas.size() ? &detail::kOpcodeSchemas[opcode] : nullptr;
}

[[nodiscard]] constexpr SctOpcodeAvailability sctOpcodeAvailability(
    const SctOpcodeSchema& schema,
    SctPlatform platform) noexcept {
    switch (platform) {
    case SctPlatform::GameCube:
        return schema.gameCubeAvailability;
    case SctPlatform::Dreamcast:
        return schema.dreamcastAvailability;
    }
    return SctOpcodeAvailability::Unknown;
}

[[nodiscard]] constexpr std::optional<SctOpcodeRepeatedGroup> sctOpcodeRepeatedGroup(
    const SctOpcodeSchema& schema) noexcept {
    const auto& parameters = schema.parameters;
    if (parameters.loopStartParam < 0 || parameters.loopEndParam < parameters.loopStartParam
        || parameters.iterationCountParam < 0) {
        return std::nullopt;
    }
    return SctOpcodeRepeatedGroup{
        static_cast<std::uint32_t>(parameters.loopStartParam),
        static_cast<std::uint32_t>(parameters.loopEndParam),
        static_cast<std::uint32_t>(parameters.iterationCountParam),
    };
}

[[nodiscard]] constexpr std::uint32_t sctOpcodeBaseParameterIndex(
    const SctOpcodeSchema& schema,
    std::uint32_t parameterIndex) noexcept {
    const auto repeated = sctOpcodeRepeatedGroup(schema);
    if (!repeated.has_value() || parameterIndex < schema.parameters.paramCount) {
        return parameterIndex;
    }
    const auto width = repeated->lastParameter - repeated->firstParameter + 1u;
    return repeated->firstParameter + ((parameterIndex - schema.parameters.paramCount) % width);
}

[[nodiscard]] constexpr SctOpcodeParameterEncoding sctOpcodeParameterEncoding(
    const SctOpcodeSchema& schema,
    std::uint32_t parameterIndex) noexcept {
    const auto baseParameterIndex = sctOpcodeBaseParameterIndex(schema, parameterIndex);
    return baseParameterIndex < schema.parameterCatalogCount
        ? schema.parameterCatalog[baseParameterIndex].encoding
        : SctOpcodeParameterEncoding::RawWord;
}

[[nodiscard]] constexpr const SctOpcodeParameterSchema* sctOpcodeParameterSchema(
    const SctOpcodeSchema& schema,
    std::uint32_t parameterIndex) noexcept {
    const auto baseParameterIndex = sctOpcodeBaseParameterIndex(schema, parameterIndex);
    return baseParameterIndex < schema.parameterCatalogCount
        ? &schema.parameterCatalog[baseParameterIndex] : nullptr;
}

[[nodiscard]] constexpr std::optional<SctOpcodeTextReferenceRule> sctOpcodeTextReference(
    const SctOpcodeSchema& schema,
    std::uint32_t parameterIndex) noexcept {
    const auto baseParameterIndex = sctOpcodeBaseParameterIndex(schema, parameterIndex);
    for (std::size_t i = 0; i < schema.textReferenceCount; ++i) {
        const auto& reference = schema.textReferences[i];
        if (reference.parameterIndex == baseParameterIndex) {
            return reference;
        }
    }
    return std::nullopt;
}

[[deprecated("Use sctOpcodeTextReference and inspect SctTextStorage")]]
[[nodiscard]] constexpr SctFooterParamMetadata sctOpcodeFooterReference(
    const SctOpcodeSchema& schema,
    std::uint32_t parameterIndex) noexcept {
    const auto reference = sctOpcodeTextReference(schema, parameterIndex);
    if (!reference.has_value() || reference->storage != SctTextStorage::Footer) {
        return {};
    }
    return {reference->kind == SctTextKind::PlainString
        ? SctFooterParamKind::String : SctFooterParamKind::SctString, reference->signedRelative};
}

[[deprecated("Use findSctOpcodeSchema and SctOpcodeSchema::semantic")]]
[[nodiscard]] constexpr SctOpcodeSemanticMetadata sctOpcodeMetadata(std::uint16_t opcode) noexcept {
    if (const auto* schema = findSctOpcodeSchema(opcode); schema != nullptr) {
        return schema->semantic;
    }
    SctOpcodeSemanticMetadata metadata{};
    metadata.opcode = opcode;
    return metadata;
}

[[deprecated("Use findSctOpcodeSchema and sctOpcodeTextReference")]]
[[nodiscard]] constexpr SctFooterParamMetadata sctFooterParamMetadata(
    std::uint16_t opcode,
    std::uint32_t parameterIndex) noexcept {
    if (const auto* schema = findSctOpcodeSchema(opcode); schema != nullptr) {
        const auto reference = sctOpcodeTextReference(*schema, parameterIndex);
        if (reference.has_value() && reference->storage == SctTextStorage::Footer) {
            return {reference->kind == SctTextKind::PlainString
                ? SctFooterParamKind::String : SctFooterParamKind::SctString,
                reference->signedRelative};
        }
    }
    return {};
}
} // namespace spice::sct
