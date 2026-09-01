#pragma once

#include "SctDocument.h"
#include "SctOpcodeMetadata.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace spice::sct {

struct SctDecodedTextResult {
    std::optional<SctTextValue> value;
    std::string error;
};

struct SctEncodedTextResult {
    std::optional<std::vector<std::uint8_t>> bytes;
    std::string error;
};

[[nodiscard]] SctDecodedTextResult decodeSctTextRecord(
    std::span<const std::uint8_t> bytes,
    SctTextKind kind,
    SctTextStorage storage,
    SctTextEncoding encoding);

[[nodiscard]] SctEncodedTextResult encodeSctTextRecord(
    const SctTextValue& value,
    SctTextKind kind,
    SctTextStorage storage,
    SctTextEncoding encoding);

} // namespace spice::sct
