#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace spice::core {

class FourCC {
public:
    constexpr FourCC() = default;
    constexpr FourCC(char a, char b, char c, char d) : bytes_{ a, b, c, d } {}

    [[nodiscard]] static FourCC from_string(std::string_view value) {
        FourCC out{};
        for (std::size_t i = 0; i < out.bytes_.size() && i < value.size(); ++i) {
            out.bytes_[i] = value[i];
        }
        return out;
    }

    [[nodiscard]] static FourCC from_bytes(std::span<const std::uint8_t, 4> value) {
        return FourCC{
            static_cast<char>(value[0]),
            static_cast<char>(value[1]),
            static_cast<char>(value[2]),
            static_cast<char>(value[3]),
        };
    }

    [[nodiscard]] static FourCC from_bytes(std::span<const std::uint8_t> value) {
        return value.size() >= 4U
            ? from_bytes(std::span<const std::uint8_t, 4>(value.data(), 4U))
            : FourCC{};
    }

    [[nodiscard]] constexpr const std::array<char, 4>& bytes() const { return bytes_; }
    [[nodiscard]] std::string string() const { return std::string(bytes_.begin(), bytes_.end()); }

    [[nodiscard]] constexpr std::uint32_t as_big_endian_u32() const {
        return (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes_[0])) << 24U) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes_[1])) << 16U) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes_[2])) << 8U) |
            static_cast<std::uint32_t>(static_cast<unsigned char>(bytes_[3]));
    }

    [[nodiscard]] constexpr bool operator==(const FourCC&) const = default;

private:
    std::array<char, 4> bytes_{ { '\0', '\0', '\0', '\0' } };
};

} // namespace spice::core

