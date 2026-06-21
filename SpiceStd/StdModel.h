#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace spice::stdfile {

enum class StdDiagnosticSeverity {
    Info,
    Warning,
    Error,
};

enum class StdSourceEncoding {
    Plain,
    Aklz,
};

enum class StdLayoutKind {
    Unknown,
    ActionRows,
    EntryTable,
};

enum class StdExportMode {
    OriginalSourceBytes,
    DecodedBytes,
    ReencodeSourceKind,
    ReencodeAklz,
};

struct StdDiagnostic {
    StdDiagnosticSeverity severity{ StdDiagnosticSeverity::Info };
    std::string message{};
    std::uint32_t offset{ 0U };
};

struct StdActionRowsHeader {
    std::uint16_t commandLow{ 0U };
    std::uint16_t commandHigh{ 0U };
    std::uint32_t combinedCommandKind{ 0U };
    std::uint32_t loaderContextWord{ 0U };
    std::uint32_t rowCount{ 0U };
    std::uint32_t rowTablePtrWord{ 0U };
};

struct StdActionRow {
    std::uint32_t index{ 0U };
    std::uint32_t decodedOffset{ 0U };
    std::int16_t actionId{ 0 };
    std::int16_t rowType{ 0 };
    std::int16_t callbackIndex{ 0 };
    std::int16_t motionSlotOrdinal{ 0 };
    std::uint32_t flags{ 0U };
    std::int16_t secondaryKey{ 0 };
    std::int16_t callbackAuxParam{ 0 };
    std::uint32_t selectionTransitionScalarBits{ 0U };
    std::uint32_t motionProgressScalarBits{ 0U };
};

struct StdActionRowsLayout {
    StdActionRowsHeader header{};
    std::vector<StdActionRow> rows{};
};

struct StdEntryTableHeader {
    std::uint16_t recordCountIncludingSentinel{ 0U };
    std::uint16_t kind{ 0U };
    std::uint32_t reserved0{ 0U };
    std::uint32_t reserved1{ 0U };
    std::uint32_t decodedSpanMinusHeader{ 0U };
};

struct StdEntryRecord {
    std::uint32_t index{ 0U };
    std::uint32_t tableOffset{ 0U };
    bool isSentinel{ false };
    std::int16_t locationCode{ 0 };
    std::int16_t opcode{ 0 };
    std::uint32_t combinedType{ 0U };
    std::uint32_t field2{ 0U };
    std::uint32_t payloadSize{ 0U };
    std::uint32_t payloadOffsetRel{ 0U };
    std::uint32_t payloadOffsetAbs{ 0U };
    std::uint32_t payloadEndRel{ 0U };
    bool payloadInBounds{ false };
};

struct StdEntryTableLayout {
    StdEntryTableHeader header{};
    std::vector<StdEntryRecord> records{};
    bool hasSentinel{ false };
    std::uint32_t sentinelIndex{ 0U };
    std::uint32_t entryCountWithoutSentinel{ 0U };
    std::uint32_t firstPayloadOffsetRel{ 0U };
    bool hasPayloads{ false };
    std::uint32_t maxPayloadEndRel{ 0U };
    std::uint32_t trailingBytesAfterMaxPayload{ 0U };
    std::int64_t headerSpanDelta{ 0 };
};

struct StdFile {
    std::string sourcePath{};
    StdSourceEncoding sourceEncoding{ StdSourceEncoding::Plain };
    std::uint32_t rawSize{ 0U };
    std::uint32_t decodedSize{ 0U };
    bool decodedAvailable{ false };
    std::vector<std::uint8_t> rawBytes{};
    std::vector<std::uint8_t> decodedBytes{};
    StdLayoutKind layoutKind{ StdLayoutKind::Unknown };
    StdActionRowsLayout actionRows{};
    StdEntryTableLayout entryTable{};
    std::vector<StdDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const;
};

struct StdExportResult {
    std::vector<std::uint8_t> bytes{};
    std::string error{};

    [[nodiscard]] bool ok() const { return error.empty(); }
};

enum class StdUsageBucket {
    Unknown,
    BcharaMFamily,
    BcharaCommon,
    BcharaDamage,
    BcharaCharacterResource,
    BcharaOther,
    OtherDirectory,
};

struct StdUsageFile {
    std::string relativePath{};
    std::string absolutePath{};
    std::string directory{};
    std::string stem{};
    bool sourceWasCompressedAklz{ false };
    std::uint32_t rawSize{ 0U };
    std::uint32_t decodedSize{ 0U };
    bool decodedOk{ true };
    std::string decodeError{};
    StdUsageBucket usageBucket{ StdUsageBucket::Unknown };
    bool alxKnownCoveredPattern{ false };
    std::string decodedHeader16Hex{};
    std::string decodedHeader32Hex{};
    std::vector<std::string> printableStrings{};
};

[[nodiscard]] const char* toString(StdDiagnosticSeverity severity);
[[nodiscard]] const char* toString(StdSourceEncoding encoding);
[[nodiscard]] const char* toString(StdLayoutKind kind);
[[nodiscard]] const char* toString(StdExportMode mode);
[[nodiscard]] const char* toString(StdUsageBucket bucket);

} // namespace spice::stdfile
