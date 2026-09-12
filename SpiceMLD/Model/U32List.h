#pragma once

#include "../../SpiceRoot/Binary/EndianReader.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace spice::mld::model {

enum class U32ListStatus { Unqualified, Absent, Present, PointerOutOfBounds, TruncatedCount, ExcessiveCount, TruncatedValues };

struct U32List {
    std::uint32_t pointer = 0;
    bool valid = false;
    std::vector<std::uint32_t> values{};
    U32ListStatus status = U32ListStatus::Unqualified;
    std::optional<std::uint32_t> declaredCount{};
};

using U32ListWarningSink = std::function<void(const std::string&)>;

[[nodiscard]] inline U32List readU32List(std::span<const std::uint8_t> bytes,
    const std::uint32_t pointer,
    const spice::root::Endian endian,
    const std::string& label,
    const U32ListWarningSink& warningSink) {
    U32List out{};
    out.pointer = pointer;
    if (pointer == 0U) {
        out.status = U32ListStatus::Absent;
        out.valid = true;
        return out;
    }
    const std::size_t offset = static_cast<std::size_t>(pointer);
    const spice::root::EndianReader reader(bytes, endian);
    const auto countOpt = reader.try_read_u32(offset);
    if (!countOpt.has_value()) {
        out.status = offset >= bytes.size() ? U32ListStatus::PointerOutOfBounds : U32ListStatus::TruncatedCount;
        warningSink(label + " pointer out of bounds: " + std::to_string(pointer));
        return out;
    }

    const std::size_t count = static_cast<std::size_t>(*countOpt);
    out.declaredCount = *countOpt;
    constexpr std::size_t hardCap = 1U << 16;
    if (count > hardCap) {
        out.status = U32ListStatus::ExcessiveCount;
        warningSink(label + " list count suspiciously large (" + std::to_string(count) + "); ignoring list.");
        return out;
    }

    if (offset + 4 + (count * 4) > bytes.size()) {
        out.status = U32ListStatus::TruncatedValues;
        warningSink(label + " list overruns file bounds (ptr=" + std::to_string(pointer) +
            ", count=" + std::to_string(count) + ")");
        return out;
    }

    out.values.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto value = reader.try_read_u32(offset + 4 + (i * 4));
        if (!value.has_value()) {
            break;
        }
        out.values.push_back(*value);
    }
    out.status = U32ListStatus::Present;
    out.valid = true;
    return out;
}

[[nodiscard]] inline std::vector<std::uint32_t> parseU32List(std::span<const std::uint8_t> bytes,
    const std::uint32_t pointer, const spice::root::Endian endian,
    const std::string& label, const U32ListWarningSink& warningSink) {
    return readU32List(bytes, pointer, endian, label, warningSink).values;
}

[[nodiscard]] inline std::shared_ptr<U32List> makeU32List(std::span<const std::uint8_t> bytes,
    const std::uint32_t pointer,
    const spice::root::Endian endian,
    const std::string& label,
    const U32ListWarningSink& warningSink) {
    return std::make_shared<U32List>(readU32List(bytes, pointer, endian, label, warningSink));
}

} // namespace spice::mld::model
