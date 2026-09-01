#pragma once

#include <compare>
#include <cstdint>

namespace spice::sct {

enum class SctTextKind {
    PlainString,
    String = PlainString,
    SctString,
};

enum class SctTextStorage {
    IndexedSection,
    Footer,
};

enum class SctCharacterEncoding {
    ShiftJis,
    Windows1252,
};

enum class SctMessageSpaceEncoding {
    Byte7F,
    ShiftJis8140,
};

struct SctTextEncoding {
    SctCharacterEncoding characters = SctCharacterEncoding::ShiftJis;
    SctMessageSpaceEncoding messageSpace = SctMessageSpaceEncoding::Byte7F;
    auto operator<=>(const SctTextEncoding&) const = default;
};

inline constexpr SctTextEncoding kSctShiftJisByte7FEncoding{};
inline constexpr SctTextEncoding kSctWindows1252Byte7FEncoding{
    SctCharacterEncoding::Windows1252, SctMessageSpaceEncoding::Byte7F};
inline constexpr SctTextEncoding kSctShiftJis8140Encoding{
    SctCharacterEncoding::ShiftJis, SctMessageSpaceEncoding::ShiftJis8140};

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
