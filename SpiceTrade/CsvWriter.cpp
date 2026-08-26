#include "CsvWriter.h"

#include <fstream>
#include <set>
#include <span>
#include <string_view>

namespace spice::trade::alx {
namespace {

void addDiagnostic(
    CsvWriteResult& result,
    const DiagnosticSeverity severity,
    std::string message,
    const std::filesystem::path& path = {},
    const std::optional<std::size_t> row = std::nullopt,
    const std::optional<std::size_t> column = std::nullopt)
{
    result.diagnostics.push_back(CsvDiagnostic{
        .severity = severity,
        .message = std::move(message),
        .relativePath = path,
        .row = row,
        .column = column,
    });
}

bool isValidUtf8(const std::string_view value) noexcept
{
    const auto* begin = reinterpret_cast<const std::uint8_t*>(value.data());
    const std::span<const std::uint8_t> bytes(begin, value.size());
    std::size_t index = 0U;
    while (index < bytes.size()) {
        const auto first = bytes[index];
        if (first <= 0x7fU) {
            ++index;
            continue;
        }

        std::size_t continuationCount = 0U;
        std::uint32_t codePoint = 0U;
        if (first >= 0xc2U && first <= 0xdfU) {
            continuationCount = 1U;
            codePoint = first & 0x1fU;
        } else if (first >= 0xe0U && first <= 0xefU) {
            continuationCount = 2U;
            codePoint = first & 0x0fU;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            continuationCount = 3U;
            codePoint = first & 0x07U;
        } else {
            return false;
        }
        if (index + continuationCount >= bytes.size()) {
            return false;
        }
        for (std::size_t continuation = 1U; continuation <= continuationCount; ++continuation) {
            const auto byte = bytes[index + continuation];
            if ((byte & 0xc0U) != 0x80U) {
                return false;
            }
            codePoint = (codePoint << 6U) | (byte & 0x3fU);
        }
        if ((continuationCount == 2U && codePoint < 0x800U)
            || (continuationCount == 3U && codePoint < 0x10000U)
            || (codePoint >= 0xd800U && codePoint <= 0xdfffU)
            || codePoint > 0x10ffffU) {
            return false;
        }
        index += continuationCount + 1U;
    }
    return true;
}

void appendBytes(std::vector<std::uint8_t>& out, const std::string_view value)
{
    out.insert(out.end(), value.begin(), value.end());
}

void appendField(std::vector<std::uint8_t>& out, const std::string_view value)
{
    const bool quote = value.find_first_of(",\"\r\n") != std::string_view::npos;
    if (!quote) {
        appendBytes(out, value);
        return;
    }

    out.push_back(static_cast<std::uint8_t>('"'));
    for (const char character : value) {
        if (character == '"') {
            out.push_back(static_cast<std::uint8_t>('"'));
        }
        out.push_back(static_cast<std::uint8_t>(character));
    }
    out.push_back(static_cast<std::uint8_t>('"'));
}

bool validateDocument(CsvWriteResult& result, const CsvDocument& document)
{
    if (document.headers.empty()) {
        addDiagnostic(result, DiagnosticSeverity::Error, "CSV header row is empty", {}, 1U);
        return false;
    }

    std::set<std::string> headers{};
    for (std::size_t column = 0U; column < document.headers.size(); ++column) {
        const auto& header = document.headers[column];
        if (header.empty()) {
            addDiagnostic(result, DiagnosticSeverity::Error, "CSV header name is empty", {}, 1U, column + 1U);
        } else if (!isValidUtf8(header)) {
            addDiagnostic(result, DiagnosticSeverity::Error, "CSV header is not valid UTF-8", {}, 1U, column + 1U);
        } else if (!headers.insert(header).second) {
            addDiagnostic(
                result,
                DiagnosticSeverity::Error,
                "CSV header name is duplicated: " + header,
                {},
                1U,
                column + 1U);
        }
    }

    for (std::size_t rowIndex = 0U; rowIndex < document.rows.size(); ++rowIndex) {
        const auto& row = document.rows[rowIndex];
        if (row.size() != document.headers.size()) {
            addDiagnostic(
                result,
                DiagnosticSeverity::Error,
                "CSV row has " + std::to_string(row.size())
                    + " fields; expected " + std::to_string(document.headers.size()),
                {},
                rowIndex + 2U);
            continue;
        }
        for (std::size_t column = 0U; column < row.size(); ++column) {
            if (!isValidUtf8(row[column])) {
                addDiagnostic(
                    result,
                    DiagnosticSeverity::Error,
                    "CSV cell is not valid UTF-8",
                    {},
                    rowIndex + 2U,
                    column + 1U);
            }
        }
    }
    return !hasErrors(result.diagnostics);
}

} // namespace

bool CsvWriteResult::ok() const noexcept
{
    return !bytes.empty() && !hasErrors(diagnostics);
}

CsvWriteResult CsvWriter::write(const CsvDocument& document, const CsvFormat& format) const
{
    CsvWriteResult result{};
    if (!validateDocument(result, document)) {
        return result;
    }

    if (format.utf8Bom) {
        result.bytes.insert(result.bytes.end(), { 0xefU, 0xbbU, 0xbfU });
    }
    const std::string_view lineEnding = format.lineEnding == CsvLineEnding::CrLf ? "\r\n" : "\n";
    const auto appendRow = [&](const CsvRow& row) {
        for (std::size_t column = 0U; column < row.size(); ++column) {
            if (column != 0U) {
                result.bytes.push_back(static_cast<std::uint8_t>(','));
            }
            appendField(result.bytes, row[column]);
        }
    };

    appendRow(document.headers);
    for (const auto& row : document.rows) {
        appendBytes(result.bytes, lineEnding);
        appendRow(row);
    }
    if (format.finalLineEnding) {
        appendBytes(result.bytes, lineEnding);
    }
    return result;
}

CsvWriteResult CsvWriter::writeFile(
    const CsvDocument& document,
    const std::filesystem::path& path,
    const CsvFormat& format) const
{
    auto result = write(document, format);
    if (!result.ok()) {
        for (auto& diagnostic : result.diagnostics) {
            diagnostic.relativePath = path;
        }
        return result;
    }

    std::error_code error{};
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            addDiagnostic(result, DiagnosticSeverity::Error, "Could not create CSV output directory", path);
            return result;
        }
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        addDiagnostic(result, DiagnosticSeverity::Error, "Could not open CSV output file", path);
        return result;
    }
    output.write(
        reinterpret_cast<const char*>(result.bytes.data()),
        static_cast<std::streamsize>(result.bytes.size()));
    if (!output.good()) {
        addDiagnostic(result, DiagnosticSeverity::Error, "Could not write CSV output file", path);
    }
    return result;
}

} // namespace spice::trade::alx
