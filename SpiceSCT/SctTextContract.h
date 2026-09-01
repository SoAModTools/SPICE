#pragma once

#include <cstdint>

namespace spice::sct {

enum class SctPlatform;

enum class SctTextKind {
    PlainString,
    String = PlainString,
    SctString,
};

enum class SctTextStorage {
    IndexedSection,
    Footer,
};

enum class SctTextProfile {
    GameCubeUs,
    GameCubeEu,
    GameCubeJp,
    DreamcastUs,
    DreamcastEu,
};

[[nodiscard]] bool sctTextProfileSupportsPlatform(
    SctTextProfile profile, SctPlatform platform) noexcept;

enum class SctRelativeReferenceBase {
    InstructionEndMinusWord,
    OperandWord,
};

struct SctOpcodeTextReferenceRule {
    std::uint32_t parameterIndex = 0;
    SctTextKind kind = SctTextKind::PlainString;
    SctTextStorage storage = SctTextStorage::Footer;
    bool signedRelative = false;
    SctRelativeReferenceBase relativeBase = SctRelativeReferenceBase::OperandWord;
    std::uint32_t targetAlignment = 1;
    std::uint32_t encodedValueMask = 0xffffffffu;
};

} // namespace spice::sct
