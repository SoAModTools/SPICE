#include "WMapAreaFileWriter.h"

#include "../Compression/Aklz.h"

#include <optional>
#include <string>
#include <utility>

namespace spice::wmap {
namespace {

void addError(
    WMapAreaWriteResult& result,
    std::string message,
    const std::optional<std::size_t> offset = std::nullopt) {
    result.diagnostics.push_back(
        WMapAreaDiagnostic{ DiagnosticSeverity::Error, std::move(message), offset });
}

} // namespace

bool WMapAreaWriteResult::ok() const noexcept {
    return !bytes.empty() && !hasErrors(diagnostics);
}

WMapAreaWriteResult WMapAreaFileWriter::write(
    const WMapAreaDocument& document,
    const WMapAreaStorage storage) const {
    WMapAreaWriteResult result{};
    std::vector<std::uint8_t> decoded(kWMapAreaSerializedSize, 0U);

    for (std::size_t layer = 0U; layer < kWMapAreaLayerCount; ++layer) {
        for (std::size_t row = 0U; row < kWMapAreaRowCount; ++row) {
            for (std::size_t column = 0U; column < kWMapAreaColumnCount; ++column) {
                const auto offset = serializedOffset(layer, row, column);
                const auto value = document.cell(layer, row, column);
                if (value > kWMapAreaMaximumCellId) {
                    addError(
                        result,
                        "WMAPAREA cell ID must be in the range 0..15.",
                        offset);
                }
                decoded[offset] = value;
            }
        }
    }
    if (hasErrors(result.diagnostics)) {
        return result;
    }

    if (storage == WMapAreaStorage::Raw) {
        result.bytes = std::move(decoded);
        return result;
    }

    auto compressed = spice::compression::aklz::compress(decoded);
    if (!compressed.ok()) {
        addError(
            result,
            "AKLZ compression failed: " +
                std::string(spice::compression::aklz::errorToString(compressed.error)));
        return result;
    }
    result.bytes = std::move(compressed.bytes);
    return result;
}

} // namespace spice::wmap
