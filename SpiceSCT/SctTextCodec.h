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

struct SctTextByteRange {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    auto operator<=>(const SctTextByteRange&) const = default;
};

enum class SctTextInspectionSpanKind {
    Text,
    MessageSpace,
    Header,
    Command,
    Terminator,
    Invalid,
    Uninterpreted,
};

struct SctTextInspectionSpan {
    SctTextByteRange source;
    SctTextInspectionSpanKind kind = SctTextInspectionSpanKind::Uninterpreted;
    std::string utf8;
};

enum class SctTextInspectionIssueCode {
    MissingTerminator,
    EmbeddedTerminator,
    InvalidCharacterEncoding,
    MalformedCommand,
    UnsupportedHeaderPlacement,
    NonCanonicalRoundTrip,
    UninterpretedRecord,
};

struct SctTextInspectionIssue {
    SctTextInspectionIssueCode code = SctTextInspectionIssueCode::UninterpretedRecord;
    SctTextByteRange source;
    std::string message;
};

struct SctTextInterpretation {
    SctTextEncoding encoding;
    std::optional<SctKnownTextConvention> knownConvention;
    bool complete = false;
    std::optional<SctTextValue> semanticValue;
    std::vector<SctTextInspectionSpan> spans;
    std::vector<SctTextInspectionIssue> issues;
};

struct SctOpaqueTextInspectionResult {
    std::vector<std::uint8_t> bytes;
    SctTextKind kind = SctTextKind::SctString;
    SctTextStorage storage = SctTextStorage::Footer;
    std::vector<SctTextInterpretation> interpretations;
    bool ambiguous = false;
};

class SctTextInspectionService {
public:
    [[nodiscard]] static SctTextInterpretation interpret(
        std::span<const std::uint8_t> bytes,
        SctTextKind kind,
        SctTextStorage storage,
        SctTextEncoding encoding);

    [[nodiscard]] static SctOpaqueTextInspectionResult inspectKnownConventions(
        std::span<const std::uint8_t> bytes,
        SctTextKind kind,
        SctTextStorage storage);

    [[nodiscard]] static SctOpaqueTextInspectionResult inspectKnownConventions(
        const SctOpaqueText& text,
        SctTextKind kind,
        SctTextStorage storage);
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
