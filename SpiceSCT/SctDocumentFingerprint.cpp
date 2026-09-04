#include "SctDocumentFingerprint.h"

#include "SctSha256.h"

#include <array>
#include <bit>
#include <string_view>
#include <type_traits>

namespace spice::sct::detail {
namespace {

class FingerprintWriter final {
public:
    void bytes(std::span<const std::uint8_t> value) noexcept { hash_.update(value); }
    void text(std::string_view value) noexcept {
        integral<std::uint64_t>(value.size());
        bytes({reinterpret_cast<const std::uint8_t*>(value.data()), value.size()});
    }
    template <typename T>
    void integral(T value) noexcept {
        using U = std::make_unsigned_t<T>;
        U bits = static_cast<U>(value);
        std::array<std::uint8_t, sizeof(U)> encoded{};
        for (std::size_t i = 0; i < encoded.size(); ++i) {
            encoded[i] = static_cast<std::uint8_t>(bits >> (i * 8u));
        }
        bytes(encoded);
    }
    template <typename E>
    void enumeration(E value) noexcept {
        integral<std::underlying_type_t<E>>(static_cast<std::underlying_type_t<E>>(value));
    }
    template <typename T, typename F>
    void optional(const std::optional<T>& value, F&& write) noexcept {
        integral<std::uint8_t>(value.has_value() ? 1u : 0u);
        if (value) write(*value);
    }
    template <typename T, typename F>
    void sequence(const std::vector<T>& values, F&& write) noexcept {
        integral<std::uint64_t>(values.size());
        for (const auto& value : values) write(value);
    }
    [[nodiscard]] SctValidationFingerprint finish() noexcept { return hash_.finish(); }

private:
    Sha256 hash_;
};

void writeExpression(FingerprintWriter& out, const SctCanonicalExpression& expression) noexcept {
    out.enumeration(expression.termination);
    out.integral<std::uint8_t>(static_cast<std::uint8_t>(expression.body.index()));
    std::visit([&](const auto& body) {
        using T = std::decay_t<decltype(body)>;
        if constexpr (std::is_same_v<T, SctTypedScptProgram>) {
            out.sequence(body.operations, [&](const SctScptOperation& operation) {
                out.integral<std::uint8_t>(static_cast<std::uint8_t>(operation.index()));
                std::visit([&](const auto& value) {
                    using O = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<O, SctScptValueOperation>) {
                        out.enumeration(value.kind);
                        out.integral(value.encodingWord);
                        out.sequence(value.payloadWords,
                            [&](std::uint32_t word) { out.integral(word); });
                    } else if constexpr (std::is_same_v<O, SctScptBinaryOperation>) {
                        out.enumeration(value.kind);
                        out.integral(value.encodingWord);
                    } else {
                        out.integral(value.encodingWord);
                    }
                }, operation);
            });
        } else {
            out.sequence(body.words, [&](std::uint32_t word) { out.integral(word); });
        }
    }, expression.body);
}

void writeText(FingerprintWriter& out, const SctTextValue& text) noexcept {
    out.integral<std::uint8_t>(static_cast<std::uint8_t>(text.index()));
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, SctPlainText>) {
            out.text(value.utf8);
        } else if constexpr (std::is_same_v<T, SctMessage>) {
            out.optional(value.headerUtf8, [&](const std::string& header) { out.text(header); });
            out.sequence(value.body.elements, [&](const SctFormattedTextElement& element) {
                out.integral<std::uint8_t>(static_cast<std::uint8_t>(element.index()));
                std::visit([&](const auto& item) {
                    using E = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<E, SctTextChunk>) {
                        out.text(item.utf8);
                    } else {
                        out.enumeration(item.code);
                        out.integral<std::uint8_t>(static_cast<std::uint8_t>(item.argument.index()));
                        std::visit([&](const auto& argument) {
                            using A = std::decay_t<decltype(argument)>;
                            if constexpr (std::is_same_v<A, SctDecimalCommandArgument>) {
                                out.optional(argument.value,
                                    [&](std::uint32_t number) { out.integral(number); });
                            } else if constexpr (std::is_same_v<A, SctByteListCommandArgument>) {
                                out.sequence(argument.values,
                                    [&](std::uint8_t byte) { out.integral(byte); });
                            }
                        }, item.argument);
                    }
                }, element);
            });
        } else if constexpr (std::is_same_v<T, SctOpaqueText>) {
            out.sequence(value.bytes, [&](std::uint8_t byte) { out.integral(byte); });
        }
    }, text);
}

void writeParameter(FingerprintWriter& out, const SctDocumentParameter& parameter) noexcept {
    out.integral(parameter.schemaIndex);
    out.integral<std::uint8_t>(static_cast<std::uint8_t>(parameter.value.index()));
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, SctEncodedWordValue>) {
            out.integral(value.value);
        } else if constexpr (std::is_same_v<T, SctCanonicalExpression>) {
            writeExpression(out, value);
        } else if constexpr (std::is_same_v<T, SctTerminatedWordSequenceValue>) {
            out.sequence(value.words, [&](std::uint32_t word) { out.integral(word); });
        } else if constexpr (std::is_same_v<T, SctInstructionReference>
            || std::is_same_v<T, SctStringReference>
            || std::is_same_v<T, SctSupplementaryTextReference>) {
            out.integral(value.target.value());
        } else if constexpr (std::is_same_v<T, SctUnresolvedReferenceValue>) {
            out.enumeration(value.expectedTarget.storage);
            out.optional(value.expectedTarget.textKind,
                [&](SctTextKind kind) { out.enumeration(kind); });
            out.sequence(value.encodedWords, [&](std::uint32_t word) { out.integral(word); });
        } else {
            out.sequence(value.words, [&](std::uint32_t word) { out.integral(word); });
        }
    }, parameter.value);
}

void writeInstruction(FingerprintWriter& out, const SctDocumentInstruction& instruction) noexcept {
    out.integral(instruction.id.value());
    out.integral(instruction.opcode);
    out.integral<std::uint8_t>(instruction.skipRefresh ? 1u : 0u);
    out.optional(instruction.scheduledExpression,
        [&](const SctCanonicalExpression& expression) { writeExpression(out, expression); });
    out.sequence(instruction.fixedParameters,
        [&](const SctDocumentParameter& parameter) { writeParameter(out, parameter); });
    out.sequence(instruction.repeatedParameterGroups,
        [&](const SctDocumentRepeatedParameterGroup& group) {
            out.sequence(group.parameters,
                [&](const SctDocumentParameter& parameter) { writeParameter(out, parameter); });
        });
}

void writeAnchor(FingerprintWriter& out, const SctOpaqueAnchor& anchor) noexcept {
    out.integral<std::uint8_t>(static_cast<std::uint8_t>(anchor.index()));
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (!std::is_same_v<T, SctDocumentAnchor>) out.integral(value.value());
    }, anchor);
}

} // namespace

SctValidationFingerprint fingerprintDocument(const SctDocument& document) noexcept {
    FingerprintWriter out;
    constexpr std::string_view domain = "SpiceSCT.DocumentValidation.v4";
    out.text(domain);
    out.integral(document.nextSectionIdValue());
    out.integral(document.nextInstructionIdValue());
    out.integral(document.nextStringIdValue());
    out.integral(document.nextSupplementaryTextIdValue());
    out.integral(document.nextOpaqueAttachmentIdValue());
    out.sequence(document.sections, [&](const SctDocumentSection& section) {
        out.integral(section.id.value());
        out.text(section.nameBytes);
        out.integral<std::uint8_t>(static_cast<std::uint8_t>(section.content.index()));
        std::visit([&](const auto& content) {
            using T = std::decay_t<decltype(content)>;
            if constexpr (std::is_same_v<T, SctScriptSectionContent>) {
                out.sequence(content.instructions,
                    [&](const SctDocumentInstruction& instruction) { writeInstruction(out, instruction); });
            } else if constexpr (std::is_same_v<T, SctStringSectionContent>) {
                out.integral(content.string.id.value());
                out.enumeration(content.string.kind);
                writeText(out, content.string.value);
                out.sequence(content.preambleWords,
                    [&](std::uint32_t word) { out.integral(word); });
            } else if constexpr (std::is_same_v<T, SctStringGroupMarkerSectionContent>) {
                out.sequence(content.preambleWords,
                    [&](std::uint32_t word) { out.integral(word); });
            }
        }, section.content);
    });
    out.sequence(document.supplementaryText, [&](const SctDocumentSupplementaryText& text) {
        out.integral(text.id.value());
        out.enumeration(text.kind);
        writeText(out, text.value);
    });
    out.sequence(document.opaqueAttachments, [&](const SctOpaqueAttachment& attachment) {
        out.integral(attachment.id.value());
        out.sequence(attachment.bytes, [&](std::uint8_t byte) { out.integral(byte); });
        writeAnchor(out, attachment.anchor);
        out.enumeration(attachment.placement);
        out.optional(attachment.fixedOffset,
            [&](std::uint32_t offset) { out.integral(offset); });
        out.integral(attachment.alignment);
        out.enumeration(attachment.relocation);
        out.enumeration(attachment.reason);
    });
    return out.finish();
}

SctValidationFingerprint fingerprintTargetValidation(
    const SctValidationFingerprint& documentFingerprint,
    SctPlatform platform,
    SctTextEncoding textEncoding,
    const SctBoundImportEvidence* evidence) noexcept {
    FingerprintWriter out;
    constexpr std::string_view domain = "SpiceSCT.TargetValidation.v4";
    out.text(domain);
    out.bytes(documentFingerprint);
    out.enumeration(platform);
    out.enumeration(textEncoding.characters);
    out.enumeration(textEncoding.messageSpace);
    out.integral<std::uint8_t>(evidence == nullptr ? 0u : 1u);
    if (evidence != nullptr) out.bytes(evidence->lineage().sha256);
    return out.finish();
}

} // namespace spice::sct::detail
