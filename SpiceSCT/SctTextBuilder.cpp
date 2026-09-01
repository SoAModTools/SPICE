#include "SctTextBuilder.h"

namespace spice::sct {
namespace {

bool validUtf8(std::string_view text) noexcept {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<std::uint8_t>(text[i]);
        std::uint32_t value = 0;
        std::size_t count = 0;
        if (first <= 0x7fu) { value = first; count = 1; }
        else if ((first & 0xe0u) == 0xc0u) { value = first & 0x1fu; count = 2; }
        else if ((first & 0xf0u) == 0xe0u) { value = first & 0x0fu; count = 3; }
        else if ((first & 0xf8u) == 0xf0u) { value = first & 0x07u; count = 4; }
        else return false;
        if (i + count > text.size()) return false;
        for (std::size_t part = 1; part < count; ++part) {
            const auto byte = static_cast<std::uint8_t>(text[i + part]);
            if ((byte & 0xc0u) != 0x80u) return false;
            value = (value << 6) | (byte & 0x3fu);
        }
        if ((count == 2 && value < 0x80u) || (count == 3 && value < 0x800u)
            || (count == 4 && value < 0x10000u) || value > 0x10ffffu
            || (value >= 0xd800u && value <= 0xdfffu)) return false;
        i += count;
    }
    return true;
}

} // namespace

std::string SctTextBuilder::normalizeLineEndings(std::string_view utf8) {
    std::string result;
    result.reserve(utf8.size());
    for (std::size_t i = 0; i < utf8.size(); ++i) {
        if (utf8[i] == '\r') {
            if (i + 1 < utf8.size() && utf8[i + 1] == '\n') ++i;
            result.push_back('\n');
        } else result.push_back(utf8[i]);
    }
    return result;
}

bool SctTextBuilder::isValidUtf8(std::string_view utf8) noexcept { return validUtf8(utf8); }

SctTextBuildResult SctTextBuilder::build(std::vector<SctFormattedTextElement> elements) {
    SctTextBuildResult result;
    for (auto& element : elements) {
        if (auto* chunk = std::get_if<SctTextChunk>(&element)) {
            chunk->utf8 = normalizeLineEndings(chunk->utf8);
            if (!validUtf8(chunk->utf8) || chunk->utf8.find('\0') != std::string::npos) {
                result.diagnostics.push_back({SctDiagnosticSeverity::Error,
                    SctDiagnosticCode::TextInvalid, std::nullopt,
                    "Text chunk must contain valid zero-free UTF-8."});
                return result;
            }
            if (chunk->utf8.empty()) continue;
            if (!result.text.elements.empty()) {
                if (auto* previous = std::get_if<SctTextChunk>(&result.text.elements.back())) {
                    previous->utf8 += chunk->utf8;
                    continue;
                }
            }
        }
        result.text.elements.push_back(std::move(element));
    }
    result.success = true;
    return result;
}

SctPlainTextBuildResult SctTextBuilder::plainText(std::string utf8) {
    if (!validUtf8(utf8) || utf8.find('\0') != std::string::npos) {
        return {std::nullopt, {{SctDiagnosticSeverity::Error, SctDiagnosticCode::TextInvalid,
            std::nullopt, "Plain text must contain valid zero-free UTF-8."}}};
    }
    return {SctPlainText{std::move(utf8)}, {}};
}

SctMessageBuildResult SctTextBuilder::message(
    std::optional<std::string> headerUtf8, std::vector<SctFormattedTextElement> body) {
    if (headerUtf8 && (!validUtf8(*headerUtf8) || headerUtf8->find('\0') != std::string::npos
        || headerUtf8->find('\r') != std::string::npos || headerUtf8->find('\n') != std::string::npos)) {
        return {std::nullopt, {{SctDiagnosticSeverity::Error, SctDiagnosticCode::TextInvalid,
            std::nullopt, "Message headers must contain valid UTF-8 without NUL or line breaks."}}};
    }
    auto builtBody = build(std::move(body));
    if (!builtBody.success) return {std::nullopt, std::move(builtBody.diagnostics)};
    return {SctMessage{std::move(headerUtf8), std::move(builtBody.text)}, {}};
}

SctInlineCommandBuildResult SctTextBuilder::noArgumentCommand(SctMessageCommandCode code) {
    switch (code) {
    case SctMessageCommandCode::B:
    case SctMessageCommandCode::C:
    case SctMessageCommandCode::D:
    case SctMessageCommandCode::E:
    case SctMessageCommandCode::R:
    case SctMessageCommandCode::U:
    case SctMessageCommandCode::X:
        return {SctInlineCommand{code, SctNoCommandArgument{}}, {}};
    default:
        return {std::nullopt, {{SctDiagnosticSeverity::Error, SctDiagnosticCode::TextInvalid,
            std::nullopt, "The selected SCT command requires a typed argument."}}};
    }
}

SctInlineCommandBuildResult SctTextBuilder::decimalCommand(
    SctMessageCommandCode code, std::optional<std::uint32_t> value) {
    switch (code) {
    case SctMessageCommandCode::A:
    case SctMessageCommandCode::S:
    case SctMessageCommandCode::Wc:
    case SctMessageCommandCode::Wo:
        return {SctInlineCommand{code, SctDecimalCommandArgument{value}}, {}};
    default:
        return {std::nullopt, {{SctDiagnosticSeverity::Error, SctDiagnosticCode::TextInvalid,
            std::nullopt, "The selected SCT command does not accept a decimal argument."}}};
    }
}

SctInlineCommandBuildResult SctTextBuilder::byteListCommand(std::vector<std::uint8_t> values) {
    return {SctInlineCommand{SctMessageCommandCode::P,
        SctByteListCommandArgument{std::move(values)}}, {}};
}

SctInlineCommandBuildResult SctTextBuilder::colorCommand(
    std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    return byteListCommand({red, green, blue});
}

SctInlineCommandBuildResult SctTextBuilder::resetColorCommand() {
    return byteListCommand({});
}

} // namespace spice::sct
