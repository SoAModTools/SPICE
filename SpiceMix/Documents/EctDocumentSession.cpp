#include "EctDocumentSession.h"

#include "DocumentSupport.h"
#include "../../SpiceEct/EctParser.h"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <utility>

namespace spice::mix {
namespace {

std::string asciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

EventLevel eventLevel(const spice::ect::DiagnosticSeverity severity) {
    switch (severity) {
    case spice::ect::DiagnosticSeverity::Warning: return EventLevel::Warning;
    case spice::ect::DiagnosticSeverity::Error: return EventLevel::Error;
    default: return EventLevel::Info;
    }
}

EctTableDetailSnapshot projectTable(
    const spice::ect::EctEncounterTable& source,
    const std::size_t index,
    const std::optional<std::size_t> containerEntryIndex,
    const std::optional<std::size_t> tableIndexWithinEntry,
    std::string containerTitle) {
    EctTableDetailSnapshot result{};
    result.summary.index = index;
    result.summary.containerEntryIndex = containerEntryIndex;
    result.summary.tableIndexWithinEntry = tableIndexWithinEntry;
    result.summary.containerTitle = std::move(containerTitle);
    result.summary.stage = source.stage;
    result.summary.overallEncounterRate = source.overallEncounterRate;
    result.encounters.reserve(source.encounters.size());
    for (std::size_t slotIndex = 0; slotIndex < source.encounters.size(); ++slotIndex) {
        const auto& slot = source.encounters[slotIndex];
        const bool nonzero = slot.encounterId != 0U || slot.encounterRate != 0U;
        result.encounters.push_back(EctEncounterSlotSnapshot{
            .index = slotIndex,
            .encounterId = slot.encounterId,
            .encounterRate = slot.encounterRate,
            .nonzero = nonzero,
        });
        if (nonzero) ++result.summary.nonzeroEncounterSlotCount;
        result.summary.encounterRateSum += slot.encounterRate;
    }
    return result;
}

} // namespace

struct EctDocumentSession::Impl {
    EctOverviewSnapshot overview{};
    std::vector<EctContainerEntrySnapshot> entries{};
    std::vector<EctTableDetailSnapshot> tables{};
    std::vector<EctDiagnosticSnapshot> diagnostics{};
};

EctDocumentSession::EctDocumentSession(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

EctDocumentSession::~EctDocumentSession() = default;
EctDocumentSession::EctDocumentSession(EctDocumentSession&&) noexcept = default;
EctDocumentSession& EctDocumentSession::operator=(EctDocumentSession&&) noexcept = default;

EctDocumentSession::OpenResult EctDocumentSession::open(
    const std::filesystem::path& path,
    const DocumentContext& context) {
    if (context.stopToken.stop_requested()) {
        return { .result = documents::cancelled() };
    }

    std::error_code error{};
    if (path.empty() || !std::filesystem::is_regular_file(path, error) || error) {
        return { .result = documents::failure("A readable ECT input file is required.") };
    }
    if (asciiLower(path.extension().string()) != ".ect") {
        return { .result = documents::failure("The ECT workbench requires an .ect file.") };
    }

    documents::emit(context, EventLevel::Progress, "Reading ECT document.");
    std::vector<std::uint8_t> sourceBytes{};
    try {
        sourceBytes = documents::readBytes(path);
    } catch (const std::exception& exception) {
        return { .result = documents::failure(exception.what()) };
    }
    if (context.stopToken.stop_requested()) {
        return { .result = documents::cancelled() };
    }

    const bool wasAklz = spice::compression::aklz::isAklz(sourceBytes);
    const auto layout = asciiLower(path.filename().string()) == "a099a.ect"
        ? spice::ect::EctLayout::OverworldIndexed
        : spice::ect::EctLayout::Flat;
    auto parsed = spice::ect::EctParser::parse(sourceBytes, layout);

    auto impl = std::make_unique<Impl>();
    impl->overview.sourcePath = path;
    impl->overview.sourceSize = sourceBytes.size();
    impl->overview.sourceWasAklz = wasAklz;
    impl->overview.platform = wasAklz ? "GameCube" : "Dreamcast";
    impl->overview.endian = wasAklz ? "Big endian" : "Little endian";
    impl->overview.layout = layout == spice::ect::EctLayout::OverworldIndexed
        ? "A099 indexed overworld"
        : "Flat encounter tables";

    if (wasAklz) {
        const auto decoded = spice::compression::aklz::decompress(sourceBytes);
        if (decoded.ok()) impl->overview.decodedSize = decoded.bytes.size();
    } else {
        impl->overview.decodedSize = sourceBytes.size();
    }

    std::vector<std::string> diagnosticMessages{};
    for (const auto& diagnostic : parsed.diagnostics) {
        EctDiagnosticSnapshot snapshot{
            .level = eventLevel(diagnostic.severity),
            .message = diagnostic.message,
            .decodedOffset = diagnostic.offset,
        };
        impl->diagnostics.push_back(snapshot);
        std::string message = diagnostic.message;
        if (diagnostic.offset.has_value()) {
            message += " (offset " + std::to_string(*diagnostic.offset) + ")";
        }
        diagnosticMessages.push_back(message);
        documents::emit(context, snapshot.level, std::move(message));
    }
    if (!parsed.ok() || !parsed.file.has_value()) {
        return { .result = documents::failure("ECT parsing failed.", std::move(diagnosticMessages)) };
    }
    if (context.stopToken.stop_requested()) {
        return { .result = documents::cancelled() };
    }

    std::size_t tableIndex = 0U;
    if (const auto* flat = std::get_if<spice::ect::EctFlatContent>(&parsed.file->content)) {
        impl->tables.reserve(flat->tables.size());
        for (const auto& table : flat->tables) {
            impl->tables.push_back(projectTable(table, tableIndex, std::nullopt, std::nullopt, {}));
            ++tableIndex;
        }
    } else {
        const auto& indexed = std::get<spice::ect::EctOverworldContent>(parsed.file->content);
        impl->entries.reserve(indexed.entries.size());
        impl->tables.reserve(indexed.entries.size() * spice::ect::kOverworldTablesPerEntry);
        for (std::size_t entryIndex = 0; entryIndex < indexed.entries.size(); ++entryIndex) {
            const auto& entry = indexed.entries[entryIndex];
            EctContainerEntrySnapshot entrySnapshot{
                .index = entryIndex,
                .title = entry.title,
            };
            entrySnapshot.tableIndexes.reserve(entry.tables.size());
            for (std::size_t localIndex = 0; localIndex < entry.tables.size(); ++localIndex) {
                entrySnapshot.tableIndexes.push_back(tableIndex);
                impl->tables.push_back(projectTable(
                    entry.tables[localIndex], tableIndex, entryIndex, localIndex, entry.title));
                ++tableIndex;
            }
            impl->entries.push_back(std::move(entrySnapshot));
        }
    }

    impl->overview.containerEntryCount = impl->entries.size();
    impl->overview.tableCount = impl->tables.size();
    for (const auto& table : impl->tables) {
        impl->overview.encounterSlotCount += table.encounters.size();
        impl->overview.nonzeroEncounterSlotCount += table.summary.nonzeroEncounterSlotCount;
    }

    documents::emit(context, EventLevel::Info,
        "Opened ECT document with " + std::to_string(impl->overview.tableCount) + " encounter tables.");
    auto session = std::shared_ptr<EctDocumentSession>(new EctDocumentSession(std::move(impl)));
    return { .session = std::move(session), .result = { .message = "Opened ECT document." } };
}

EctOverviewSnapshot EctDocumentSession::overview() const {
    return impl_->overview;
}

std::vector<std::filesystem::path> EctDocumentSession::sourcePaths() const {
    return { impl_->overview.sourcePath };
}

std::vector<EctContainerEntrySnapshot> EctDocumentSession::containerEntries() const {
    return impl_->entries;
}

std::vector<EctTableSummarySnapshot> EctDocumentSession::tables() const {
    std::vector<EctTableSummarySnapshot> result{};
    result.reserve(impl_->tables.size());
    for (const auto& table : impl_->tables) result.push_back(table.summary);
    return result;
}

std::optional<EctTableDetailSnapshot> EctDocumentSession::table(const std::size_t index) const {
    if (index >= impl_->tables.size()) return std::nullopt;
    return impl_->tables[index];
}

std::vector<EctDiagnosticSnapshot> EctDocumentSession::diagnostics() const {
    return impl_->diagnostics;
}

} // namespace spice::mix
