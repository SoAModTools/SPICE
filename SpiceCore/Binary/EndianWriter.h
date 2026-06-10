#pragma once

#include "Alignment.h"
#include "Endian.h"
#include "FourCC.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

namespace spice::core {

class EndianWriter {
public:
    explicit EndianWriter(Endian endian) : endian_(endian) {}

    [[nodiscard]] std::uint32_t pointer_position() const {
        return static_cast<std::uint32_t>(data_.size());
    }

    void reserve(std::size_t size) { data_.reserve(size); }
    void resize(std::size_t size, std::uint8_t fill = 0U) { data_.resize(size, fill); }
    void align(std::size_t alignment, std::uint8_t fill = 0U) { resize(align_up(data_.size(), alignment), fill); }

    void write_u8(std::uint8_t value) { data_.push_back(value); }
    void write_i8(std::int8_t value) { write_u8(static_cast<std::uint8_t>(value)); }
    void write_u16(std::uint16_t value) { write_raw(needs_swap(endian_) ? byteswap(value) : value); }
    void write_i16(std::int16_t value) { write_u16(static_cast<std::uint16_t>(value)); }
    void write_u32(std::uint32_t value) { write_raw(needs_swap(endian_) ? byteswap(value) : value); }
    void write_i32(std::int32_t value) { write_u32(static_cast<std::uint32_t>(value)); }
    void write_f32(float value) { write_u32(std::bit_cast<std::uint32_t>(value)); }
    void write_fourcc(const FourCC& value) { append(value.bytes().begin(), value.bytes().end()); }
    void write_bytes(std::span<const std::uint8_t> bytes) { append(bytes.begin(), bytes.end()); }

    void write_u16_at(std::size_t offset, std::uint16_t value) { write_at(offset, needs_swap(endian_) ? byteswap(value) : value); }
    void write_i16_at(std::size_t offset, std::int16_t value) { write_u16_at(offset, static_cast<std::uint16_t>(value)); }
    void write_u32_at(std::size_t offset, std::uint32_t value) { write_at(offset, needs_swap(endian_) ? byteswap(value) : value); }
    void write_i32_at(std::size_t offset, std::int32_t value) { write_u32_at(offset, static_cast<std::uint32_t>(value)); }
    void write_f32_at(std::size_t offset, float value) { write_u32_at(offset, std::bit_cast<std::uint32_t>(value)); }
    void write_fourcc_at(std::size_t offset, const FourCC& value) {
        if (!bounds_contains(data_.size(), offset, 4U)) {
            throw std::out_of_range("write beyond end of buffer");
        }
        std::copy(value.bytes().begin(), value.bytes().end(), data_.begin() + static_cast<std::ptrdiff_t>(offset));
    }

    [[nodiscard]] Endian endian() const { return endian_; }
    [[nodiscard]] const std::vector<std::uint8_t>& data() const { return data_; }
    [[nodiscard]] std::vector<std::uint8_t> take_data() { return std::move(data_); }

private:
    template <class T>
    void write_raw(T value) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
        append(bytes, bytes + sizeof(T));
    }

    template <class T>
    void write_at(std::size_t offset, T value) {
        if (!bounds_contains(data_.size(), offset, sizeof(T))) {
            throw std::out_of_range("write beyond end of buffer");
        }
        std::memcpy(data_.data() + offset, &value, sizeof(T));
    }

    template <class It>
    void append(It begin, It end) {
        data_.insert(data_.end(), begin, end);
    }

    Endian endian_;
    std::vector<std::uint8_t> data_;
};

} // namespace spice::core
