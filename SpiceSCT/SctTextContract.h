#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace spice::sct {

enum class SctTextKind {
    PlainString,
    String = PlainString,
    SctString,
};

enum class SctTextStorage {
    IndexedSection,
    // Physical SCT footer storage; canonical documents expose known records as
    // document-owned supplementary text rather than an editable footer container.
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

// These names describe combinations observed in source material. They are
// discovery conveniences, not a validity boundary: callers may continue to
// construct and use any SctTextEncoding combination directly.
enum class SctKnownTextConvention {
    Windows1252Byte7F,
    ShiftJisByte7F,
    ShiftJis8140,
};

struct SctKnownTextConventionDescriptor {
    SctKnownTextConvention convention = SctKnownTextConvention::ShiftJisByte7F;
    std::string_view stableName;
    SctTextEncoding encoding;
};

inline constexpr SctTextEncoding kSctShiftJisByte7FEncoding{};
inline constexpr SctTextEncoding kSctWindows1252Byte7FEncoding{
    SctCharacterEncoding::Windows1252, SctMessageSpaceEncoding::Byte7F};
inline constexpr SctTextEncoding kSctShiftJis8140Encoding{
    SctCharacterEncoding::ShiftJis, SctMessageSpaceEncoding::ShiftJis8140};

inline constexpr std::array<SctKnownTextConventionDescriptor, 3> kSctKnownTextConventions{{
    {SctKnownTextConvention::Windows1252Byte7F, "windows-1252-byte-7f",
        kSctWindows1252Byte7FEncoding},
    {SctKnownTextConvention::ShiftJisByte7F, "shift-jis-byte-7f",
        kSctShiftJisByte7FEncoding},
    {SctKnownTextConvention::ShiftJis8140, "shift-jis-8140",
        kSctShiftJis8140Encoding},
}};

[[nodiscard]] constexpr std::span<const SctKnownTextConventionDescriptor>
sctKnownTextConventions() noexcept {
    return kSctKnownTextConventions;
}

[[nodiscard]] constexpr const SctKnownTextConventionDescriptor*
findSctKnownTextConvention(SctKnownTextConvention convention) noexcept {
    for (const auto& descriptor : kSctKnownTextConventions) {
        if (descriptor.convention == convention) return &descriptor;
    }
    return nullptr;
}

[[nodiscard]] constexpr std::optional<SctTextEncoding>
sctTextEncodingFor(SctKnownTextConvention convention) noexcept {
    const auto* descriptor = findSctKnownTextConvention(convention);
    return descriptor == nullptr ? std::nullopt
        : std::optional<SctTextEncoding>{descriptor->encoding};
}

[[nodiscard]] constexpr std::optional<SctKnownTextConvention>
findSctKnownTextConvention(SctTextEncoding encoding) noexcept {
    for (const auto& descriptor : kSctKnownTextConventions) {
        if (descriptor.encoding == encoding) return descriptor.convention;
    }
    return std::nullopt;
}

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
