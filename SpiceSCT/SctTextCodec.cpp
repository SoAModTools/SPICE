#include "SctTextCodec.h"

#include "SctTextBuilder.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <limits>
#include <string_view>

namespace spice::sct {
namespace {

constexpr bool isShiftJis(SctTextEncoding encoding) {
    return encoding.characters == SctCharacterEncoding::ShiftJis;
}

constexpr bool usesByte7FSpace(SctTextEncoding encoding) {
    return encoding.messageSpace == SctMessageSpaceEncoding::Byte7F;
}

constexpr bool usesShiftJis8140Space(SctTextEncoding encoding) {
    return encoding.messageSpace == SctMessageSpaceEncoding::ShiftJis8140;
}

constexpr UINT codePage(SctTextEncoding encoding) {
    return isShiftJis(encoding) ? 932u : 1252u;
}

bool strictCp1252(std::span<const std::uint8_t> bytes) {
    constexpr std::uint8_t invalid[]{0x81u, 0x8du, 0x8fu, 0x90u, 0x9du};
    return std::none_of(bytes.begin(), bytes.end(), [&](std::uint8_t byte) {
        return std::find(std::begin(invalid), std::end(invalid), byte) != std::end(invalid);
    });
}

std::optional<std::wstring> decodeWide(std::span<const std::uint8_t> bytes, UINT page) {
    if (bytes.empty()) return std::wstring{};
    if (page == 1252u && !strictCp1252(bytes)) return std::nullopt;
    const auto* source = reinterpret_cast<const char*>(bytes.data());
    const auto count = static_cast<int>(bytes.size());
    const int needed = MultiByteToWideChar(page, MB_ERR_INVALID_CHARS, source, count, nullptr, 0);
    if (needed <= 0) return std::nullopt;
    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    if (MultiByteToWideChar(page, MB_ERR_INVALID_CHARS, source, count, result.data(), needed) != needed) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::string> wideToUtf8(std::wstring_view text) {
    if (text.empty()) return std::string{};
    const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return std::nullopt;
    std::string result(static_cast<std::size_t>(needed), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), result.data(), needed, nullptr, nullptr) != needed) return std::nullopt;
    return result;
}

std::optional<std::wstring> utf8ToWide(std::string_view text) {
    if (text.empty()) return std::wstring{};
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) return std::nullopt;
    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), result.data(), needed) != needed) return std::nullopt;
    return result;
}

std::optional<std::vector<std::uint8_t>> encodeWide(std::wstring_view text, UINT page) {
    if (text.empty()) return std::vector<std::uint8_t>{};
    BOOL usedDefault = FALSE;
    const int needed = WideCharToMultiByte(page, WC_NO_BEST_FIT_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, &usedDefault);
    if (needed <= 0 || usedDefault) return std::nullopt;
    std::vector<std::uint8_t> result(static_cast<std::size_t>(needed));
    usedDefault = FALSE;
    if (WideCharToMultiByte(page, WC_NO_BEST_FIT_CHARS, text.data(), static_cast<int>(text.size()),
        reinterpret_cast<char*>(result.data()), needed, nullptr, &usedDefault) != needed || usedDefault) return std::nullopt;
    if (page == 1252u && !strictCp1252(result)) return std::nullopt;
    return result;
}

std::optional<std::string> decodeCharacters(std::span<const std::uint8_t> bytes, SctTextEncoding encoding) {
    const auto wide = decodeWide(bytes, codePage(encoding));
    return wide ? wideToUtf8(*wide) : std::nullopt;
}

std::optional<std::vector<std::uint8_t>> encodeCharacters(std::string_view utf8, SctTextEncoding encoding) {
    const auto wide = utf8ToWide(utf8);
    return wide ? encodeWide(*wide, codePage(encoding)) : std::nullopt;
}

bool genericShiftJisLead(std::uint8_t byte) {
    return (byte >= 0x81u && byte <= 0x9fu) || (byte >= 0xe0u && byte <= 0xfcu);
}

bool sctShiftJisTrail(std::uint8_t byte) {
    return (byte >= 0x40u && byte <= 0x7eu) || (byte >= 0x80u && byte <= 0xfcu);
}

bool sctShiftJisPair(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return offset + 1u < bytes.size()
        && bytes[offset] >= 0x81u && bytes[offset] <= 0x98u
        && sctShiftJisTrail(bytes[offset + 1u]);
}

std::optional<std::size_t> semanticCodeUnitSize(
    std::span<const std::uint8_t> bytes, std::size_t offset, SctTextEncoding encoding) {
    if (!isShiftJis(encoding) || offset >= bytes.size()) return 1u;
    if (sctShiftJisPair(bytes, offset)) {
        const auto combined = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes[offset]) << 8u | bytes[offset + 1u]);
        if (combined > 0x9872u) return std::nullopt;
        return 2u;
    }
    if (genericShiftJisLead(bytes[offset])) return std::nullopt;
    return 1u;
}

void appendText(std::vector<SctFormattedTextElement>& elements, std::string text) {
    if (text.empty()) return;
    if (!elements.empty()) {
        if (auto* previous = std::get_if<SctTextChunk>(&elements.back())) {
            previous->utf8 += text;
            return;
        }
    }
    elements.push_back(SctTextChunk{std::move(text)});
}

bool isSimpleCommand(std::uint8_t byte, SctMessageCommandCode& code) {
    switch (byte) {
    case 'b': code = SctMessageCommandCode::B; return true;
    case 'c': code = SctMessageCommandCode::C; return true;
    case 'd': code = SctMessageCommandCode::D; return true;
    case 'e': code = SctMessageCommandCode::E; return true;
    case 'r': code = SctMessageCommandCode::R; return true;
    case 'u': code = SctMessageCommandCode::U; return true;
    case 'x': code = SctMessageCommandCode::X; return true;
    default: return false;
    }
}

bool parseDecimal(std::span<const std::uint8_t> bytes, std::size_t& cursor,
    std::optional<std::uint32_t>& value) {
    if (cursor >= bytes.size() || bytes[cursor] != '(') return false;
    const auto begin = ++cursor;
    while (cursor < bytes.size() && bytes[cursor] >= '0' && bytes[cursor] <= '9') ++cursor;
    if (cursor >= bytes.size() || bytes[cursor] != ')') return false;
    if (cursor == begin) value.reset();
    else {
        std::uint32_t parsed = 0;
        const auto* first = reinterpret_cast<const char*>(bytes.data() + begin);
        const auto* last = reinterpret_cast<const char*>(bytes.data() + cursor);
        const auto converted = std::from_chars(first, last, parsed);
        if (converted.ec != std::errc{} || converted.ptr != last) return false;
        value = parsed;
    }
    ++cursor;
    return true;
}

bool parseByteList(std::span<const std::uint8_t> bytes, std::size_t& cursor,
    std::vector<std::uint8_t>& values) {
    if (cursor >= bytes.size() || bytes[cursor] != '(') return false;
    ++cursor;
    if (cursor < bytes.size() && bytes[cursor] == ')') { ++cursor; return true; }
    while (cursor < bytes.size()) {
        const auto begin = cursor;
        while (cursor < bytes.size() && bytes[cursor] >= '0' && bytes[cursor] <= '9') ++cursor;
        if (cursor == begin) return false;
        unsigned parsed = 0;
        const auto* first = reinterpret_cast<const char*>(bytes.data() + begin);
        const auto* last = reinterpret_cast<const char*>(bytes.data() + cursor);
        const auto converted = std::from_chars(first, last, parsed);
        if (converted.ec != std::errc{} || converted.ptr != last || parsed > 255u) return false;
        values.push_back(static_cast<std::uint8_t>(parsed));
        if (cursor >= bytes.size()) return false;
        if (bytes[cursor] == ')') { ++cursor; return true; }
        if (bytes[cursor++] != ',') return false;
    }
    return false;
}

std::optional<std::size_t> findHeaderEnd(std::span<const std::uint8_t> bytes, SctTextEncoding encoding) {
    for (std::size_t cursor = 3; cursor < bytes.size();) {
        if (bytes[cursor] == ')') return cursor;
        const auto size = semanticCodeUnitSize(bytes, cursor, encoding);
        if (!size || cursor + *size > bytes.size()) return std::nullopt;
        cursor += *size;
    }
    return std::nullopt;
}

std::optional<std::string> decodeMessageFragment(std::span<const std::uint8_t> bytes,
    SctTextEncoding encoding) {
    std::string result;
    std::vector<std::uint8_t> pending;
    const auto flush = [&]() -> bool {
        const auto decoded = decodeCharacters(pending, encoding);
        if (!decoded) return false;
        result += *decoded;
        pending.clear();
        return true;
    };
    for (std::size_t cursor = 0; cursor < bytes.size();) {
        if (usesByte7FSpace(encoding) && bytes[cursor] == 0x7fu) {
            if (!flush()) return std::nullopt;
            result.push_back(' ');
            ++cursor;
            continue;
        }
        if (usesShiftJis8140Space(encoding) && cursor + 1 < bytes.size()
            && bytes[cursor] == 0x81u && bytes[cursor + 1] == 0x40u) {
            if (!flush()) return std::nullopt;
            result.push_back(' ');
            cursor += 2;
            continue;
        }
        const auto size = semanticCodeUnitSize(bytes, cursor, encoding);
        if (!size || cursor + *size > bytes.size()) return std::nullopt;
        pending.insert(pending.end(), bytes.begin() + cursor, bytes.begin() + cursor + *size);
        cursor += *size;
    }
    if (!flush()) return std::nullopt;
    return result;
}

std::optional<SctMessage> decodeMessage(std::span<const std::uint8_t> payload, SctTextEncoding encoding) {
    SctMessage message;
    std::size_t cursor = 0;
    if (payload.size() >= 3u && payload[0] == '\\' && payload[1] == 'h' && payload[2] == '(') {
        const auto end = findHeaderEnd(payload, encoding);
        if (!end) return std::nullopt;
        const auto header = decodeMessageFragment(payload.subspan(3u, *end - 3u), encoding);
        if (!header) return std::nullopt;
        message.headerUtf8 = *header;
        cursor = *end + 1u;
    }

    std::vector<std::uint8_t> pending;
    const auto flush = [&]() -> bool {
        const auto decoded = decodeCharacters(pending, encoding);
        if (!decoded) return false;
        appendText(message.body.elements, *decoded);
        pending.clear();
        return true;
    };

    while (cursor < payload.size()) {
        if (usesByte7FSpace(encoding) && payload[cursor] == 0x7fu) {
            if (!flush()) return std::nullopt;
            appendText(message.body.elements, " ");
            ++cursor;
            continue;
        }
        if (usesShiftJis8140Space(encoding) && cursor + 1 < payload.size()
            && payload[cursor] == 0x81u && payload[cursor + 1] == 0x40u) {
            if (!flush()) return std::nullopt;
            appendText(message.body.elements, " ");
            cursor += 2;
            continue;
        }
        if (payload[cursor] != '\\') {
            const auto size = semanticCodeUnitSize(payload, cursor, encoding);
            if (!size || cursor + *size > payload.size()) return std::nullopt;
            pending.insert(pending.end(), payload.begin() + cursor, payload.begin() + cursor + *size);
            cursor += *size;
            continue;
        }
        if (cursor + 1 >= payload.size()) { pending.push_back('\\'); ++cursor; continue; }
        const auto command = payload[cursor + 1];
        if (command == 'n') {
            if (!flush()) return std::nullopt;
            appendText(message.body.elements, "\n");
            cursor += 2;
            continue;
        }
        if (command == 'h') return std::nullopt;
        SctMessageCommandCode code{};
        if (isSimpleCommand(command, code)) {
            if (!flush()) return std::nullopt;
            message.body.elements.push_back(SctInlineCommand{code, SctNoCommandArgument{}});
            cursor += 2;
            continue;
        }
        if (command == 'a' || command == 's') {
            if (!flush()) return std::nullopt;
            code = command == 'a' ? SctMessageCommandCode::A : SctMessageCommandCode::S;
            cursor += 2;
            std::optional<std::uint32_t> value;
            if (!parseDecimal(payload, cursor, value)) return std::nullopt;
            message.body.elements.push_back(SctInlineCommand{code, SctDecimalCommandArgument{value}});
            continue;
        }
        if (command == 'p') {
            if (!flush()) return std::nullopt;
            cursor += 2;
            std::vector<std::uint8_t> values;
            if (!parseByteList(payload, cursor, values)) return std::nullopt;
            message.body.elements.push_back(SctInlineCommand{SctMessageCommandCode::P,
                SctByteListCommandArgument{std::move(values)}});
            continue;
        }
        if (command == 'w' && cursor + 2 < payload.size()
            && (payload[cursor + 2] == 'c' || payload[cursor + 2] == 'o')) {
            if (!flush()) return std::nullopt;
            code = payload[cursor + 2] == 'c' ? SctMessageCommandCode::Wc : SctMessageCommandCode::Wo;
            cursor += 3;
            std::optional<std::uint32_t> value;
            if (!parseDecimal(payload, cursor, value)) return std::nullopt;
            message.body.elements.push_back(SctInlineCommand{code, SctDecimalCommandArgument{value}});
            continue;
        }
        pending.push_back('\\');
        ++cursor;
    }
    if (!flush()) return std::nullopt;
    return message;
}

std::string_view commandSpelling(SctMessageCommandCode code) {
    switch (code) {
    case SctMessageCommandCode::A: return "a";
    case SctMessageCommandCode::B: return "b";
    case SctMessageCommandCode::C: return "c";
    case SctMessageCommandCode::D: return "d";
    case SctMessageCommandCode::E: return "e";
    case SctMessageCommandCode::P: return "p";
    case SctMessageCommandCode::R: return "r";
    case SctMessageCommandCode::S: return "s";
    case SctMessageCommandCode::U: return "u";
    case SctMessageCommandCode::X: return "x";
    case SctMessageCommandCode::Wc: return "wc";
    case SctMessageCommandCode::Wo: return "wo";
    }
    return {};
}

bool decimalCommand(SctMessageCommandCode code) {
    return code == SctMessageCommandCode::A || code == SctMessageCommandCode::S
        || code == SctMessageCommandCode::Wc || code == SctMessageCommandCode::Wo;
}

bool appendMessageText(std::vector<std::uint8_t>& out, std::string_view utf8,
    SctTextEncoding encoding, bool header) {
    const auto wide = utf8ToWide(utf8);
    if (!wide) return false;
    std::wstring pending;
    const auto flush = [&]() -> bool {
        const auto encoded = encodeWide(pending, codePage(encoding));
        if (!encoded) return false;
        out.insert(out.end(), encoded->begin(), encoded->end());
        pending.clear();
        return true;
    };
    for (wchar_t character : *wide) {
        if (character == L'\r' || character == L'\0' || (header && (character == L')' || character == L'\n'))) {
            return false;
        }
        if (character == L' ') {
            if (!flush()) return false;
            if (usesShiftJis8140Space(encoding)) out.insert(out.end(), {0x81u, 0x40u});
            else out.push_back(0x7fu);
        } else if (character == L'\n') {
            if (!flush()) return false;
            out.insert(out.end(), {'\\', 'n'});
        } else pending.push_back(character);
    }
    return flush();
}

bool appendCommand(std::vector<std::uint8_t>& out, const SctInlineCommand& command) {
    out.push_back('\\');
    const auto spelling = commandSpelling(command.code);
    out.insert(out.end(), spelling.begin(), spelling.end());
    if (decimalCommand(command.code)) {
        const auto* argument = std::get_if<SctDecimalCommandArgument>(&command.argument);
        if (!argument) return false;
        out.push_back('(');
        if (argument->value) {
            const auto text = std::to_string(*argument->value);
            out.insert(out.end(), text.begin(), text.end());
        }
        out.push_back(')');
        return true;
    }
    if (command.code == SctMessageCommandCode::P) {
        const auto* argument = std::get_if<SctByteListCommandArgument>(&command.argument);
        if (!argument) return false;
        out.push_back('(');
        for (std::size_t index = 0; index < argument->values.size(); ++index) {
            if (index) out.push_back(',');
            const auto text = std::to_string(argument->values[index]);
            out.insert(out.end(), text.begin(), text.end());
        }
        out.push_back(')');
        return true;
    }
    return std::holds_alternative<SctNoCommandArgument>(command.argument);
}

bool containsLiteralExecutableEscape(
    std::span<const std::uint8_t> bytes, SctTextEncoding encoding) {
    for (std::size_t cursor = 0; cursor + 1u < bytes.size();) {
        if (isShiftJis(encoding) && sctShiftJisPair(bytes, cursor)) {
            cursor += 2u;
            continue;
        }
        if (bytes[cursor] != '\\') {
            ++cursor;
            continue;
        }
        const auto next = bytes[cursor + 1u];
        if (next == 'n' || next == 'a' || next == 'b' || next == 'c' || next == 'd'
            || next == 'e' || next == 'h' || next == 'p' || next == 'r' || next == 's'
            || next == 'u' || next == 'x') return true;
        if (next == 'w' && cursor + 2u < bytes.size()
            && (bytes[cursor + 2u] == 'c' || bytes[cursor + 2u] == 'o')) return true;
        ++cursor;
    }
    return false;
}

std::optional<std::vector<std::uint8_t>> encodeMessage(const SctMessage& message, SctTextEncoding encoding) {
    std::vector<std::uint8_t> out;
    if (message.headerUtf8) {
        out.insert(out.end(), {'\\', 'h', '('});
        if (!appendMessageText(out, *message.headerUtf8, encoding, true)) return std::nullopt;
        out.push_back(')');
    }
    for (const auto& element : message.body.elements) {
        if (const auto* chunk = std::get_if<SctTextChunk>(&element)) {
            std::vector<std::uint8_t> encoded;
            const auto literalBytes = encodeCharacters(chunk->utf8, encoding);
            if (!literalBytes || containsLiteralExecutableEscape(*literalBytes, encoding)
                || !appendMessageText(encoded, chunk->utf8, encoding, false)) return std::nullopt;
            out.insert(out.end(), encoded.begin(), encoded.end());
        } else if (!appendCommand(out, std::get<SctInlineCommand>(element))) return std::nullopt;
    }
    out.push_back(0u);
    return out;
}

} // namespace

SctDecodedTextResult decodeSctTextRecord(std::span<const std::uint8_t> bytes,
    SctTextKind kind, SctTextStorage storage, SctTextEncoding encoding) {
    if (bytes.empty()) {
        if (storage == SctTextStorage::IndexedSection && kind == SctTextKind::SctString) {
            return {SctEmptyIndexedText{}, {}};
        }
        return {std::nullopt, "Text record has no terminator."};
    }
    if (bytes.back() != 0u
        || std::find(bytes.begin(), bytes.end() - 1, 0u) != bytes.end() - 1) {
        return {std::nullopt, "Text record does not contain exactly one final terminator."};
    }
    const auto payload = bytes.first(bytes.size() - 1u);
    SctTextValue value;
    if (kind == SctTextKind::PlainString) {
        const auto decoded = decodeCharacters(payload, encoding);
        if (!decoded) return {std::nullopt, "Plain text is not reversible under the selected encoding."};
        value = SctPlainText{*decoded};
    } else {
        const auto decoded = decodeMessage(payload, encoding);
        if (!decoded) return {std::nullopt, "SCT message syntax or encoding is not safely understood."};
        value = *decoded;
    }
    const auto encoded = encodeSctTextRecord(value, kind, storage, encoding);
    if (!encoded.bytes) {
        return {std::nullopt, "Decoded text cannot be encoded in canonical form."};
    }
    if (*encoded.bytes != std::vector<std::uint8_t>(bytes.begin(), bytes.end())) {
        const auto common = std::min(encoded.bytes->size(), bytes.size());
        std::size_t mismatch = 0;
        while (mismatch < common && (*encoded.bytes)[mismatch] == bytes[mismatch]) ++mismatch;
        return {std::nullopt, "Decoded text does not reproduce its source byte stream canonically at byte "
            + std::to_string(mismatch) + "."};
    }
    return {std::move(value), {}};
}

SctEncodedTextResult encodeSctTextRecord(const SctTextValue& value, SctTextKind kind,
    SctTextStorage storage, SctTextEncoding encoding) {
    if (const auto* opaque = std::get_if<SctOpaqueText>(&value)) return {opaque->bytes, {}};
    if (std::holds_alternative<SctEmptyIndexedText>(value)) {
        if (kind == SctTextKind::SctString && storage == SctTextStorage::IndexedSection) return {std::vector<std::uint8_t>{}, {}};
        return {std::nullopt, "Empty unterminated text is valid only for indexed SCT strings."};
    }
    if (kind == SctTextKind::PlainString) {
        const auto* plain = std::get_if<SctPlainText>(&value);
        if (!plain || !SctTextBuilder::isValidUtf8(plain->utf8) || plain->utf8.find('\0') != std::string::npos) {
            return {std::nullopt, "Plain text value is not valid zero-free UTF-8."};
        }
        auto encoded = encodeCharacters(plain->utf8, encoding);
        if (!encoded) return {std::nullopt, "Plain text is not encodable under the selected encoding."};
        encoded->push_back(0u);
        return {std::move(encoded), {}};
    }
    const auto* message = std::get_if<SctMessage>(&value);
    if (!message) return {std::nullopt, "SCT message kind requires a semantic message value."};
    auto encoded = encodeMessage(*message, encoding);
    if (!encoded) return {std::nullopt, "SCT message contains invalid UTF-8, command structure, or unencodable literal escape text."};
    return {std::move(encoded), {}};
}

} // namespace spice::sct
