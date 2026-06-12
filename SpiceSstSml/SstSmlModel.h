#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace spice::sstsml {

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error,
};

struct ParseDiagnostic {
    DiagnosticSeverity severity{ DiagnosticSeverity::Info };
    std::string message{};
    std::uint32_t offset{ 0U };
};

enum class CommandFieldKind {
    ModelIndex,
    LookupKey,
    RawWord,
    HalfwordParameter,
    FloatParameter,
    RuntimePointer,
    VectorComponent,
    VectorDelta,
    Duration,
    Counter,
    AxisSelector,
    BufferPointer,
    ReservedRaw,
};

enum class CommandFieldWidth {
    I8,
    U8,
    I16,
    U16,
    U32,
    F32,
};

enum class CommandFieldEvidence {
    Gekko,
    GekkoAndCorpus,
    CorpusStable,
    CodeSupportedCorpusAbsent,
    Provisional,
};

enum class CommandFieldScope {
    StructuralPayload,
    ConsumerTrailing,
    RuntimeLocal,
};

struct CommandFieldSummary {
    std::uint32_t offset{ 0U };
    CommandFieldWidth width{ CommandFieldWidth::U32 };
    CommandFieldKind kind{ CommandFieldKind::ReservedRaw };
    std::string name{};
    CommandFieldEvidence evidence{ CommandFieldEvidence::Provisional };
    CommandFieldScope scope{ CommandFieldScope::StructuralPayload };
    bool provisional{ true };
    std::string description{};
};

struct CommandConsumerWindow {
    std::string name{};
    std::uint32_t offset{ 0U };
    std::uint32_t size{ 0U };
    bool inBounds{ false };
    std::vector<std::uint8_t> bytes{};
    std::vector<CommandFieldSummary> fieldSummaries{};
    std::string description{};
};

struct SmlRecord {
    std::size_t index{ 0U };
    std::uint32_t recordOffset{ 0U };
    std::uint32_t rawWord0{ 0U };
    std::uint32_t embeddedMldOffset{ 0U };
    std::uint32_t embeddedMldSize{ 0U };
    std::uint32_t rawWord12{ 0U };
    bool embeddedMldInBounds{ false };
    std::vector<std::uint8_t> embeddedMldBytes{};
};

struct SmlParseResult {
    std::string sourcePath{};
    bool sourceWasCompressedAklz{ false };
    std::uint32_t decodedSize{ 0U };
    std::uint32_t rawHeader0{ 0U };
    std::uint32_t rawRecordCountWord{ 0U };
    std::uint32_t recordCount{ 0U };
    std::vector<SmlRecord> records{};
    std::vector<ParseDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const;
};

struct SstTopLevelRecord {
    std::size_t index{ 0U };
    std::uint32_t recordOffset{ 0U };
    std::uint32_t rawWord0{ 0U };
    std::uint32_t rawWord4{ 0U };
    std::uint32_t rawWord8{ 0U };
    std::uint32_t commandBlockOffset{ 0U };
};

struct SstCommandRecord {
    std::size_t index{ 0U };
    std::uint32_t recordOffset{ 0U };
    std::int16_t type{ 0 };
    std::int16_t argument{ 0 };
    std::uint32_t rawWord4{ 0U };
    std::uint32_t rawWord8{ 0U };
    std::uint32_t onDiskWord12{ 0U };
    std::uint32_t payloadOffset{ 0U };
    std::uint32_t payloadSize{ 0U };
    bool typeKnown{ false };
    bool payloadInBounds{ false };
    bool modelIndexCandidate{ false };
    std::optional<std::int16_t> modelIndex{};
    std::vector<std::uint8_t> payloadBytes{};
    std::vector<CommandFieldSummary> fieldSummaries{};
    std::vector<CommandConsumerWindow> consumerWindows{};
};

struct SstCommandBlock {
    std::size_t topLevelRecordIndex{ 0U };
    std::uint32_t blockOffset{ 0U };
    std::uint32_t commandCount{ 0U };
    std::uint32_t recordsOffset{ 0U };
    std::uint32_t sentinelOffset{ 0U };
    std::uint32_t payloadStartOffset{ 0U };
    std::uint32_t payloadEndOffset{ 0U };
    bool valid{ false };
    std::int16_t sentinelType{ 0 };
    std::int16_t sentinelArgument{ 0 };
    std::vector<SstCommandRecord> commands{};
};

struct SstParseResult {
    std::string sourcePath{};
    bool sourceWasCompressedAklz{ false };
    std::uint32_t decodedSize{ 0U };
    std::uint16_t recordCount{ 0U };
    std::vector<SstTopLevelRecord> topLevelRecords{};
    std::vector<SstCommandBlock> commandBlocks{};
    std::vector<ParseDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const;
};

struct ResolvedCommandLink {
    std::size_t topLevelRecordIndex{ 0U };
    std::size_t commandIndex{ 0U };
    std::int16_t commandType{ 0 };
    std::int16_t modelIndex{ 0 };
    bool resolved{ false };
    std::optional<std::size_t> smlRecordIndex{};
};

struct BattleStageParseResult {
    std::string stem{};
    SmlParseResult sml{};
    SstParseResult sst{};
    bool recordCountsAgree{ false };
    std::vector<ResolvedCommandLink> commandLinks{};
    std::vector<std::pair<std::int16_t, std::uint32_t>> commandTypeHistogram{};
    std::vector<ParseDiagnostic> diagnostics{};

    [[nodiscard]] bool ok() const;
};

} // namespace spice::sstsml
