#pragma once

#include "../SpiceRoot/Binary/Endian.h"

#include <cstddef>
#include <cstdint>
#include <optional>
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

enum class StdParseStatus {
    Empty,
    Partial,
    Complete,
    Failed,
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

struct StdSourceRange {
    std::size_t offset{ 0U };
    std::size_t size{ 0U };
    std::string label{};
    bool known{ false };
    bool pinned{ false };
};

struct StdUnknownRange {
    std::size_t offset{ 0U };
    std::size_t size{ 0U };
    std::string label{};
    bool pinned{ true };
    std::vector<std::uint8_t> bytes{};
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
    std::uint32_t sourceTableOffset{ 0U };
    bool isSentinel{ false };
    std::int16_t locationCode{ 0 };
    std::int16_t opcode{ 0 };
    std::uint32_t combinedType{ 0U };
    std::uint32_t field2{ 0U };
    std::uint32_t payloadSize{ 0U };
    std::uint32_t sourcePayloadSize{ 0U };
    std::uint32_t payloadOffsetRel{ 0U };
    std::uint32_t sourcePayloadOffsetRel{ 0U };
    std::uint32_t payloadOffsetAbs{ 0U };
    std::uint32_t payloadEndRel{ 0U };
    bool payloadInBounds{ false };
    std::vector<std::uint8_t> payloadBytes{};
    spice::root::Endian sourceEndian{ spice::root::Endian::Big };
};

struct StdEntryTableLayout {
    StdEntryTableHeader header{};
    std::uint16_t sourceRecordCountIncludingSentinel{ 0U };
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

inline constexpr std::uint32_t kStdActionViewCombinedType = 0x0003002aU;
inline constexpr std::uint32_t kStdActionViewPayloadSize = 0x24U;

struct StdActionViewPayload {
    std::int16_t primaryActionKey{ 0 };
    std::int16_t routeSecondaryKey{ 0 };
    std::int16_t directSecondaryKey{ 0 };
    std::uint16_t lowFlags{ 0U };
    std::uint32_t reserved08{ 0U };
    std::uint32_t reserved0c{ 0U };
    std::uint32_t actionViewFlags{ 0U };
    std::uint32_t modeLocalAngleOrOffsetBits{ 0U };
    std::int16_t startFrame{ 0 };
    std::uint16_t reserved1a{ 0U };
    std::int16_t endFrame{ 0 };
    std::int16_t holdFrameCount{ 0 };
    std::int16_t stepFrameCount{ 0 };
    std::int16_t requestedMode{ 0 };
};

struct StdFile {
    std::string sourcePath{};
    StdParseStatus parseStatus{ StdParseStatus::Empty };
    StdSourceEncoding sourceEncoding{ StdSourceEncoding::Plain };
    spice::root::Endian sourceEndian{ spice::root::Endian::Big };
    bool endianWasForced{ false };
    std::uint32_t rawSize{ 0U };
    std::uint32_t decodedSize{ 0U };
    bool decodedAvailable{ false };
    std::vector<std::uint8_t> rawBytes{};
    std::vector<std::uint8_t> decodedBytes{};
    StdLayoutKind layoutKind{ StdLayoutKind::Unknown };
    StdActionRowsLayout actionRows{};
    StdEntryTableLayout entryTable{};
    std::vector<StdSourceRange> sourceRanges{};
    std::vector<StdUnknownRange> unknownRanges{};
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
[[nodiscard]] const char* toString(StdParseStatus status);
[[nodiscard]] const char* toString(StdLayoutKind kind);
[[nodiscard]] const char* toString(StdExportMode mode);
[[nodiscard]] const char* toString(StdUsageBucket bucket);

[[nodiscard]] const std::vector<std::uint8_t>* findEntryPayload(const StdFile& file, std::uint32_t recordIndex);
[[nodiscard]] std::vector<std::uint8_t>* findMutableEntryPayload(StdFile& file, std::uint32_t recordIndex);
[[nodiscard]] std::optional<StdActionViewPayload> readActionViewPayload(const StdEntryRecord& record);
[[nodiscard]] bool writeActionViewPayload(StdEntryRecord& record, const StdActionViewPayload& payload);

} // namespace spice::stdfile
