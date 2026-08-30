#pragma once

#include "DocumentTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace spice::mix {

struct EctOverviewSnapshot {
    std::filesystem::path sourcePath{};
    std::string layout{};
    std::string platform{};
    std::string endian{};
    bool sourceWasAklz = false;
    std::uint64_t sourceSize = 0;
    std::uint64_t decodedSize = 0;
    std::size_t containerEntryCount = 0;
    std::size_t tableCount = 0;
    std::size_t encounterSlotCount = 0;
    std::size_t nonzeroEncounterSlotCount = 0;
};

struct EctEncounterSlotSnapshot {
    std::size_t index = 0;
    std::uint16_t encounterId = 0;
    std::uint16_t encounterRate = 0;
    bool nonzero = false;
};

struct EctTableSummarySnapshot {
    std::size_t index = 0;
    std::optional<std::size_t> containerEntryIndex{};
    std::optional<std::size_t> tableIndexWithinEntry{};
    std::string containerTitle{};
    std::uint16_t stage = 0;
    std::uint16_t overallEncounterRate = 0;
    std::size_t nonzeroEncounterSlotCount = 0;
    std::uint64_t encounterRateSum = 0;
};

struct EctTableDetailSnapshot {
    EctTableSummarySnapshot summary{};
    std::vector<EctEncounterSlotSnapshot> encounters{};
};

struct EctContainerEntrySnapshot {
    std::size_t index = 0;
    std::string title{};
    std::vector<std::size_t> tableIndexes{};
};

struct EctDiagnosticSnapshot {
    EventLevel level = EventLevel::Info;
    std::string message{};
    std::optional<std::size_t> decodedOffset{};
};

class EctDocumentSession {
public:
    struct OpenResult {
        std::shared_ptr<EctDocumentSession> session{};
        DocumentResult result{};
    };

    static OpenResult open(const std::filesystem::path& path,
        const DocumentContext& context = {});

    ~EctDocumentSession();
    EctDocumentSession(EctDocumentSession&&) noexcept;
    EctDocumentSession& operator=(EctDocumentSession&&) noexcept;
    EctDocumentSession(const EctDocumentSession&) = delete;
    EctDocumentSession& operator=(const EctDocumentSession&) = delete;

    [[nodiscard]] EctOverviewSnapshot overview() const;
    [[nodiscard]] std::vector<std::filesystem::path> sourcePaths() const;
    [[nodiscard]] std::vector<EctContainerEntrySnapshot> containerEntries() const;
    [[nodiscard]] std::vector<EctTableSummarySnapshot> tables() const;
    [[nodiscard]] std::optional<EctTableDetailSnapshot> table(std::size_t index) const;
    [[nodiscard]] std::vector<EctDiagnosticSnapshot> diagnostics() const;

private:
    struct Impl;
    explicit EctDocumentSession(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace spice::mix
