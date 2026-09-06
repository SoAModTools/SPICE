#include "WMapAreaParser.h"

#include "../Compression/Aklz.h"

#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace spice::wmap {
namespace {

void addError(
    WMapAreaParseResult& result,
    std::string message,
    const std::optional<std::size_t> offset = std::nullopt) {
    result.diagnostics.push_back(
        WMapAreaDiagnostic{ DiagnosticSeverity::Error, std::move(message), offset });
}

} // namespace

bool WMapAreaParseResult::ok() const noexcept {
    return document.has_value() && !hasErrors(diagnostics);
}

WMapAreaParseResult WMapAreaParser::parse(
    const std::span<const std::uint8_t> bytes) {
    WMapAreaParseResult result{};
    result.rawSize = bytes.size();

    std::vector<std::uint8_t> decodedStorage{};
    std::span<const std::uint8_t> decoded = bytes;
    if (spice::compression::aklz::isAklz(bytes)) {
        result.sourceWasCompressedAklz = true;
        auto decodeResult = spice::compression::aklz::decompress(bytes);
        if (!decodeResult.ok()) {
            addError(
                result,
                "AKLZ decompression failed: " +
                    std::string(spice::compression::aklz::errorToString(decodeResult.error)));
            return result;
        }
        decodedStorage = std::move(decodeResult.bytes);
        decoded = decodedStorage;
    }

    result.decodedSize = decoded.size();
    if (decoded.size() != kWMapAreaSerializedSize) {
        addError(
            result,
            "WMAPAREA payload must be exactly 0x7e0 decoded bytes.");
        return result;
    }

    std::size_t invalidCount = 0U;
    std::optional<std::size_t> firstInvalidOffset{};
    for (std::size_t offset = 0U; offset < decoded.size(); ++offset) {
        if (decoded[offset] > kWMapAreaMaximumCellId) {
            if (!firstInvalidOffset.has_value()) {
                firstInvalidOffset = offset;
            }
            ++invalidCount;
        }
    }
    if (invalidCount != 0U) {
        addError(
            result,
            "WMAPAREA contains " + std::to_string(invalidCount) +
                " cell ID(s) outside the supported range 0..15.",
            firstInvalidOffset);
        return result;
    }

    WMapAreaDocument document{};
    for (std::size_t layer = 0U; layer < kWMapAreaLayerCount; ++layer) {
        for (std::size_t row = 0U; row < kWMapAreaRowCount; ++row) {
            for (std::size_t column = 0U; column < kWMapAreaColumnCount; ++column) {
                document.cell(layer, row, column) =
                    decoded[serializedOffset(layer, row, column)];
            }
        }
    }
    result.document = std::move(document);
    return result;
}

WMapAreaParseResult WMapAreaParser::parseFile(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        WMapAreaParseResult result{};
        addError(result, "Unable to open WMAPAREA file.");
        return result;
    }

    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    if (!input.eof() && input.fail()) {
        WMapAreaParseResult result{};
        addError(result, "Unable to read complete WMAPAREA file.");
        return result;
    }
    return parse(bytes);
}

} // namespace spice::wmap
