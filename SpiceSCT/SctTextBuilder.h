#pragma once

#include "SctDocument.h"

#include <string>
#include <string_view>
#include <vector>

namespace spice::sct {

struct SctTextBuildResult {
    bool success = false;
    SctFormattedText text;
    std::vector<SctDocumentDiagnostic> diagnostics;
};

struct SctInlineCommandBuildResult {
    std::optional<SctInlineCommand> command;
    std::vector<SctDocumentDiagnostic> diagnostics;
};

struct SctPlainTextBuildResult {
    std::optional<SctPlainText> text;
    std::vector<SctDocumentDiagnostic> diagnostics;
};

struct SctMessageBuildResult {
    std::optional<SctMessage> message;
    std::vector<SctDocumentDiagnostic> diagnostics;
};

class SctTextBuilder {
public:
    [[nodiscard]] static std::string normalizeLineEndings(std::string_view utf8);
    [[nodiscard]] static bool isValidUtf8(std::string_view utf8) noexcept;
    [[nodiscard]] static SctTextBuildResult build(std::vector<SctFormattedTextElement> elements);
    [[nodiscard]] static SctPlainTextBuildResult plainText(std::string utf8);
    [[nodiscard]] static SctMessageBuildResult message(
        std::optional<std::string> headerUtf8, std::vector<SctFormattedTextElement> body);
    [[nodiscard]] static SctInlineCommandBuildResult noArgumentCommand(SctMessageCommandCode code);
    [[nodiscard]] static SctInlineCommandBuildResult decimalCommand(
        SctMessageCommandCode code, std::optional<std::uint32_t> value);
    [[nodiscard]] static SctInlineCommandBuildResult colorCommand(
        std::uint8_t red, std::uint8_t green, std::uint8_t blue);
    [[nodiscard]] static SctInlineCommandBuildResult resetColorCommand();
    [[nodiscard]] static SctInlineCommandBuildResult byteListCommand(std::vector<std::uint8_t> values);
};

} // namespace spice::sct
