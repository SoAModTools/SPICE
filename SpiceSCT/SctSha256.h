#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace spice::sct::detail {

class Sha256 final {
public:
    Sha256() noexcept;
    void update(std::span<const std::uint8_t> bytes) noexcept;
    [[nodiscard]] std::array<std::uint8_t, 32> finish() noexcept;

private:
    void transform(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t byteCount_ = 0;
    std::size_t buffered_ = 0;
    bool finished_ = false;
};

[[nodiscard]] std::array<std::uint8_t, 32> sha256(
    std::span<const std::uint8_t> bytes) noexcept;

} // namespace spice::sct::detail
