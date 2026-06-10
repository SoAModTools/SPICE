#pragma once

#include "Alignment.h"
#include "Endian.h"
#include "FourCC.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>

namespace spice::core {

class EndianReader {
public:
    EndianReader(std::span<const std::uint8_t> data, Endian endian)
        : data_(data), endian_(endian) {}

    EndianReader(std::span<const std::byte> data, Endian endian)
        : data_(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()), endian_(endian) {}

    [[nodiscard]] std::optional<std::uint8_t> try_read_u8(std::size_t offset) const {
        if (!can_read(offset, sizeof(std::uint8_t))) {
            return std::nullopt;
        }
        return data_[offset];
    }

    [[nodiscard]] std::optional<std::int8_t> try_read_i8(std::size_t offset) const {
        const auto value = try_read_u8(offset);
        return value.has_value() ? std::optional<std::int8_t>(static_cast<std::int8_t>(*value)) : std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint16_t> try_read_u16(std::size_t offset) const {
        auto value = try_read_raw<std::uint16_t>(offset);
        if (!value.has_value()) {
            return std::nullopt;
        }
        return needs_swap(endian_) ? byteswap(*value) : *value;
    }

    [[nodiscard]] std::optional<std::int16_t> try_read_i16(std::size_t offset) const {
        const auto value = try_read_u16(offset);
        return value.has_value() ? std::optional<std::int16_t>(static_cast<std::int16_t>(*value)) : std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint32_t> try_read_u32(std::size_t offset) const {
        auto value = try_read_raw<std::uint32_t>(offset);
        if (!value.has_value()) {
            return std::nullopt;
        }
        return needs_swap(endian_) ? byteswap(*value) : *value;
    }

    [[nodiscard]] std::optional<std::int32_t> try_read_i32(std::size_t offset) const {
        const auto value = try_read_u32(offset);
        return value.has_value() ? std::optional<std::int32_t>(static_cast<std::int32_t>(*value)) : std::nullopt;
    }

    [[nodiscard]] std::optional<float> try_read_f32(std::size_t offset) const {
        const auto value = try_read_u32(offset);
        if (!value.has_value()) {
            return std::nullopt;
        }
        return std::bit_cast<float>(*value);
    }

    [[nodiscard]] std::optional<FourCC> try_read_fourcc(std::size_t offset) const {
        if (!can_read(offset, 4U)) {
            return std::nullopt;
        }
        return FourCC::from_bytes(data_.subspan(offset, 4U));
    }

    [[nodiscard]] std::uint8_t read_u8(std::size_t offset) const { return require(try_read_u8(offset)); }
    [[nodiscard]] std::int8_t read_i8(std::size_t offset) const { return require(try_read_i8(offset)); }
    [[nodiscard]] std::uint16_t read_u16(std::size_t offset) const { return require(try_read_u16(offset)); }
    [[nodiscard]] std::int16_t read_i16(std::size_t offset) const { return require(try_read_i16(offset)); }
    [[nodiscard]] std::uint32_t read_u32(std::size_t offset) const { return require(try_read_u32(offset)); }
    [[nodiscard]] std::int32_t read_i32(std::size_t offset) const { return require(try_read_i32(offset)); }
    [[nodiscard]] float read_f32(std::size_t offset) const { return require(try_read_f32(offset)); }
    [[nodiscard]] FourCC read_fourcc(std::size_t offset) const { return require(try_read_fourcc(offset)); }

    [[nodiscard]] std::uint32_t ReadUInt(std::uint32_t address) const { return read_u32(address); }
    [[nodiscard]] float ReadFloat(std::uint32_t address) const { return read_f32(address); }

    [[nodiscard]] bool can_read(std::size_t offset, std::size_t length) const {
        return bounds_contains(data_.size(), offset, length);
    }

    [[nodiscard]] Endian endian() const { return endian_; }
    [[nodiscard]] std::size_t size() const { return data_.size(); }
    [[nodiscard]] std::span<const std::uint8_t> bytes() const { return data_; }

private:
    template <class T>
    [[nodiscard]] std::optional<T> try_read_raw(std::size_t offset) const {
        if (!can_read(offset, sizeof(T))) {
            return std::nullopt;
        }
        T value{};
        std::memcpy(&value, data_.data() + offset, sizeof(T));
        return value;
    }

    template <class T>
    [[nodiscard]] static T require(std::optional<T> value) {
        if (!value.has_value()) {
            throw std::out_of_range("read beyond end of buffer");
        }
        return *value;
    }

    std::span<const std::uint8_t> data_;
    Endian endian_;
};

} // namespace spice::core

