#include "CsvReader.h"

#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <utility>

namespace spice::trade::alx::detail {
namespace {

void addDiagnostic(
    CsvReadResult& result,
    const DiagnosticSeverity severity,
    std::string message,
    const std::filesystem::path& path,
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

bool isValidUtf8(const std::span<const std::uint8_t> bytes) noexcept
{
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

void validateDocument(
    CsvReadResult& result,
    const CsvDocument& document,
    const std::filesystem::path& path)
{
    if (document.headers.empty()) {
        addDiagnostic(result, DiagnosticSeverity::Error, "CSV header row is empty", path, 1U);
        return;
    }

    std::set<std::string> headers{};
    for (std::size_t column = 0U; column < document.headers.size(); ++column) {
        const auto& header = document.headers[column];
        if (header.empty()) {
            addDiagnostic(
                result,
                DiagnosticSeverity::Error,
                "CSV header name is empty",
                path,
                1U,
                column + 1U);
        } else if (!headers.insert(header).second) {
            addDiagnostic(
                result,
                DiagnosticSeverity::Error,
                "CSV header name is duplicated: " + header,
                path,
                1U,
                column + 1U);
        }
    }

    for (std::size_t row = 0U; row < document.rows.size(); ++row) {
        if (document.rows[row].size() != document.headers.size()) {
            addDiagnostic(
                result,
                DiagnosticSeverity::Error,
                "CSV row has " + std::to_string(document.rows[row].size())
                    + " fields; expected " + std::to_string(document.headers.size()),
                path,
                row + 2U);
        }
    }
}

} // namespace

bool CsvReadResult::ok() const noexcept
{
    return document.has_value() && !hasErrors(diagnostics);
}

CsvReadResult CsvReader::parse(
    const std::span<const std::uint8_t> inputBytes,
    const std::filesystem::path& diagnosticPath) const
{
    CsvReadResult result{};
    std::size_t offset = 0U;
    if (inputBytes.size() >= 3U
        && inputBytes[0] == 0xefU
        && inputBytes[1] == 0xbbU
        && inputBytes[2] == 0xbfU) {
        result.format.utf8Bom = true;
        offset = 3U;
    }

    const auto bytes = inputBytes.subspan(offset);
    if (bytes.empty()) {
        addDiagnostic(result, DiagnosticSeverity::Error, "CSV file is empty", diagnosticPath);
        return result;
    }
    if (!isValidUtf8(bytes)) {
        addDiagnostic(result, DiagnosticSeverity::Error, "CSV is not valid UTF-8", diagnosticPath);
        return result;
    }

    std::vector<CsvRow> records{};
    CsvRow row{};
    std::string field{};
    bool inQuotedField = false;
    bool afterClosingQuote = false;
    bool atFieldStart = true;
    bool endedWithLineEnding = false;
    bool sawLineEnding = false;
    bool warnedMixedLineEndings = false;

    const auto finishField = [&]() {
        row.push_back(std::move(field));
        field.clear();
        atFieldStart = true;
        afterClosingQuote = false;
    };
    const auto finishRecord = [&]() {
        finishField();
        records.push_back(std::move(row));
        row.clear();
        endedWithLineEnding = true;
    };

    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        const char character = static_cast<char>(bytes[index]);
        endedWithLineEnding = false;

        if (inQuotedField) {
            if (character == '"') {
                if (index + 1U < bytes.size() && bytes[index + 1U] == static_cast<std::uint8_t>('"')) {
                    field.push_back('"');
                    ++index;
                } else {
                    inQuotedField = false;
                    afterClosingQuote = true;
                }
            } else {
                field.push_back(character);
            }
            continue;
        }

        if (afterClosingQuote) {
            if (character == ',') {
                finishField();
                continue;
            }
        } else {
            if (character == '"') {
                if (!atFieldStart) {
                    addDiagnostic(
                        result,
                        DiagnosticSeverity::Error,
                        "Quote is not allowed inside an unquoted CSV field",
                        diagnosticPath,
                        records.size() + 1U,
                        row.size() + 1U);
                    return result;
                }
                inQuotedField = true;
                atFieldStart = false;
                continue;
            }
            if (character == ',') {
                finishField();
                continue;
            }
        }

        CsvLineEnding currentLineEnding{};
        bool isLineEnding = false;
        if (character == '\n') {
            currentLineEnding = CsvLineEnding::Lf;
            isLineEnding = true;
        } else if (character == '\r') {
            if (index + 1U >= bytes.size() || bytes[index + 1U] != static_cast<std::uint8_t>('\n')) {
                addDiagnostic(
                    result,
                    DiagnosticSeverity::Error,
                    "Bare carriage return is not a supported CSV record separator",
                    diagnosticPath,
                    records.size() + 1U,
                    row.size() + 1U);
                return result;
            }
            currentLineEnding = CsvLineEnding::CrLf;
            isLineEnding = true;
            ++index;
        }

        if (isLineEnding) {
            if (!sawLineEnding) {
                result.format.lineEnding = currentLineEnding;
                sawLineEnding = true;
            } else if (currentLineEnding != result.format.lineEnding && !warnedMixedLineEndings) {
                addDiagnostic(
                    result,
                    DiagnosticSeverity::Warning,
                    "CSV uses mixed record line endings; output will use the first detected style",
                    diagnosticPath,
                    records.size() + 1U);
                warnedMixedLineEndings = true;
            }
            finishRecord();
            continue;
        }

        if (afterClosingQuote) {
            addDiagnostic(
                result,
                DiagnosticSeverity::Error,
                "Unexpected character after a closing CSV quote",
                diagnosticPath,
                records.size() + 1U,
                row.size() + 1U);
            return result;
        }

        field.push_back(character);
        atFieldStart = false;
    }

    if (inQuotedField) {
        addDiagnostic(
            result,
            DiagnosticSeverity::Error,
            "CSV contains an unterminated quoted field",
            diagnosticPath,
            records.size() + 1U,
            row.size() + 1U);
        return result;
    }

    result.format.finalLineEnding = endedWithLineEnding;
    if (!endedWithLineEnding) {
        finishField();
        records.push_back(std::move(row));
    }
    if (records.empty()) {
        addDiagnostic(result, DiagnosticSeverity::Error, "CSV file is empty", diagnosticPath);
        return result;
    }

    CsvDocument document{};
    document.headers = std::move(records.front());
    document.rows.assign(
        std::make_move_iterator(records.begin() + 1),
        std::make_move_iterator(records.end()));
    validateDocument(result, document, diagnosticPath);
    if (!hasErrors(result.diagnostics)) {
        result.document = std::move(document);
    }
    return result;
}

CsvReadResult CsvReader::readFile(const std::filesystem::path& path) const
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        CsvReadResult result{};
        addDiagnostic(result, DiagnosticSeverity::Error, "Could not open CSV file", path);
        return result;
    }

    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>() };
    if (!input.good() && !input.eof()) {
        CsvReadResult result{};
        addDiagnostic(result, DiagnosticSeverity::Error, "Could not read CSV file", path);
        return result;
    }
    return parse(bytes, path);
}

} // namespace spice::trade::alx::detail
