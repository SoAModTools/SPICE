#pragma once

#include <array>
#include <cstdint>

namespace spice::mld::model {

struct TriangleMetadataDecodeResult {
    std::uint16_t rawWord = 0;
    std::uint16_t selectorLow15 = 0;
    bool streamWindingHighBit = false;
    std::uint8_t onesDigit = 0;
    std::uint8_t tensDigit = 0;
    std::uint8_t hundredsDigit = 0;
    std::uint8_t thousandsDigit = 0;
    std::uint8_t ignoredTenThousandsDigit = 0;
    std::uint16_t decodedU16 = 0;
    bool decodedHighBit = false;
    std::uint8_t decodedClassBits = 0;
    std::uint8_t payloadGroupBits = 0;
    std::uint8_t encounterSelectorBits = 0;
};

[[nodiscard]] constexpr TriangleMetadataDecodeResult decodeTriangleMetadataWord(
    const std::uint16_t rawWord) noexcept {
    constexpr std::array<std::uint16_t, 10> kOnes{
        0x0000U, 0x6800U, 0x7800U, 0x1800U, 0x1400U,
        0x0100U, 0x1200U, 0x1300U, 0x0000U, 0x0000U,
    };
    constexpr std::array<std::uint16_t, 10> kTens{
        0x0000U, 0x0001U, 0x0002U, 0x0003U, 0x0004U,
        0x0005U, 0x0006U, 0x0007U, 0x0008U, 0x0009U,
    };
    constexpr std::array<std::uint16_t, 10> kHundreds{
        0x0000U, 0x0010U, 0x0020U, 0x0030U, 0x0040U,
        0x0050U, 0x0060U, 0x0000U, 0x0000U, 0x8000U,
    };
    constexpr std::array<std::uint16_t, 10> kThousands{
        0x0000U, 0x8000U, 0x8200U, 0x8400U, 0x8600U,
        0x8800U, 0x8A00U, 0x8C00U, 0x8E00U, 0x9000U,
    };

    const auto selector = static_cast<std::uint16_t>(rawWord & 0x7FFFU);
    const auto ones = static_cast<std::uint8_t>(selector % 10U);
    const auto tens = static_cast<std::uint8_t>((selector / 10U) % 10U);
    const auto hundreds = static_cast<std::uint8_t>((selector / 100U) % 10U);
    const auto thousands = static_cast<std::uint8_t>((selector / 1000U) % 10U);
    const auto tenThousands = static_cast<std::uint8_t>((selector / 10000U) % 10U);

    std::uint16_t decoded = kOnes[ones];
    if (selector / 10U != 0U) {
        decoded = static_cast<std::uint16_t>(decoded | kTens[tens]);
    }
    if (selector / 100U != 0U) {
        decoded = static_cast<std::uint16_t>(decoded | kHundreds[hundreds]);
    }
    if (selector / 1000U != 0U) {
        decoded = static_cast<std::uint16_t>(decoded + kThousands[thousands]);
    }

    return TriangleMetadataDecodeResult{
        .rawWord = rawWord,
        .selectorLow15 = selector,
        .streamWindingHighBit = (rawWord & 0x8000U) != 0U,
        .onesDigit = ones,
        .tensDigit = tens,
        .hundredsDigit = hundreds,
        .thousandsDigit = thousands,
        .ignoredTenThousandsDigit = tenThousands,
        .decodedU16 = decoded,
        .decodedHighBit = (decoded & 0x8000U) != 0U,
        .decodedClassBits = static_cast<std::uint8_t>((decoded >> 8U) & 0x7FU),
        .payloadGroupBits = static_cast<std::uint8_t>((decoded >> 4U) & 0x0FU),
        .encounterSelectorBits = static_cast<std::uint8_t>(decoded & 0x0FU),
    };
}

} // namespace spice::mld::model
