#pragma once

#include "MldDocumentSession.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace spice::mix {

enum class SstSmlFieldEvidence {
    Gekko,
    GekkoAndCorpus,
    CorpusStable,
    CodeSupportedCorpusAbsent,
    Provisional,
};

struct SstSmlPairOverviewSnapshot {
    std::string stem{};
    std::filesystem::path smlPath{};
    std::filesystem::path sstPath{};
    std::uint64_t smlSourceSize = 0;
    std::uint64_t sstSourceSize = 0;
    std::uint32_t smlDecodedSize = 0;
    std::uint32_t sstDecodedSize = 0;
    bool smlWasAklz = false;
    bool sstWasAklz = false;
    std::string smlEndian{};
    std::string sstEndian{};
    std::string platformContext{};
    std::uint32_t recordCount = 0;
    bool recordCountsAgree = false;
    std::size_t embeddedMldParsedCount = 0;
    std::size_t embeddedMldFailedCount = 0;
};

struct SstSmlRecordSnapshot {
    std::size_t index = 0;
    std::uint32_t smlRecordOffset = 0;
    std::uint32_t embeddedMldOffset = 0;
    std::uint32_t embeddedMldSize = 0;
    bool embeddedMldInBounds = false;
    bool embeddedMldParsed = false;
    std::string embeddedMldParseStatus{};
    std::size_t embeddedMldEntryCount = 0;
    std::size_t embeddedMldTextureCount = 0;
    std::uint32_t sstRecordOffset = 0;
    std::uint32_t commandBlockOffset = 0;
    std::uint32_t commandCount = 0;
    bool commandBlockValid = false;
};

struct SstSmlCommandSummarySnapshot {
    std::size_t index = 0;
    std::int16_t type = 0;
    std::string typeLabel{};
    std::string typeDescription{};
    std::int16_t argument = 0;
    std::uint32_t recordOffset = 0;
    std::uint32_t payloadOffset = 0;
    std::uint32_t payloadSize = 0;
    bool typeKnown = false;
    bool payloadInBounds = false;
    std::optional<std::int16_t> localSlotIndex{};
    bool localSlotRangeKnown = false;
    bool localSlotInRange = false;
    std::optional<std::uint32_t> localSlotCount{};
};

struct SstSmlCommandFieldSnapshot {
    std::uint32_t offset = 0;
    std::string width{};
    std::string kind{};
    std::string name{};
    SstSmlFieldEvidence evidence = SstSmlFieldEvidence::Provisional;
    std::string scope{};
    bool provisional = true;
    bool valueAvailable = false;
    std::string value{};
    std::string rawHex{};
    std::string description{};
};

struct SstSmlConsumerWindowSnapshot {
    std::string name{};
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    bool inBounds = false;
    std::string rawHex{};
    std::string description{};
    std::vector<SstSmlCommandFieldSnapshot> fields{};
};

struct SstSmlLightingRowSnapshot {
    std::size_t index = 0;
    std::uint32_t rowOffset = 0;
    std::int8_t state = 0;
    bool sentinel = false;
    std::int16_t classSelector = 0;
    std::uint32_t flags = 0;
    bool enablesLightSetup = false;
    bool enablesVectorSetup = false;
    std::int16_t runtimeSlotId = 0;
    std::array<float, 3> lightVector{};
    std::array<float, 3> slotRgb{};
    std::array<float, 3> globalRgb{};
    float attenuationOrSpot0 = 0;
    float attenuationOrSpot1 = 0;
    std::uint32_t rawTailWord = 0;
    std::string rawHex{};
};

struct SstSmlCommandDetailSnapshot {
    SstSmlCommandSummarySnapshot summary{};
    std::uint32_t rawWord4 = 0;
    std::uint32_t rawWord8 = 0;
    std::uint32_t onDiskWord12 = 0;
    std::string payloadHex{};
    std::vector<SstSmlCommandFieldSnapshot> fields{};
    std::vector<SstSmlConsumerWindowSnapshot> consumerWindows{};
    std::vector<SstSmlLightingRowSnapshot> lightingRows{};
};

struct SstSmlRuntimeFieldSnapshot {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::string name{};
    std::string description{};
};

struct SstSmlRuntimeContextSnapshot {
    std::uint32_t provedRowStride = 0;
    std::uint32_t allocationWidthPerRecord = 0;
    std::string allocationWidthNote{};
    std::vector<SstSmlRuntimeFieldSnapshot> fields{};
};

struct SstSmlLocalSlotLinkSnapshot {
    std::size_t recordIndex = 0;
    std::size_t commandIndex = 0;
    std::int16_t commandType = 0;
    std::int16_t localSlotIndex = 0;
    bool rangeKnown = false;
    bool inRange = false;
    std::optional<std::uint32_t> localSlotCount{};
};

struct SstSmlBattleGridSnapshot {
    std::uint32_t sourceOffset = 0;
    std::uint32_t sourceSize = 0;
    bool inBounds = false;
    std::array<std::uint8_t, 81> values{};
    std::string paddingHex{};
};

struct SstSmlDiagnosticSnapshot {
    EventLevel level = EventLevel::Info;
    std::string origin{};
    std::string message{};
    std::optional<std::uint32_t> sourceOffset{};
    std::optional<std::size_t> recordIndex{};
};

class SstSmlDocumentSession {
public:
    struct OpenResult {
        std::shared_ptr<SstSmlDocumentSession> session{};
        DocumentResult result{};
    };

    static OpenResult open(const std::filesystem::path& eitherPairPath,
        const DocumentContext& context = {});

    ~SstSmlDocumentSession();
    SstSmlDocumentSession(SstSmlDocumentSession&&) noexcept;
    SstSmlDocumentSession& operator=(SstSmlDocumentSession&&) noexcept;
    SstSmlDocumentSession(const SstSmlDocumentSession&) = delete;
    SstSmlDocumentSession& operator=(const SstSmlDocumentSession&) = delete;

    [[nodiscard]] SstSmlPairOverviewSnapshot overview() const;
    [[nodiscard]] std::vector<std::filesystem::path> sourcePaths() const;
    [[nodiscard]] std::vector<SstSmlRecordSnapshot> records() const;
    [[nodiscard]] std::vector<SstSmlCommandSummarySnapshot> commands(std::size_t recordIndex) const;
    [[nodiscard]] std::optional<SstSmlCommandDetailSnapshot> commandDetail(
        std::size_t recordIndex, std::size_t commandIndex) const;
    [[nodiscard]] std::vector<std::pair<std::int16_t, std::uint32_t>> commandTypeHistogram() const;
    [[nodiscard]] SstSmlRuntimeContextSnapshot runtimeContext() const;
    [[nodiscard]] std::vector<SstSmlLocalSlotLinkSnapshot> localSlotLinks() const;
    [[nodiscard]] std::optional<SstSmlBattleGridSnapshot> battleGrid() const;
    [[nodiscard]] std::vector<SstSmlDiagnosticSnapshot> diagnostics() const;

    [[nodiscard]] std::optional<MldOverviewSnapshot> embeddedMldOverview(std::size_t recordIndex) const;
    [[nodiscard]] std::vector<MldEntrySnapshot> embeddedMldEntries(std::size_t recordIndex) const;
    [[nodiscard]] std::vector<MldEntryDetailSnapshot> embeddedMldEntryDetails(
        std::size_t recordIndex) const;
    [[nodiscard]] std::vector<MldTextureSnapshot> embeddedMldTextures(std::size_t recordIndex) const;
    [[nodiscard]] std::vector<DocumentDiagnostic> embeddedMldDiagnostics(std::size_t recordIndex) const;
    [[nodiscard]] std::optional<RgbaImageSnapshot> embeddedMldTexturePreview(
        std::size_t recordIndex, std::size_t textureIndex) const;

private:
    struct Impl;
    explicit SstSmlDocumentSession(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace spice::mix
