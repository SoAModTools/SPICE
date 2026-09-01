#pragma once

#include <compare>
#include <cstdint>

namespace spice::sct {

struct SctHeaderValues {
    std::uint16_t sourceYear = 0;
    std::uint16_t sourceMonth = 0;
    std::uint16_t sourceDay = 0;
    std::uint16_t fourthValue = 0;
    auto operator<=>(const SctHeaderValues&) const = default;
};

inline constexpr SctHeaderValues kCanonicalSctHeaderValues{2002u, 6u, 14u, 0u};

enum class SctHeaderExportMode { ReuseReceiptOrCanonical, ExplicitValues };

struct SctHeaderExportOptions {
    SctHeaderExportMode mode = SctHeaderExportMode::ReuseReceiptOrCanonical;
    SctHeaderValues explicitValues = kCanonicalSctHeaderValues;
};

enum class SctHeaderMaterializationStatus {
    PreservedSourceBytes,
    ReencodedSourceValues,
    ExplicitValues,
    CanonicalDefault,
};

struct SctHeaderMaterializationRecord {
    SctHeaderMaterializationStatus status = SctHeaderMaterializationStatus::CanonicalDefault;
    SctHeaderValues values = kCanonicalSctHeaderValues;
};

} // namespace spice::sct
